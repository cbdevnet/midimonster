.PHONY: all clean full
OPTIONAL_BACKENDS = ola.so
LINUX_BACKENDS = midi.so evdev.so
BACKENDS = artnet.so osc.so loopback.so sacn.so

SYSTEM := $(shell uname -s)

CFLAGS += -fPIC -I../
CPPFLAGS += -fPIC -I../
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
ola.so: LDLIBS = -lola
ola.so: CPPFLAGS += -Wno-write-strings

%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

%.so :: %.cpp %.h
	$(CXX) $(CPPFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

all: $(BACKENDS)

full: $(BACKENDS) $(OPTIONAL_BACKENDS)

clean:
	$(RM) $(BACKENDS) $(OPTIONAL_BACKENDS)
