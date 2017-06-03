LDLIBS = -lasound
CFLAGS = -g -Wall

BACKENDS = artnet.o midi.o osc.o
OBJS = config.o backend.o $(BACKENDS)

midimonster: midimonster.h $(OBJS)

all: midimonster

clean:
	$(RM) midimonster
	$(RM) $(OBJS)

run:
	valgrind --leak-check=full --show-leak-kinds=all ./midimonster
