// Copyright © 2018-2024 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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

int
main(int argc, char **argv)
{
    g_autofree gchar *arg_left = NULL;
    g_autofree gchar *arg_top = NULL;
    g_autofree gchar *arg_width = NULL;
    g_autofree gchar *arg_height = NULL;
    gint pageno = 1;

    GOptionEntry option_entries[] = {
        { "page",   'p', 0, G_OPTION_ARG_INT,    &pageno,   "Page",   "1" },
        { "left",   'x', 0, G_OPTION_ARG_STRING, &arg_left,   "Left",   "0" },
        { "top",    'y', 0, G_OPTION_ARG_STRING, &arg_top,    "Top",    "0" },
        { "width",  'w', 0, G_OPTION_ARG_STRING, &arg_width,  "Width",  "0" },
        { "height", 'h', 0, G_OPTION_ARG_STRING, &arg_height, "Height", "0" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Cut out a piece of a PDF document.\n"
        "\n"
        "The PDF file is read from standard input, and the transformed PDF file is\n"
        "written onto the standard output.\n"
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

    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    if (arg_left && !jkpdf_parse_single_length(arg_left, &x, &error)) {
        fprintf(stderr, "ERROR: invalid --left '%s': %s\n", arg_left, error->message);
        return 1;
    }
    if (arg_top && !jkpdf_parse_single_length(arg_top, &y, &error)) {
        fprintf(stderr, "ERROR: invalid --top '%s': %s\n", arg_top, error->message);
        return 1;
    }
    if (arg_width && !jkpdf_parse_single_length(arg_width, &w, &error)) {
        fprintf(stderr, "ERROR: invalid --width '%s': %s\n", arg_width, error->message);
        return 1;
    }
    if (arg_height && !jkpdf_parse_single_length(arg_height, &h, &error)) {
        fprintf(stderr, "ERROR: invalid --height '%s': %s\n", arg_height, error->message);
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();

    if (pageno < 1 || pageno > poppler_document_get_n_pages(doc)) {
        fprintf(stderr, "ERROR: invalid page number %d\n", pageno);
        return 1;
    }

    g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno-1);


    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();
    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);


    double pw, ph;
    poppler_page_get_size(page, &pw, &ph);

    if (x < 0.0)
        x = 0;
    if (y < 0.0)
        y = 0;
    if (w <= 0.0)
        w = pw - x;
    if (h <= 0.0)
        h = ph - y;

    cairo_save(cr);

    cairo_pdf_surface_set_size(surf, w, h);
    cairo_translate(cr, -x, -y);

    poppler_page_render_for_printing(page, cr);

    cairo_restore(cr);
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
