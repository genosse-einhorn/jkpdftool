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
#include "jkpdf-transform.h"

#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

static inline void
rasterize(cairo_surface_t **psurf, PopplerPage *page, double dpi)
{
    double pagewidth, pageheight;
    poppler_page_get_size(page, &pagewidth, &pageheight);

    int imgwidth = (int)round(pagewidth * dpi / 72.0);
    int imgheight = (int)round(pageheight * dpi / 72.0);

    if (*psurf) {
        int oldwidth = cairo_image_surface_get_width(*psurf);
        int oldheight = cairo_image_surface_get_height(*psurf);

        if (oldwidth != imgwidth || oldheight != imgheight) {
            cairo_surface_destroy(*psurf);
            *psurf = NULL;
        }
    }

    if (!*psurf) {
        *psurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, imgwidth, imgheight);
    }

    g_autoptr(JKPdfCairoT) cr = cairo_create(*psurf);

    // white bg
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, imgwidth, imgheight);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_scale(cr, imgwidth/pagewidth, imgheight/pageheight);
    poppler_page_render_for_printing(page, cr);
    cairo_restore(cr);

    cairo_surface_flush(*psurf);

    // TRANSPARENCY HACK
    int imgstride = cairo_image_surface_get_stride(*psurf);
    unsigned char *imgdata = cairo_image_surface_get_data(*psurf);

    for (int y = 0; y < imgheight; ++y) {
        for (int x = 0; x < imgwidth; ++x) {
            uint32_t p;
            memcpy(&p, &imgdata[y * imgstride + x*4], 4);

            uint8_t a = (uint8_t)((p & 0xff000000) >> 24);
            uint8_t r = (uint8_t)((p & 0x00ff0000) >> 16);
            uint8_t g = (uint8_t)((p & 0x0000ff00) >> 8);
            uint8_t b = (uint8_t)((p & 0x000000ff));

            g_assert(a == 0xff);

            uint8_t whiteness = MIN(r, MIN(g, b));
            a = (uint8_t)(a - whiteness);
            r = (uint8_t)(r - whiteness);
            g = (uint8_t)(g - whiteness);
            b = (uint8_t)(b - whiteness);

            p = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            memcpy(&imgdata[y * imgstride + x*4], &p, 4);
        }
    }

    cairo_surface_mark_dirty(*psurf);
}

int
main(int argc, char **argv)
{
    double arg_resolution = 600;

    GOptionEntry option_entries[] = {
        { "resolution",       'r', 0, G_OPTION_ARG_DOUBLE, &arg_resolution, "Resolution to rasterize (default: 600)", "DPI" },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Rasterize PDF into images (contained in PDF).\n");

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "ERROR: option parsing failed: %s\n", error->message);
        return 1;
    }

    if (argc > 1) {
        fprintf(stderr, "ERROR: unexpected positional argument '%s'\n", argv[1]);
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    g_autoptr(JKPdfCairoSurfaceT) imgsurf = NULL;

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        double pagewidth, pageheight;
        poppler_page_get_size(page, &pagewidth, &pageheight);

        rasterize(&imgsurf, page, arg_resolution);

        cairo_save(cr);
        cairo_pdf_surface_set_size(surf, pagewidth, pageheight);

        double imgwidth = cairo_image_surface_get_width(imgsurf);
        double imgheight = cairo_image_surface_get_height(imgsurf);

        cairo_scale(cr, pagewidth/imgwidth, pageheight/imgheight);

        cairo_rectangle(cr, 0, 0, imgwidth, imgheight);
        cairo_set_source_surface(cr, imgsurf, 0, 0);
        cairo_fill(cr);

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
