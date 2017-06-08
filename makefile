.PHONY: clean
BACKENDS = artnet.so midi.so osc.so
OBJS = config.o backend.o plugin.o
PLUGINDIR = "\"./\""

LDLIBS = -lasound -ldl
CFLAGS ?= -g -Wall

midimonster: CFLAGS += -rdynamic -DPLUGINS=$(PLUGINDIR)
%.so: CFLAGS += -fPIC
%.so: LDFLAGS += -shared

%.so :: %.c %.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

all: midimonster $(BACKENDS)

midimonster: midimonster.h $(OBJS)

clean:
	$(RM) midimonster
	$(RM) $(OBJS)
	$(RM) $(BACKENDS)

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster
