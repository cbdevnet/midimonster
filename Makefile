.PHONY: all clean run sanitize backends windows full backends-full
OBJS = config.o backend.o plugin.o
PLUGINDIR = "\"./backends/\""
PLUGINDIR_W32 = "\"backends\\\\\""

SYSTEM := $(shell uname -s)

CFLAGS ?= -g -Wall -Wpedantic
# Hide all non-API symbols for export
CFLAGS += -fvisibility=hidden

#CFLAGS += -DDEBUG
midimonster: LDLIBS = -ldl
midimonster: CFLAGS += -DPLUGINS=$(PLUGINDIR)

# Work around strange linker passing convention differences in Linux and OSX
ifeq ($(SYSTEM),Linux)
midimonster: LDFLAGS += -Wl,-export-dynamic
endif
ifeq ($(SYSTEM),Darwin)
midimonster: LDFLAGS += -Wl,-export_dynamic
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
midimonster.exe: CFLAGS += -DPLUGINS=$(PLUGINDIR_W32) -Wno-format
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

sanitize: export CC = clang
sanitize: export CFLAGS = -g -Wall -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
sanitize: midimonster backends
