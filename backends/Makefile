.PHONY: all clean
LINUX_BACKENDS = midi.so evdev.so
BACKENDS = artnet.so osc.so loopback.so sacn.so

SYSTEM := $(shell uname -s)

CFLAGS += -fPIC -I../
LDFLAGS += -shared

# Build Linux backends if possible
ifeq ($(SYSTEM),Linux)
BACKENDS += $(LINUX_BACKENDS)
endif
# Convince OSX that missing functions are present at runtime
ifeq ($(SYSTEM),Darwin)
LDFLAGS += -undefined dynamic_lookup
endif

midi.so: LDLIBS = -lasound
evdev.so: CFLAGS += $(shell pkg-config --cflags libevdev)
evdev.so: LDLIBS = $(shell pkg-config --libs libevdev)

%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

all: $(BACKENDS)

clean:
	$(RM) $(BACKENDS)
