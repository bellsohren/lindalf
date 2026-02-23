# ─────────────────────────────────────────────────────────────────────
# lindalf  –  Makefile
# ─────────────────────────────────────────────────────────────────────

PROG      := lindalf
VERSION   := 1.0.0

CC        := gcc
CFLAGS    := -std=c11 -Wall -Wextra -Wpedantic \
             -O2 -march=native \
             -D_GNU_SOURCE \
             $(shell pkg-config --cflags alsa 2>/dev/null)
LDFLAGS   := $(shell pkg-config --libs alsa 2>/dev/null || echo -lasound) \
             -lpthread -lm

SRC       := main.c animation.c audio.c dsp.c input.c
OBJ       := $(SRC:.c=.o)

PREFIX    ?= /usr/local
BINDIR    := $(PREFIX)/bin
MANDIR    := $(PREFIX)/share/man/man1

# ── Targets ──────────────────────────────────────────────────────────

.PHONY: all clean install uninstall deb

all: $(PROG)

$(PROG): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Header dependencies ───────────────────────────────────────────────

main.o:      main.c      shared.h animation.h audio.h input.h
animation.o: animation.c animation.h shared.h
audio.o:     audio.c     audio.h shared.h dsp.h
dsp.o:       dsp.c       dsp.h shared.h
input.o:     input.c     input.h shared.h

# ── Install ───────────────────────────────────────────────────────────

install: $(PROG)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	@echo "Installed to $(DESTDIR)$(BINDIR)/$(PROG)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROG)

# ── Packaging (Debian .deb) ───────────────────────────────────────────
#
# Requires: dpkg-buildpackage, devscripts, debhelper
# Usage:  make deb
#

deb:
	@echo "Building Debian package …"
	dpkg-buildpackage -us -uc -b
	@echo "Package built in parent directory."

# ── Clean ─────────────────────────────────────────────────────────────

clean:
	rm -f $(OBJ) $(PROG)
