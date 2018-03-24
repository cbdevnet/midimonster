.PHONY: all clean run sanitize backends
OBJS = config.o backend.o plugin.o
PLUGINDIR = "\"./backends/\""

SYSTEM := $(shell uname -s)

CFLAGS ?= -g -Wall
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

backends:
	$(MAKE) -C backends

# This rule can not be the default rule because OSX the target prereqs are not exactly the build prereqs
midimonster: midimonster.c portability.h $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(OBJS) $(LDLIBS) -o $@

clean:
	$(RM) midimonster
	$(RM) $(OBJS)
	$(MAKE) -C backends clean

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster

sanitize: export CC = clang
sanitize: export CFLAGS = -g -Wall -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
sanitize: midimonster backends
