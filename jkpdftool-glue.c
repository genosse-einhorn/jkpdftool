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


int
main(int argc, char **argv)
{
    g_autofree gchar *arg_margin = NULL;

    GOptionEntry option_entries[] = {
        { "margin",      'm', 0, G_OPTION_ARG_STRING, &arg_margin, "space between glued pages", "MARGIN" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Glue pages together onto a single page.\n"
        "\n"
        "The PDF file is read from standard input, and the transformed PDF file is\n"
        "written onto the standard output.\n"
        "\n"
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

    double margin = 0.0;
    if (arg_margin && !jkpdf_parse_single_length(arg_margin, &margin, &error)) {
        fprintf(stderr, "ERROR: invalid --margin '%s': %s\n", arg_margin, error->message);
        return 1;
    }


    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    int n_pages = poppler_document_get_n_pages(doc);

    double output_w = 1.0, output_h = -margin;

    for (int i = 0; i < n_pages; ++i) {
        g_autoptr(JKPdfPopplerPage) p = poppler_document_get_page(doc, i);
        double page_w, page_h;
        poppler_page_get_size(p, &page_w, &page_h);

        output_w = output_w < page_w ? page_w : output_w;
        output_h += page_h + margin;
    }

    cairo_pdf_surface_set_size(surf, output_w, output_h);

    double y = 0.0;
    for (int i = 0; i < n_pages; ++i) {
        cairo_save(cr);

        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, i);

        double page_w, page_h;
        poppler_page_get_size(page, &page_w, &page_h);

        cairo_translate(cr, 0, y);

        poppler_page_render_for_printing(page, cr);

        cairo_restore(cr);

        y += page_h + margin;
    }

    cairo_surface_show_page(surf);

    cairo_status_t status = cairo_status(cr);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));

    cairo_surface_finish(surf);
    status = cairo_surface_status(surf);
    if (status)
        fprintf(stderr, "WTF: cairo status: %s\n", cairo_status_to_string(status));

    return 0;
}

