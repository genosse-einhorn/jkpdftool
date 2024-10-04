
CC             := cc
PKGCONFIG      := pkg-config

PKGS           := cairo poppler-glib glib-2.0 gio-2.0

CFLAGS         := -Wall -Wextra -Wconversion -Og -g
CFLAGS_PKG     != $(PKGCONFIG) --cflags $(PKGS)
LIBS           := -lm
LIBS_PKG       != $(PKGCONFIG) --libs $(PKGS)

EXE            := out/jkpdftool-pagefit out/jkpdftool-rotate out/jkpdftool-nup out/jkpdftool-splice out/jkpdftool-crop out/jkpdftool-ndown out/jkpdftool-overlay out/jkpdftool-rasterize out/jkpdftool-reencode out/jkpdftool-pasta out/jkpdftool-booklet out/jkpdftool-splice-qpdf out/jkpdftool-cut out/jkpdftool-glue out/jkpdftool-color2black out/jkpdftool-mirror

all: $(EXE)

out/%: %.c $(wildcard *.h) Makefile
	@mkdir -p out
	$(CC) -std=c11 $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS) $(LIBS_PKG)

out/jkpdftool-reencode: jkpdftool-reencode.sh Makefile
	@mkdir -p out
	cp $< $@
	chmod u+x $@

out/jkpdftool-color2black: jkpdftool-color2black.sh Makefile
	@mkdir -p out
	cp $< $@
	chmod u+x $@

out/jkpdftool-splice-qpdf: jkpdftool-splice-qpdf.sh Makefile
	@mkdir -p out
	cp $< $@
	chmod u+x $@

clean:
	rm -f $(EXE)
