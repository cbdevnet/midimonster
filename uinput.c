#include <linux/input.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "midimonster.h"
#include "uinput.h"

#define BACKEND_NAME "uinput"

int init() {

	backend uinput = {
		.name = BACKEND_NAME,
		.conf = backend_configure,
		.create = backend_instance,
		.conf_instance = backend_configure_instance,
		.channel = backend_channel,
		.handle = backend_set,
		.process = backend_handle,
		.start = backend_start,
		.shutdown = backend_shutdown
	};

	if (mm_backend_register(uinput)) {
		fprintf(stderr, "Failed to register uinput backend\n");
		return 1;
	}

	return 0;
}

static int backend_configure(char* option, char* value) {
	fprintf(stderr, "Not implemented\n");
	return 1;
}

static int backend_configure_instance(instance* inst, char* option, char* value) {
	uinput_instance* data = (uinput_instance*) inst->impl;

	if (!strcmp(option, "device")) {
		if (data->device_path) {
			free(data->device_path);
		}
		data->device_path = strdup(value);

		if (!data->device_path) {
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	} else if (!strcmp(option, "name")) {
		if (data->name) {
			free(data->name);
		}

		data->name = strdup(option);

		if (data->name) {
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	} else {
		fprintf(stderr, "Unkown configuration parameter %s for uinput backend\n", option);
		return 1;
	}
	return 0;
}

static channel* backend_channel(instance* inst, char* spec) {
	uinput_instance* data = (uinput_instance*) inst->impl;
	char* next = spec;
	// type
	unsigned long type = strtoul(spec, &next, 10);

	if (spec == next) {
		fprintf(stderr, "Cannot parse type\n");
		return NULL;
	}

	if (type >= EV_MAX) {
		fprintf(stderr, "Type is out of range\n");
		return NULL;
	}

	if (next[0] != '.') {
		fprintf(stderr, "Cannot parse code. Unknown character %c\n", next[0]);
		return NULL;
	}

	spec = next + 1;

	unsigned long code = strtoul(spec, &next, 10);

	if (spec == next) {
		fprintf(stderr, "Cannot parse code\n");
		return NULL;
	}

	if (type == EV_SYN && code >= SYN_MAX) {
		fprintf(stderr, "Code is out of range. Limit for SYN is %d\n", SYN_MAX);
	} else if (type == EV_KEY && code >= KEY_MAX) {
		fprintf(stderr, "Code is out of range. Limit for KEY is %d\n", KEY_MAX);
		return NULL;
	} else if (type == EV_REL && code >= REL_MAX) {
		fprintf(stderr, "Code is out of range. Limit for REL is %d\n", REL_MAX);
		return NULL;
	} else if (type == EV_ABS && code >= ABS_MAX) {
		fprintf(stderr, "Code is out of range. Limit for ABS is %d\n", ABS_MAX);
		return NULL;
	} else if (type == EV_SW && code >= SW_MAX) {
		fprintf(stderr, "Code is out of range. Limit for SW is %d\n", SW_MAX);
		return NULL;
	} else if (type == EV_MSC && code >= MSC_MAX) {
		fprintf(stderr, "Code is out of range. Limit for MSC is %d\n", MSC_MAX);
		return NULL;
	} else if (type == EV_LED && code >= LED_MAX) {
		fprintf(stderr, "Code is out of range. Limit for LED is %d\n", LED_MAX);
		return NULL;
	} else if (type == EV_REP && code >= REP_MAX) {
		fprintf(stderr, "Code is out of range. Limit for REP is %d\n", REP_MAX);
		return NULL;
	} else if (type == EV_SND && code >= SND_MAX) {
		fprintf(stderr, "Code is out of range. Limit for SND is %d\n", SND_MAX);
	}

	if (next[0] != '.') {
		fprintf(stderr, "Cannot parse value. Unknown character %c\n", next[0]);
		return NULL;
	}

	spec = next + 1;

	long value = strtol(spec, &next, 10);

	if (spec == next) {
		fprintf(stderr, "Cannot parse value\n");
		return NULL;
	}

	if (type == EV_KEY && (value != 0 || value != 1)) {
		fprintf(stderr, "Value of KEY is out of range. Only values 0 and 1 are supported for KEY.");
		return NULL;
	}

	// find event
	unsigned u;
	for (u = 0; u < data->size_events; u++) {
		if (data->events[u].type == type
				&& data->events[u].code == code
				&& data->events[u].value == value) {
			break;
		}
	}

	if (u == data->size_events) {
		data->events = realloc(data->events, (u + 1) * sizeof(struct input_event));

		if (!data->events) {
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

		data->events[u].type = (uint16_t) type;
		data->events[u].code = (uint16_t) code;
		data->events[u].value = (int32_t) value;
		data->size_events++;
	}
	return mm_channel(inst, u, 1);
}

static instance* backend_instance() {
	// TODO impl
	return NULL;
}

static int backend_handle(size_t num, managed_fd* fds) {
	//TODO impl
	return 0;
}

static int uinput_open_input_device(uinput_instance* data) {
	if (!data->device_path) {
		return 0;
	}

	data->fd_in = open(data->device_path, O_RDONLY | O_NONBLOCK);

	if (data->fd_in < 0) {
		fprintf(stderr, "Failed to open device %s: %s\n", data->device_path, strerror(errno));
		return 1;
	}

	return 0;
}

static int uinput_create_output_device(uinput_instance* data) {
	//TODO impl
	return 0;
}

static int backend_start() {

	size_t n;
	instance** inst = NULL;
	uinput_instance* data;
	if (mm_backend_instances(BACKEND_NAME, &n, &inst)) {
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if (!n) {
		free(inst);
		return 0;
	}

	for (unsigned p = 0; p < n; p++) {
		data = (uinput_instance*) inst[p]->impl;

		if (data->name) {
			uinput_open_input_device(data);
			uinput_create_output_device(data);
		}
	}

	free(inst);
	return 0;
}

static int backend_set(instance* inst, size_t num, channel** c, channel_value* v) {
	//TODO impl
	return 0;
}

static int backend_shutdown() {
	uinput_instance* data = NULL;
	instance** instances = NULL;
	size_t n = 0;

	if (mm_backend_instances(BACKEND_NAME, &n, &instances)) {
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if (!n) {
		free(instances);
		return 0;
	}

	for (unsigned p = 0; p < n; p++) {
		data = (uinput_instance*) instances[p]->impl;
		if (data->fd_in < 0) {
			close(data->fd_in);
			data->fd_in = -1;
		}

		if (data->fd_out < 0) {
			close(data->fd_out);
			data->fd_out = -1;
		}
	}
	free(instances);
	return 0;
}
