.PHONY: clean
BACKENDS = artnet.so midi.so osc.so loopback.so evdev.so
OBJS = config.o backend.o plugin.o
PLUGINDIR = "\"./\""

CFLAGS ?= -g -Wall
#CFLAGS += -DDEBUG
%.so: CFLAGS += -fPIC
%.so: LDFLAGS += -shared

midimonster: LDLIBS = -ldl
midimonster: CFLAGS += -rdynamic -DPLUGINS=$(PLUGINDIR)
midi.so: LDLIBS = -lasound


%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

all: midimonster $(BACKENDS)

midimonster: midimonster.h $(OBJS)

clean:
	$(RM) midimonster
	$(RM) $(OBJS)
	$(RM) $(BACKENDS)

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster
