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

#define _XOPEN_SOURCE 700

#include "jkpdf-io.h"
#include "jkpdf-parsesize.h"
#include "jkpdf-transform.h"

int
main(int argc, char **argv)
{
    g_autofree gchar *arg_overlap = NULL;

    GOptionEntry option_entries[] = {
        { "overlap", 'o', 0, G_OPTION_ARG_STRING, &arg_overlap, "Overlap", "LENGTH" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("[COLSxROWS] <INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Transform the PDF read from stdin to a n-down'ed PDF written to stdout.\n"
        "\n"
        "To 'n-down' a PDF is the reverse of 'n-up'ing it, i.e. split every page\n"
        "into multiple pages. For example, calling 'jkpdftool-ndown 3x2' will split\n"
        "each page into six pages. Each output page will be 1/3 as wide and 1/2 as\n"
        "high as the originating input page.\n");

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    if (argc > 2) {
        fprintf(stderr, "ERROR: expected at most one positional argument\n");
        return 1;
    }

    int arg_rows = 0;
    int arg_cols = 0;

    if (argc >= 2 && argv[1][0]) {
        if (!jkpdf_parse_integral_size(argv[1], &arg_cols, &arg_rows, &error)) {
            fprintf(stderr, "ERROR: invalid n-down specification '%s': %s\n", argv[1], error->message);
            return 1;
        }
    }

    double overlap_pt = 0;
    if (arg_overlap) {
        if (!jkpdf_parse_single_length(arg_overlap, &overlap_pt, &error)) {
            fprintf(stderr, "ERROR: invalid overlap size: %s\n", error->message);
            return 1;
        }
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        double w, h;
        poppler_page_get_size(page, &w, &h);

        int rows = arg_rows;
        int cols = arg_cols;
        if (rows == 0 || cols == 0) {
            if (w < h) {
                rows = 2;
                cols = 1;
            } else {
                rows = 1;
                cols = 2;
            }
        }

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                double pagewidth = round(w/cols);
                double pageheight = round(h/rows);
                cairo_pdf_surface_set_size(surf, pagewidth, pageheight);

                cairo_save(cr);

                cairo_rectangle_t source_r = { x * w / cols, y * h / rows, w/cols, h/rows };
                cairo_rectangle_t page_r = { overlap_pt, overlap_pt, pagewidth - 2*overlap_pt, pageheight - 2*overlap_pt };

                cairo_rectangle(cr, 0, 0, pagewidth, pageheight);
                cairo_clip(cr);

                cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
                cairo_transform(cr, &m);

                poppler_page_render_for_printing(page, cr);

                cairo_restore(cr);

                cairo_surface_show_page(surf);
            }
        }
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

