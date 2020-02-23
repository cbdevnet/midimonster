#define BACKEND_NAME "openpixelcontrol"

#include <string.h>

#include "libmmbackend.h"
#include "openpixelcontrol.h"

/*
 * TODO handle destination close/unregister/reopen
 */

MM_PLUGIN_API int init(){
	backend openpixel = {
		.name = BACKEND_NAME,
		.conf = openpixel_configure,
		.create = openpixel_instance,
		.conf_instance = openpixel_configure_instance,
		.channel = openpixel_channel,
		.handle = openpixel_set,
		.process = openpixel_handle,
		.start = openpixel_start,
		.shutdown = openpixel_shutdown
	};

	//register backend
	if(mm_backend_register(openpixel)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int openpixel_configure(char* option, char* value){
	//no global configuration
	LOG("No backend configuration possible");
	return 1;
}

static int openpixel_configure_instance(instance* inst, char* option, char* value){
	char* host = NULL, *port = NULL;
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;

	//FIXME this should store the destination/listen address and establish on _start
	if(!strcmp(option, "destination")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);
		if(!host || !port){
			LOGPF("Invalid destination address specified for instance %s", inst->name);
			return 1;
		}

		data->dest_fd = mmbackend_socket(host, port, SOCK_STREAM, 0, 0);
		if(data->dest_fd >= 0){
			return 0;
		}
		return 1;
	}
	if(!strcmp(option, "listen")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);
		if(!host || !port){
			LOGPF("Invalid listen address specified for instance %s", inst->name);
			return 1;
		}

		data->listen_fd = mmbackend_socket(host, port, SOCK_STREAM, 1, 0);
		if(data->listen_fd >= 0 && listen(data->listen_fd, SOMAXCONN)){
			return 0;
		}
		return 1;
	}
	else if(!strcmp(option, "mode")){
		if(!strcmp(value, "16bit")){
			data->mode = rgb16;
			return 0;
		}
		else if(!strcmp(value, "8bit")){
			data->mode = rgb8;
			return 0;
		}
		LOGPF("Unknown instance mode %s\n", value);
		return 1;
	}

	LOGPF("Unknown instance option %s for instance %s", option, inst->name);
	return 1;
}

static int openpixel_instance(instance* inst){
	openpixel_instance_data* data = calloc(1, sizeof(openpixel_instance_data));
	inst->impl = data;
	if(!inst->impl){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->dest_fd = -1;
	data->listen_fd = -1;
	return 0;
}

static ssize_t openpixel_buffer_find(openpixel_instance_data* data, uint8_t strip, uint8_t input){
	ssize_t n = 0;

	for(n = 0; n < data->buffers; n++){
		if(data->buffer[n].strip == strip
				&& (data->buffer[n].flags & OPENPIXEL_INPUT) >= input){
			return n;
		}
	}
	return -1;
}

static int openpixel_buffer_extend(openpixel_instance_data* data, uint8_t strip, uint8_t input, uint8_t length){
	ssize_t buffer = openpixel_buffer_find(data, strip, input);
	length = (length % 3) ? ((length / 3) + 1) * 3 : length;
	size_t bytes_required = (data->mode == rgb8) ? length : length * 2;
	if(buffer < 0){
		//allocate new buffer
		data->buffer = realloc(data->buffer, (data->buffers + 1) * sizeof(openpixel_buffer));
		if(!data->buffer){
			data->buffers = 0;
			LOG("Failed to allocate memory");
			return -1;
		}

		buffer = data->buffers;
		data->buffers++;

		data->buffer[buffer].strip = strip;
		data->buffer[buffer].flags = input ? OPENPIXEL_INPUT : 0;
		data->buffer[buffer].bytes = 0;
		data->buffer[buffer].data.u8 = NULL;
	}

	if(data->buffer[buffer].bytes < bytes_required){
		//resize buffer
		data->buffer[buffer].data.u8 = realloc(data->buffer[buffer].data.u8, bytes_required);
		if(!data->buffer[buffer].data.u8){
			data->buffer[buffer].bytes = 0;
			LOG("Failed to allocate memory");
			return 1;
		}
		//FIXME might want to memset() only newly allocated channels
		memset(data->buffer[buffer].data.u8, 0, bytes_required);
		data->buffer[buffer].bytes = bytes_required;
	}
	return 0;
}

static channel* openpixel_channel(instance* inst, char* spec, uint8_t flags){
	uint32_t strip = 0, channel = 0;
	char* token = spec;
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;

	//read strip index if supplied
	if(!strncmp(spec, "strip", 5)){
		strip = strtoul(spec + 5, &token, 10);
		//skip the dot
		token++;
	}

	//read (and calculate) channel index
	if(!strncmp(token, "channel", 7)){
		channel = strtoul(token + 7, NULL, 10);
	}
	else if(!strncmp(token, "red", 3)){
		channel = strtoul(token + 3, NULL, 10) * 3 - 2;
	}
	else if(!strncmp(token, "green", 5)){
		channel = strtoul(token + 5, NULL, 10) * 3 - 1;
	}
	else if(!strncmp(token, "blue", 4)){
		channel = strtoul(token + 4, NULL, 10) * 3;
	}

	if(!channel){
		LOGPF("Invalid channel specification %s", spec);
		return NULL;
	}

	//check channel direction
	if(flags & mmchannel_input){
		//strip 0 (bcast) can not be mapped as input
		if(!strip){
			LOGPF("Broadcast channel %s.%s can not be mapped as an input", inst->name, spec);
			return NULL;
		}
		if(data->listen_fd < 0){
			LOGPF("Channel %s mapped as input, but instance %s is not accepting input", spec, inst->name);
			return NULL;
		}

		if(openpixel_buffer_extend(data, strip, 1, channel)){
			return NULL;
		}
	}

	if(flags & mmchannel_output){
		if(data->dest_fd < 0){
			LOGPF("Channel %s mapped as output, but instance %s is not sending output", spec, inst->name);
			return NULL;
		}

		if(openpixel_buffer_extend(data, strip, 0, channel)){
			return NULL;
		}
	}

	return mm_channel(inst, ((uint64_t) strip) << 32 | channel, 1);
}

static int openpixel_set(instance* inst, size_t num, channel** c, channel_value* v){
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	size_t u, p;
	ssize_t buffer;
	uint32_t strip, channel;
	openpixel_header hdr;

	for(u = 0; u < num; u++){
		//read strip/channel
		strip = c[u]->ident >> 32;
		channel = c[u]->ident & 0xFFFFFFFF;
		channel--;

		//find the buffer
		buffer = openpixel_buffer_find(data, strip, 0);
		if(buffer < 0){
			LOGPF("No buffer for channel %s.%d.%d\n", inst->name, strip, channel);
			continue;
		}

		//mark buffer for output
		data->buffer[buffer].flags |= OPENPIXEL_MARK;

		//update data
		switch(data->mode){
			case rgb8:
				data->buffer[buffer].data.u8[channel] = ((uint8_t)(v[u].normalised * 255.0));
				break;
			case rgb16:
				data->buffer[buffer].data.u16[channel] = ((uint16_t)(v[u].normalised * 65535.0));
				break;
		}

		if(strip == 0){
			//update values in all other output strips, dont mark
			for(p = 0; p < data->buffers; p++){
				if(!(data->buffer[p].flags & OPENPIXEL_INPUT)){
					//check whether the buffer is large enough
					if(data->mode == rgb8 && data->buffer[p].bytes >= channel){
						data->buffer[p].data.u8[channel] = ((uint8_t)(v[u].normalised * 255.0));
					}
					else if(data->mode == rgb16 && data->buffer[p].bytes >= channel * 2){
						data->buffer[p].data.u16[channel] = ((uint16_t)(v[u].normalised * 65535.0));
					}
				}
			}
		}
	}

	//send updated strips
	for(u = 0; u < data->buffers; u++){
		if(!(data->buffer[u].flags & OPENPIXEL_INPUT) && (data->buffer[u].flags & OPENPIXEL_MARK)){
			//remove mark
			data->buffer[u].flags &= ~OPENPIXEL_MARK;

			//prepare header
			hdr.strip = data->buffer[u].strip;
			hdr.mode = data->mode;
			hdr.length = htobe16(data->buffer[u].bytes);

			//output data
			if(mmbackend_send(data->dest_fd, (uint8_t*) &hdr, sizeof(hdr))
					|| mmbackend_send(data->dest_fd, data->buffer[u].data.u8, data->buffer[u].bytes)){
				return 1;
			}
		}
	}
	return 0;
}

static int openpixel_handle(size_t num, managed_fd* fds){
	//TODO handle bcast
	return 0;
}

static int openpixel_start(size_t n, instance** inst){
	int rv = -1;
	size_t u, nfds = 0;
	openpixel_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (openpixel_instance_data*) inst[u]->impl;

		//register fds
		if(data->dest_fd >= 0){
			if(mm_manage_fd(data->dest_fd, BACKEND_NAME, 1, inst[u])){
				LOGPF("Failed to register destination descriptor for instance %s with core", inst[u]->name);
				goto bail;
			}
			nfds++;
		}
		if(data->listen_fd >= 0){
			if(mm_manage_fd(data->listen_fd, BACKEND_NAME, 1, inst[u])){
				LOGPF("Failed to register host descriptor for instance %s with core", inst[u]->name);
				goto bail;
			}
			nfds++;
		}
	}

	LOGPF("Registered %" PRIsize_t " descriptors to core", nfds);
	rv = 0;
bail:
	return rv;
}

static int openpixel_shutdown(size_t n, instance** inst){
	size_t u, p;
	openpixel_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (openpixel_instance_data*) inst[u]->impl;

		//shutdown all clients
		for(p = 0; p < data->clients; p++){
			if(data->client_fd[p] >= 0){
				close(data->client_fd[p]);
			}
		}
		free(data->client_fd);
		free(data->bytes_left);

		//close all configured fds
		if(data->listen_fd >= 0){
			close(data->listen_fd);
		}
		if(data->dest_fd >= 0){
			close(data->dest_fd);
		}

		//free all buffers
		for(p = 0; p < data->buffers; p++){
			free(data->buffer[p].data.u8);
		}
		free(data->buffer);

		free(data);
		inst[u]->impl = NULL;
	}

	LOG("Backend shut down");
	return 0;
}
