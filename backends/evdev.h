#include <sys/types.h>

#include "midimonster.h"

/*
 * This provides read-write access to the Linux kernel evdev subsystem
 * via libevdev. On systems where uinput is not supported, output can be
 * disabled by building with -DEVDEV_NO_UINPUT
 */

MM_PLUGIN_API int init();
static int evdev_configure(char* option, char* value);
static int evdev_configure_instance(instance* instance, char* option, char* value);
static instance* evdev_instance();
static channel* evdev_channel(instance* instance, char* spec, uint8_t flags);
static int evdev_set(instance* inst, size_t num, channel** c, channel_value* v);
static int evdev_handle(size_t num, managed_fd* fds);
static int evdev_start(size_t n, instance** inst);
static int evdev_shutdown(size_t n, instance** inst);

#define INPUT_NODES "/dev/input"
#define INPUT_PREFIX "event"
#ifndef UINPUT_MAX_NAME_SIZE
	#define UINPUT_MAX_NAME_SIZE 512
#endif

typedef struct /*_evdev_relative_axis_config*/ {
	uint8_t inverted;
	int code;
	int64_t max;
	int64_t current;
} evdev_relaxis_config;

typedef struct /*_evdev_instance_model*/ {
	int input_fd;
	struct libevdev* input_ev;
	int exclusive;
	size_t relative_axes;
	evdev_relaxis_config* relative_axis;

	int output_enabled;
#ifndef EVDEV_NO_UINPUT
	struct libevdev* output_proto;
	struct libevdev_uinput* output_ev;
#endif
} evdev_instance_data;

typedef union {
	struct {
		uint32_t pad;
		uint16_t type;
		uint16_t code;
	} fields;
	uint64_t label;
} evdev_channel_ident;

