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

static void
print_help(const char *argv0)
{
    printf("Usage:\n");
    printf("  %s [COLSxROWS]  <INPUT-PDF  >OUTPUT-PDF\n", argv0);
    printf("\n");
    printf("Transform the PDF file read from stdin to a n-up'ed PDF file written to stdout.\n");
    printf("\n");
    printf("To 'n-up' a PDF means to arrange multiple input pages on one output page for\n");
    printf("more economical printing. For example, calling 'jkpdf-nup 3x2' will arrange\n");
    printf("six input pages in a 3x2 grid on the output page. The output page will be\n");
    printf("three times as wide and two times as high as the first input page.\n");
    printf("\n");
    printf("If not specified, two input pages will be printed per output page.\n");
}

int
main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "ERROR: expected at most one argument, see '%s --help'\n", argv[0]);
        return 1;
    }
    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-?"))) {
        print_help(argv[0]);
        return 0;
    }

    int arg_rows = 0;
    int arg_cols = 0;

    if (argc >= 2 && argv[1][0]) {
        g_autoptr(GError) error = NULL;
        if (!jkpdf_parse_integral_size(argv[1], &arg_cols, &arg_rows, &error)) {
            fprintf(stderr, "ERROR: Invalid n-up specification '%s': %s\n", argv[1], error->message);
            return 1;
        }
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    int pageno = 0;
    g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);
    while (pageno < poppler_document_get_n_pages(doc)) {
        double w, h;
        poppler_page_get_size(page, &w, &h);

        int rows = arg_rows;
        int cols = arg_cols;
        if (rows == 0 || cols == 0) {
            if (w > h) {
                rows = 2;
                cols = 1;
            } else {
                rows = 1;
                cols = 2;
            }
        }

        cairo_pdf_surface_set_size(surf, w * cols, h * rows);

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                if (page) {
                    cairo_save(cr);

                    cairo_rectangle_t page_r = { x * w, y * h, w, h };
                    cairo_rectangle_t source_r = { 0, 0, 0, 0 };
                    poppler_page_get_size(page, &source_r.width, &source_r.height);

                    cairo_rectangle(cr, page_r.x, page_r.y, page_r.width, page_r.height);
                    cairo_clip(cr);

                    cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
                    cairo_transform(cr, &m);

                    poppler_page_render_for_printing(page, cr);

                    cairo_restore(cr);
                }

                g_clear_object(&page);
                pageno++;
                if (pageno < poppler_document_get_n_pages(doc)) {
                    page = poppler_document_get_page(doc, pageno);
                }
            }
        }

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
