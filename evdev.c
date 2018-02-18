#include <linux/input.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#include "midimonster.h"
#include "evdev.h"

#define BACKEND_NAME "evdev"
#define UINPUT_PATH "/dev/uinput"

int init(){
	backend evdev = {
		.name = BACKEND_NAME,
		.conf = evdev_configure,
		.create = evdev_instance,
		.conf_instance = evdev_configure_instance,
		.channel = evdev_channel,
		.handle = evdev_set,
		.process = evdev_handle,
		.start = evdev_start,
		.shutdown = evdev_shutdown
	};

	if(mm_backend_register(evdev)){
		fprintf(stderr, "Failed to register evdev backend\n");
		return 1;
	}

	return 0;
}

static int evdev_configure(char* option, char* value) {
	//intentionally ignored
	return 0;
}

static int evdev_configure_instance(instance* inst, char* option, char* value) {
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;

	if (!strcmp(option, "device")) {
		if (data->device_path) {
			free(data->device_path);
		}
		data->device_path = strdup(value);

		if (!data->device_path) {
			fprintf(stderr, "Failed to allocate memory for device path: %s\n", strerror(errno));
			return 1;
		}
	} else if (!strcmp(option, "exclusive")) {
		data->exclusive = strtoul(value, NULL, 10);
	} else if (!strcmp(option, "name")) {
		if (data->name) {
			free(data->name);
		}

		data->name = strdup(value);

		if (!data->name) {
			fprintf(stderr, "Failed to allocate memory for name: %s\n", strerror(errno));
			return 1;
		}
	} else {
		fprintf(stderr, "Unkown configuration parameter %s for evdev backend\n", option);
		return 1;
	}
	return 0;
}

static channel* evdev_channel(instance* inst, char* spec) {
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
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

	uint64_t u;
	if (next[0] == '.') {
		spec = next + 1;
		long value = strtol(spec, &next, 10);
		if (spec == next) {
			fprintf(stderr, "Cannot parse value\n");
			return NULL;
		}
		if (type == EV_KEY && (value != 0 && value != 1)) {
			fprintf(stderr, "Value of KEY %ld is out of range. Only values 0 and 1 are supported for KEY.\n", value);
			return NULL;
		}
		// find event with value
		for (u = 0; u < data->size_events; u++) {
			if (data->events[u].type == type
					&& data->events[u].code == code
					&& data->events[u].value == value) {
				break;
			}
		}
	} else if (next[0] != '\0') {
		fprintf(stderr, "Unkown characters: %s\n", next);
		return NULL;
	} else {
		// find event
		for (u = 0; u < data->size_events; u++) {
			if (data->events[u].type == type
					&& data->events[u].code == code) {
				break;
			}
		}
	}

	// check if no event was found
	if (u == data->size_events) {
		fprintf(stderr, "Alloc dev %ld: %ld, %ld\n", u, type, code);
		data->events = realloc(data->events, (u + 1) * sizeof(struct input_event));

		if (!data->events) {
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

		data->events[u].type = (uint16_t) type;
		data->events[u].code = (uint16_t) code;
		data->size_events++;
	}
	return mm_channel(inst, u, 1);
}

static instance* evdev_instance() {
	instance* inst = mm_instance();
	if (!inst) {
		return NULL;
	}

	inst->impl = calloc(1, sizeof(evdev_instance_data));
	if (!inst->impl) {
		fprintf(stderr, "Failed to allocate memory for instance\n");
		return NULL;
	}
	return inst;
}

static channel_value evdev_normalize(evdev_instance_data* data, uint64_t ident, struct input_event* event) {
	channel_value value = {};

	switch (event->type) {
		case EV_KEY:
			value.normalised = event->value > 0;
			break;
		case EV_REL:
			if (event->value > 0) {
				value.normalised = 1.0;
			} else {
				value.normalised = 0.0;
			}
			break;
	}

	return value;
}

static uint32_t evdev_convert_normalised(struct input_event event, channel_value* value) {
	switch (event.type) {
		case EV_KEY:
			return value->normalised < 0.5;
		case EV_REL:
			return (value->normalised < 0.5) - 1;
		default:
			return value->normalised < 0.5;
	}
}


static int evdev_handle(size_t num, managed_fd* fds) {
	struct input_event event;
	ssize_t bytes = 0;
	uint64_t ident;

	evdev_instance_data* data;

	channel* channel;
	for (int i = 0; i < num; i++) {
		bytes = read(fds[i].fd, &event, sizeof(struct input_event));

		if (bytes < sizeof(struct input_event)) {
			fprintf(stderr, "Failed to read an complete event\n");
			return 1;
		}
		data = (evdev_instance_data*) fds[0].impl;
		for (ident = 0; ident < data->size_events; ident++) {
			if (data->events[ident].type == event.type
					&& data->events[ident].code == event.code) {
				break;
			}
		}
		fprintf(stderr, "Found event: %d, %d, %d (%ld/%ld)\n", event.type, event.code, event.value, ident, data->size_events);

		if (ident >= data->size_events) {
			fprintf(stderr, "Event not registered.\n");
			continue;
		}
		channel = mm_channel(mm_instance_find(BACKEND_NAME, data->ident), ident, 0);

		if (channel) {
			fprintf(stderr, "Channel found\n");
			if (mm_channel_event(channel, evdev_normalize(data, ident, &event))) {
				return 1;
			}
		}
	}

	return 0;
}

static int evdev_open_input_device(evdev_instance_data* data) {
	if (!data->device_path) {
		return 0;
	}

	data->fd_in = open(data->device_path, O_RDONLY | O_NONBLOCK);

	if (data->fd_in < 0) {
		fprintf(stderr, "Failed to open device %s: %s\n", data->device_path, strerror(errno));
		return 1;
	}
	int grab = data->exclusive;
	if (ioctl(data->fd_in, EVIOCGRAB, &grab) > 0) {
		fprintf(stderr, "Cannot set exclusive lock on device %s\n", data->device_path);
		close(data->fd_in);
		data->fd_in = -1;
		return 1;
	}

	if (!mm_manage_fd(data->fd_in, BACKEND_NAME, 1, data)) {
		return 1;
	}

	return 0;
}

static int enable_device_keys(evdev_instance_data* data, int uinput_fd, struct uinput_user_dev* dev) {
	unsigned int u;
	int ret;
	int action;
	uint8_t first_bits[EV_CNT];
	memset(first_bits, 0, EV_CNT * sizeof(uint8_t));
	for (u = 0; u < data->size_events; u++) {
		if (data->events[u].type < EV_MAX && !first_bits[data->events[u].type]) {
			ret = ioctl(uinput_fd, UI_SET_EVBIT, data->events[u].type);

			if (ret < 0) {
				fprintf(stderr, "Cannot enable type: %d\n", data->events[u].type);
				return 1;
			}
		}
		switch (data->events[u].type) {
			case EV_KEY:
				action = UI_SET_KEYBIT;
				break;
			case EV_ABS:
				action = UI_SET_ABSBIT;
				break;
			case EV_REL:
				action = UI_SET_RELBIT;
				break;
			case EV_MSC:
				action = UI_SET_MSCBIT;
				break;
			default:
				fprintf(stderr, "Event code not supported: %d\n", data->events[u].type);
				return 1;
		}
		ret = ioctl(uinput_fd,  action, data->events[u].code);

		if (ret < 0) {
			fprintf(stderr, "Cannot enable code: %d\n", data->events[u].code);
			return 1;
		}
	}
	return 0;
}

static int uinput_create_output_device(evdev_instance_data* data) {

	int uinput_fd = open(UINPUT_PATH, O_WRONLY | O_NONBLOCK);

	if (uinput_fd < 0) {
		fprintf(stderr, "Cannot open uinput device: %s\n", strerror(errno));
		return 1;
	}

	struct uinput_user_dev dev = {};
	memset(&dev, 0, sizeof(dev));
	strncpy(dev.name, data->name, UINPUT_MAX_NAME_SIZE - 1);
	dev.id.bustype = 0;
	dev.id.vendor = 0;
	dev.id.product = 0;
	dev.id.version = 0;

	if (enable_device_keys(data, uinput_fd, &dev)) {
		close(uinput_fd);
		return 1;
	}
	// write config to uinput
	int ret = write(uinput_fd, &dev, sizeof(dev));

	if (ret < 0) {
		fprintf(stderr, "Cannot write to uinput device: %s\n", strerror(errno));
		close(uinput_fd);
		return 1;
	}

	ret = ioctl(uinput_fd, UI_DEV_CREATE);

	if (ret < 0) {
		fprintf(stderr, "Cannot create device: %s\n", strerror(errno));
		close(uinput_fd);
		return 1;
	}

	data->fd_out = uinput_fd;

	return 0;
}

static int evdev_start() {
	size_t n;
	instance** inst = NULL;
	evdev_instance_data* data;
	if (mm_backend_instances(BACKEND_NAME, &n, &inst)) {
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if (!n) {
		free(inst);
		return 0;
	}

	for (unsigned p = 0; p < n; p++) {
		data = (evdev_instance_data*) inst[p]->impl;

		if (data->name) {
			uinput_create_output_device(data);
		}

		if (data->device_path) {
			evdev_open_input_device(data);
		}
		data->ident = p;
		inst[p]->ident = data->ident;
	}

	free(inst);
	return 0;
}

static int evdev_set(instance* inst, size_t num, channel** c, channel_value* v) {
	size_t u;
	evdev_instance_data* data;
	uint64_t ident;
	int ret;
	struct input_event event = {};

	for (u = 0; u < num; u++) {
		data = (evdev_instance_data*) c[u]->instance->impl;
		ident = c[u]->ident;

		memcpy(&event, &data->events[ident], sizeof(struct input_event));
		event.value = evdev_convert_normalised(event, v);

		ret = write(data->fd_out, &event, sizeof(event));
		if (ret < 0 ) {
			fprintf(stderr, "Cannot write event: %s\n", strerror(errno));
			return 1;
		}
		event.type = EV_SYN;
		event.code = SYN_REPORT;
		event.value = 0;

		ret = write(data->fd_out, &event, sizeof(event));
		if (ret < 0) {
			fprintf(stderr, "Cannot send SYN_REPORT event: %s\n", strerror(errno));
			return 1;
		}
	}

	return 0;
}

static int evdev_shutdown() {
	evdev_instance_data* data = NULL;
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
		data = (evdev_instance_data*) instances[p]->impl;
		if (data->fd_in < 0) {
			close(data->fd_in);
			data->fd_in = -1;
		}

		if (data->fd_out < 0) {
			int ret = ioctl(data->fd_out, UI_DEV_DESTROY);

			if (ret < 0) {
				fprintf(stderr, "Could not destroy device: %s\n", strerror(errno));
				return 1;
			}
			close(data->fd_out);
			data->fd_out = -1;
		}

		free(data->events);
		free(data->name);
		free(data->device_path);
		free(data);
	}
	free(instances);
	return 0;
}
