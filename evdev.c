#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include "midimonster.h"
#include "evdev.h"

#define BACKEND_NAME "evdev"

typedef union {
	struct {
		uint32_t pad;
		uint16_t type;
		uint16_t code;
	} fields;
	uint64_t label;
} evdev_channel_ident;

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
	fprintf(stderr, "The evdev backend does not take any global configuration\n");
	return 1;
}

static instance* evdev_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	evdev_instance_data* data = calloc(1, sizeof(evdev_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	data->input_fd = -1;
	data->output_fd = -1;

	inst->impl = data;
	return inst;
}

static int evdev_configure_instance(instance* inst, char* option, char* value) {
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;

	if(!strcmp(option, "input")){
		if(data->input_fd >= 0){
			fprintf(stderr, "Instance %s already was assigned an input device\n", inst->name);
			return 1;
		}

		data->input_fd = open(value, O_RDONLY | O_NONBLOCK);
		if(data->input_fd < 0){
			fprintf(stderr, "Failed to open evdev input device node %s: %s\n", value, strerror(errno));
			return 1;
		}

		if(libevdev_new_from_fd(data->input_fd, &data->input_ev)){
			fprintf(stderr, "Failed to initialize libevdev for %s\n", value);
			close(data->input_fd);
			data->input_fd = -1;
			return 1;
		}

		if(data->exclusive && libevdev_grab(data->input_ev, LIBEVDEV_GRAB)){
			fprintf(stderr, "Failed to obtain exclusive device access on %s\n", value);
		}
	}
	else if (!strcmp(option, "exclusive")){
		if(data->input_fd >= 0 && libevdev_grab(data->input_ev, LIBEVDEV_GRAB)){
			fprintf(stderr, "Failed to obtain exclusive device access on %s\n", inst->name);
		}
		data->exclusive = 1;
	}
	else if (!strcmp(option, "name")){
		if(data->output_name){
			fprintf(stderr, "Instance %s evdev device name already assigned\n", inst->name);
			return 1;
		}

		data->output_name = strdup(value);
		if (!data->output_name) {
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
	}
	else{
		fprintf(stderr, "Unknown configuration parameter %s for evdev backend\n", option);
		return 1;
	}
	return 0;
}

static channel* evdev_channel(instance* inst, char* spec) {
	char* separator = strchr(spec, '.');
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
	evdev_channel_ident ident = {
		.label = 0
	};

	if(!separator){
		fprintf(stderr, "Invalid evdev channel specification %s\n", spec);
		return NULL;
	}

	*(separator++) = 0;

	if(libevdev_event_type_from_name(spec) < 0){
		fprintf(stderr, "Invalid evdev type specification: %s", spec);
		return NULL;
	}
	ident.fields.type = libevdev_event_type_from_name(spec);

	if(libevdev_event_code_from_name(ident.fields.type, separator) >= 0){
		ident.fields.code = libevdev_event_code_from_name(ident.fields.type, separator);
	}
	else{
		fprintf(stderr, "evdev Code name not recognized, using as number: %s\n", separator);
		ident.fields.code = strtoul(separator, NULL, 10);
	}

	//TODO If allowing output, push to enable list
	return mm_channel(inst, ident.label, 1);
}

static int evdev_push_event(instance* inst, evdev_instance_data* data, struct input_event event){
	uint64_t range = 0;
	channel_value val;
	evdev_channel_ident ident = {
		.fields.type = event.type,
		.fields.code = event.code
	};
	channel* chan = mm_channel(inst, ident.label, 0);

	if(chan){
		val.raw.u64 = event.value;
		switch(event.type){
			case EV_REL:
				val.normalised = 0.5 + ((event.value < 0) ? 0.5 : -0.5);
				break;
			case EV_ABS:
				range = libevdev_get_abs_maximum(data->input_ev, event.code) - libevdev_get_abs_minimum(data->input_ev, event.code);
				val.normalised = (event.value - libevdev_get_abs_minimum(data->input_ev, event.code)) / (double) range;
				break;
			case EV_KEY:
			case EV_SW:
			default:
				val.normalised = 1.0 * event.value;
				break;
		}

		if(mm_channel_event(chan, val)){
			fprintf(stderr, "Failed to push evdev channel event to core\n");
			return 1;
		}
	}

	return 0;
}

static int evdev_handle(size_t num, managed_fd* fds){
	instance* inst = NULL;
	evdev_instance_data* data = NULL;
	size_t fd;
	unsigned int read_flags = LIBEVDEV_READ_FLAG_NORMAL;
	int read_status;
	struct input_event ev;

	if(!num){
		return 0;
	}

	for(fd = 0; fd < num; fd++){
		inst = (instance*) fds[fd].impl;
		if(!inst){
			fprintf(stderr, "evdev backend signaled for unknown fd\n");
			continue;
		}

		data = (evdev_instance_data*) inst->impl;

		for(read_status = libevdev_next_event(data->input_ev, read_flags, &ev); read_status >= 0; read_status = libevdev_next_event(data->input_ev, read_flags, &ev)){
			read_flags = LIBEVDEV_READ_FLAG_NORMAL;
			if(read_status == LIBEVDEV_READ_STATUS_SYNC){
				read_flags = LIBEVDEV_READ_FLAG_SYNC;
			}

			//handle event
			if(evdev_push_event(inst, data, ev)){
				return 1;
			}
		}
	}

	return 0;
}

static int evdev_start() {
	size_t n, u, fds = 0;
	instance** inst = NULL;
	evdev_instance_data* data = NULL;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if(!n){
		free(inst);
		return 0;
	}

	for(u = 0; u < n; u++){
		data = (evdev_instance_data*) inst[u]->impl;

		if(data->output_name) {
			//TODO
			//if(evdev_create_output(data)){
			//	return 1;
			//}
		}

		inst[u]->ident = data->input_fd;
		if(data->input_fd >= 0){
			if(mm_manage_fd(data->input_fd, BACKEND_NAME, 1, inst[u])){
				fprintf(stderr, "Failed to register event input descriptor for instance %s\n", inst[u]->name);
				free(inst);
				return 1;
			}
			fds++;
		}

	}

	fprintf(stderr, "evdev backend registered %zu descriptors to core\n", fds);
	free(inst);
	return 0;
}

static int evdev_set(instance* inst, size_t num, channel** c, channel_value* v) {
	if(!num){
		return 0;
	}
/*	size_t u;
	evdev_instance_data* data;
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

	return 0;*/
	fprintf(stderr, "Awaiting rework, %zu channels signaled\n", num);
	return 0;
}

static int evdev_shutdown() {
	evdev_instance_data* data = NULL;
	instance** instances = NULL;
	size_t n, u;

	if(mm_backend_instances(BACKEND_NAME, &n, &instances)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (evdev_instance_data*) instances[u]->impl;

		if(data->input_fd >= 0){
			libevdev_free(data->input_ev);
			close(data->input_fd);
		}

		if(data->output_fd >= 0){
			libevdev_uinput_destroy(data->output_ev);
			close(data->output_fd);
		}

		free(data->output_name);
		free(data->enabled_events);
	}

	free(instances);
	return 0;
}
