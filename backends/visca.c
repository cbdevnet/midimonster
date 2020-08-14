#define BACKEND_NAME "visca"
#define DEBUG

#include <string.h>
#include "visca.h"
#include "libmmbackend.h"

/* TODO
 *	VISCA server
 */

MM_PLUGIN_API int init(){
	backend ptz = {
		.name = BACKEND_NAME,
		.conf = ptz_configure,
		.create = ptz_instance,
		.conf_instance = ptz_configure_instance,
		.channel = ptz_channel,
		.handle = ptz_set,
		.process = ptz_handle,
		.start = ptz_start,
		.shutdown = ptz_shutdown
	};

	//register backend
	if(mm_backend_register(ptz)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int ptz_configure(char* option, char* value){
	LOG("No backend configuration possible");
	return 1;
}

static int ptz_configure_instance(instance* inst, char* option, char* value){
	char* host = NULL, *port = NULL, *options = NULL;
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	uint8_t mode = 0;

	if(!strcmp(option, "id")){
		data->cam_address = strtoul(value, NULL, 10);
		return 0;
	}
	if(!strcmp(option, "connect")){
		if(data->fd >= 0){
			LOGPF("Instance %s already connected", inst->name);
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port, &options);
		if(!host || !port){
			LOGPF("Invalid destination address specified for instance %s", inst->name);
			return 1;
		}

		if(options && !strcmp(options, "udp")){
			mode = 1;
		}

		data->fd = mmbackend_socket(host, port, mode ? SOCK_DGRAM : SOCK_STREAM, 0, 0, 1);
		if(data->fd < 0){
			LOGPF("Failed to connect instance %s", inst->name);
			return 1;
		}
		return 0;
	}

	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static int ptz_instance(instance* inst){
	ptz_instance_data* data = calloc(1, sizeof(ptz_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->fd = -1;
	data->cam_address = 1;
	//start with maximum speeds
	data->panspeed = ptz_channels[panspeed].max;
	data->tiltspeed = ptz_channels[tiltspeed].max;

	inst->impl = data;
	return 0;
}

static channel* ptz_channel(instance* inst, char* spec, uint8_t flags){
	uint64_t ident = pan;
	size_t command = 0;

	if(flags & mmchannel_input){
		LOG("This backend currently only supports output channels");
		return NULL;
	}

	for(command = 0; command < sentinel; command++){
		if(!strncmp(spec, ptz_channels[command].name, strlen(ptz_channels[command].name))){
			ident = command;
		}
	}

	if(ident == sentinel){
		LOGPF("Unknown channel spec %s", spec);
		return NULL;
	}

	if(ident == call){
		ident |= (strtoul(spec + strlen(ptz_channels[call].name), NULL, 10) << 8);
	}

	return mm_channel(inst, ident, 1);
}

static size_t ptz_set_pantilt(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	uint32_t* x = (uint32_t*) msg + 6;
	uint32_t* y = (uint32_t*) msg + 10;
	
	if(c->ident == pan){
		data->x = ((ptz_channels[pan].max - ptz_channels[pan].min) * v->normalised) + ptz_channels[pan].min;
	}
	else{
		data->y = ((ptz_channels[tilt].max - ptz_channels[tilt].min) * v->normalised) + ptz_channels[tilt].min;
	}

	msg[4] = data->panspeed;
	msg[5] = data->tiltspeed;
	*x = htobe32(data->x);
	*y = htobe32(data->y);

	return ptz_channels[pan].bytes;
}

static size_t ptz_set_ptspeed(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	if(c->ident == panspeed){
		data->panspeed = ((ptz_channels[panspeed].max - ptz_channels[panspeed].min) * v->normalised) + ptz_channels[panspeed].min;
	}
	else{
		data->tiltspeed = ((ptz_channels[tiltspeed].max - ptz_channels[tiltspeed].min) * v->normalised) + ptz_channels[tiltspeed].min;
	}
	return 0;
}

static size_t ptz_set_zoom(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	uint32_t* position = (uint32_t*) msg + 4;
	*position = htobe32(((ptz_channels[zoom].max - ptz_channels[zoom].min) * v->normalised) + ptz_channels[zoom].min);
	return ptz_channels[zoom].bytes;
}

static size_t ptz_set_focus(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	uint32_t* position = (uint32_t*) msg + 4;
	*position = htobe32(((ptz_channels[focus].max - ptz_channels[focus].min) * v->normalised) + ptz_channels[focus].min);
	return ptz_channels[focus].bytes;
}

static size_t ptz_set_memory(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	if(v->normalised < 0.9){
		return 0;
	}

	msg[5] = (c->ident >> 8);
	return ptz_channels[call].bytes;
}

static int ptz_set(instance* inst, size_t num, channel** c, channel_value* v){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	size_t n = 0, bytes = 0;
	uint8_t tx[VISCA_BUFFER_LENGTH] = "";
	uint8_t command = 0;

	for(n = 0; n < num; n++){
		bytes = 0;
		command = c[n]->ident & 0xFF;

		if(ptz_channels[command].bytes){
			memcpy(tx, ptz_channels[command].pattern, ptz_channels[command].bytes);
		}
		tx[0] = 0x80 | (data->cam_address & 0xF);

		if(ptz_channels[command].set){
			bytes = ptz_channels[command].set(inst, c[n], v + n, tx);
		}

		if(bytes && mmbackend_send(data->fd, tx, bytes)){
			LOGPF("Failed to push %s command on instance %s", ptz_channels[command].name, inst->name);
		}
	}
	return 0;
}

static int ptz_handle(size_t num, managed_fd* fds){
	//no events generated here
	return 0;
}

static int ptz_start(size_t n, instance** inst){
	//no startup needed yet
	return 0;
}

static int ptz_shutdown(size_t n, instance** inst){
	size_t u;
	ptz_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (ptz_instance_data*) inst[u]->impl;
		if(data->fd >= 0){
			close(data->fd);
		}
		free(data);
		inst[u]->impl = NULL;
	}

	LOG("Backend shut down");
	return 0;
}
