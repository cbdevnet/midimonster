LDLIBS = -lasound
CFLAGS = -g -Wall

all: midimonster

clean:
	$(RM) midimonster
