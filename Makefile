.PHONY: all clean run sanitize backends windows full backends-full install
OBJS = core/core.o core/config.o core/backend.o core/plugin.o core/routing.o

PREFIX ?= /usr
PLUGIN_INSTALL = $(PREFIX)/lib/midimonster
EXAMPLES ?= $(PREFIX)/share/midimonster
SYSTEM := $(shell uname -s)
GITVERSION = $(shell git describe)

# Default compilation CFLAGS
CFLAGS ?= -g -Wall -Wpedantic
#CFLAGS += -DDEBUG
# Hide all non-API symbols for export
CFLAGS += -fvisibility=hidden

# Subdirectory objects need the include path
RCCFLAGS += -I./
core/%: CFLAGS += -I./

midimonster: LDLIBS = -ldl
# Replace version string with current git-describe if possible
ifneq "$(GITVERSION)" ""
midimonster: CFLAGS += -DMIDIMONSTER_VERSION=\"$(GITVERSION)\"
midimonster.exe: CFLAGS += -DMIDIMONSTER_VERSION=\"$(GITVERSION)\"
assets/resource.o: RCCFLAGS += -DMIDIMONSTER_VERSION=\\\"$(GITVERSION)\\\"
endif

# Work around strange linker passing convention differences in Linux and OSX
ifeq ($(SYSTEM),Linux)
midimonster: LDFLAGS += -Wl,-export-dynamic
endif
ifeq ($(SYSTEM),Darwin)
midimonster: LDFLAGS += -Wl,-export_dynamic
endif

# Allow overriding the locations for backend plugins and default configuration
ifdef DEFAULT_CFG
midimonster: CFLAGS += -DDEFAULT_CFG=\"$(DEFAULT_CFG)\"
endif
ifdef PLUGINS
core/core.o: CFLAGS += -DPLUGINS=\"$(PLUGINS)\"
PLUGIN_INSTALL = $(PLUGINS)
endif

all: midimonster backends

full: midimonster backends-full

windows: midimonster.exe
	$(MAKE) -C backends windows

backends:
	$(MAKE) -C backends

backends-full:
	$(MAKE) -C backends full

# This rule can not be the default rule because OSX the target prereqs are not exactly the build prereqs
midimonster: midimonster.c portability.h $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(OBJS) $(LDLIBS) -o $@

assets/resource.o: assets/midimonster.rc assets/midimonster.ico
	$(RCC) $(RCCFLAGS) $< -o $@ --output-format=coff

assets/midimonster.ico: assets/MIDIMonster.svg
	convert -density 384 $< -define icon:auto-resize $@

midimonster.exe: export CC = x86_64-w64-mingw32-gcc
midimonster.exe: RCC ?= x86_64-w64-mingw32-windres
midimonster.exe: CFLAGS += -Wno-format
midimonster.exe: LDLIBS = -lws2_32
midimonster.exe: LDFLAGS += -Wl,--out-implib,libmmapi.a
midimonster.exe: midimonster.c portability.h $(OBJS) assets/resource.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(OBJS) assets/resource.o $(LDLIBS) -o $@

clean:
	$(RM) midimonster
	$(RM) midimonster.exe
	$(RM) libmmapi.a
	$(RM) assets/resource.o
	$(RM) $(OBJS)
	$(MAKE) -C backends clean

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster

install:
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 midimonster "$(DESTDIR)$(PREFIX)/bin"
	install -d "$(DESTDIR)$(PLUGIN_INSTALL)"
	install -m 0755 backends/*.so "$(DESTDIR)$(PLUGIN_INSTALL)"
	install -d "$(DESTDIR)$(EXAMPLES)"
	install -m 0644 configs/* "$(DESTDIR)$(EXAMPLES)"
ifdef DEFAULT_CFG
# Only install the default configuration if it is not already present to avoid overwriting it
ifeq (,$(wildcard $(DEFAULT_CFG)))
	install -Dm 0644 monster.cfg "$(DESTDIR)$(DEFAULT_CFG)"
endif
endif

sanitize: export CC = clang
sanitize: export CFLAGS += -g -Wall -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
sanitize: midimonster backends
