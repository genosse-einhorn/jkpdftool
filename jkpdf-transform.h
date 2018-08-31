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

static inline cairo_matrix_t
jkpdf_transform_rect_into_bounds(const cairo_rectangle_t source, const cairo_rectangle_t dest)
{
    double scale_x = dest.width / source.width;
    double scale_y = dest.height / source.height;
    double scale = scale_x < scale_y ? scale_x : scale_y;

    double scaled_width = source.width * scale;
    double scaled_height = source.height * scale;

    cairo_matrix_t m;
    cairo_matrix_init_identity(&m);
    cairo_matrix_translate(&m, dest.x + (dest.width - scaled_width) / 2, dest.y + (dest.height - scaled_height) / 2);
    cairo_matrix_scale(&m, scale, scale);
    cairo_matrix_translate(&m, -source.x, -source.y);
    return m;
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
