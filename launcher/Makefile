# tux-mc, the launcher. Needs gtk4, json-glib and libcurl.
CC      ?= cc
CFLAGS  ?= -O2 -pipe
CFLAGS  += -std=gnu11 -Wall -Wextra
PREFIX  ?= $(HOME)/.local
NATIVE  := ../mod/native
PKGS    := gtk4 json-glib-1.0 libcurl
SRC     := src/mc.c src/mc-net.c src/mc-pages.c src/mc-configs.c src/mc3d.c $(NATIVE)/capes.c

all: tux-mc

tux-mc: $(SRC) src/mc.h src/mc3d.h $(NATIVE)/tuxmc.h
	$(CC) $(CFLAGS) -I$(NATIVE) -o $@ $(SRC) $$(pkg-config --cflags --libs $(PKGS)) -lm

install: tux-mc
	install -Dm755 tux-mc $(DESTDIR)$(PREFIX)/bin/tux-mc
	install -Dm644 gentoo-cape.png $(DESTDIR)$(PREFIX)/share/tux-mc/data/gentoo-cape.png
	sed 's|@BIN@|$(PREFIX)/bin/tux-mc|' tux-mc.desktop.in > tux-mc.desktop
	install -Dm644 tux-mc.desktop $(DESTDIR)$(PREFIX)/share/applications/tux-mc.desktop

clean:
	rm -f tux-mc tux-mc.desktop

.PHONY: all install clean
