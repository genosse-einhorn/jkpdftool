// Copyright © 2018 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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
#include "jkpdf-detect-bug104864.h"
#include <stdbool.h>

typedef struct {
    int doc_count;
    int total_page_count;
    struct {
        PopplerDocument *doc;
        int start_page;
        int end_page;
    } documents[];
} DocumentCollection;

static void
document_collection_free(DocumentCollection *coll)
{
    for (int i = 0; i < coll->doc_count; ++i) {
        g_clear_object(&coll->documents[i].doc);
    }

    g_free(coll);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DocumentCollection, document_collection_free)

static DocumentCollection *
document_collection_new_from_filelist(GStrv filenames, GError **error)
{
    unsigned count = g_strv_length(filenames);

    g_autoptr(DocumentCollection) coll = g_malloc0(sizeof(*coll) + count * sizeof(coll->documents[0]));

    coll->doc_count = (int)count;
    coll->total_page_count = 0;

    for (unsigned i = 0; i < count; ++i) {
        g_autoptr(GFile) file = g_file_new_for_commandline_arg(filenames[i]);
        coll->documents[i].doc = poppler_document_new_from_gfile(file, NULL, NULL, error);
        if (!coll->documents[i].doc) {
            g_prefix_error(error, "While opening '%s': ", filenames[i]);
            return NULL;
        }

        coll->documents[i].start_page = coll->total_page_count;
        coll->total_page_count += poppler_document_get_n_pages(coll->documents[i].doc);
        coll->documents[i].end_page = coll->total_page_count;
    }

    return g_steal_pointer(&coll);
}

static DocumentCollection *
document_collection_new_from_one_doc(PopplerDocument *doc)
{
    DocumentCollection *coll = g_malloc0(sizeof(*coll) + sizeof(coll->documents[0]));
    coll->doc_count = 1;
    coll->total_page_count = poppler_document_get_n_pages(doc);
    coll->documents[0].doc = g_object_ref(doc);
    coll->documents[0].start_page = 0;
    coll->documents[0].end_page = coll->total_page_count;
    return coll;
}

static PopplerPage *
document_collection_get_page(DocumentCollection *coll, int pageno)
{
    for (int i = 0; i < coll->doc_count; ++i) {
        if (pageno >= coll->documents[i].start_page && pageno < coll->documents[i].end_page) {
            return poppler_document_get_page(coll->documents[i].doc, pageno - coll->documents[i].start_page);
        }
    }

    g_return_val_if_reached(NULL);
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

static void
print_page(DocumentCollection *coll, cairo_t *cr, cairo_surface_t *surf, int pageno)
{
    g_autoptr(JKPdfPopplerPage) page = document_collection_get_page(coll, pageno-1);
    g_return_if_fail(page != NULL);

    double w, h;
    poppler_page_get_size(page, &w, &h);

    cairo_pdf_surface_set_size(surf, w, h);

    poppler_page_render_for_printing(page, cr);

    cairo_surface_show_page(surf);
}

static bool
print_output(DocumentCollection *coll, cairo_surface_t *surf, GArray *page_range, GError **error)
{
    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (size_t i = 0; i < page_range->len; ++i) {
        struct range_expr range = g_array_index(page_range, struct range_expr, i);
        int inc = range.begin <= range.end ? 1 : -1;
        for (int pageno = range.begin; (inc > 0 && pageno <= range.end) || (inc < 0 && pageno >= range.end); pageno += inc) {
            if (pageno < 1 || pageno > coll->total_page_count) {
                g_set_error(error, RANGE_ERROR, RANGE_ERROR_PAGENOTFOUND, "Page %d not found", pageno);
                return false;
            }

            print_page(coll, cr, surf, pageno);
        }
    }

    cairo_status_t status = cairo_status(cr);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));

    return true;
}

int main(int argc, char **argv)
{
    g_autofree gchar *arg_pages  = NULL;
    g_auto(GStrv)     arg_inputs = NULL;

    GOptionEntry option_entries[] = {
        { "pages", 'p', 0, G_OPTION_ARG_STRING, &arg_pages, "Page selector", "PAGESPEC" },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &arg_inputs, "Input files", "FILENAME..." },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new(">OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Concatenate or split PDF files.\n"
        "\n"
        "Input files:\n"
        "  One or more input files can be specified as arguments on the command line. If\n"
        "  and only if no input file is specified, the standard input is being used.\n"
        "\n"
        "Selecting pages:\n"
        "  The -p option can be used to specify a range of pages, e.g. '1-2,5,7'. Page\n"
        "  numbers are continguous over all input files.\n"
    );

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(DocumentCollection) coll = NULL;
    if (arg_inputs && *arg_inputs) {
        coll = document_collection_new_from_filelist(arg_inputs, &error);
        if (!coll) {
            fprintf(stderr, "ERROR: %s\n", error->message);
            return 1;
        }
    } else {
        coll = document_collection_new_from_one_doc(jkpdf_create_poppler_document_for_stdin());
    }

    { // HACK!
        g_autoptr(GPtrArray) docarr = g_ptr_array_new_full((unsigned)coll->doc_count, g_object_unref);
        for (int i = 0; i < coll->doc_count; ++i) {
            g_ptr_array_add(docarr, g_object_ref(coll->documents[i].doc));
        }

        jkpdf_warn_bug104864(docarr->len, (PopplerDocument**)docarr->pdata);
    }

    g_autoptr(GArray) page_range = NULL;
    if (arg_pages && *arg_pages) {
        page_range = parse_range(arg_pages, &error);
        if (!page_range) {
            fprintf(stderr, "ERROR: Invalid page selection: %s\n", error->message);
            return 1;
        }
    } else {
        page_range = g_array_new(FALSE, TRUE, sizeof(struct range_expr));
        g_array_append_val(page_range, ((struct range_expr){ 1, coll->total_page_count }));
    }

    if (!print_output(coll, surf, page_range, &error)) {
        fprintf(stderr, "ERROR: %s\n", error->message);
        return 1;
    }

    cairo_surface_finish(surf);
    cairo_status_t status = cairo_surface_status(surf);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));
}
