
CC             := gcc
PKGCONFIG      := pkg-config

CFLAGS         := -Wall -Wextra -Wconversion -Og -g
CFLAGS_PKG     != $(PKGCONFIG) --cflags --libs cairo poppler-glib glib-2.0 gio-2.0
LIBS           := -lm


jkpdftool: jkpdftool.c Makefile
	$(CC) -std=c11 $(CFLAGS) $(CFLAGS_PKG) -o $@ $< $(LIBS)

clean:
	rm -f jkpdftool
