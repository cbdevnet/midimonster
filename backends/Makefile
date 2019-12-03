.PHONY: all clean full
LINUX_BACKENDS = midi.so evdev.so
WINDOWS_BACKENDS = artnet.dll osc.dll loopback.dll sacn.dll maweb.dll winmidi.dll
BACKENDS = artnet.so osc.so loopback.so sacn.so lua.so maweb.so jack.so
OPTIONAL_BACKENDS = ola.so
BACKEND_LIB = libmmbackend.o

SYSTEM := $(shell uname -s)

CFLAGS += -g -fPIC -I../ -Wall -Wpedantic
CPPFLAGS += -g -fPIC -I../
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
artnet.dll: ADDITIONAL_OBJS += $(BACKEND_LIB)
artnet.dll: LDLIBS += -lws2_32

osc.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
osc.dll: ADDITIONAL_OBJS += $(BACKEND_LIB)
osc.dll: LDLIBS += -lws2_32

sacn.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
sacn.dll: ADDITIONAL_OBJS += $(BACKEND_LIB)
sacn.dll: LDLIBS += -lws2_32

maweb.so: ADDITIONAL_OBJS += $(BACKEND_LIB)
maweb.so: LDLIBS = -lssl
maweb.dll: ADDITIONAL_OBJS += $(BACKEND_LIB)
maweb.dll: LDLIBS += -lws2_32
maweb.dll: CFLAGS += -DMAWEB_NO_LIBSSL

winmidi.dll: ADDITIONAL_OBJS += $(BACKEND_LIB)
winmidi.dll: LDLIBS += -lwinmm -lws2_32

jack.so: LDLIBS = -ljack -lpthread
midi.so: LDLIBS = -lasound
evdev.so: CFLAGS += $(shell pkg-config --cflags libevdev || echo "-DBUILD_ERROR=\"Missing pkg-config data for libevdev\"")
evdev.so: LDLIBS = $(shell pkg-config --libs libevdev || echo "-DBUILD_ERROR=\"Missing pkg-config data for libevdev\"")
ola.so: LDLIBS = -lola
ola.so: CPPFLAGS += -Wno-write-strings
# The pkg-config name for liblua5.3 is subject to discussion. I prefer 'lua5.3' (which works on Debian and OSX),
# but Arch requires 'lua53' which works on Debian, too, but breaks on OSX.
lua.so: CFLAGS += $(shell pkg-config --cflags lua53 || pkg-config --cflags lua5.3 || echo "-DBUILD_ERROR=\"Missing pkg-config data for lua53\"")
lua.so: LDLIBS += $(shell pkg-config --libs lua53 || pkg-config --libs lua5.3 || echo "-DBUILD_ERROR=\"Missing pkg-config data for lua53\"")

%.so :: %.c %.h $(BACKEND_LIB)
	$(CC) $(CFLAGS) $(LDLIBS) $< $(ADDITIONAL_OBJS) -o $@ $(LDFLAGS)

%.dll :: %.c %.h $(BACKEND_LIB)
	$(CC) $(CFLAGS) $< $(ADDITIONAL_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

%.so :: %.cpp %.h
	$(CXX) $(CPPFLAGS) $(LDLIBS) $< $(ADDITIONAL_OBJS) -o $@ $(LDFLAGS)

all: $(BACKEND_LIB) $(BACKENDS)

../libmmapi.a:
	$(MAKE) -C ../ midimonster.exe

windows: export CC = x86_64-w64-mingw32-gcc
windows: LDLIBS += -lmmapi
windows: LDFLAGS += -L../
windows: CFLAGS += -Wno-format -Wno-pointer-sign
windows: ../libmmapi.a $(BACKEND_LIB) $(WINDOWS_BACKENDS)

full: $(BACKEND_LIB) $(BACKENDS) $(OPTIONAL_BACKENDS)

clean:
	$(RM) $(BACKEND_LIB) $(BACKENDS) $(OPTIONAL_BACKENDS) $(WINDOWS_BACKENDS)
