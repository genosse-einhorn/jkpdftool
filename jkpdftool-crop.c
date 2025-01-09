// Copyright © 2018-2021 Jonas Kümmerlin <jonas@kuemmerlin.eu>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "jkpdf-io.h"
#include "jkpdf-transform.h"
#include "jkpdf-parsesize.h"

#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

static inline int
parse_hexdigit(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    } else if (digit >= 'a' && digit <= 'f') {
        return 10 + (digit - 'a');
    } else if (digit >= 'A' && digit <= 'F') {
        return 10 + (digit - 'A');
    } else {
        return -1;
    }
}

static inline bool
parse_color_spec(const char *color, float *r, float *g, float *b)
{
    int i = 0, j = 0;

    i = parse_hexdigit(color[0]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[1]);
    if (j < 0)
        return false;
    *r = ((float)(i * 0x10 + j) / 0xFF);

    i = parse_hexdigit(color[2]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[3]);
    if (j < 0)
        return false;
    *g = ((float)(i * 0x10 + j) / 0xFF);

    i = parse_hexdigit(color[4]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[5]);
    if (j < 0)
        return false;
    *b = ((float)(i * 0x10 + j) / 0xFF);

    return color[6] == 0;
}

struct crop_bounds {
    double left;
    double right;
    double top;
    double bottom;
};

static inline bool color_equal_with_fuzz(const void *a, const void *b, int fuzz) {
    unsigned char rgba[4];
    unsigned char rgbb[4];

    memcpy(rgba, a, 4);
    memcpy(rgbb, b, 4);

    for (int i = 0; i < 4; ++i) {
        if (abs((int)rgba[i] - (int)rgbb[i]) > fuzz)
            return false;
    }

    return true;
}

static inline struct crop_bounds
calc_crop_bounds(PopplerPage *page, double dpi, int pxl_limit, int color_fuzz, float bg_r, float bg_g, float bg_b)
{
    struct crop_bounds retval = { 0.0, 0.0, 0.0, 0.0 };

    double pagewidth, pageheight;
    poppler_page_get_size(page, &pagewidth, &pageheight);

    int surfwidth  = (int)(pagewidth / 72.0 * dpi);
    int surfheight = (int)(pageheight / 72.0 * dpi);

    uint32_t bgcolor = (0xffu << 24) | (uint32_t)(bg_r * 0xff) << 16 | (uint32_t)(bg_g * 0xff) << 8 | (uint32_t)(bg_b * 0xff);

    g_autoptr(JKPdfCairoSurfaceT) img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfwidth, surfheight);

    g_autoptr(JKPdfCairoT) cr = cairo_create(img);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_rectangle(cr, 0, 0, surfwidth, surfheight);
    cairo_fill(cr);

    cairo_scale(cr, surfwidth / pagewidth, surfheight / pageheight);
    poppler_page_render_for_printing(page, cr);

    cairo_surface_flush(img);

    int stride = cairo_image_surface_get_stride(img);
    unsigned char *data = cairo_image_surface_get_data(img);

    // top
    int min_top = 0;
    for (int y = 0; y < surfheight; ++y) {
        unsigned char *row = data + y * stride;

        int num_mismatch = 0;
        for (int x = 0; x < surfwidth; ++x) {
            if (!color_equal_with_fuzz(&row[4*x], &bgcolor, color_fuzz))
                num_mismatch++;
        }

        if (num_mismatch > pxl_limit)
            break;

        min_top++;
    }

    // bottom
    int min_bottom = 0;
    for (int y = surfheight-1; y >= min_top; --y) {
        unsigned char *row = data + y * stride;

        int num_mismatch = 0;
        for (int x = 0; x < surfwidth; ++x) {
            if (!color_equal_with_fuzz(&row[4*x], &bgcolor, color_fuzz))
                num_mismatch++;
        }

        if (num_mismatch > pxl_limit)
            break;

        min_bottom++;
    }

    // left
    int min_left = 0;
    for (int x = 0; x < surfwidth; ++x) {
        int num_mismatch = 0;

        for (int y = 0; y < surfheight; ++y) {
            unsigned char *row = data + y * stride;
            if (!color_equal_with_fuzz(&row[4*x], &bgcolor, color_fuzz))
                num_mismatch++;
        }

        if (num_mismatch > pxl_limit)
            break;

        min_left++;
    }

    // right
    int min_right = 0;
    for (int x = surfwidth-1; x > min_left; --x) {
        int num_mismatch = 0;

        for (int y = 0; y < surfheight; ++y) {
            unsigned char *row = data + y * stride;
            if (!color_equal_with_fuzz(&row[4*x], &bgcolor, color_fuzz))
                num_mismatch++;
        }

        if (num_mismatch > pxl_limit)
            break;

        min_right++;
    }

    retval.left   = (double)min_left   * (pagewidth / surfwidth);
    retval.right  = (double)min_right  * (pagewidth / surfwidth);
    retval.top    = (double)min_top    * (pageheight / surfheight);
    retval.bottom = (double)min_bottom * (pageheight / surfheight);

    return retval;
}

static inline struct crop_bounds
calc_crop_bounds_for_all(PopplerDocument *doc, double dpi, int pxl_limit, int color_fuzz, float bg_r, float bg_g, float bg_b)
{
    struct crop_bounds retval = { INFINITY, INFINITY, INFINITY, INFINITY };

    for (int i = 0; i < poppler_document_get_n_pages(doc); ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, i);

        struct crop_bounds b = calc_crop_bounds(page, dpi, pxl_limit, color_fuzz, bg_r, bg_b, bg_g);

        retval.left   = MIN(retval.left, b.left);
        retval.right  = MIN(retval.right, b.right);
        retval.top    = MIN(retval.top, b.top);
        retval.bottom = MIN(retval.bottom, b.bottom);
    }

    return retval;
}

int
main(int argc, char **argv)
{
    g_autofree gchar *arg_bgcolor = NULL;
    double arg_resolution = 72;
    gboolean arg_per_page = FALSE;
    int arg_pxl_limit = 0;
    gboolean arg_no_top = FALSE;
    gboolean arg_no_bottom = FALSE;
    gboolean arg_no_left = FALSE;
    gboolean arg_no_right = FALSE;
    g_autofree gchar *arg_margin = NULL;
    int arg_color_fuzz = 0;
    g_autofree gchar *arg_target_w = NULL;
    g_autofree gchar *arg_target_h = NULL;

    GOptionEntry option_entries[] = {
        { "background-color", 'c', 0, G_OPTION_ARG_STRING, &arg_bgcolor,    "Background color to crop (default: white)", "RRGGBB" },
        { "resolution",       'r', 0, G_OPTION_ARG_DOUBLE, &arg_resolution, "Resolution to detect content (default: 72)", "DPI" },
        { "per-page",         'p', 0, G_OPTION_ARG_NONE,   &arg_per_page,   "Calculate offsets per page (default: no)", NULL },
        { "allow-mismatch",   'l', 0, G_OPTION_ARG_INT,    &arg_pxl_limit,  "Tolerated non-background pixels (default: 0)", "NUM" },
        { "no-top",           0,   0, G_OPTION_ARG_NONE,   &arg_no_top,     "Do not crop the top side", NULL },
        { "no-bottom",        0,   0, G_OPTION_ARG_NONE,   &arg_no_bottom,  "Do not crop the bottom side", NULL },
        { "no-left",          0,   0, G_OPTION_ARG_NONE,   &arg_no_left,    "Do not crop the left side", NULL },
        { "no-right",         0,   0, G_OPTION_ARG_NONE,   &arg_no_right,   "Do not crop the right side", NULL },
        { "margin",           'm', 0, G_OPTION_ARG_STRING, &arg_margin,     "Margin to leave around detected content", "MARGIN" },
        { "fuzz",             'f', 0, G_OPTION_ARG_INT,    &arg_color_fuzz, "Allowed color variation (default: 0)", "0-255" },
        { "target-width",     'w', 0, G_OPTION_ARG_STRING, &arg_target_w,   "Scale result to target width", "WIDTH" },
        { "target-height",    'h', 0, G_OPTION_ARG_STRING, &arg_target_h,   "Scale result to target height", "HEIGHT" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Remove empty borders around PDF content.\n"
        "\n"
        "The PDF file is read from standard input, and the transformed PDF file is\n"
        "written onto the standard output.\n"
        "\n"
        "Influencing border detection:\n"
        "  Croppable borders are detected by rastering the page and checking for\n"
        "  pixels in background color. You can use --background-color=RRGGBB to\n"
        "  set a different background color and --resolution to influence the\n"
        "  dpi used in the rastering.\n"
        "\n"
        "Per page cropping mode:\n"
        "  By default, crop bounds are calculated so that all pages are cropped by\n"
        "  the same amount on each side. This allows you to chain jkpdf-crop(1) with\n"
        "  jkpdf-pagefit(1) and have all pages scaled identically.\n"
        "  With the --per-page option, this can be changed so that every page is\n"
        "  cropped individually. With per page cropping, pages will end up having\n"
        "  different sizes.\n"
        "\n"
        "Fuzzy content detection:\n"
        "  The --fuzz option allows you to specify how closely a color needs to match\n"
        "  to be considered part of the background. This is useful when cropping\n"
        "  compressed images where color representation might not always be exact.\n"
        "  The --allow-mismatch option specifies the number of pixels per row or\n"
        "  column which can be of non-background color while still considering the\n"
        "  row or column empty. This is useful for ignoring tiny dust particles\n"
        "  on scanned images.\n"
        "\n"
    );

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    if (argc > 1) {
        fprintf(stderr, "ERROR: unexpected positional argument '%s'\n", argv[1]);
        return 1;
    }

    float r = 1.0f, g = 1.0f, b = 1.0f;
    struct crop_bounds global_bounds;

    if (arg_bgcolor && !parse_color_spec(arg_bgcolor, &r, &g, &b)) {
        fprintf(stderr, "ERROR: not a valid color: %s\n", arg_bgcolor);
        return 1;
    }

    double margins[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (arg_margin && !jkpdf_parse_margin_spec(arg_margin, margins, &error)) {
        fprintf(stderr, "ERROR: invalid margin specification '%s': %s\n", arg_margin, error->message);
        return 1;
    }

    double target_w = 0.0;
    if (arg_target_w && !jkpdf_parse_single_length(arg_target_w, &target_w, &error)) {
        fprintf(stderr, "ERROR: invalid target width '%s': %s\n", arg_target_w, error->message);
        return 1;
    }

    double target_h = 0.0;
    if (arg_target_h && !jkpdf_parse_single_length(arg_target_h, &target_h, &error)) {
        fprintf(stderr, "ERROR: invalid target width '%s': %s\n", arg_target_h, error->message);
        return 1;
    }

    if ((target_w > 0.0) && (margins[1] + margins[3] >= target_w)) {
        fprintf(stderr, "ERROR: margins greater than target width\n");
        return 1;
    }

    if ((target_h > 0.0) && (margins[0] + margins[2] >= target_h)) {
        fprintf(stderr, "ERROR: margins greater than target height\n");
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    if (!arg_per_page) {
        global_bounds = calc_crop_bounds_for_all(doc, arg_resolution, arg_pxl_limit, arg_color_fuzz, r, g, b);
    }

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        double pagewidth, pageheight;
        poppler_page_get_size(page, &pagewidth, &pageheight);

        struct crop_bounds bounds;
        if (arg_per_page) {
            bounds = calc_crop_bounds(page, arg_resolution, arg_pxl_limit, arg_color_fuzz, r, g, b);
        } else {
            bounds = global_bounds;
        }

        if (arg_no_left)
            bounds.left = 0.0;
        if (arg_no_top)
            bounds.top = 0.0;
        if (arg_no_right)
            bounds.right = 0.0;
        if (arg_no_bottom)
            bounds.bottom = 0.0;

        cairo_save(cr);

        if (target_w > 0.0 || target_h > 0.0) {
            // scaled version

            double cropped_w = pagewidth - bounds.left - bounds.right;
            double cropped_h = pageheight - bounds.top - bounds.bottom;
            double zoom_w = 0.0;
            double zoom_h = 0.0;
            double zoom = 0.0;

            if (target_w > 0.0)
                zoom_w = (target_w - margins[1] - margins[3]) / cropped_w;
            if (target_h > 0.0)
                zoom_h = (target_h - margins[0] - margins[2]) / cropped_h;

            if (zoom_w > 0.0 && zoom_h > 0.0)
                zoom = MIN(zoom_w, zoom_h);
            else
                zoom = MAX(zoom_w, zoom_h);

            double target_page_w = cropped_w * zoom + margins[1] + margins[3];
            double target_page_h = cropped_h * zoom + margins[0] + margins[2];

            cairo_pdf_surface_set_size(surf, target_page_w, target_page_h);
            cairo_translate(cr, margins[3], margins[0]);
            cairo_scale(cr, zoom, zoom);
            cairo_translate(cr, -bounds.left, -bounds.top);
        } else {
            // classic non-scaled version

            bounds.top    = MAX(0.0, bounds.top - margins[0]);
            bounds.right  = MAX(0.0, bounds.right - margins[1]);
            bounds.bottom = MAX(0.0, bounds.bottom - margins[2]);
            bounds.left   = MAX(0.0, bounds.left - margins[3]);


            cairo_pdf_surface_set_size(surf, pagewidth - bounds.left - bounds.right, pageheight - bounds.top - bounds.bottom);
            cairo_translate(cr, -bounds.left, -bounds.top);
        }

        poppler_page_render_for_printing(page, cr);
        cairo_restore(cr);
        cairo_surface_show_page(surf);
    }

    cairo_status_t status = cairo_status(cr);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));

    cairo_surface_finish(surf);
    status = cairo_surface_status(surf);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));

    return 0;
}
