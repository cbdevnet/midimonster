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

/* uinput_instance */
typedef struct {
	int ident;
	char* device_path;
	char* name;
	int fd_in;
	int fd_out;
	int exclusive;
	size_t size_events;
	struct input_event* events;
} evdev_instance_data;
