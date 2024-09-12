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

#include <poppler.h>
#include <gio/gio.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

typedef cairo_t JKPdfCairoT;
typedef cairo_surface_t JKPdfCairoSurfaceT;
typedef PopplerDocument JKPdfPopplerDocument;
typedef PopplerPage     JKPdfPopplerPage;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JKPdfCairoT, cairo_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JKPdfCairoSurfaceT, cairo_surface_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JKPdfPopplerDocument, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JKPdfPopplerPage, g_object_unref)

static inline cairo_status_t
_jkpdf_cairo_write_to_stdout(void *closure, const unsigned char *data, unsigned int length)
{
    (void)closure;

    ssize_t written = write(1, data, length);
    if (written < 0) {
        perror("write(2)");
        return CAIRO_STATUS_WRITE_ERROR;
    }

    if (written != (ssize_t)length) {
        fprintf(stderr, "WTF: tried to write %u bytes, but wrote %zd\n", length, written);
        return CAIRO_STATUS_WRITE_ERROR;
    }

    return CAIRO_STATUS_SUCCESS;
}

static inline cairo_surface_t *
jkpdf_create_surface_for_stdout(void)
{
    if (isatty(1)) {
        fprintf(stderr, "ERROR: refusing to write PDF to terminal\n");
        exit(1);
    } else if (errno == EBADF) {
        fprintf(stderr, "WTF: stdout is not a valid file descriptor\n");
        exit(1);
    }

    return cairo_pdf_surface_create_for_stream(_jkpdf_cairo_write_to_stdout, NULL, 100, 100);
}

static inline PopplerDocument *
jkpdf_create_poppler_document_from_bytes(GBytes *bytes)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(JKPdfPopplerDocument) doc = poppler_document_new_from_bytes(bytes,
                                                                          NULL, &error);
    if (!doc) {
        fprintf(stderr, "ERROR: could not open PDF: %s\n", error->message);
        exit(1);
    }

    if (poppler_document_get_n_pages(doc) < 1) {
        fprintf(stderr, "WTF: input PDF has no pages\n");
        exit(1);
    }

    return g_steal_pointer(&doc);
}

static inline PopplerDocument *
jkpdf_create_poppler_document_for_fd(int fd)
{
    g_autoptr(GMappedFile) map = g_mapped_file_new_from_fd(fd, FALSE, NULL);
    if (map) {
        // regular file we can randomly access
        g_autoptr(GBytes) bytes = g_mapped_file_get_bytes(map);

        return jkpdf_create_poppler_document_from_bytes(bytes);
    } else {
        GByteArray *arr = g_byte_array_new();

        guint8 buf[2048];
        ssize_t count = 0;
        while ((count = read(fd, buf, sizeof(buf))) > 0) {
            g_byte_array_append(arr, buf, (guint)count);
        }
        if (count < 0) {
            perror("ERROR: while read(2)'ing input file");
            exit(1);
        }

        return jkpdf_create_poppler_document_from_bytes(g_byte_array_free_to_bytes(arr));
    }
}

static inline PopplerDocument *
jkpdf_create_poppler_document_for_stdin(void)
{
    if (isatty(0)) {
        fprintf(stderr, "ERROR: refusing to read PDF from terminal\n");
        exit(1);
    } else if (errno == EBADF) {
        fprintf(stderr, "WTF: stdin is not a valid file descriptor\n");
        exit(1);
    }

    return jkpdf_create_poppler_document_for_fd(0);
}

static inline void
_jkpdf_weak_close_fd(gpointer data, GObject *where_the_object_was)
{
    (void)where_the_object_was;

    close(GPOINTER_TO_INT(data));
}


static inline PopplerDocument *
jkpdf_create_poppler_document_for_commandline_arg(const char *arg)
{
    int fd = open(arg, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERROR: while opening '%s': %s", arg, strerror(errno));
        exit(1);
    }

    PopplerDocument *doc = jkpdf_create_poppler_document_for_fd(fd);
    if (doc) {
        g_object_weak_ref(G_OBJECT(doc), _jkpdf_weak_close_fd, GINT_TO_POINTER(fd));
    } else {
        close(fd);
    }

    return doc;
}
