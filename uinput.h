#pragma once

#include <sys/types.h>
#include <linux/input.h>

#include "midimonster.h"

int init();
static int backend_configure(char* option, char* value);
static int backend_configure_instance(instance* instance, char* option, char* value);
static instance* backend_instance();
static channel* backend_channel(instance* instance, char* spec);
static int backend_set(instance* inst, size_t num, channel** c, channel_value* v);
static int backend_handle(size_t num, managed_fd* fds);
static int backend_start();
static int backend_shutdown();

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
} uinput_instance;
