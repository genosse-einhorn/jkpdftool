// Copyright © 2024 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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
    printf("  %s <INPUT-PDF  >OUTPUT-PDF\n", argv0);
    printf("\n");
    printf("Mirror the PDF\n");
}

int
main(int argc, char **argv)
{
    if (argc != 1) {
        print_help(argv[0]);
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        cairo_rectangle_t source_r = { 0, 0, 0, 0 };
        poppler_page_get_size(page, &source_r.width, &source_r.height);

        cairo_pdf_surface_set_size(surf, source_r.width, source_r.height);

        cairo_save(cr);

        cairo_translate(cr, source_r.width, 0);
        cairo_scale(cr, -1, 1);
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

