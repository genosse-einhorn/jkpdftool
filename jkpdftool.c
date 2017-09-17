#define _POSIX_C_SOURCE 200809L
#include <malloc.h>
#include <libgen.h>
#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <math.h>
#include <inttypes.h>
#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>

//////////////////////////////////////
// Whitespace cropping
//////////////////////////////////////
struct crop_bounds {
    double top, right, bottom, left;
};
static struct crop_bounds calc_crop_bounds(PopplerPage *page)
{
    double width, height;
    poppler_page_get_size(page, &width, &height);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)width, (int)height);

    cairo_t *cr = cairo_create(surface);
    poppler_page_render_for_printing(page, cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER); // white bg
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct crop_bounds retval = {
        .left = INT_MAX,
        .right = INT_MAX,
        .top = 0,
        .bottom = 0
    };

    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *dataptr = cairo_image_surface_get_data(surface);

    int last_empty_row_count = 0;
    int last_filled_row_count = 0;
    for (int i = 0; i < (int)height; ++i) {
        uint32_t *row = (uint32_t*)(dataptr + i*stride);

        // check the whole thing for non-transparent pixels
        int firstseen = INT_MAX;
        int lastseen = -1;

        for (int x = 0; x < (int)width; ++x) {
            if (row[x] != 0xffffffff) {
                // pixel!
                firstseen = firstseen < x ? firstseen : x;
                lastseen = x;
            }
        }

        if (lastseen < 0) {
            //printf("DEBUG: row %d/%f is completely empty\n", i, height);

            // completely empty
            if (last_filled_row_count == 0) {
                // beginning
                retval.top++;
            }

            last_empty_row_count++;
        } else {
            //printf("DEBUG: row %d/%f has first at %d, last at %d\n", i, height, firstseen, lastseen);

            // at least partially filled line
            last_filled_row_count++;
            last_empty_row_count = 0;

            // evaluate left/right crops
            int left = firstseen;
            int right = (int)width - lastseen - 1;

            retval.left = fmin(retval.left, left);
            retval.right = fmin(retval.right, right);
        }
    }

    //cairo_surface_write_to_png(surface, "debug.png");
    retval.bottom = last_empty_row_count;

    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    return retval;
}

//////////////////////////////////////
// Page size and orientation parsing
//////////////////////////////////////
enum orientation_mode {
    ORIENTATION_AUTO,
    ORIENTATION_LANDSCAPE,
    ORIENTATION_PORTRAIT,
    ORIENTATION_MODE_INVALID = -1
};

static bool parse_paper_size(double *width, double *height, const char *spec)
{
    if (!g_ascii_strcasecmp(spec, "a5")) {
        *width = 420.0;
        *height = 595.0;
        return true;
    }

    if (!g_ascii_strcasecmp(spec, "a4")) {
        *width = 595.0;
        *height = 842.0;
        return true;
    }

    if (!g_ascii_strcasecmp(spec, "a3")) {
        *width = 842.0;
        *height = 1190.0;
        return true;
    }

    // width x height
    // FIXME: floating point inaccuracies
    *width = 0.0;
    *height = 0.0;
    for (;;) {
        switch (*spec) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                *width = *width * 10 + (*spec++ - '0');
                break;
            case 'x': case 'X':
                spec++;
                goto parse_height;
            default:
                return false;
        }
    }
parse_height:
    for (;;) {
        switch (*spec) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                *height = *height * 10 + (*spec++ - '0');
                break;
            case 0:
                goto finished_parsing;
            default:
                return false;
        }
    }
finished_parsing:

    return (*width > 0) && (*height > 0);
}

static enum orientation_mode parse_orientation(const char *orientation)
{
    if (!orientation)
        return ORIENTATION_AUTO;

    size_t len = strlen(orientation);
    if (!g_ascii_strncasecmp("landscape", orientation, len))
        return ORIENTATION_LANDSCAPE;

    if (!g_ascii_strncasecmp("portrait", orientation, len))
        return ORIENTATION_PORTRAIT;

    return ORIENTATION_MODE_INVALID;
}

void swap_doubles(double *a, double *b)
{
    double tmp = *a;
    *a = *b;
    *b = tmp;
}

/////////////////////////////////////////
// Formatting and size calculation
/////////////////////////////////////////
static void calc_pdf_size(double *out_w, double *out_h, const double target_w, const double target_h,
                          enum orientation_mode orientation, PopplerPage *page, bool two_per_page)
{
    double page_w, page_h;
    poppler_page_get_size(page, &page_w, &page_h);

    if (target_w > 0.0)
        *out_w = target_w;
    else
        *out_w = page_w;

    if (target_h > 0.0)
        *out_h = target_h;
    else
        *out_h = page_h;

    // resolve auto orientation
    if (orientation == ORIENTATION_AUTO) {
        if (two_per_page)
            orientation = page_w > page_h ? ORIENTATION_PORTRAIT : ORIENTATION_LANDSCAPE;
        else
            orientation = page_w > page_h ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT;
    }

    // maybe swap w/h for orientation
    if (orientation == ORIENTATION_LANDSCAPE && *out_h > *out_w) {
        if (*out_h > *out_w)
            swap_doubles(out_h, out_w);
    } else if (orientation == ORIENTATION_PORTRAIT) {
        if (*out_w > *out_h)
            swap_doubles(out_w, out_h);
    }
}

static cairo_matrix_t get_scale_matrix(double paper_w, double paper_h,
                                       double source_w, double source_h,
                                       double margin, const struct crop_bounds cropbounds)
{
    cairo_matrix_t m;
    cairo_matrix_init_identity(&m);

    double scale_x = (paper_w - 2*margin) / (source_w - cropbounds.left - cropbounds.right);
    double scale_y = (paper_h - 2*margin) / (source_h - cropbounds.top - cropbounds.bottom);
    double scale = scale_x < scale_y ? scale_x : scale_y;

    double scaled_w = (source_w - cropbounds.left - cropbounds.right) * scale;
    double scaled_h = (source_h - cropbounds.top - cropbounds.bottom) * scale;

    cairo_matrix_translate(&m, (paper_w - scaled_w)/2, (paper_h - scaled_h)/2);
    cairo_matrix_scale(&m, scale, scale);
    cairo_matrix_translate(&m, -cropbounds.left, -cropbounds.top);

    return m;
}


//////////////////////////
// Page selection helpers
//////////////////////////
static GPtrArray *load_all_pages(char **inputfiles)
{
    g_autoptr(GError)    error = NULL;
    g_autoptr(GPtrArray) arr = g_ptr_array_new_with_free_func(g_object_unref);

    for (char **pinput = inputfiles; *pinput; ++pinput) {
        g_autoptr(GFile) inputf = g_file_new_for_commandline_arg(*pinput);

        PopplerDocument *document = poppler_document_new_from_gfile(inputf, NULL, NULL, &error);
        if (!document) {
            fprintf(stderr, "ERROR: poppler fail: %s\n", error->message);
            exit(1);
        }

        int num_pages = poppler_document_get_n_pages(document);
        for (int i = 0; i < num_pages; i++) {
            PopplerPage *page = poppler_document_get_page(document, i);
            if (!page) {
                fprintf(stderr, "ERROR: poppler fail: page not found\n");
                exit(1);
            }

            g_ptr_array_add(arr, page);
        }

        g_object_unref(document);
    }

    return g_steal_pointer(&arr);
}

#define RANGE_ERROR range_error_quark()
static GQuark range_error_quark(void)
{
    return g_quark_from_static_string("range-parse-error-quark");
}

enum {
    RANGE_ERROR_PARSEFAIL,
    RANGE_ERROR_ILLOGICAL,
    RANGE_ERROR_PAGENOTFOUND
};

// num ::= '1'|'2'|...|'9' { '0'|...|'9' }
static size_t parse_num(const char *str, size_t i, int *num, GError **error)
{
    switch (str[i]) {
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            *num = str[i] - '0';
            break;
        default:
            g_set_error(error, RANGE_ERROR, RANGE_ERROR_PARSEFAIL, "Unexpected '%c' at position %zu", str[i], i);
            return (size_t)-1;
    }

    ++i;
    size_t retval = 1;

    for (;;) {
        switch (str[i]) {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                *num = *num * 10 + (str[i] - '0');
                ++i;
                ++retval;
                break;
            default:
                return retval;
        }
    }
}

// expr ::= num [ '-' num ]
struct range_expr { int begin; int end; };
static size_t parse_expr(const char *str, size_t i, struct range_expr *out, GError **error)
{
    size_t beginlen = parse_num(str, i, &out->begin, error);
    if (beginlen == (size_t)-1)
        return (size_t)-1;

    if (str[i + beginlen] == '-') {
        size_t endlen = parse_num(str, i + beginlen + 1, &out->end, error);
        if (endlen == (size_t)-1)
            return (size_t)-1;

        if (out->begin > out->end) {
            g_set_error(error, RANGE_ERROR, RANGE_ERROR_ILLOGICAL,
                        "Illogical range '%d-%d': Begin greater than end",
                        out->begin, out->end);
            return (size_t)-1;
        }

        return beginlen + endlen + 1;
    } else {
        out->end = out->begin;
        return beginlen;
    }
}

// range ::= expr { ',' expr }
static GArray *parse_range(const char *str, GError **error)
{
    g_autoptr(GArray) arr = g_array_new(FALSE, TRUE, sizeof(struct range_expr));
    struct range_expr tmp;

    size_t exprlen = parse_expr(str, 0, &tmp, error);
    if (exprlen == (size_t)-1)
        return NULL;

    g_array_append_val(arr, tmp);
    size_t i = exprlen;
    for (;;) {
        if (str[i] == ',') {
            ++i;
            exprlen = parse_expr(str, i, &tmp, error);
            if (exprlen == (size_t)-1)
                return NULL;

            i += exprlen;
            g_array_append_val(arr, tmp);
        } else if (str[i] == 0) {
            return g_steal_pointer(&arr);
        } else {
            g_set_error(error, RANGE_ERROR, RANGE_ERROR_PARSEFAIL, "Unexpected '%c' at position %zu", str[i], i);
            return NULL;
        }
    }
}

static GPtrArray *get_pages_from_range(GPtrArray *all_pages, GArray *range, GError **error)
{
    g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func(g_object_unref);

    for (size_t i = 0; i < range->len; ++i) {
        struct range_expr *e = &g_array_index(range, struct range_expr, i);

        for (int pageno = e->begin; pageno <= e->end; ++pageno) {
            if (pageno < 1 || (size_t)pageno > all_pages->len) {
                g_set_error(error, RANGE_ERROR, RANGE_ERROR_PAGENOTFOUND, "Page %d not found", pageno);
                return NULL;
            }

            PopplerPage *page = g_object_ref(all_pages->pdata[pageno-1]);
            g_ptr_array_add(result, page);
        }
    }

    return g_steal_pointer(&result);
}

//////////////////////
// Output mechanics
//////////////////////
static cairo_status_t write_cairo_to_gfile(void *closure, const unsigned char *data, unsigned int length)
{
    GFileOutputStream *stream = closure;
    gsize written_dummy;
    if (!g_output_stream_write_all(G_OUTPUT_STREAM(stream),
                                   data,
                                   length,
                                   &written_dummy,
                                   NULL,
                                   NULL)) {
        return CAIRO_STATUS_WRITE_ERROR;
    } else {
        return CAIRO_STATUS_SUCCESS;
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    setlocale(LC_ALL, "");

    g_autoptr(GError) error = NULL;
    cairo_status_t status;

#ifdef G_OS_WIN32
    g_auto(GStrv) args = g_win32_get_command_line();
#else
    g_auto(GStrv) args = g_strdupv(argv);
#endif

    g_autoptr(GOptionContext) context = g_option_context_new(" - Process PDF files");

    // command line option stuff
    g_autofree gchar *outputfile    = NULL;
    g_autofree gchar *papersize     = NULL;
    g_autofree gchar *orientation   = NULL;
    g_autofree gchar *pagespec      = NULL;
    double            margin        = 0.0;
    g_auto(GStrv)     inputfiles    = NULL;
    gboolean          two_per_page  = FALSE;
    gboolean          crop          = FALSE;


    GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &outputfile, "Output file", NULL },
        { "", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &inputfiles, "Input file(s).", "FILES..." },
        { "pages", 'p', 0, G_OPTION_ARG_STRING, &pagespec, "Pages to process, e.g. '1-2,5,7'", NULL },
        { "crop", 'c', 0, G_OPTION_ARG_NONE, &crop, "Remove whitespace around page", NULL },
        { "size", 's', 0, G_OPTION_ARG_STRING, &papersize, "Output paper size in points, or A3, A4, A5", "WIDTHxHEIGHT" },
        { "orientation", 'l', 0, G_OPTION_ARG_STRING, &orientation, "Output orientation: 'Landscape' or 'Portrait'", NULL },
        { "two-per-sheet", '2', 0, G_OPTION_ARG_NONE, &two_per_page, "Emit two pages per sheet", NULL },
        { "margin", 'm', 0, G_OPTION_ARG_DOUBLE, &margin, "Extra page margin to add (in points)", "MARGIN" },
        { NULL }
    };
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_summary(context,
        "Massage PDF files for when your printer can't do it.\n"
        "\n"
        "Concatenate multiple PDF files:\n"
        "   Multiple input files will be concatenated\n"
        "\n"
        "Select individual pages:\n"
        "   The --pages option allows you to select one or more pages from \n"
        "   the source files. Page numbers are contiguous over multiple input files.\n"
        "\n"
        "Crop margins:\n"
        "   The --crop option removes whitespace around the page.\n"
        "   Cropped margins are calculated so that each page is scaled equally.\n"
        "\n"
        "Add margins:\n"
        "   The --margin option adds a margin around the page.\n"
        "\n"
        "Resize page:\n"
        "   The --size option allows you to scale the PDF to A5, A3, A4, or a custom\n"
        "   paper size. You can also set a custom orientation, although the\n"
        "   auto-detected one should usually be fine.\n"
        "   By default, the size and orientation of the first source page is used.\n"
        "\n"
        "Two pages per sheet:\n"
        "   With the '-2' option, two pages are laid out side-by-side.\n"
    );
    g_option_context_set_description(context, "Written by Jonas Kümmerlin <jonas@kuemmerlin.eu>");

    if (!g_option_context_parse_strv(context, &args, &error)) {
       fprintf(stderr, "ERROR: %s\n", error->message);
       return 1;
    }

    if (!inputfiles || g_strv_length(inputfiles) < 1) {
        fprintf(stderr, "ERROR: Need at least one input file\n");
        return 1;
    }

    double paper_w = 0.0;
    double paper_h = 0.0;
    if (papersize) {
        if (!parse_paper_size(&paper_w, &paper_h, papersize)) {
            fprintf(stderr, "ERROR: Illegal paper size specification '%s'\n", papersize);
            return 1;
        }
    }

    enum orientation_mode paper_orientation = parse_orientation(orientation);
    if (paper_orientation == ORIENTATION_MODE_INVALID) {
        fprintf(stderr, "ERROR: Illegal orientation specification '%s'\n", orientation);
        return -1;
    }

    g_autoptr(GFile) ofile = NULL;
    g_autoptr(GFileOutputStream) ostream = NULL;

    // create output file if not given by user
    if (!outputfile) {
        fprintf(stderr, "ERROR: no output file specified.\n");
        fprintf(stderr, "Run it like this: %s -o PATH/TO/FILE.pdf ...\n", argv[0]);
        return 1;
    } else {
        ofile = g_file_new_for_commandline_arg(outputfile);
        ostream = g_file_replace(ofile,
                                 NULL,
                                 FALSE,
                                 0,
                                 NULL,
                                 &error);
        if (!ostream) {
            fprintf(stderr, "ERROR: Couldn't open output file: %s\n", error->message);
            return 1;
        }
    }

    g_autoptr(GPtrArray) all_pages = load_all_pages(inputfiles);
    g_autoptr(GPtrArray) pages = NULL;

    if (pagespec) {
        g_autoptr(GArray) range = parse_range(pagespec, &error);
        if (!range) {
            fprintf(stderr, "ERROR: Illegal page range: %s\n", error->message);
            return 1;
        }

        pages = get_pages_from_range(all_pages, range, &error);
        if (!pages) {
            fprintf(stderr, "ERROR: Illegal page range: %s\n", error->message);
            return 1;
        }
    } else {
        pages = g_ptr_array_ref(all_pages);
    }

    if (!pages->len) {
        fprintf(stderr, "ERROR: No pages to process.\n");
        return 1;
    }

    // calculate page size and orientation based on the first page
    PopplerPage *firstpage = pages->pdata[0];
    double width, height;
    calc_pdf_size(&width, &height, paper_w, paper_h, paper_orientation, firstpage, two_per_page);

    // do cropping
    struct crop_bounds cropbounds = { 0, 0, 0, 0 };
    if (crop) {
        cropbounds = (struct crop_bounds){ DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX };

        for (guint i = 0; i < pages->len; ++i) {
            PopplerPage *page = pages->pdata[i];

            struct crop_bounds tmp = calc_crop_bounds(page);

            cropbounds.left = fmin(cropbounds.left, tmp.left);
            cropbounds.right = fmin(cropbounds.right, tmp.right);
            cropbounds.top = fmin(cropbounds.top, tmp.top);
            cropbounds.bottom = fmin(cropbounds.bottom, tmp.bottom);
        }
    }

    // render
    cairo_surface_t *surface = cairo_pdf_surface_create_for_stream(write_cairo_to_gfile, ostream, width, height);
    cairo_t *cr = cairo_create(surface);

    for (size_t pageno = 0; pageno < pages->len; ++pageno) {
        PopplerPage *page = pages->pdata[pageno];

        double source_w, source_h;
        poppler_page_get_size(page, &source_w, &source_h);

        cairo_save(cr);

        double area_w, area_h;
        if (two_per_page) {
            if (width > height) {
                // landscape - split left/right
                area_w = width/2;
                area_h = height;

                // move paint area
                cairo_translate(cr, area_w * (int)(pageno % 2), 0);
            } else {
                // portrait - split top/bottom
                area_w = width;
                area_h = height/2;

                // move area
                cairo_translate(cr, 0, area_h * (int)(pageno % 2));
            }
        } else {
            // use the full page
            area_w = width;
            area_h = height;
        }

        // setup clip - important for cropped rendering in split page mode
        cairo_rectangle(cr, 0, 0, area_w, area_h);
        cairo_clip(cr);

        cairo_matrix_t m = get_scale_matrix(area_w, area_h, source_w, source_h, margin, cropbounds);

        cairo_transform(cr, &m);

        poppler_page_render_for_printing(page, cr);

        cairo_restore(cr);

        if ((pageno % 2) == 1 || !two_per_page) {
            cairo_surface_show_page(surface);
        }
    }

    status = cairo_status(cr);
    if (status)
        fprintf(stderr, "??? cairo: %s\n", cairo_status_to_string(status));

    cairo_destroy(cr);
    cairo_surface_finish(surface);
    status = cairo_surface_status(surface);
    if (status)
        fprintf(stderr, "??? cairo: %s\n", cairo_status_to_string(status));
    cairo_surface_destroy(surface);

    if (!g_output_stream_close(G_OUTPUT_STREAM(ostream), NULL, &error)) {
        fprintf(stderr, "ERROR: Failed to close stream: %s\n", error->message);
        return 1;
    }

    return 0;
}
