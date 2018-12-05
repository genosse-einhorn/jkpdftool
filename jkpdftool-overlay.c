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
#include "jkpdf-detect-bug104864.h"
#include <stdbool.h>

static inline bool
jkpdf_parse_offset_spec(const char *spec, double offsets[2], GError **error)
{
    enum JkpdfSizeUnit units[2] = { JKPDF_SIZE_UNIT_UNSPECIFIED, JKPDF_SIZE_UNIT_UNSPECIFIED };

    size_t i = 0;

    size_t w = jkpdf_parse_floatval_with_unit(spec, i, &offsets[0], &units[0], error);
    if (w == 0)
        return false;

    i += w;

    if (spec[i] != ',') {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, spec[i], i);
        return false;
    }

    i++;

    w = jkpdf_parse_floatval_with_unit(spec, i, &offsets[1], &units[1], error);
    if (w == 0)
        return false;

    i += w;

    if (spec[i]) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, spec[i], i);
        return false;
    }

    if (units[1] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[1] = JKPDF_SIZE_UNIT_PT;
    if (units[0] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[0] = units[1];

    offsets[0] = jkpdf_size_in_pt(offsets[0], units[0]);
    offsets[1] = jkpdf_size_in_pt(offsets[1], units[1]);

    return true;
}

int main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *arg_offset  = NULL;
    g_auto(GStrv)     arg_overlays = NULL;

    GOptionEntry option_entries[] = {
        { "offset", 'o', 0, G_OPTION_ARG_STRING, &arg_offset, "Offset", "X,Y" },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &arg_overlays, "Overlay files", "FILENAME..." },
        { NULL }
    };

    g_autoptr(GOptionContext) context = g_option_context_new("OVERLAY... <INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Overlay PDF files onto another");

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    double offset[2] = { 0.0, 0.0 };
    if (arg_offset) {
        if (!jkpdf_parse_offset_spec(arg_offset, offset, &error)) {
            fprintf(stderr, "ERROR: invalid offset '%s': %s\n", arg_offset, error->message);
            return 1;
        }
    }

    if (!arg_overlays) {
        fprintf(stderr, "ERROR: expected at least one overlay file\n");
        return 1;
    }

    // create inputs and output
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();
    g_autoptr(JKPdfPopplerDocument) main_doc = jkpdf_create_poppler_document_for_stdin();

    unsigned overlay_count = g_strv_length(arg_overlays);
    g_autoptr(GPtrArray) docarr = g_ptr_array_new_full(overlay_count + 1, g_object_unref);
    g_ptr_array_add(docarr, g_object_ref(main_doc));
    for (unsigned i = 0; i < overlay_count; ++i) {
        PopplerDocument *doc = jkpdf_create_poppler_document_for_commandline_arg(arg_overlays[i]);

        g_ptr_array_add(docarr, doc);
    }

    // HACK!
    jkpdf_warn_bug104864(docarr->len, (PopplerDocument**)docarr->pdata);

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int i = 0; i < poppler_document_get_n_pages(main_doc); ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(main_doc, i);

        double w, h;
        poppler_page_get_size(page, &w, &h);

        cairo_pdf_surface_set_size(surf, w, h);

        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_clip(cr);

        poppler_page_render_for_printing(page, cr);

        cairo_translate(cr, offset[0], offset[1]);

        for (unsigned k = 1; k <= overlay_count; ++k) {
            if (i >= poppler_document_get_n_pages(docarr->pdata[k]))
                continue;

            g_autoptr(JKPdfPopplerPage) overlay_page = poppler_document_get_page(docarr->pdata[k], i);

            poppler_page_render_for_printing(overlay_page, cr);
        }

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
}
