#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libevdev/libevdev.h>
#ifndef EVDEV_NO_UINPUT
#include <libevdev/libevdev-uinput.h>
#endif

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
#ifndef EVDEV_NO_UINPUT
	data->output_proto = libevdev_new();
	if(!data->output_proto){
		fprintf(stderr, "Failed to initialize libevdev output prototype device\n");
		free(data);
		return NULL;
	}
#endif

	inst->impl = data;
	return inst;
}

static int evdev_configure_instance(instance* inst, char* option, char* value) {
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
#ifndef EVDEV_NO_UINPUT
	char* next_token = NULL;
	struct input_absinfo abs_info = {
		0
	};
#endif

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
	else if(!strcmp(option, "exclusive")){
		if(data->input_fd >= 0 && libevdev_grab(data->input_ev, LIBEVDEV_GRAB)){
			fprintf(stderr, "Failed to obtain exclusive device access on %s\n", inst->name);
		}
		data->exclusive = 1;
	}
#ifndef EVDEV_NO_UINPUT
	else if(!strcmp(option, "name")){
		data->output_enabled = 1;
		libevdev_set_name(data->output_proto, value);
	}
	else if(!strcmp(option, "id")){
		next_token = value;
		libevdev_set_id_vendor(data->output_proto, strtol(next_token, &next_token, 0));
		libevdev_set_id_product(data->output_proto, strtol(next_token, &next_token, 0));
		libevdev_set_id_version(data->output_proto, strtol(next_token, &next_token, 0));
	}
	else if(!strncmp(option, "axis.", 5)){
		//value minimum maximum fuzz flat resolution
		next_token = value;
		abs_info.value = strtol(next_token, &next_token, 0);
		abs_info.minimum = strtol(next_token, &next_token, 0);
		abs_info.maximum = strtol(next_token, &next_token, 0);
		abs_info.fuzz = strtol(next_token, &next_token, 0);
		abs_info.flat = strtol(next_token, &next_token, 0);
		abs_info.resolution = strtol(next_token, &next_token, 0);
		if(libevdev_enable_event_code(data->output_proto, EV_ABS, libevdev_event_code_from_name(EV_ABS, option + 5), &abs_info)){
			fprintf(stderr, "Failed to enable absolute axis %s for output\n", option + 5);
			return 1;
		}
	}
#endif
	else{
		fprintf(stderr, "Unknown configuration parameter %s for evdev backend\n", option);
		return 1;
	}
	return 0;
}

static channel* evdev_channel(instance* inst, char* spec){
#ifndef EVDEV_NO_UINPUT
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
#endif
	char* separator = strchr(spec, '.');
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

#ifndef EVDEV_NO_UINPUT
	if(data->output_enabled){
		if(!libevdev_has_event_code(data->output_proto, ident.fields.type, ident.fields.code)){
			//enable the event on the device
			//the previous check is necessary to not fail while enabling axes, which require additional information
			if(libevdev_enable_event_code(data->output_proto, ident.fields.type, ident.fields.code, NULL)){
				fprintf(stderr, "Failed to enable output event %s.%s%s\n",
						libevdev_event_type_get_name(ident.fields.type),
						libevdev_event_code_get_name(ident.fields.type, ident.fields.code),
						(ident.fields.type == EV_ABS) ? ": To output absolute axes, specify their details in the configuration":"");
				return NULL;
			}
		}
	}
#endif

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

static int evdev_start(){
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

#ifndef EVDEV_NO_UINPUT
		if(data->output_enabled){
			if(libevdev_uinput_create_from_device(data->output_proto, LIBEVDEV_UINPUT_OPEN_MANAGED, &data->output_ev)){
				fprintf(stderr, "Failed to create evdev output device: %s\n", strerror(errno));
				return 1;
			}
			fprintf(stderr, "Created device node %s for instance %s\n", libevdev_uinput_get_devnode(data->output_ev), inst[u]->name);
		}
#endif

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
#ifndef EVDEV_NO_UINPUT
	size_t evt = 0;
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
	evdev_channel_ident ident = {
		.label = 0
	};
	int32_t value = 0;
	uint64_t range = 0;

	if(!num){
		return 0;
	}

	if(!data->output_enabled){
		fprintf(stderr, "Instance %s not enabled for output\n", inst->name);
		return 0;
	}

	for(evt = 0; evt < num; evt++){
		ident.label = c[evt]->ident;

		switch(ident.fields.type){
			case EV_REL:
				value = (v[evt].normalised < 0.5) ? -1 : ((v[evt].normalised > 0.5) ? 1 : 0);
				break;
			case EV_ABS:
				range = libevdev_get_abs_maximum(data->output_proto, ident.fields.code) - libevdev_get_abs_minimum(data->output_proto, ident.fields.code);
				value = (range * v[evt].normalised) + libevdev_get_abs_minimum(data->output_proto, ident.fields.code);
				break;
			case EV_KEY:
			case EV_SW:
			default:
				value = (v[evt].normalised > 0.9) ? 1 : 0;
				break;
		}

		if(libevdev_uinput_write_event(data->output_ev, ident.fields.type, ident.fields.code, value)){
			fprintf(stderr, "Failed to output event on instance %s\n", inst->name);
			return 1;
		}
	}

	//send syn event to publish all events
	if(libevdev_uinput_write_event(data->output_ev, EV_SYN, SYN_REPORT, 0)){
		fprintf(stderr, "Failed to output sync event on instance %s\n", inst->name);
		return 1;
	}

	return 0;
#else
	fprintf(stderr, "The evdev backend does not support output on this platform\n");
	return 1;
#endif
}

static int evdev_shutdown(){
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

#ifndef EVDEV_NO_UINPUT
		if(data->output_enabled){
			libevdev_uinput_destroy(data->output_ev);
		}

		libevdev_free(data->output_proto);
#endif
		free(data);
	}

	free(instances);
	return 0;
}
