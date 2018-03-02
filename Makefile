.PHONY: all clean run sanitize backends
OBJS = config.o backend.o plugin.o
PLUGINDIR = "\"./backends/\""

CFLAGS ?= -g -Wall
LDLIBS = -ldl
CFLAGS += -DPLUGINS=$(PLUGINDIR)
#CFLAGS += -DDEBUG
LDFLAGS += -Wl,-export-dynamic

all: midimonster backends

backends:
	$(MAKE) -C backends

midimonster: midimonster.c $(OBJS)

clean:
	$(RM) midimonster
	$(RM) $(OBJS)
	$(MAKE) -C backends clean

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster

sanitize: export CC = clang
sanitize: export CFLAGS = -g -Wall -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
sanitize: midimonster backends
