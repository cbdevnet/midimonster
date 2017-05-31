LDLIBS = -lasound
CFLAGS = -g -Wall

BACKENDS = artnet.o midi.o
OBJS = $(BACKENDS)

midimonster: midimonster.h $(OBJS)

all: midimonster

clean:
	$(RM) midimonster
	$(RM) $(OBJS)
