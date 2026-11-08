# tux-mc, the launcher. Needs gtk4, json-glib and libcurl.
CC      ?= cc
CFLAGS  ?= -O2 -pipe
CFLAGS  += -std=gnu11 -Wall -Wextra
PREFIX  ?= $(HOME)/.local
NATIVE  := ../mod/native
PKGS    := gtk4 json-glib-1.0 libcurl zlib
VERSION := $(shell cat ../VERSION 2>/dev/null || echo 0.0.0)
CFLAGS  += -DTUX_MC_VERSION='"$(VERSION)"'
SRC     := src/mc.c src/mc-net.c src/mc-pages.c src/mc-configs.c src/mc3d.c src/mc-sys.c src/mc-setup.c \
           src/mc-instances.c src/mc-update.c src/mc-themes.c src/mc-account.c src/mc-capeedit.c src/mc-import.c src/mc-zip.c \
           src/mc-hat.c src/mc-hats.c src/mc-menubg.c src/mc-packs.c src/mc-colour.c \
           $(NATIVE)/capes.c

# MSYS2 (UCRT64) on Windows: a GUI .exe, no console window
ifeq ($(OS),Windows_NT)
EXE     := tux-mc.exe
LDEXTRA := -mwindows
else
EXE     := tux-mc
LDEXTRA :=
endif

all: $(EXE)

$(EXE): $(SRC) src/mc.h src/mc3d.h $(NATIVE)/tuxmc.h ../VERSION
	$(CC) $(CFLAGS) -I$(NATIVE) -o $@ $(SRC) $$(pkg-config --cflags --libs $(PKGS)) -lm $(LDEXTRA)

install: tux-mc
	install -Dm755 tux-mc $(DESTDIR)$(PREFIX)/bin/tux-mc
	install -d $(DESTDIR)$(PREFIX)/share/tux-mc/data
	install -m644 data/*-cape.png $(DESTDIR)$(PREFIX)/share/tux-mc/data/
	sed 's|@BIN@|$(PREFIX)/bin/tux-mc|' tux-mc.desktop.in > tux-mc.desktop
	install -Dm644 tux-mc.desktop $(DESTDIR)$(PREFIX)/share/applications/tux-mc.desktop

clean:
	rm -f tux-mc tux-mc.exe tux-mc.desktop

.PHONY: all install clean
