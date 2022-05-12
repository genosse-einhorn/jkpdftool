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
#include "jkpdf-parsesize.h"
#include "jkpdf-transform.h"

static inline void
swap_doubles(double *a, double *b)
{
    double tmp = *a;
    *a = *b;
    *b = tmp;
}

int
main(int argc, char **argv)
{
    g_autofree gchar *arg_size = NULL;
    g_autofree gchar *arg_orientation = NULL;
    g_autofree gchar *arg_margin = NULL;
    g_autofree gchar *arg_halign = NULL;
    g_autofree gchar *arg_valign = NULL;

    GOptionEntry option_entries[] = {
        { "size",        's', 0, G_OPTION_ARG_STRING, &arg_size, "Page size", "WIDTHxHEIGHT" },
        { "orientation", 'o', 0, G_OPTION_ARG_STRING, &arg_orientation, "Orientation (landscape or portrait)", "ORIENTATION" },
        { "margin",      'm', 0, G_OPTION_ARG_STRING, &arg_margin, "Additional margin", "MARGIN" },
        { "halign",      0,   0, G_OPTION_ARG_STRING, &arg_halign, "Horizontal Alignment", "left|center|right" },
        { "valign",      0,   0, G_OPTION_ARG_STRING, &arg_valign, "Vertical Alignment", "top|center|bottom" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Scale a PDF to fit onto a given page format.\n"
        "\n"
        "The PDF file is read from standard input, and the transformed PDF file is\n"
        "written onto the standard output.\n"
        "\n"
        "Page size:\n"
        "  Use the --size option to set the paper size. Fixed sizes A3, A4, A5 are\n"
        "  supported, as well as custom page sizes WIDTHxHEIGHT. Width and height\n"
        "  can be specified in millimeters (mm) or points (pt, the default).\n"
        "  If you don't specify a page size, the size of the source page is used.\n"
        "\n"
        "Page orientation:\n"
        "  The --orientation=portrait or --orientation=landscape flags force portrait\n"
        "  or landscape orientation. By default, the orientation of the source page\n"
        "  is used. Note: this does not rotate the page content.\n"
        "\n"
        "Additional margins:\n"
        "  The --margin option takes one to four comma-separated arguments:\n"
        "    --margin=ALL\n"
        "    --margin=VERTICAL,HORIZONTAL\n"
        "    --margin=TOP,HORIZONTAL,BOTTOM\n"
        "    --margin=TOP,RIGHT,BOTTOM,LEFT\n"
        "  Margins can be specified in millimeters (mm) or points (pt, the default).\n"
    );

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    if (argc > 1) {
        fprintf(stderr, "ERROR: unexpected positional argument '%s'\n", argv[1]);
        return 1;
    }

    double arg_width = 0.0;
    double arg_height = 0.0;

    if (arg_size && !jkpdf_parse_paper_size(&arg_width, &arg_height, arg_size, &error)) {
        fprintf(stderr, "ERROR: invalid paper size '%s': %s\n", arg_size, error->message);
        return 1;
    }

    enum JkpdfOrientation orientation = jkpdf_parse_orientation(arg_orientation);
    if (orientation == JKPDF_ORIENTATION_INVALID) {
        fprintf(stderr, "ERROR: invalid orientation '%s'\n", arg_orientation);
        return 1;
    }

    double margins[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (arg_margin && !jkpdf_parse_margin_spec(arg_margin, margins, &error)) {
        fprintf(stderr, "ERROR: invalid margin specification '%s': %s\n", arg_margin, error->message);
        return 1;
    }

    JkPdfAlignment halign = JKPDF_ALIGN_CENTER;
    JkPdfAlignment valign = JKPDF_ALIGN_CENTER;
    if (arg_halign != NULL) {
        if (!g_ascii_strcasecmp(arg_halign, "left")) {
            halign = JKPDF_ALIGN_START;
        } else if (!g_ascii_strcasecmp(arg_halign, "center")) {
            halign = JKPDF_ALIGN_CENTER;
        } else if (!g_ascii_strcasecmp(arg_halign, "right")) {
            halign = JKPDF_ALIGN_END;
        } else {
            fprintf(stderr, "ERROR: invalid horizontal alignment '%s', must be one of left, center or right\n", arg_halign);
            return 1;
        }
    }

    if (arg_valign != NULL) {
        if (!g_ascii_strcasecmp(arg_valign, "top")) {
            valign = JKPDF_ALIGN_START;
        } else if (!g_ascii_strcasecmp(arg_valign, "center")) {
            valign = JKPDF_ALIGN_CENTER;
        } else if (!g_ascii_strcasecmp(arg_valign, "bottom")) {
            valign = JKPDF_ALIGN_END;
        } else {
            fprintf(stderr, "ERROR: invalid vertical alignment '%s', must be one of top, center or bottom\n", arg_valign);
            return 1;
        }
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        cairo_rectangle_t source_r = { 0, 0, 0, 0 };
        cairo_rectangle_t page_r = { 0, 0, arg_width, arg_height };
        poppler_page_get_size(page, &source_r.width, &source_r.height);

        if (arg_width == 0.0 || arg_height == 0.0) {
            page_r.width = source_r.width;
            page_r.height = source_r.height;
        }

        if (orientation == JKPDF_ORIENTATION_AUTO) {
            if ((source_r.width > source_r.height && page_r.width < page_r.height) ||
                (source_r.width < source_r.height && page_r.width > page_r.height)) {
                swap_doubles(&page_r.width, &page_r.height);
            }
        } else if (orientation == JKPDF_ORIENTATION_LANDSCAPE) {
            if (page_r.width < page_r.height) {
                swap_doubles(&page_r.width, &page_r.height);
            }
        } else if (orientation == JKPDF_ORIENTATION_PORTRAIT) {
            if (page_r.width > page_r.height) {
                swap_doubles(&page_r.width, &page_r.height);
            }
        }

        cairo_pdf_surface_set_size(surf, page_r.width, page_r.height);
        cairo_save(cr);

        if (margins[0] + margins[2] >= page_r.height || margins[1] + margins[3] >= page_r.width) {
            fprintf(stderr, "ERROR: while processing page %d: margins greater than the page\n", pageno+1);
            return 1;
        }

        page_r.x += margins[3];
        page_r.y += margins[0];
        page_r.width -= margins[3] + margins[1];
        page_r.height -= margins[0] + margins[2];

        cairo_matrix_t m = jkpdf_transform_rect_into_bounds_with_alignment(source_r, page_r, halign, valign);
        cairo_transform(cr, &m);
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
