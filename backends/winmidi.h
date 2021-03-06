#include "midimonster.h"

MM_PLUGIN_API int init();
static int winmidi_configure(char* option, char* value);
static int winmidi_configure_instance(instance* inst, char* option, char* value);
static int winmidi_instance(instance* inst);
static channel* winmidi_channel(instance* inst, char* spec, uint8_t flags);
static int winmidi_set(instance* inst, size_t num, channel** c, channel_value* v);
static int winmidi_handle(size_t num, managed_fd* fds);
static int winmidi_start(size_t n, instance** inst);
static int winmidi_shutdown(size_t n, instance** inst);

#define EPN_NRPN 8
#define EPN_PARAMETER_HI 4
#define EPN_PARAMETER_LO 2
#define EPN_VALUE_HI 1

typedef struct /*_winmidi_instance_data*/ {
	char* read;
	char* write;
	
	uint8_t epn_tx_short;
	uint16_t epn_control[16];
	uint16_t epn_value[16];
	uint8_t epn_status[16];

	HMIDIIN device_in;
	HMIDIOUT device_out;
} winmidi_instance_data;

enum /*_winmidi_channel_type*/ {
	none = 0,
	note = 0x90,
	pressure = 0xA0,
	cc = 0xB0,
	program = 0xC0,
	aftertouch = 0xD0,
	pitchbend = 0xE0,
	rpn = 0xF1,
	nrpn = 0xF2
};

typedef union {
	struct {
		uint8_t pad[4];
		uint8_t type;
		uint8_t channel;
		uint16_t control;
	} fields;
	uint64_t label;
} winmidi_channel_ident;

typedef struct /*_winmidi_event_queue_entry*/ {
	instance* inst;
	winmidi_channel_ident channel;
	channel_value value;
} winmidi_event;
