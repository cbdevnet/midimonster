.PHONY: all clean
BACKENDS = artnet.so midi.so osc.so loopback.so evdev.so sacn.so

CFLAGS += -fPIC -I../
LDFLAGS += -shared

midi.so: LDLIBS = -lasound
evdev.so: CFLAGS += $(shell pkg-config --cflags libevdev)
evdev.so: LDLIBS = $(shell pkg-config --libs libevdev)

%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

all: $(BACKENDS)

clean:
	$(RM) $(BACKENDS)
