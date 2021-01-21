// Copyright © 2021 Jonas Kümmerlin <jonas@kuemmerlin.eu>
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

// Rendering, via a scene graph-like tree structure
struct JkpdfPastaNode {
    void (*render)(cairo_t * /*cr*/, struct JkpdfPastaNode * /*closure*/);
    double width;
    double height;
};

struct JkpdfPastaSourceNode {
    struct JkpdfPastaNode node;
    PopplerPage *page;
};

struct JkpdfPastaDeleteNode {
    struct JkpdfPastaNode node;
    double x;
    double y;
    double w;
    double h;
    struct JkpdfPastaNode *source;
};

struct JkpdfPastaCopyNode {
    struct JkpdfPastaNode node;
    double x;
    double y;
    struct JkpdfPastaNode *source;
};

struct JkpdfPastaPasteNode {
    struct JkpdfPastaNode node;
    double x;
    double y;
    struct JkpdfPastaNode *source;
    struct JkpdfPastaNode *clipboard;
};

static void
jkpdf_pasta_null_node_render_func(cairo_t *cr, struct JkpdfPastaNode *closure)
{
    (void)cr; (void)closure;
}

static struct JkpdfPastaNode *
jkpdf_pasta_create_null_node(void)
{
    struct JkpdfPastaNode *node = g_new0(struct JkpdfPastaNode, 1);
    node->render = jkpdf_pasta_null_node_render_func;
    node->width = 0;
    node->height = 0;

    return node;
}


static void
jkpdf_pasta_source_node_render_func(cairo_t *cr, struct JkpdfPastaNode *closure)
{
    struct JkpdfPastaSourceNode *source = (struct JkpdfPastaSourceNode *)closure;

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, source->node.width, source->node.height);
    cairo_clip(cr);
    poppler_page_render_for_printing(source->page, cr);
    cairo_restore(cr);
}

static struct JkpdfPastaNode *
jkpdf_pasta_create_source_node(PopplerPage *page)
{
    double w = 0, h = 0;
    poppler_page_get_size(page, &w, &h);

    struct JkpdfPastaSourceNode *node = g_new0(struct JkpdfPastaSourceNode, 1);
    node->node.render = jkpdf_pasta_source_node_render_func;
    node->node.width = w;
    node->node.height = h;
    node->page = page;
    return &node->node;
}

static void
jkpdf_pasta_delete_node_render_func(cairo_t *cr, struct JkpdfPastaNode *closure)
{
    struct JkpdfPastaDeleteNode *node = (struct JkpdfPastaDeleteNode *)closure;

    cairo_save(cr);
    cairo_new_path(cr);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, node->x, node->y);
    cairo_line_to(cr, node->x + node->w, node->y);
    cairo_line_to(cr, node->x + node->w, node->y + node->h);
    cairo_line_to(cr, node->x, node->y + node->h);
    cairo_close_path(cr);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, 0, node->node.height);
    cairo_line_to(cr, node->node.width, node->node.height);
    cairo_line_to(cr, node->node.width, 0);
    cairo_close_path(cr);
    cairo_clip(cr);

    node->source->render(cr, node->source);

    cairo_restore(cr);
}

static struct JkpdfPastaNode *
jkpdf_pasta_create_delete_node(struct JkpdfPastaNode *source, double x, double y, double w, double h)
{
    struct JkpdfPastaDeleteNode *node = g_new0(struct JkpdfPastaDeleteNode, 1);
    node->node.render = jkpdf_pasta_delete_node_render_func;
    node->node.width = source->width;
    node->node.height = source->height;
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;
    node->source = source;

    return &node->node;
}

static void
jkpdf_pasta_copy_node_render_func(cairo_t *cr, struct JkpdfPastaNode *closure)
{
    struct JkpdfPastaCopyNode *node = (struct JkpdfPastaCopyNode *)closure;

    cairo_save(cr);
    cairo_translate(cr, -node->x, -node->y);
    cairo_rectangle(cr, node->x, node->y, node->node.width, node->node.height);
    cairo_clip(cr);
    node->source->render(cr, node->source);
    cairo_restore(cr);
}

static struct JkpdfPastaNode *
jkpdf_pasta_create_copy_node(struct JkpdfPastaNode *source, double x, double y, double w, double h)
{
    struct JkpdfPastaCopyNode *node = g_new0(struct JkpdfPastaCopyNode, 1);
    node->node.render = jkpdf_pasta_copy_node_render_func;
    node->node.width = w;
    node->node.height = h;
    node->x = x;
    node->y = y;
    node->source = source;

    return &node->node;
}

static void
jkpdf_pasta_paste_node_render_func(cairo_t *cr, struct JkpdfPastaNode *closure)
{
    struct JkpdfPastaPasteNode *node = (struct JkpdfPastaPasteNode *)closure;

    node->source->render(cr, node->source);

    cairo_save(cr);
    cairo_translate(cr, node->x, node->y);
    node->clipboard->render(cr, node->clipboard);
    cairo_restore(cr);
}

static struct JkpdfPastaNode *
jkpdf_pasta_create_paste_node(struct JkpdfPastaNode *source, struct JkpdfPastaNode *clipboard, double x, double y)
{
    struct JkpdfPastaPasteNode *node = g_new0(struct JkpdfPastaPasteNode, 1);
    node->node.render = jkpdf_pasta_paste_node_render_func;
    node->node.width = source->width;
    node->node.height = source->height;
    node->source = source;
    node->clipboard = clipboard;
    node->x = x;
    node->y = y;

    return &node->node;
}

// command line spec parsing

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

    g_option_context_set_description(context, "Copy and paste in PDF files (EXPERIMENTAL)\n"
        "\n"
        "Copy/Paste specs have the following form:\n"
        "   cut:    x,PAGENO,X,Y,WIDTH,HEIGHT\n"
        "   copy:   c,PAGENO,X,Y,WIDTH,HEIGHT\n"
        "   paste:  v,PAGENO,X,Y\n"
    );

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

    // FIXME! memory handling, we currently just leak the render node stuff
    int npages = poppler_document_get_n_pages(main_doc);
    struct JkpdfPastaNode **pageNodes = g_new0(struct JkpdfPastaNode *, npages);
    struct JkpdfPastaNode *clipboard = jkpdf_pasta_create_null_node();
    for (int i = 0; i < npages; ++i) {
        pageNodes[i] = jkpdf_pasta_create_source_node(poppler_document_get_page(main_doc, i));
    }

    for (unsigned i = 0; i < g_strv_length(arg_commands); ++i) {
        int p = 0;
        double x = 0, y = 0, w = 0, h = 0;
        enum JkpdfCopyPasteSpecType type;

        if (!jkpdf_parse_copy_paste_spec(arg_commands[i], &type, &p, &x, &y, &w, &h)) {
            fprintf(stderr, "ERROR: invalid copy/paste spec: ‘%s’\n", arg_commands[i]);
            return  1;
        }

        // FIXME! validate p, segfault danger!
        x = x / arg_dpi * 72.0;
        y = y / arg_dpi * 72.0;
        w = w / arg_dpi * 72.0;
        h = h / arg_dpi * 72.0;

        if (type == JKPDF_CPS_TYPE_CUT) {
            clipboard = jkpdf_pasta_create_copy_node(pageNodes[p-1], x, y, w, h);
            pageNodes[p-1] = jkpdf_pasta_create_delete_node(pageNodes[p-1], x, y, w, h);
        } else if (type == JKPDF_CPS_TYPE_COPY) {
            clipboard = jkpdf_pasta_create_copy_node(pageNodes[p-1], x, y, w, h);
        } else if (type == JKPDF_CPS_TYPE_PASTE) {
            pageNodes[p-1] = jkpdf_pasta_create_paste_node(pageNodes[p-1], clipboard, x, y);
        } else if (type == JKPDF_CPS_TYPE_PASTE_INTO_CUT) {
            pageNodes[p-1] = jkpdf_pasta_create_delete_node(pageNodes[p-1], x, y, clipboard->width, clipboard->height);
            pageNodes[p-1] = jkpdf_pasta_create_paste_node(pageNodes[p-1], clipboard, x, y);
        }
    }

    g_autoptr(JKPdfCairoT) cr = cairo_create(surf);
    for (int i = 0; i < npages; ++i) {
        cairo_pdf_surface_set_size(surf, pageNodes[i]->width, pageNodes[i]->height);

        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, pageNodes[i]->width, pageNodes[i]->height);
        cairo_clip(cr);

        pageNodes[i]->render(cr, pageNodes[i]);

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

