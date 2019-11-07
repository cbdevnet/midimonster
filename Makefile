.PHONY: all clean run sanitize backends windows full backends-full install
OBJS = config.o backend.o plugin.o

PREFIX ?= /usr
SYSTEM := $(shell uname -s)

CFLAGS ?= -g -Wall -Wpedantic
# Hide all non-API symbols for export
CFLAGS += -fvisibility=hidden

#CFLAGS += -DDEBUG
midimonster: LDLIBS = -ldl

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
midimonster: CFLAGS += -DPLUGINS=\"$(PLUGINS)\"
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

midimonster.exe: export CC = x86_64-w64-mingw32-gcc
midimonster.exe: CFLAGS += -Wno-format
midimonster.exe: LDLIBS = -lws2_32
midimonster.exe: LDFLAGS += -Wl,--out-implib,libmmapi.a
midimonster.exe: midimonster.c portability.h $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(OBJS) $(LDLIBS) -o $@

clean:
	$(RM) midimonster
	$(RM) midimonster.exe
	$(RM) libmmapi.a
	$(RM) $(OBJS)
	$(MAKE) -C backends clean

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster

install:
		install -d "$(DESTDIR)$(PREFIX)/bin"
		install -d "$(DESTDIR)$(PREFIX)/lib/midimonster"
		install -m 0755 midimonster "$(DESTDIR)$(PREFIX)/bin"
		install -m 0755 backends/*.so "$(DESTDIR)$(PREFIX)/lib/midimonster"

sanitize: export CC = clang
sanitize: export CFLAGS += -g -Wall -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
sanitize: midimonster backends
