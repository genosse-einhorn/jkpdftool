
CC             := cc
PKGCONFIG      := pkg-config

PKGS           := cairo poppler-glib glib-2.0 gio-2.0

CFLAGS         := -Wall -Wextra -Wconversion -Og -g
CFLAGS_PKG     != $(PKGCONFIG) --cflags $(PKGS)
LIBS           := -lm
LIBS_PKG       != $(PKGCONFIG) --libs $(PKGS)

EXE            := out/jkpdftool-pagefit out/jkpdftool-rotate out/jkpdftool-nup out/jkpdftool-splice out/jkpdftool-crop

all: $(EXE)

out/%: %.c $(wildcard *.h) Makefile
	@mkdir -p out
	$(CC) -std=c11 $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS) $(LIBS_PKG)

clean:
	rm -f $(EXE)
