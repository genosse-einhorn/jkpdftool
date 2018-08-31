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

static inline int
parse_hexdigit(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    } else if (digit >= 'a' && digit <= 'f') {
        return 10 + (digit - 'a');
    } else if (digit >= 'A' && digit <= 'F') {
        return 10 + (digit - 'A');
    } else {
        return -1;
    }
}

static inline bool
parse_color_spec(const char *color, float *r, float *g, float *b)
{
    int i = 0, j = 0;

    i = parse_hexdigit(color[0]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[1]);
    if (j < 0)
        return false;
    *r = ((float)(i * 0x10 + j) / 0xFF);

    i = parse_hexdigit(color[2]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[3]);
    if (j < 0)
        return false;
    *g = ((float)(i * 0x10 + j) / 0xFF);

    i = parse_hexdigit(color[4]);
    if (i < 0)
        return false;
    j = parse_hexdigit(color[5]);
    if (j < 0)
        return false;
    *b = ((float)(i * 0x10 + j) / 0xFF);

    return color[6] == 0;
}

struct crop_bounds {
    double left;
    double right;
    double top;
    double bottom;
};

static inline struct crop_bounds
calc_crop_bounds(PopplerPage *page, double dpi, float bg_r, float bg_g, float bg_b)
{
    struct crop_bounds retval = { 0.0, 0.0, 0.0, 0.0 };

    double pagewidth, pageheight;
    poppler_page_get_size(page, &pagewidth, &pageheight);

    int surfwidth  = (int)(pagewidth / 72.0 * dpi);
    int surfheight = (int)(pageheight / 72.0 * dpi);

    uint32_t bgcolor = (0xffu << 24) | (uint32_t)(bg_r * 0xff) << 16 | (uint32_t)(bg_g * 0xff) << 8 | (uint32_t)(bg_b * 0xff);

    g_autoptr(JKPdfCairoSurfaceT) img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfwidth, surfheight);

    g_autoptr(JKPdfCairoT) cr = cairo_create(img);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_rectangle(cr, 0, 0, surfwidth, surfheight);
    cairo_fill(cr);

    cairo_scale(cr, surfwidth / pagewidth, surfheight / pageheight);
    poppler_page_render_for_printing(page, cr);

    cairo_surface_flush(img);

    int stride = cairo_image_surface_get_stride(img);
    unsigned char *data = cairo_image_surface_get_data(img);

    int min_left = INT_MAX, min_right  = INT_MAX;
    int min_top  = INT_MAX, min_bottom = INT_MAX;
    for (int y = 0; y < surfheight; ++y) {
        unsigned char *row = data + y * stride;

        int left = INT_MAX, right = INT_MAX;

        for (int x = 0; x < surfwidth; ++x) {
            if (memcmp(&row[4*x], &bgcolor, 4)) {
                left = MIN(left, x);
                right = MIN(surfwidth - 1 - x, right);
            }
        }

        min_left = MIN(left, min_left);
        min_right = MIN(right, min_right);

        if (left != INT_MAX || right != INT_MAX) {
            min_top = MIN(min_top, y);
            min_bottom = MIN(min_bottom, surfheight - 1 - y);
        }
    }

    retval.left   = min_left   == INT_MAX ? 0 : (double)min_left   * (pagewidth / surfwidth);
    retval.right  = min_right  == INT_MAX ? 0 : (double)min_right  * (pagewidth / surfwidth);
    retval.top    = min_top    == INT_MAX ? 0 : (double)min_top    * (pageheight / surfheight);
    retval.bottom = min_bottom == INT_MAX ? 0 : (double)min_bottom * (pageheight / surfheight);

    return retval;
}

static inline struct crop_bounds
calc_crop_bounds_for_all(PopplerDocument *doc, double dpi, float bg_r, float bg_g, float bg_b)
{
    struct crop_bounds retval = { INFINITY, INFINITY, INFINITY, INFINITY };

    for (int i = 0; i < poppler_document_get_n_pages(doc); ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, i);

        struct crop_bounds b = calc_crop_bounds(page, dpi, bg_r, bg_b, bg_g);

        retval.left   = MIN(retval.left, b.left);
        retval.right  = MIN(retval.right, b.right);
        retval.top    = MIN(retval.top, b.top);
        retval.bottom = MIN(retval.bottom, b.bottom);
    }

    return retval;
}

int
main(int argc, char **argv)
{
    g_autofree gchar *arg_bgcolor = NULL;
    double arg_resolution = 72;
    gboolean arg_per_page = FALSE;

    GOptionEntry option_entries[] = {
        { "background-color", 'c', 0, G_OPTION_ARG_STRING, &arg_bgcolor,    "Background color to crop (default: white)", "RRGGBB" },
        { "resolution",       'r', 0, G_OPTION_ARG_DOUBLE, &arg_resolution, "Resolution to detect content (default: 72)", "DPI" },
        { "per-page",         'p', 0, G_OPTION_ARG_NONE,   &arg_per_page,   "Calculate offsets per page (default: no)", NULL },
        { NULL }
    };

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) context = g_option_context_new("<INPUT >OUTPUT");
    g_option_context_add_main_entries(context, option_entries, NULL);

    g_option_context_set_description(context, "Remove empty borders around PDF content.\n"
        "\n"
        "The PDF file is read from standard input, and the transformed PDF file is\n"
        "written onto the standard output.\n"
        "\n"
        "Influencing border detection:\n"
        "  Croppable borders are detected by rastering the page and checking for\n"
        "  pixels in background color. You can use --background-color=RRGGBB to\n"
        "  set a different background color and --resolution to influence the\n"
        "  dpi used in the rastering.\n"
        "\n"
        "Per page cropping mode:\n"
        "  By default, crop bounds are calculated so that all pages are cropped by\n"
        "  the same amount on each side. This allows you to chain jkpdf-crop(1) with\n"
        "  jkpdf-pagefit(1) and have all pages scaled identically.\n"
        "  With the --per-page option, this can be changed so that every page is\n"
        "  cropped individually. With per page cropping, pages will end up having\n"
        "  different sizes.\n"
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

    float r = 1.0f, g = 1.0f, b = 1.0f;
    struct crop_bounds global_bounds;

    if (arg_bgcolor && !parse_color_spec(arg_bgcolor, &r, &g, &b)) {
        fprintf(stderr, "ERROR: not a valid color: %s\n", arg_bgcolor);
        return 1;
    }

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    if (!arg_per_page) {
        global_bounds = calc_crop_bounds_for_all(doc, arg_resolution, r, g, b);
    }

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        double pagewidth, pageheight;
        poppler_page_get_size(page, &pagewidth, &pageheight);

        struct crop_bounds bounds;
        if (arg_per_page) {
            bounds = calc_crop_bounds(page, arg_resolution, r, g, b);
        } else {
            bounds = global_bounds;
        }

        cairo_rectangle_t source = { bounds.left, bounds.top,
            pagewidth - bounds.left - bounds.right, pageheight - bounds.top - bounds.bottom };
        cairo_rectangle_t target = { 0.0, 0.0, round(source.width), round(source.height) };
        cairo_matrix_t t = jkpdf_transform_rect_into_bounds(source, target);

        cairo_save(cr);

        cairo_pdf_surface_set_size(surf, target.width, target.height);
        cairo_transform(cr, &t);

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
