#include <glib.h>
#include <locale.h>
#include <poppler.h>

static void
run_tool_on_testpdf(const char *args)
{
    g_autofree gchar *fullcmdline = g_strdup_printf("jkpdftool -o test-tmp/testout.pdf %s test.pdf", args);

    int exitstatus;
    gboolean r = g_spawn_command_line_sync(fullcmdline, NULL, NULL, &exitstatus, NULL);
    g_assert(r);

    g_assert(g_spawn_check_exit_status(exitstatus, NULL));
}

static void
assert_page_size(double width, double height)
{
    GFile *file = g_file_new_for_path("test-tmp/testout.pdf");
    PopplerDocument *doc = poppler_document_new_from_gfile(file, NULL, NULL, NULL);
    g_object_unref(file);
    g_assert(doc != NULL);

    for (int i = 0; i < poppler_document_get_n_pages(doc); ++i) {
        PopplerPage *page = poppler_document_get_page(doc, i);

        double pwidth, pheight;
        poppler_page_get_size(page, &pwidth, &pheight);

        g_assert_cmpfloat(width, ==, pwidth);
        g_assert_cmpfloat(height, ==, pheight);

        g_object_unref(page);
    }

    g_object_unref(doc);
}

static void
assert_red_rectangle(int pageno, double x, double y, double w, double h)
{
    GFile *file = g_file_new_for_path("test-tmp/testout.pdf");
    PopplerDocument *doc = poppler_document_new_from_gfile(file, NULL, NULL, NULL);
    g_object_unref(file);
    g_assert(doc != NULL);

    PopplerPage *page = poppler_document_get_page(doc, pageno);
    g_assert(page != NULL);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 5, 5);

    cairo_t *cr = cairo_create(surface);

    cairo_save(cr);
    cairo_translate(cr, 1.5, 1.5);
    cairo_scale(cr, 2.0/w, 2.0/h);
    cairo_translate(cr, -x, -y);
    poppler_page_render_for_printing(page, cr);
    cairo_restore(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *dataptr = cairo_image_surface_get_data(surface);

    cairo_surface_write_to_png(surface, "test-tmp/debug.png");

    guint32 *row = (guint32*)dataptr;
    g_assert_cmphex(row[0], ==, 0xffffffff);
    g_assert_cmphex(row[1], ==, 0xffffffff);
    g_assert_cmphex(row[2], ==, 0xffffffff);
    g_assert_cmphex(row[3], ==, 0xffffffff);
    g_assert_cmphex(row[4], ==, 0xffffffff);
    row = (guint32*)(dataptr + stride);
    g_assert_cmphex(row[0], ==, 0xffffffff);
    g_assert_cmphex(row[4], ==, 0xffffffff);
    row = (guint32*)(dataptr + 2*stride);
    g_assert_cmphex(row[0], ==, 0xffffffff);
    g_assert_cmphex(row[2], ==, 0xffff0000);
    g_assert_cmphex(row[4], ==, 0xffffffff);
    row = (guint32*)(dataptr + 3*stride);
    g_assert_cmphex(row[0], ==, 0xffffffff);
    g_assert_cmphex(row[4], ==, 0xffffffff);
    g_assert_cmphex(row[0], ==, 0xffffffff);
    row = (guint32*)(dataptr + 4*stride);
    g_assert_cmphex(row[1], ==, 0xffffffff);
    g_assert_cmphex(row[2], ==, 0xffffffff);
    g_assert_cmphex(row[3], ==, 0xffffffff);
    g_assert_cmphex(row[4], ==, 0xffffffff);

    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);

    g_object_unref(page);
    g_object_unref(doc);
}

static double
mm2pt(double mm)
{
    return mm/25.4*72.0;
}

static void
assert_page_count(int count)
{
    GFile *file = g_file_new_for_path("test-tmp/testout.pdf");
    PopplerDocument *doc = poppler_document_new_from_gfile(file, NULL, NULL, NULL);
    g_object_unref(file);
    g_assert(doc != NULL);

    g_assert_cmpint(poppler_document_get_n_pages(doc), ==, count);

    g_object_unref(doc);
}

static void
test_passthrough_works(void)
{
    run_tool_on_testpdf("-p 1,1 ");

    assert_page_count(2);
    assert_page_size(595, 842);
    assert_red_rectangle(0, mm2pt(30), mm2pt(50), mm2pt(80), mm2pt(60));
}

static void
test_scale_to_a5(void)
{
    run_tool_on_testpdf("-s a5");

    assert_page_count(1);
    assert_page_size(420, 595);
    assert_red_rectangle(0, mm2pt(21.213), mm2pt(35.355), mm2pt(56.569), mm2pt(42.426));
}

int main(int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/passthrough", test_passthrough_works);
    g_test_add_func("/scale/a5", test_scale_to_a5);

    return g_test_run();
}
