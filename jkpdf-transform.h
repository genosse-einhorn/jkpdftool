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

#pragma once

#include <cairo.h>
#include <math.h>

typedef enum {
    JKPDF_ALIGN_START,
    JKPDF_ALIGN_CENTER,
    JKPDF_ALIGN_END
} JkPdfAlignment;

#define JKPDF_SCALE_FIT        -1.0
#define JKPDF_SCALE_COVER      -2.0
#define JKPDF_SCALE_INVALID     0.0

static inline cairo_matrix_t
jkpdf_transform_rect_into_bounds_3(const cairo_rectangle_t source, const cairo_rectangle_t dest, JkPdfAlignment halign, JkPdfAlignment valign, double scale)
{
    cairo_matrix_t m;
    cairo_matrix_init_identity(&m);

    if (scale == JKPDF_SCALE_FIT)
        scale = MIN(dest.width / source.width, dest.height / source.height);

    if (scale == JKPDF_SCALE_COVER)
        scale = MAX(dest.width / source.width, dest.height / source.height);

    double scaled_width = source.width * scale;
    double scaled_height = source.height * scale;

    double tx = 0;
    double ty = 0;

    if (halign == JKPDF_ALIGN_CENTER) {
        tx = (dest.width - scaled_width) / 2;
    } else if (halign == JKPDF_ALIGN_END) {
        tx = dest.width - scaled_width;
    }

    if (valign == JKPDF_ALIGN_CENTER) {
        ty = (dest.height - scaled_height) / 2;
    } else if(valign == JKPDF_ALIGN_END) {
        ty = dest.height - scaled_height;
    }

    cairo_matrix_translate(&m, dest.x + tx, dest.y + ty);
    cairo_matrix_scale(&m, scale, scale);
    cairo_matrix_translate(&m, -source.x, -source.y);
    return m;
}

static inline cairo_matrix_t
jkpdf_transform_rect_into_bounds_with_alignment(const cairo_rectangle_t source, const cairo_rectangle_t dest, JkPdfAlignment halign, JkPdfAlignment valign)
{
    return jkpdf_transform_rect_into_bounds_3(source, dest, halign, valign, JKPDF_SCALE_FIT);
}

static inline cairo_matrix_t
jkpdf_transform_rect_into_bounds(const cairo_rectangle_t source, const cairo_rectangle_t dest)
{
    return jkpdf_transform_rect_into_bounds_with_alignment(source, dest, JKPDF_ALIGN_CENTER, JKPDF_ALIGN_CENTER);
}

static inline cairo_rectangle_t
jkpdf_transform_bounding_rect(const cairo_rectangle_t *source, const cairo_matrix_t *transform)
{
    // FIXME! right/bottom is one-past the actual rectangle size
    // can we fix that by doing -1? on a float rectangle?
    double points[4][2] = {
        { source->x, source->y }, // TOP LEFT
        { source->x, source->y + source->height }, // BOTTOM LEFT
        { source->x + source->width, source->y }, // TOP RIGHT
        { source->x + source->width, source->y + source->height }, // BOTTOM RIGHT
    };

    for (int i = 0; i < 4; ++i) {
        cairo_matrix_transform_point(transform, &points[i][0], &points[i][1]);
    }

    double left = fmin(points[0][0], fmin(points[1][0], fmin(points[2][0], points[3][0])));
    double top = fmin(points[0][1], fmin(points[1][1], fmin(points[2][1], points[3][1])));
    double right = fmax(points[0][0], fmax(points[1][0], fmax(points[2][0], points[3][0])));
    double bottom = fmax(points[0][1], fmax(points[1][1], fmax(points[2][1], points[3][1])));

    return (cairo_rectangle_t){ left, top, right - left, bottom - top };
}
