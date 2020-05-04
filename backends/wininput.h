#include "midimonster.h"

MM_PLUGIN_API int init();
static int wininput_configure(char* option, char* value);
static int wininput_configure_instance(instance* inst, char* option, char* value);
static int wininput_instance(instance* inst);
static channel* wininput_channel(instance* inst, char* spec, uint8_t flags);
static uint32_t wininput_interval();
static int wininput_set(instance* inst, size_t num, channel** c, channel_value* v);
static int wininput_handle(size_t num, managed_fd* fds);
static int wininput_start(size_t n, instance** inst);
static int wininput_shutdown(size_t n, instance** inst);

enum /*wininput_channel_type*/ {
	none = 0,
	mouse,
	keyboard,
	joystick
};

enum /*wininput_control_channel*/ {
	keypress = 0,
	button,
	position,
	//wheel, /*relative*/

	key_unicode
};

typedef struct /*_wininput_key_info*/ {
	uint8_t keycode;
	char* name;
	uint8_t channel;
} key_info;

typedef union {
	struct {
		uint8_t pad[4];
		uint8_t type;
		uint8_t channel;
		uint16_t control;
	} fields;
	uint64_t label;
} wininput_channel_ident;

typedef struct /*_input_request*/ {
	wininput_channel_ident ident;
	size_t channels;
	channel** channel;
	uint16_t state;
} wininput_request;
