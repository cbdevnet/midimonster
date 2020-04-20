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
		LOGPF("Failed to connect to server for instance %s", inst->name);
		return 1;
	}
	if(!strcmp(option, "listen")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);
		if(!host || !port){
			LOGPF("Invalid listen address specified for instance %s", inst->name);
			return 1;
		}

		data->listen_fd = mmbackend_socket(host, port, SOCK_STREAM, 1, 0);
		if(data->listen_fd >= 0 && !listen(data->listen_fd, SOMAXCONN)){
			return 0;
		}
		LOGPF("Failed to bind server descriptor for instance %s", inst->name);
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
			DBGPF("Using allocated %s buffer for requested strip %d, size %d", input ? "input" : "output", strip, data->buffer[n].bytes);
			return n;
		}
	}
	DBGPF("Instance has no %s buffer for requested strip %d", input ? "input" : "output", strip);
	return -1;
}

static int openpixel_buffer_extend(openpixel_instance_data* data, uint8_t strip, uint8_t input, uint16_t length){
	ssize_t buffer = openpixel_buffer_find(data, strip, input);

	//length is in component-channels, round it to the nearest rgb-triplet
	//this guarantees that any allocated buffer has at least three bytes, which is important to parts of the receive handler
	length = (length % 3) ? ((length / 3) + 1) * 3 : length;

	//calculate required buffer length
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

static int openpixel_output_data(openpixel_instance_data* data){
	size_t u;
	openpixel_header hdr;

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

static int openpixel_set(instance* inst, size_t num, channel** c, channel_value* v){
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	size_t u, p;
	ssize_t buffer;
	uint32_t strip, channel;

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
			//update values in all other output strips, don't mark
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

	return openpixel_output_data(data);
}

static int openpixel_client_new(instance* inst, int fd){
	if(fd < 0){
		return 1;
	}
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	size_t u;

	//mark nonblocking
	#ifdef _WIN32
	unsigned long flags = 1;
	if(ioctlsocket(fd, FIONBIO, &flags)){
	#else
	int flags = fcntl(fd, F_GETFL, 0);
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){
	#endif
		LOGPF("Failed to set client descriptor on %s nonblocking", inst->name);
		close(fd);
		return 0;
	}

	//find a client block
	for(u = 0; u < data->clients; u++){
		if(data->client[u].fd <= 0){
			break;
		}
	}

	//if no free slot, make one
	if(u == data->clients){
		data->client = realloc(data->client, (data->clients + 1) * sizeof(openpixel_client));
		if(!data->client){
			data->clients = 0;
			LOG("Failed to allocate memory");
			return 1;
		}
		data->clients++;
	}

	data->client[u].fd = fd;
	data->client[u].buffer = -1;
	data->client[u].offset = 0;

	LOGPF("New client on instance %s", inst->name);
	return mm_manage_fd(fd, BACKEND_NAME, 1, inst);
}

static size_t openpixel_strip_pixeldata8(instance* inst, openpixel_client* client, uint8_t* data, openpixel_buffer* buffer, size_t bytes_left){
	channel* chan = NULL;
	channel_value val;
	size_t u;

	for(u = 0; u < bytes_left; u++){
		//if over buffer length, ignore
		if(u + client->offset >= buffer->bytes){
			client->buffer = -2;
			break;
		}

		//FIXME if at start of trailing non-multiple of 3, ignore

		//update changed channels
		if(buffer->data.u8[u + client->offset] != data[u]){
			buffer->data.u8[u + client->offset] = data[u];
			chan = mm_channel(inst, ((uint64_t) buffer->strip << 32) | (u + client->offset + 1), 0);
			if(chan){
				//push event
				val.raw.u64 = data[u];
				val.normalised = (double) data[u] / 255.0;
				if(mm_channel_event(chan, val)){
					LOG("Failed to push channel event to core");
				}
			}
		}
	}
	return u;
}

static size_t openpixel_strip_pixeldata16(instance* inst, openpixel_client* client, uint8_t* data, openpixel_buffer* buffer, size_t bytes_left){
	channel* chan = NULL;
	channel_value val;
	size_t u;

	for(u = 0; u < bytes_left; u++){
		//if over buffer length, ignore
		if(u + client->offset >= buffer->bytes){
			client->buffer = -2;
			break;
		}

		//if at start of trailing non-multiple of 6, ignore
		if((client->offset + u) >= (client->offset + client->left) - ((client->offset + client->left) % 6)){
			client->buffer = -2;
			break;
		}

		//byte-order conversion may be on message boundary, do it via a buffer
		client->boundary.u8[(client->offset + u) % 2] = data[u];

		//detect and update changed channels
		if((client->offset + u) % 2
				&& buffer->data.u16[(u + client->offset) / 2] != be16toh(client->boundary.u16)){
			buffer->data.u16[(u + client->offset) / 2] = be16toh(client->boundary.u16);
			chan = mm_channel(inst, ((uint64_t) buffer->strip << 32) | ((u + client->offset) / 2 + 1), 0);
			if(chan){
				//push event
				val.raw.u64 = be16toh(client->boundary.u16);;
				val.normalised = (double) val.raw.u64 / 65535.0;
				if(mm_channel_event(chan, val)){
					LOG("Failed to push channel event to core");
				}
			}

		}
	}
	return u;
}

static ssize_t openpixel_client_pixeldata(instance* inst, openpixel_client* client, uint8_t* buffer, size_t bytes_left){
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	openpixel_client temp_client = {
		.fd = -1
	};
	ssize_t u, p;
	uint8_t processing_done = 1;

	//ignore data
	if(client->buffer == -2){
		//ignore data
		client->offset += bytes_left;
		client->left -= bytes_left;
		return bytes_left;
	}
	//handle broadcast data
	else if(client->buffer == -3){
		//iterate all input strips
		for(p = 0; p < data->buffers; p++){
			if(data->buffer[p].flags & OPENPIXEL_INPUT){
				//prepare temporary client
				temp_client.buffer = p;
				temp_client.hdr = client->hdr;
				temp_client.hdr.strip = data->buffer[p].strip;
				temp_client.offset = client->offset;
				temp_client.left = client->left;

				//run processing on strip
				if(data->mode == rgb8){
					openpixel_strip_pixeldata8(inst, &temp_client, buffer, data->buffer + p, bytes_left);
				}
				else{
					openpixel_strip_pixeldata16(inst, &temp_client, buffer, data->buffer + p, bytes_left);
				}
				if(temp_client.buffer != -2){
					processing_done = 0;
				}
			}
		}

		//if all strips report being done, ignore the rest of the data
		if(processing_done){
			client->buffer = -2;
		}

		//remove data
		u = min(client->left, bytes_left);
		client->offset += u;
		client->left -= u;
		return u;
	}
	//process data
	else{
		if(data->mode == rgb8){
			u = openpixel_strip_pixeldata8(inst, client, buffer, data->buffer + client->buffer, bytes_left);
		}
		else{
			u = openpixel_strip_pixeldata16(inst, client, buffer, data->buffer + client->buffer, bytes_left);
		}

		//update offsets
		client->offset += u;
		client->left -= u;
		return u;
	}
	return -1;
}

static ssize_t openpixel_client_headerdata(instance* inst, openpixel_client* client, uint8_t* buffer, size_t bytes_left){
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	size_t bytes_consumed = min(sizeof(openpixel_header) - client->offset, bytes_left);

	DBGPF("Reading %" PRIsize_t " bytes to header at offset %" PRIsize_t ", header size %" PRIsize_t ", %" PRIsize_t " bytes left", bytes_consumed, client->offset, sizeof(openpixel_header), bytes_left);
	memcpy(((uint8_t*) (&client->hdr)) + client->offset, buffer, bytes_consumed);

	//if done, resolve buffer
	if(sizeof(openpixel_header) - client->offset <= bytes_left){
		//if broadcast strip, mark broadcast
		if(client->hdr.strip == 0
				&& data->mode == client->hdr.mode){
			client->buffer = -3;
		}
		else{
			client->buffer = openpixel_buffer_find(data, client->hdr.strip, 1);
			//if no buffer or mode mismatch, ignore data
			if(client->buffer < 0
					|| data->mode != client->hdr.mode){
				client->buffer = -2; //mark for ignore
			}
		}
		client->left = be16toh(client->hdr.length);
		client->offset = 0;
	}
	//if not, update client offset
	else{
		client->offset += bytes_consumed;
	}

	//update scan offset
	return bytes_consumed;
}

static int openpixel_client_handle(instance* inst, int fd){
	openpixel_instance_data* data = (openpixel_instance_data*) inst->impl;
	uint8_t buffer[8192];
	size_t c = 0, offset = 0;
	ssize_t bytes_left = 0, bytes_handled;

	for(c = 0; c < data->clients; c++){
		if(data->client[c].fd == fd){
			break;
		}
	}

	if(c == data->clients){
		LOGPF("Unknown client descriptor signaled on %s", inst->name);
		return 1;
	}

	//FIXME might want to read until EAGAIN
	ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
	if(bytes <= 0){
		if(bytes < 0){
			LOGPF("Failed to receive from client: %s", mmbackend_socket_strerror(errno));
		}

		//close the connection
		close(fd);
		data->client[c].fd = -1;

		//unmanage the fd
		LOGPF("Client disconnected on %s", inst->name);
		mm_manage_fd(fd, BACKEND_NAME, 0, NULL);
		return 0;
	}
	DBGPF("Received %" PRIsize_t " bytes on %s", bytes, inst->name);

	for(bytes_left = bytes - offset; bytes_left > 0; bytes_left = bytes - offset){
		if(data->client[c].buffer == -1){
			//read a header
			bytes_handled = openpixel_client_headerdata(inst, data->client + c, buffer + offset, bytes_left);
			if(bytes_handled < 0){
				//FIXME handle errors
			}
		}
		else{
			//read data
			bytes_handled = openpixel_client_pixeldata(inst, data->client + c, buffer + offset, min(bytes_left, data->client[c].left));
			if(bytes_handled < 0){
				//FIXME handle errors
			}

			//end of data, return to reading headers
			if(data->client[c].left == 0){
				data->client[c].buffer = -1;
				data->client[c].offset = 0;
				data->client[c].left = 0;
			}
		}
		offset += bytes_handled;
	}
	DBGPF("Processing done on %s", inst->name);

	return 0;
}

static int openpixel_handle(size_t num, managed_fd* fds){
	size_t u;
	instance* inst = NULL;
	openpixel_instance_data* data = NULL;
	uint8_t buffer[8192];
	ssize_t bytes;

	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		data = (openpixel_instance_data*) inst->impl;

		if(fds[u].fd == data->dest_fd){
			//destination fd ready to read
			//since the protocol does not define any responses, the connection was probably closed
			bytes = recv(data->dest_fd, buffer, sizeof(buffer), 0);
			if(bytes <= 0){
				LOGPF("Output descriptor closed on instance %s", inst->name);
				//unmanage the fd to give the core some rest
				mm_manage_fd(data->dest_fd, BACKEND_NAME, 0, NULL);
			}
			else{
				LOGPF("Unhandled response data on %s (%" PRIsize_t" bytes)", inst->name, bytes);
			}
		}
		else if(fds[u].fd == data->listen_fd){
			//listen fd ready to read, accept a new client
			if(openpixel_client_new(inst, accept(data->listen_fd, NULL, NULL))){
				return 1;
			}
		}
		else{
			//handle client input
			if(openpixel_client_handle(inst, fds[u].fd)){
				return 1;
			}
		}
	}
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
			if(data->client[p].fd>= 0){
				close(data->client[p].fd);
			}
		}
		free(data->client);

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
