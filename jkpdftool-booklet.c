// Copyright © 2021 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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
    printf("  %s  <INPUT-PDF  >OUTPUT-PDF\n", argv0);
    printf("\n");
    printf("Transform the PDF file read from stdin to a booklet written to stdout.\n");
    printf("\n");
    printf("The resulting PDF is made in such a way that if you print it duplex\n");
    printf("and then fold it in the middle, you have a booklet\n");
}

int
main(int argc, char **argv)
{
    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-?"))) {
        print_help(argv[0]);
        return 0;
    }

    if (argc > 1) {
        fprintf(stderr, "ERROR: expected no arguments, see '%s --help'\n", argv[0]);
        return 1;
    }


    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    int n_input_pages = poppler_document_get_n_pages(doc);
    int n_output_sheets = (n_input_pages + 3) / 4;

    double output_w = 1.0, output_h = 1.0;

    {
        g_autoptr(JKPdfPopplerPage) page1 = poppler_document_get_page(doc, 0);
        if (page1) {
            double page_w, page_h;
            poppler_page_get_size(page1, &page_w, &page_h);

            output_w = page_h;
            output_h = page_w * 2;
        }
    }

    cairo_pdf_surface_set_size(surf, output_w, output_h);

    for (int i = 0; i < n_output_sheets; ++i) {
        int pageno1 = i * 2;
        int pageno2 = i * 2 + 1;
        int pageno3 = (n_output_sheets * 4 - i * 2 - 1);
        int pageno4 = (n_output_sheets * 4 - i * 2 - 2);

        if (pageno1 >= 0 && pageno1 < n_input_pages) {
            cairo_save(cr);

            g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno1);

            double page_w, page_h;
            poppler_page_get_size(page, &page_w, &page_h);

            cairo_rectangle_t source_r = { 0, -page_w, page_h, page_w };
            cairo_rectangle_t page_r = { 0, 0, output_w, output_h / 2 };

            cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
            cairo_transform(cr, &m);
            cairo_rotate(cr, -M_PI_2);

            poppler_page_render_for_printing(page, cr);

            cairo_restore(cr);
        }

        if (pageno3 >= 0 && pageno3 < n_input_pages) {
            cairo_save(cr);

            g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno3);

            double page_w, page_h;
            poppler_page_get_size(page, &page_w, &page_h);

            cairo_rectangle_t source_r = { 0, -page_w, page_h, page_w };
            cairo_rectangle_t page_r = { 0, output_h / 2, output_w, output_h / 2 };

            cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
            cairo_transform(cr, &m);
            cairo_rotate(cr, -M_PI_2);

            poppler_page_render_for_printing(page, cr);

            cairo_restore(cr);
        }

        cairo_surface_show_page(surf);

        if (pageno2 >= 0 && pageno2 < n_input_pages) {
            cairo_save(cr);

            g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno2);

            double page_w, page_h;
            poppler_page_get_size(page, &page_w, &page_h);

            cairo_rectangle_t source_r = { -page_h, 0, page_h, page_w };
            cairo_rectangle_t page_r = { 0, 0, output_w, output_h / 2 };

            cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
            cairo_transform(cr, &m);
            cairo_rotate(cr, M_PI_2);

            poppler_page_render_for_printing(page, cr);

            cairo_restore(cr);
        }

        if (pageno4 >= 0 && pageno4 < n_input_pages) {
            cairo_save(cr);

            g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno4);

            double page_w, page_h;
            poppler_page_get_size(page, &page_w, &page_h);

            cairo_rectangle_t source_r = { -page_h, 0, page_h, page_w };
            cairo_rectangle_t page_r = { 0, output_h / 2, output_w, output_h / 2 };

            cairo_matrix_t m = jkpdf_transform_rect_into_bounds(source_r, page_r);
            cairo_transform(cr, &m);
            cairo_rotate(cr, M_PI_2);

            poppler_page_render_for_printing(page, cr);

            cairo_restore(cr);
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

