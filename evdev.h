#include <sys/types.h>
#include <linux/input.h>

#include "midimonster.h"

int init();
static int evdev_configure(char* option, char* value);
static int evdev_configure_instance(instance* instance, char* option, char* value);
static instance* evdev_instance();
static channel* evdev_channel(instance* instance, char* spec);
static int evdev_set(instance* inst, size_t num, channel** c, channel_value* v);
static int evdev_handle(size_t num, managed_fd* fds);
static int evdev_start();
static int evdev_shutdown();

typedef struct /*_evdev_instance_model*/ {
	int input_fd;
	struct libevdev* input_ev;
	int exclusive;

	int output_fd;
	char* output_name;
	struct libevdev_uinput* output_ev;

	size_t nenabled_events;
	struct input_event* enabled_events;
} evdev_instance_data;
