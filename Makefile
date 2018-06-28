
CC             := gcc
PKGCONFIG      := pkg-config

PKGS           := cairo poppler-glib glib-2.0 gio-2.0

CFLAGS         := -Wall -Wextra -Wconversion -Og -g
CFLAGS_PKG     != $(PKGCONFIG) --cflags $(PKGS)
LIBS           := -lm
LIBS_PKG       != $(PKGCONFIG) --libs $(PKGS)


jkpdftool: jkpdftool.c Makefile
	$(CC) -std=c11 $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS) $(LIBS_PKG)

test-tmp/jkpdftool: jkpdftool.c Makefile
	@mkdir -p test-tmp
	$(CC) -std=c11 --coverage $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS) $(LIBS_PKG)

test-tmp/jkpdftool-test: jkpdftool-test.c Makefile
	@mkdir -p test-tmp
	$(CC) -std=c11 $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS) $(LIBS_PKG)

test-tmp/jkpdftool-cov.gcda: test-tmp/jkpdftool test-tmp/jkpdftool-test
	PATH=$$PWD/test-tmp:$$PATH test-tmp/jkpdftool-test

test-tmp/lcov-test.info: test-tmp/jkpdftool-cov.gcda
	lcov -t "result" -o $@ -c -d . --no-external --rc lcov_branch_coverage=1

coverage-html: test-tmp/lcov-test.info
	genhtml -o test-tmp/cov-html test-tmp/lcov-test.info --branch-coverage

clean:
	rm -f jkpdftool
