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
    g_autofree gchar *arg_move_x = NULL;
    g_autofree gchar *arg_move_y = NULL;
    g_autofree gchar *arg_correct_x = NULL;
    g_autofree gchar *arg_correct_y = NULL;

    GOptionEntry option_entries[] = {
        { "move-x",        'x', 0, G_OPTION_ARG_STRING, &arg_move_x,   "distance in X direction", "0" },
        { "move-y",        'y', 0, G_OPTION_ARG_STRING, &arg_move_y,   "distance in Y direction", "0" },
        { "correct-odd-x", 'c', 0, G_OPTION_ARG_STRING, &arg_correct_x, "correction value for odd pages (x)", "0" },
        { "correct-odd-y", 'v', 0, G_OPTION_ARG_STRING, &arg_correct_y, "correction value for odd pages (y)", "0" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Move page content for duplex printing\n"
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

    double move_x = 0.0;
    double move_y = 0.0;
    double correct_x = 0.0;
    double correct_y = 0.0;

    if (arg_move_x && !jkpdf_parse_single_length(arg_move_x, &move_x, &error)) {
        fprintf(stderr, "ERROR: invalid --move-x '%s': %s\n", arg_move_x, error->message);
        return 1;
    }
    if (arg_move_y && !jkpdf_parse_single_length(arg_move_y, &move_y, &error)) {
        fprintf(stderr, "ERROR: invalid --move-y '%s': %s\n", arg_move_x, error->message);
        return 1;
    }
    if (arg_correct_x && !jkpdf_parse_single_length(arg_correct_x, &correct_x, &error)) {
        fprintf(stderr, "ERROR: invalid --correct-odd-x '%s': %s\n", arg_correct_x, error->message);
        return 1;
    }
    if (arg_correct_y && !jkpdf_parse_single_length(arg_correct_y, &correct_y, &error)) {
        fprintf(stderr, "ERROR: invalid --correct-odd-y '%s': %s\n", arg_correct_y, error->message);
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();

    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();
    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int i = 0; i < poppler_document_get_n_pages(doc); ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, i);

        double pw, ph;
        poppler_page_get_size(page, &pw, &ph);

        cairo_save(cr);

        cairo_pdf_surface_set_size(surf, pw, ph);

        if (i % 2 == 0) {
            // odd page
            cairo_translate(cr, move_x + correct_x, move_y + correct_y);
        } else {
            // even page
            cairo_translate(cr, -move_x, -move_y);
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
