#include "midimonster.h"

MM_PLUGIN_API int init();
static int wininput_configure(char* option, char* value);
static int wininput_configure_instance(instance* inst, char* option, char* value);
static int wininput_instance(instance* inst);
static channel* wininput_channel(instance* inst, char* spec, uint8_t flags);
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
	position,
	//wheel, /*relative*/
	button,

	keypress,
	key_unicode
};

typedef union {
	struct {
		uint8_t pad[4];
		uint8_t type;
		uint8_t channel;
		uint16_t control;
	} fields;
	uint64_t label;
} wininput_channel_ident;

typedef struct {
	struct {
		uint16_t x;
		uint16_t y;
	} mouse;
} wininput_instance_data;
