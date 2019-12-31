#define BACKEND_NAME "evdev"

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/input.h>
#ifndef EVDEV_NO_UINPUT
#include <libevdev/libevdev-uinput.h>
#endif

#include "midimonster.h"
#include "evdev.h"

static struct {
	uint8_t detect;
} evdev_config = {
	.detect = 0
};

MM_PLUGIN_API int init(){
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

	if(sizeof(evdev_channel_ident) != sizeof(uint64_t)){
		LOG("Channel identification union out of bounds");
		return 1;
	}

	if(mm_backend_register(evdev)){
		LOG("Failed to register backend");
		return 1;
	}

	return 0;
}

static int evdev_configure(char* option, char* value) {
	if(!strcmp(option, "detect")){
		evdev_config.detect = 1;
		if(!strcmp(value, "off")){
			evdev_config.detect = 0;
		}
		return 0;
	}

	LOGPF("Unknown backend configuration option %s", option);
	return 1;
}

static instance* evdev_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	evdev_instance_data* data = calloc(1, sizeof(evdev_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return NULL;
	}

	data->input_fd = -1;
#ifndef EVDEV_NO_UINPUT
	data->output_proto = libevdev_new();
	if(!data->output_proto){
		LOG("Failed to initialize libevdev output prototype device");
		free(data);
		return NULL;
	}
#endif

	inst->impl = data;
	return inst;
}

static int evdev_attach(instance* inst, evdev_instance_data* data, char* node){
	if(data->input_fd >= 0){
		LOGPF("Instance %s already assigned an input device", inst->name);
		return 1;
	}

	data->input_fd = open(node, O_RDONLY | O_NONBLOCK);
	if(data->input_fd < 0){
		LOGPF("Failed to open input device node %s: %s", node, strerror(errno));
		return 1;
	}

	if(libevdev_new_from_fd(data->input_fd, &data->input_ev)){
		LOGPF("Failed to initialize libevdev for %s", node);
		close(data->input_fd);
		data->input_fd = -1;
		return 1;
	}

	if(data->exclusive && libevdev_grab(data->input_ev, LIBEVDEV_GRAB)){
		LOGPF("Failed to obtain exclusive device access on %s", node);
	}

	return 0;
}

static char* evdev_find(char* name){
	int fd = -1;
	struct dirent* file = NULL;
	char file_path[PATH_MAX * 2];
	DIR* nodes = opendir(INPUT_NODES);
	char device_name[UINPUT_MAX_NAME_SIZE], *result = NULL;

	if(!nodes){
		LOGPF("Failed to query input device nodes in %s: %s", INPUT_NODES, strerror(errno));
		return NULL;
	}

	for(file = readdir(nodes); file; file = readdir(nodes)){
		if(!strncmp(file->d_name, INPUT_PREFIX, strlen(INPUT_PREFIX)) && file->d_type == DT_CHR){
			snprintf(file_path, sizeof(file_path), "%s/%s", INPUT_NODES, file->d_name);

			fd = open(file_path, O_RDONLY);
			if(fd < 0){
				LOGPF("Failed to access %s: %s", file_path, strerror(errno));
				continue;
			}

			if(ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name) < 0){
				LOGPF("Failed to read name for %s: %s", file_path, strerror(errno));
				close(fd);
				continue;
			}

			close(fd);

			if(!strncmp(device_name, name, strlen(name))){
				LOGPF("Matched name %s for %s: %s", device_name, name, file_path);
				break;
			}
		}
	}

	if(file){
		result = calloc(strlen(file_path) + 1, sizeof(char));
		if(result){
			strncpy(result, file_path, strlen(file_path));
		}
	}

	closedir(nodes);
	return result;
}

static int evdev_configure_instance(instance* inst, char* option, char* value) {
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
	char* next_token = NULL;
#ifndef EVDEV_NO_UINPUT
	struct input_absinfo abs_info = {
		0
	};
#endif

	if(!strcmp(option, "device")){
		return evdev_attach(inst, data, value);
	}
	else if(!strcmp(option, "input")){
		next_token = evdev_find(value);
		if(!next_token){
			LOGPF("Failed to match input device with name %s for instance %s", value, inst->name);
			return 1;
		}
		if(evdev_attach(inst, data, next_token)){
			free(next_token);
			return 1;
		}
		free(next_token);
		return 0;
	}
	else if(!strcmp(option, "exclusive")){
		if(data->input_fd >= 0 && libevdev_grab(data->input_ev, LIBEVDEV_GRAB)){
			LOGPF("Failed to obtain exclusive device access on %s", inst->name);
		}
		data->exclusive = 1;
		return 0;
	}
	else if(!strncmp(option, "relaxis.", 8)){
		data->relative_axis = realloc(data->relative_axis, (data->relative_axes + 1) * sizeof(evdev_relaxis_config));
		if(!data->relative_axis){
			LOG("Failed to allocate memory");
			return 1;
		}
		data->relative_axis[data->relative_axes].inverted = 0;
		data->relative_axis[data->relative_axes].code = libevdev_event_code_from_name(EV_REL, option + 8);
		data->relative_axis[data->relative_axes].max = strtoll(value, &next_token, 0);
		if(data->relative_axis[data->relative_axes].max < 0){
			data->relative_axis[data->relative_axes].max *= -1;
			data->relative_axis[data->relative_axes].inverted = 1;
		}
		else if(data->relative_axis[data->relative_axes].max == 0){
			LOGPF("Relative axis configuration for %s.%s has invalid range", inst->name, option + 8);
		}
		data->relative_axis[data->relative_axes].current = strtoul(next_token, NULL, 0);
		if(data->relative_axis[data->relative_axes].code < 0){
			LOGPF("Failed to configure relative axis extents for %s.%s", inst->name, option + 8);
			return 1;
		}
		data->relative_axes++;
		return 0;
	}
#ifndef EVDEV_NO_UINPUT
	else if(!strcmp(option, "output")){
		data->output_enabled = 1;
		libevdev_set_name(data->output_proto, value);
		return 0;
	}
	else if(!strcmp(option, "id")){
		next_token = value;
		libevdev_set_id_vendor(data->output_proto, strtol(next_token, &next_token, 0));
		libevdev_set_id_product(data->output_proto, strtol(next_token, &next_token, 0));
		libevdev_set_id_version(data->output_proto, strtol(next_token, &next_token, 0));
		return 0;
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
			LOGPF("Failed to enable absolute axis %s.%s for output", inst->name, option + 5);
			return 1;
		}
		return 0;
	}
#endif
	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static channel* evdev_channel(instance* inst, char* spec, uint8_t flags){
#ifndef EVDEV_NO_UINPUT
	evdev_instance_data* data = (evdev_instance_data*) inst->impl;
#endif
	char* separator = strchr(spec, '.');
	evdev_channel_ident ident = {
		.label = 0
	};

	if(!separator){
		LOGPF("Invalid channel specification %s", spec);
		return NULL;
	}

	*(separator++) = 0;

	if(libevdev_event_type_from_name(spec) < 0){
		LOGPF("Invalid type specification: %s", spec);
		return NULL;
	}
	ident.fields.type = libevdev_event_type_from_name(spec);

	if(libevdev_event_code_from_name(ident.fields.type, separator) >= 0){
		ident.fields.code = libevdev_event_code_from_name(ident.fields.type, separator);
	}
	else{
		LOGPF("Code name not recognized, using as number: %s.%s", inst->name, separator);
		ident.fields.code = strtoul(separator, NULL, 10);
	}

#ifndef EVDEV_NO_UINPUT
	if(data->output_enabled){
		if(!libevdev_has_event_code(data->output_proto, ident.fields.type, ident.fields.code)){
			//enable the event on the device
			//the previous check is necessary to not fail while enabling axes, which require additional information
			if(libevdev_enable_event_code(data->output_proto, ident.fields.type, ident.fields.code, NULL)){
				LOGPF("Failed to enable output event %s.%s%s",
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
	size_t axis;

	if(chan){
		val.raw.u64 = event.value;
		switch(event.type){
			case EV_REL:
				for(axis = 0; axis < data->relative_axes; axis++){
					if(data->relative_axis[axis].code == event.code){
						if(data->relative_axis[axis].inverted){
							event.value *= -1;
						}
						data->relative_axis[axis].current = clamp(data->relative_axis[axis].current + event.value, data->relative_axis[axis].max, 0);
						val.normalised = (double) data->relative_axis[axis].current / (double) data->relative_axis[axis].max;
						break;
					}
				}
				if(axis == data->relative_axes){
					val.normalised = 0.5 + ((event.value < 0) ? 0.5 : -0.5);
					break;
				}
				break;
			case EV_ABS:
				range = libevdev_get_abs_maximum(data->input_ev, event.code) - libevdev_get_abs_minimum(data->input_ev, event.code);
				val.normalised = clamp((event.value - libevdev_get_abs_minimum(data->input_ev, event.code)) / (double) range, 1.0, 0.0);
				break;
			case EV_KEY:
			case EV_SW:
			default:
				val.normalised = clamp(1.0 * event.value, 1.0, 0.0);
				break;
		}

		if(mm_channel_event(chan, val)){
			LOG("Failed to push channel event to core");
			return 1;
		}
	}

	if(evdev_config.detect){
		LOGPF("Incoming data for channel %s.%s.%s", inst->name, libevdev_event_type_get_name(event.type), libevdev_event_code_get_name(event.type, event.code));
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
			LOG("Signaled for unknown FD");
			continue;
		}

		data = (evdev_instance_data*) inst->impl;

		for(read_status = libevdev_next_event(data->input_ev, read_flags, &ev); read_status >= 0; read_status = libevdev_next_event(data->input_ev, read_flags, &ev)){
			read_flags = LIBEVDEV_READ_FLAG_NORMAL;
			if(read_status == LIBEVDEV_READ_STATUS_SYNC){
				read_flags = LIBEVDEV_READ_FLAG_SYNC;
			}

			//exclude synchronization events
			if(ev.type == EV_SYN){
				continue;
			}

			//handle event
			if(evdev_push_event(inst, data, ev)){
				return 1;
			}
		}
	}

	return 0;
}

static int evdev_start(size_t n, instance** inst){
	size_t u, fds = 0;
	evdev_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (evdev_instance_data*) inst[u]->impl;

#ifndef EVDEV_NO_UINPUT
		if(data->output_enabled){
			if(libevdev_uinput_create_from_device(data->output_proto, LIBEVDEV_UINPUT_OPEN_MANAGED, &data->output_ev)){
				LOGPF("Failed to create output device: %s", strerror(errno));
				return 1;
			}
			LOGPF("Created device node %s for instance %s", libevdev_uinput_get_devnode(data->output_ev), inst[u]->name);
		}
#endif

		inst[u]->ident = data->input_fd;
		if(data->input_fd >= 0){
			if(mm_manage_fd(data->input_fd, BACKEND_NAME, 1, inst[u])){
				LOGPF("Failed to register input descriptor for instance %s", inst[u]->name);
				return 1;
			}
			fds++;
		}

		if(data->input_fd <= 0 && !data->output_ev){
			LOGPF("Instance %s has neither input nor output device set up", inst[u]->name);
		}

	}

	LOGPF("Registered %zu descriptors to core", fds);
	return 0;
}

static int evdev_set(instance* inst, size_t num, channel** c, channel_value* v) {
#ifndef EVDEV_NO_UINPUT
	size_t evt = 0, axis = 0;
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
		LOGPF("Instance %s not enabled for output (%" PRIsize_t " channel events)", inst->name, num);
		return 0;
	}

	for(evt = 0; evt < num; evt++){
		ident.label = c[evt]->ident;

		switch(ident.fields.type){
			case EV_REL:
				for(axis = 0; axis < data->relative_axes; axis++){
					if(data->relative_axis[axis].code == ident.fields.code){
						value = (v[evt].normalised * data->relative_axis[axis].max) - data->relative_axis[axis].current;
						data->relative_axis[axis].current = v[evt].normalised * data->relative_axis[axis].max;

						if(data->relative_axis[axis].inverted){
							value *= -1;
						}
						break;
					}
				}
				if(axis == data->relative_axes){
					value = (v[evt].normalised < 0.5) ? -1 : ((v[evt].normalised > 0.5) ? 1 : 0);
				}
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
			LOGPF("Failed to output event on instance %s", inst->name);
			return 1;
		}
	}

	//send syn event to publish all events
	if(libevdev_uinput_write_event(data->output_ev, EV_SYN, SYN_REPORT, 0)){
		LOGPF("Failed to output sync event on instance %s", inst->name);
		return 1;
	}

	return 0;
#else
	LOG("No output support on this platform");
	return 1;
#endif
}

static int evdev_shutdown(size_t n, instance** inst){
	evdev_instance_data* data = NULL;
	size_t u;

	for(u = 0; u < n; u++){
		data = (evdev_instance_data*) inst[u]->impl;

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
		data->relative_axes = 0;
		free(data->relative_axis);
		free(inst[u]->impl);
	}

	LOG("Backend shut down");
	return 0;
}
