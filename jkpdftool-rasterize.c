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

static bool DEBUG_MODE = false;

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
}

static inline void
transparentize(cairo_surface_t *surf)
{
    cairo_surface_flush(surf);

    int imgwidth  = cairo_image_surface_get_width(surf);
    int imgheight = cairo_image_surface_get_height(surf);
    int imgstride = cairo_image_surface_get_stride(surf);
    unsigned char *imgdata = cairo_image_surface_get_data(surf);

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

    cairo_surface_mark_dirty(surf);
}

static inline void
make_grayscale(cairo_surface_t *surf)
{
    cairo_surface_flush(surf);

    int imgwidth  = cairo_image_surface_get_width(surf);
    int imgheight = cairo_image_surface_get_height(surf);
    int imgstride = cairo_image_surface_get_stride(surf);
    unsigned char *imgdata = cairo_image_surface_get_data(surf);

    for (int y = 0; y < imgheight; ++y) {
        for (int x = 0; x < imgwidth; ++x) {
            uint32_t p;
            memcpy(&p, &imgdata[y * imgstride + x*4], 4);

            uint8_t a = (uint8_t)((p & 0xff000000) >> 24);
            uint8_t r = (uint8_t)((p & 0x00ff0000) >> 16);
            uint8_t g = (uint8_t)((p & 0x0000ff00) >> 8);
            uint8_t b = (uint8_t)((p & 0x000000ff));

            uint8_t luminance = (uint8_t)((MIN(r, MIN(g, b)) + MAX(r, MAX(g, b))) / 2);

            p = ((uint32_t)a << 24) | ((uint32_t)luminance << 16) | ((uint32_t)luminance << 8) | (uint32_t)luminance;

            memcpy(&imgdata[y * imgstride + x*4], &p, 4);
        }
    }

    cairo_surface_mark_dirty(surf);
}

static inline bool
pixel_is_opaque(unsigned char *data, int imgstride, int x, int y)
{
    uint32_t p;
    memcpy(&p, &data[y * imgstride + x * 4], 4);

    return p > 0x00ffffff;
}

static inline int
border_left(unsigned char *data, int imgstride, int left, int y, int right)
{
    for (int i = 0; i < right-left; ++i) {
        if (pixel_is_opaque(data, imgstride, left + i, y))
            return i;
    }

    return right - left;
}

static inline int
border_right(unsigned char *data, int imgstride, int left, int y, int right)
{
    for (int i = 0; i < right-left; ++i) {
        if (pixel_is_opaque(data, imgstride, right - i - 1, y))
            return i;
    }

    return right - left;
}

static inline int
border_top(unsigned char *data, int imgstride, int top, int x, int bottom)
{
    for (int i = 0; i < bottom-top; ++i) {
        if (pixel_is_opaque(data, imgstride, x, top + i))
            return i;
    }

    return bottom - top;
}

static inline int
border_bottom(unsigned char *data, int imgstride, int top, int x, int bottom)
{
    for (int i = 0; i < bottom-top; ++i) {
        if (pixel_is_opaque(data, imgstride, x, bottom - i - 1))
            return i;
    }

    return bottom - top;
}

static inline void
emit_rect(unsigned char *data, int imgstride, int top, int right, int bottom, int left, cairo_t *cr)
{
    cairo_save(cr);

    if (DEBUG_MODE) {
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_rectangle(cr, left, top, right-left, bottom-top);
        cairo_stroke(cr);
    }

    // copy just this part of the image
    g_autoptr(JKPdfCairoSurfaceT) copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, right-left, bottom-top);
    int copystride = cairo_image_surface_get_stride(copy);
    unsigned char *copydata = cairo_image_surface_get_data(copy);

    for (int y = 0; y < bottom - top; ++y) {
        memcpy(&copydata[y * copystride], &data[(y + top) * imgstride + left * 4], (size_t)(right - left)*4);
    }
    cairo_surface_mark_dirty(copy);

    // by setting a content-dependent unique ID, the PDF surface will recognize duplicated images
    // and embed them only once. For text files with lots of identical glyphs, this will lead
    // to a dramatically reduced file size.
    g_autofree gchar *checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, copydata, (size_t)((bottom-top)*copystride));
    gchar *id = g_strdup_printf("jkpdftool-rasterize-surf-%s", checksum);
    cairo_surface_set_mime_data(copy, CAIRO_MIME_TYPE_UNIQUE_ID, (unsigned char*)id, strlen(id), free, id);

    cairo_translate(cr, left, top);
    cairo_rectangle(cr, 0, 0, right-left, bottom-top);
    cairo_set_source_surface(cr, copy, 0, 0);
    cairo_fill(cr);

    cairo_restore(cr);

    // clear emitted part in original image
    for (int y = top; y < bottom; ++y) {
        memset(&data[y * imgstride + 4*left], 0, (size_t)(right - left) * 4);
    }
}

static inline void
find_rec_recurse(unsigned char *data, int imgstride, int top, int right, int bottom, int left, cairo_t *cr)
{
    // crop top
    while (top < bottom) {
        if (border_left(data, imgstride, left, top, right) != right - left)
            break;

        top++;
    }

    if (top == bottom)
        return;

    // crop bottom
    while (top < bottom) {
        if (border_left(data, imgstride, left, bottom-1, right) != right - left)
            break;

        bottom--;
    }

    // crop left
    while (left < right) {
        if (border_top(data, imgstride, top, left, bottom) != bottom - top)
            break;

        left++;
    }

    // crop right
    while (left < right) {
        if (border_top(data, imgstride, top, right-1, bottom) != bottom - top)
            break;

        right--;
    }

    // if the region was empty, we should have returned after cropping the top
    g_assert(top < bottom);
    g_assert(left < right);

    // try to split horizontally
    for (int y = top; y < bottom; ++y) {
        if (border_left(data, imgstride, left, y, right) == right - left) {
            find_rec_recurse(data, imgstride, top, right, y, left, cr);
            find_rec_recurse(data, imgstride, y, right, bottom, left, cr);
            return;
        }
    }

    // try to split vertically
    for (int x = left; x < right; ++x) {
        if (border_top(data, imgstride, top, x, bottom) == bottom - top) {
            find_rec_recurse(data, imgstride, top, x, bottom, left, cr);
            find_rec_recurse(data, imgstride, top, right, bottom, x, cr);
            return;
        }
    }

    // then try chopping away at the corners
    g_autofree int *borders_top    = g_new0(int, right-left);
    g_autofree int *borders_bottom = g_new0(int, right-left);
    g_autofree int *borders_left   = g_new0(int, bottom-top);
    g_autofree int *borders_right  = g_new0(int, bottom-top);

    for (int x = left; x < right; ++x) {
        borders_top[x-left]    = border_top(data, imgstride, top, x, bottom);
        borders_bottom[x-left] = border_bottom(data, imgstride, top, x, bottom);
    }
    for (int y = top; y < bottom; ++y) {
        borders_left[y-top]  = border_left(data, imgstride, left, y, right);
        borders_right[y-top] = border_right(data, imgstride, left, y, right);
    }

    // top left and right corner
    for (int x = left + 1; x < right-1; ++x) {
        int b_self  = borders_top[x-left];
        int b_left  = borders_top[x-left-1];
        int b_right = borders_top[x-left+1];

        if (b_self > b_left) {
            // potentially chop left
            for (int y = top + b_self - 1; y >= top + b_left; --y) {
                if (borders_left[y-top] >= x-left) {
                    find_rec_recurse(data, imgstride, top, x, y, left, cr);
                    find_rec_recurse(data, imgstride, top, right, bottom, left, cr);
                    return;
                }
            }
        }
        if (b_self > b_right) {
            // potentially chop right
            for (int y = top + b_self - 1; y >= top + b_right; --y) {
                if (borders_right[y-top] >= right-x) {
                    find_rec_recurse(data, imgstride, top, right, y, x, cr);
                    find_rec_recurse(data, imgstride, top, right, bottom, left, cr);
                    return;
                }
            }
        }
    }

    // bottom left and right corner
    for (int x = left + 1; x < right-1; ++x) {
        int b_self  = borders_bottom[x-left];
        int b_left  = borders_bottom[x-left-1];
        int b_right = borders_bottom[x-left+1];

        if (b_self > b_left) {
            // potentially chop left
            for (int y = bottom - b_self - 1; y < bottom - b_left; ++y) {
                if (borders_left[y-top] >= x-left) {
                    find_rec_recurse(data, imgstride, y, x, bottom, left, cr);
                    find_rec_recurse(data, imgstride, top, right, bottom, left, cr);
                    return;
                }
            }
        }
        if (b_self > b_right) {
            // potentially chop right
            for (int y = bottom - b_self - 1; y < bottom - b_right; ++y) {
                if (borders_right[y-top] >= right-x) {
                    find_rec_recurse(data, imgstride, y, right, bottom, x, cr);
                    find_rec_recurse(data, imgstride, top, right, bottom, left, cr);
                    return;
                }
            }
        }
    }

    // no parts to chop -> finished with this rect
    emit_rect(data, imgstride, top, right, bottom, left, cr);
}

static inline void
paint_chopped(cairo_surface_t *imgsurf, cairo_t *cr_out)
{
    int imgwidth  = cairo_image_surface_get_width(imgsurf);
    int imgheight = cairo_image_surface_get_height(imgsurf);
    int imgstride = cairo_image_surface_get_stride(imgsurf);
    unsigned char *imgdata = cairo_image_surface_get_data(imgsurf);

    find_rec_recurse(imgdata, imgstride, 0, imgwidth, imgheight, 0, cr_out);
}

static inline cairo_surface_t *
clone_image_surface(cairo_surface_t *source)
{
    int w = cairo_image_surface_get_width(source);
    int h = cairo_image_surface_get_height(source);

    g_autoptr(JKPdfCairoSurfaceT) result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    g_autoptr(JKPdfCairoT) cr = cairo_create(result);

    cairo_rectangle(cr, 0, 0, w, h);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_fill(cr);

    cairo_surface_flush(result);
    return g_steal_pointer(&result);
}

int
main(int argc, char **argv)
{
    double   arg_resolution   = 600;
    gboolean arg_chopped      = FALSE;
    gboolean arg_transparency = FALSE;
    gboolean arg_grayscale    = FALSE;
    gboolean arg_debug        = FALSE;

    GOptionEntry option_entries[] = {
        { "resolution",  'r', 0, G_OPTION_ARG_DOUBLE, &arg_resolution, "Resolution to rasterize (default: 600)", "DPI" },
        { "chop",        'c', 0, G_OPTION_ARG_NONE, &arg_chopped, "Chop image into opaque parts. Implies --transparent.", NULL },
        { "transparent", 't', 0, G_OPTION_ARG_NONE, &arg_transparency, "Make white pixels transparent.", NULL },
        { "grayscale",   'g', 0, G_OPTION_ARG_NONE, &arg_grayscale, "Turn image into grayscale", NULL },
        { "debug",       'd', 0, G_OPTION_ARG_NONE, &arg_debug, "Mark chop regions with red rectangles.", NULL },
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

    if (arg_debug)
        DEBUG_MODE = true;

    if (arg_chopped)
        arg_transparency = TRUE;

    g_autoptr(JKPdfPopplerDocument) doc = jkpdf_create_poppler_document_for_stdin();
    g_autoptr(JKPdfCairoSurfaceT) surf = jkpdf_create_surface_for_stdout();

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);

    g_autoptr(JKPdfCairoSurfaceT) imgsurf = NULL;

    for (int pageno = 0; pageno < poppler_document_get_n_pages(doc); ++pageno) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, pageno);

        double pagewidth, pageheight;
        poppler_page_get_size(page, &pagewidth, &pageheight);

        rasterize(&imgsurf, page, arg_resolution);

        if (arg_grayscale)
            make_grayscale(imgsurf);

        if (arg_transparency)
            transparentize(imgsurf);

        cairo_save(cr);
        cairo_pdf_surface_set_size(surf, pagewidth, pageheight);

        double imgwidth = cairo_image_surface_get_width(imgsurf);
        double imgheight = cairo_image_surface_get_height(imgsurf);

        cairo_scale(cr, pagewidth/imgwidth, pageheight/imgheight);

        if (arg_chopped) {
            paint_chopped(imgsurf, cr);
        } else {
            cairo_set_source_surface(cr, imgsurf, 0, 0);
            cairo_rectangle(cr, 0, 0, imgwidth, imgheight);
            cairo_fill(cr);
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

    return 0;
}
