// Copyright © 2019 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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
#include <stdbool.h>

struct JkpdfPasteSpec {
    int targetPage;
    double x;
    double y;
    double w;
    double h;
    int sourcePage;
    double sourceX;
    double sourceY;
};

enum JkpdfCopyPasteSpecType {
    JKPDF_CPS_TYPE_CUT,
    JKPDF_CPS_TYPE_COPY,
    JKPDF_CPS_TYPE_PASTE,
    JKPDF_CPS_TYPE_PASTE_INTO_CUT
};

static bool
jkpdf_parse_copy_paste_spec(const char *spec, enum JkpdfCopyPasteSpecType *type, int *opage, double *ox, double *oy, double *ow, double *oh)
{
    if (*spec == 'x' || *spec == 'c') {
        if (*spec == 'x')
            *type = JKPDF_CPS_TYPE_CUT;
        else
            *type = JKPDF_CPS_TYPE_COPY;

        spec++;
        if (*spec != ',')
            return false;

        spec++;

        int page = 0;
        while (*spec >= '0' && *spec <= '9')
            page = page * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;


        int x = 0;
        while (*spec >= '0' && *spec <= '9')
            x = x * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;

        int y = 0;
        while (*spec >= '0' && *spec <= '9')
            y = y * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;

        int w = 0;
        while (*spec >= '0' && *spec <= '9')
            w = w * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;

        int h = 0;
        while (*spec >= '0' && *spec <= '9')
            h = h * 10 + (*spec++ - '0');

        if (*spec)
            return false;

        *opage = page;
        *ox = x;
        *oy = y;
        *ow = w;
        *oh = h;

        return true;
    } else if (*spec == 'v') {
        *type = JKPDF_CPS_TYPE_PASTE;

        spec++;

        if (*spec == 'x') {
            *type = JKPDF_CPS_TYPE_PASTE_INTO_CUT;
            spec++;
        }

        if (*spec != ',')
            return false;

        spec++;

        int page = 0;
        while (*spec >= '0' && *spec <= '9')
            page = page * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;


        int x = 0;
        while (*spec >= '0' && *spec <= '9')
            x = x * 10 + (*spec++ - '0');

        if (*spec != ',')
            return false;

        spec++;

        int y = 0;
        while (*spec >= '0' && *spec <= '9')
            y = y * 10 + (*spec++ - '0');

        if (*spec)
            return false;

        *opage = page;
        *ox = x;
        *oy = y;

        return true;
    } else {
        // FIXME! error message
        return false;
    }
}

static inline void
jkpdf_render_poppler_page_via_recording_surface(cairo_t *cr, PopplerPage *p)
{
    cairo_save(cr);

    g_autoptr(JKPdfCairoSurfaceT) rs = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
    g_autoptr(JKPdfCairoT) crrs = cairo_create(rs);

    poppler_page_render_for_printing(p, crrs);

    cairo_set_source_surface(cr, rs, 0.0, 0.0);
    cairo_paint(cr);

    cairo_restore(cr);
}

int main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_auto(GStrv)     arg_commands = NULL;
    double            arg_dpi = 72;

    GOptionEntry option_entries[] = {
        { "dpi", 'd', 0, G_OPTION_ARG_DOUBLE, &arg_dpi, "DPI factor (default: 72)", "DPI" },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &arg_commands, "Copy/Paste spec", "..." },
        { NULL }
    };

    g_autoptr(GOptionContext) context = g_option_context_new("SPEC... <INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Copy and paste in PDF files (EXPERIMENTAL)");

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    if (!arg_commands) {
        fprintf(stderr, "ERROR: expected at least one copy/paste spec\n");
        return 1;
    }

    // create inputs and output
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();
    g_autoptr(JKPdfPopplerDocument) main_doc = jkpdf_create_poppler_document_for_stdin();

    g_autoptr(GArray) specarr = g_array_new(FALSE, TRUE, sizeof(struct JkpdfPasteSpec));
    struct JkpdfPasteSpec last = {0};
    for (unsigned i = 0; i < g_strv_length(arg_commands); ++i) {
        int p = 0;
        double x = 0, y = 0, w = 0, h = 0;
        enum JkpdfCopyPasteSpecType type;

        if (!jkpdf_parse_copy_paste_spec(arg_commands[i], &type, &p, &x, &y, &w, &h)) {
            fprintf(stderr, "ERROR: invalid copy/paste spec: ‘%s’\n", arg_commands[i]);
            return  1;
        }

        if (type == JKPDF_CPS_TYPE_CUT) {
            last.x = x / arg_dpi * 72.0;
            last.y = y / arg_dpi * 72.0;
            last.w = w / arg_dpi * 72.0;
            last.h = h / arg_dpi * 72.0;
            last.sourcePage = -1;
            last.sourceX = 0;
            last.sourceY = 0;
            last.targetPage = p;
            g_array_append_val(specarr, last);

            last.sourceX = last.x;
            last.sourceY = last.y;
            last.sourcePage = last.targetPage;
        } else if (type == JKPDF_CPS_TYPE_COPY) {
            last.sourceX = x / arg_dpi * 72.0;
            last.sourceY = y / arg_dpi * 72.0;
            last.sourcePage = p;
            last.w = w / arg_dpi * 72.0;
            last.h = h / arg_dpi * 72.0;
        } else if (type == JKPDF_CPS_TYPE_PASTE || type == JKPDF_CPS_TYPE_PASTE_INTO_CUT) {
            last.x = x / arg_dpi * 72.0;
            last.y = y / arg_dpi * 72.0;
            last.targetPage = p;

            if (last.sourcePage <= 0) {
                fprintf(stderr, "ERROR: paste before cut or copy\n");
                return 1;
            }


            if (type == JKPDF_CPS_TYPE_PASTE_INTO_CUT) {
                struct JkpdfPasteSpec t = last;
                t.sourcePage = -2;
                t.sourceX = 0;
                t.sourceY = 0;

                g_array_append_val(specarr, t);
            }

            g_array_append_val(specarr, last);
        }
    }

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int i = 0; i < poppler_document_get_n_pages(main_doc); ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(main_doc, i);

        double w, h;
        poppler_page_get_size(page, &w, &h);

        cairo_pdf_surface_set_size(surf, w, h);

        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_clip(cr);

        for (int j = (int)specarr->len - 1; j >= 0; --j) {
            struct JkpdfPasteSpec *ps = &g_array_index(specarr, struct JkpdfPasteSpec, j);

            if (ps->targetPage != i + 1)
                continue;

            if (ps->sourcePage == -2) {
                cairo_new_path(cr);
                cairo_new_sub_path(cr);
                cairo_move_to(cr, ps->x, ps->y);
                cairo_line_to(cr, ps->x + ps->w, ps->y);
                cairo_line_to(cr, ps->x + ps->w, ps->y + ps->h);
                cairo_line_to(cr, ps->x, ps->y + ps->h);
                cairo_close_path(cr);
                cairo_new_sub_path(cr);
                cairo_move_to(cr, 0, 0);
                cairo_line_to(cr, 0, h);
                cairo_line_to(cr, w, h);
                cairo_line_to(cr, w, 0);
                cairo_close_path(cr);

                cairo_clip(cr);
            }

            if (ps->sourcePage > 0) {
                g_autoptr(JKPdfPopplerPage) page2 = poppler_document_get_page(main_doc, ps->sourcePage - 1);

                cairo_save(cr);

                cairo_translate(cr, ps->x - ps->sourceX, ps->y - ps->sourceY);
                cairo_rectangle(cr, ps->sourceX, ps->sourceY, ps->w, ps->h);
                cairo_clip(cr);

                cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
                jkpdf_render_poppler_page_via_recording_surface(cr, page2);

                cairo_restore(cr);
            }
        }

        cairo_save(cr);

        for (unsigned j = 0; j < specarr->len; ++j) {
            struct JkpdfPasteSpec *ps = &g_array_index(specarr, struct JkpdfPasteSpec, j);

            if (ps->sourcePage < 0 && ps->targetPage == i + 1) {
                cairo_new_path(cr);
                cairo_new_sub_path(cr);
                cairo_move_to(cr, ps->x, ps->y);
                cairo_line_to(cr, ps->x + ps->w, ps->y);
                cairo_line_to(cr, ps->x + ps->w, ps->y + ps->h);
                cairo_line_to(cr, ps->x, ps->y + ps->h);
                cairo_close_path(cr);
                cairo_new_sub_path(cr);
                cairo_move_to(cr, 0, 0);
                cairo_line_to(cr, 0, h);
                cairo_line_to(cr, w, h);
                cairo_line_to(cr, w, 0);
                cairo_close_path(cr);

                cairo_clip(cr);
            }
        }

        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
        jkpdf_render_poppler_page_via_recording_surface(cr, page);

        cairo_restore(cr);

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

