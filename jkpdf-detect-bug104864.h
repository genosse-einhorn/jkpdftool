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

#include "jkpdf-io.h"

#include <stdbool.h>
#include <stdlib.h>

// https://gitlab.freedesktop.org/poppler/poppler/issues/371
// (formerly https://bugs.freedesktop.org/show_bug.cgi?id=104864)
// strikes whenever we process two or more PDF files which contain images.

static inline bool
jkpdf_page_has_image(PopplerPage *page)
{
    GList *l = poppler_page_get_image_mapping(page);
    if (l) {
        poppler_page_free_image_mapping(l);
        return true;
    }

    return false;
}

static inline bool
jkpdf_document_has_image(PopplerDocument *doc)
{
    int n = poppler_document_get_n_pages(doc);
    for (int i = 0; i < n; ++i) {
        g_autoptr(JKPdfPopplerPage) page = poppler_document_get_page(doc, i);
        if (jkpdf_page_has_image(page))
            return true;
    }

    return false;
}

static inline bool
jkpdf_is_affected_by_bug104864(size_t doc_length, PopplerDocument **doc_list)
{
    size_t count = 0;
    for (size_t i = 0; i < doc_length; ++i) {
        count += jkpdf_document_has_image(doc_list[i]);
        if (count > 1) {
            return true;
        }
    }

    return false;
}

static inline void
jkpdf_warn_bug104864(size_t doc_length, PopplerDocument **doc_list)
{
    // TODO: version-guard this once it is fixed in poppler

    if (!jkpdf_is_affected_by_bug104864(doc_length, doc_list))
        return;

    fprintf(stderr, "WARN: you are affected by poppler bug 104864. The output may be incorrect.\n");
    fprintf(stderr, "WARN: see https://gitlab.freedesktop.org/poppler/poppler/issues/371\n");
}
