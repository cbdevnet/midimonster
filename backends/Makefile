.PHONY: all clean full
OPTIONAL_BACKENDS = ola.so
LINUX_BACKENDS = midi.so evdev.so
BACKENDS = artnet.so osc.so loopback.so sacn.so
BACKEND_LIB = libmmbackend.o

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

artnet.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
osc.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
sacn.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
midi.so: LDLIBS = -lasound
evdev.so: CFLAGS += $(shell pkg-config --cflags libevdev)
evdev.so: LDLIBS = $(shell pkg-config --libs libevdev)
ola.so: LDLIBS = -lola
ola.so: CPPFLAGS += -Wno-write-strings

%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< $(ADDITIONAL_OBJS) -o $@ $(LDFLAGS)

%.so :: %.cpp %.h
	$(CXX) $(CPPFLAGS) $(LDLIBS) $< $(ADDITIONAL_OBJS) -o $@ $(LDFLAGS)

all: $(BACKEND_LIB) $(BACKENDS)

full: $(BACKEND_LIB) $(BACKENDS) $(OPTIONAL_BACKENDS)

clean:
	$(RM) $(BACKEND_LIB) $(BACKENDS) $(OPTIONAL_BACKENDS)
