#include "midimonster.h"

MM_PLUGIN_API int init();
static int winmidi_configure(char* option, char* value);
static int winmidi_configure_instance(instance* inst, char* option, char* value);
static instance* winmidi_instance();
static channel* winmidi_channel(instance* inst, char* spec, uint8_t flags);
static int winmidi_set(instance* inst, size_t num, channel** c, channel_value* v);
static int winmidi_handle(size_t num, managed_fd* fds);
static int winmidi_start(size_t n, instance** inst);
static int winmidi_shutdown(size_t n, instance** inst);

typedef struct /*_winmidi_instance_data*/ {
	char* read;
	char* write;
	HMIDIIN device_in;
	HMIDIOUT device_out;
} winmidi_instance_data;

enum /*_winmidi_channel_type*/ {
	none = 0,
	note = 0x90,
	cc = 0xB0,
	pressure = 0xA0,
	aftertouch = 0xD0,
	pitchbend = 0xE0
};

typedef union {
	struct {
		uint8_t pad[5];
		uint8_t type;
		uint8_t channel;
		uint8_t control;
	} fields;
	uint64_t label;
} winmidi_channel_ident;

typedef struct /*_winmidi_event_queue_entry*/ {
	instance* inst;
	winmidi_channel_ident channel;
	channel_value value;
} winmidi_event;
