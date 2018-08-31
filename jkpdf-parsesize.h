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

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

//////////////////////////////////////
// Page size and orientation parsing
//////////////////////////////////////
#define JKPDF_SIZE_ERROR jkpdf_size_error_quark()
static inline GQuark jkpdf_size_error_quark(void)
{
    return g_quark_from_static_string("jkpdf-size-parse-error-quark");
}

enum {
    JKPDF_SIZE_ERROR_PARSEFAIL,
    JKPDF_SIZE_ERROR_OUTOFRANGE
};

enum JkpdfSizeUnit {
    JKPDF_SIZE_UNIT_PT,
    JKPDF_SIZE_UNIT_MM,
    JKPDF_SIZE_UNIT_CM,
    JKPDF_SIZE_UNIT_UNSPECIFIED
};

enum JkpdfOrientation {
    JKPDF_ORIENTATION_AUTO,
    JKPDF_ORIENTATION_LANDSCAPE,
    JKPDF_ORIENTATION_PORTRAIT,
    JKPDF_ORIENTATION_INVALID = -1
};

static inline void
jkpdf_set_unexpected_char_error(GError **error, GQuark domain, int code, int c, size_t pos)
{
    if (c) {
        g_set_error(error, domain, code, "Unexpected character '%c' at position %zu", c, pos);
    } else {
        g_set_error(error, domain, code, "Unexpected end of string at position %zu", pos);
    }
}

// unit ::= 'mm' | 'pt' | {}
static inline size_t
jkpdf_parse_unit(const char *str, size_t i, enum JkpdfSizeUnit *unit)
{
    if (str[i] == 'm' && str[i+1] == 'm') {
        *unit = JKPDF_SIZE_UNIT_MM;
        return 2;
    } else if (str[i] == 'c' && str[i+1] == 'm') {
        *unit = JKPDF_SIZE_UNIT_CM;
        return 2;
    } else if (str[i] == 'p' && str[i+1] == 't') {
        *unit = JKPDF_SIZE_UNIT_PT;
        return 2;
    } else {
        *unit = JKPDF_SIZE_UNIT_UNSPECIFIED;
        return 0;
    }
}

static inline double
jkpdf_size_in_pt(double size, enum JkpdfSizeUnit unit)
{
    switch (unit) {
        case JKPDF_SIZE_UNIT_CM:
            return size / 2.54 * 72;
        case JKPDF_SIZE_UNIT_MM:
            return size / 25.4 * 72;
        default:
            return size;
    }
}

static inline size_t
jkpdf_parse_floatval(const char *str, size_t i, double *out, GError **error)
{
    char *endptr = NULL;

    double r = g_strtod(&str[i], &endptr);
    if (endptr == &str[i]) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, "Invalid number at position %zu", i);
        return 0;
    }

    if (!isfinite(r) || errno == ERANGE) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_OUTOFRANGE, "Number at position %zu is out of range", i);
        return 0;
    }

    *out = r;
    return (size_t)(endptr - &str[i]);
}

static inline size_t
jkpdf_parse_floatval_with_unit(const char *str, size_t i, double *out, enum JkpdfSizeUnit *unit, GError **error)
{
    size_t floatsize = jkpdf_parse_floatval(str, i, out, error);
    if (floatsize == 0)
        return 0;

    size_t unitsize = jkpdf_parse_unit(str, i + floatsize, unit);
    return floatsize + unitsize;
}

// papersize ::= float [unit] 'x' float [unit]
static inline bool
jkpdf_parse_paper_size(double *width, double *height, const char *spec, GError **error)
{
    *width = 0.0;
    *height = 0.0;

    if (!g_ascii_strcasecmp(spec, "a5")) {
        *width = 420.0;
        *height = 595.0;
        return true;
    }

    if (!g_ascii_strcasecmp(spec, "a4")) {
        *width = 595.0;
        *height = 842.0;
        return true;
    }

    if (!g_ascii_strcasecmp(spec, "a3")) {
        *width = 842.0;
        *height = 1190.0;
        return true;
    }

    enum JkpdfSizeUnit widthunit, heightunit;
    double rawwidth, rawheight;

    size_t widthsize = jkpdf_parse_floatval_with_unit(spec, 0, &rawwidth, &widthunit, error);
    if (widthsize == 0)
        return false;

    if (spec[widthsize] != 'x' && spec[widthsize] != 'X') {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL,
                    spec[widthsize], widthsize);
        return false;
    }

    size_t heightsize = jkpdf_parse_floatval_with_unit(spec, widthsize + 1, &rawheight, &heightunit, error);
    if (heightsize == 0)
        return false;

    if (spec[widthsize + heightsize + 1]) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL,
                    spec[widthsize + heightsize + 1], widthsize + heightsize + 1);
        return false;
    }

    if (widthunit == JKPDF_SIZE_UNIT_UNSPECIFIED)
        widthunit = heightunit;

    *width = jkpdf_size_in_pt(rawwidth, widthunit);
    *height = jkpdf_size_in_pt(rawheight, heightunit);

    if (*width <= 0.0) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_OUTOFRANGE, "Width must be greater than zero");
        return false;
    }

    if (*height <= 0.0) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_OUTOFRANGE, "Height must be greater than zero");
        return false;
    }

    return true;
}

static inline enum JkpdfOrientation
jkpdf_parse_orientation(const char *orientation)
{
    if (!orientation)
        return JKPDF_ORIENTATION_AUTO;

    size_t len = strlen(orientation);
    if (!g_ascii_strncasecmp("landscape", orientation, len))
        return JKPDF_ORIENTATION_LANDSCAPE;

    if (!g_ascii_strncasecmp("portrait", orientation, len))
        return JKPDF_ORIENTATION_PORTRAIT;

    return JKPDF_ORIENTATION_INVALID;
}

static inline bool
jkpdf_parse_margin_spec(const char *spec, double margins[4], GError **error)
{
    enum JkpdfSizeUnit units[4] = { JKPDF_SIZE_UNIT_UNSPECIFIED, JKPDF_SIZE_UNIT_UNSPECIFIED,
        JKPDF_SIZE_UNIT_UNSPECIFIED, JKPDF_SIZE_UNIT_UNSPECIFIED };

    size_t valuecount = 0;
    size_t i = 0;

    for (int j = 0; j < 4; ++j) {
        size_t w = jkpdf_parse_floatval_with_unit(spec, i, &margins[j], &units[j], error);
        if (w == 0)
            return false;

        valuecount++;
        i += w;

        if (spec[i] == 0)
            break;

        if (spec[i] != ',') {
            jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, spec[i], i);
            return false;
        }

        i++;
    }

    if (spec[i]) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, spec[i], i);
        return false;
    }

    if (valuecount < 2) {
        margins[1] = margins[0];
        units[1] = units[0];
    }
    if (valuecount < 3) {
        margins[2] = margins[0];
        units[2] = units[0];
    }
    if (valuecount < 4) {
        margins[3] = margins[1];
        units[3] = units[1];
    }

    if (units[3] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[3] = JKPDF_SIZE_UNIT_PT;
    if (units[2] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[2] = units[3];
    if (units[1] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[1] = units[3];
    if (units[0] == JKPDF_SIZE_UNIT_UNSPECIFIED)
        units[0] = units[3];

    for (int j = 0; j < 4; ++j)
        margins[j] = jkpdf_size_in_pt(margins[j], units[j]);

    return true;
}

static inline size_t
jkpdf_parse_integer(const char *arg, size_t i, int *val)
{
    *val = 0;
    size_t l = 0;
    while (arg[i + l] >= '0' && arg[i + l] <= '9') {
        *val = *val * 10 + (arg[i + l] - '0');
        l++;
    }

    return l;
}

static inline bool
jkpdf_parse_integral_size(const char *spec, int *cols, int *rows, GError **error)
{
    size_t widthsize = jkpdf_parse_integer(spec, 0, cols);
    if (widthsize == 0) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL, spec[0], 0);
        return false;
    }

    if (spec[widthsize] != 'x' && spec[widthsize] != 'X') {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL,
                    spec[widthsize], widthsize);
        return false;
    }

    size_t heightsize = jkpdf_parse_integer(spec, widthsize + 1, rows);
    if (heightsize == 0) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL,
                    spec[widthsize + 1], widthsize + 1);
        return false;
    }

    if (spec[widthsize + heightsize + 1]) {
        jkpdf_set_unexpected_char_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_PARSEFAIL,
                    spec[widthsize + heightsize + 1], widthsize + heightsize + 1);
        return false;
    }

    if (*cols <= 0) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_OUTOFRANGE, "Number of columns must be greater than zero");
        return false;
    }

    if (*rows <= 0) {
        g_set_error(error, JKPDF_SIZE_ERROR, JKPDF_SIZE_ERROR_OUTOFRANGE, "Number of rows must be greater than zero");
        return false;
    }

    return true;
}
