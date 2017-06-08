#include <string.h>
#include "artnet.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define BACKEND_NAME "artnet"
static uint8_t default_net = 0;
static struct {
	char* host;
	char* port;
} bind_info = {
	.host = NULL,
	.port = NULL
};
int artnet_fd = -1;

static int artnet_listener(char* host, char* port){
	int fd = -1, status, yes = 1;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE
	};
	struct addrinfo* info;
	struct addrinfo* addr_it;

	status = getaddrinfo(host, port, &hints, &info);
	if(status){
		fprintf(stderr, "Failed to get socket info for %s port %s: %s\n", host, port, gai_strerror(status));
		return -1;
	}

	for(addr_it = info; addr_it != NULL; addr_it = addr_it->ai_next){
		fd = socket(addr_it->ai_family, addr_it->ai_socktype, addr_it->ai_protocol);
		if(fd < 0){
			continue;
		}

		yes = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to set SO_REUSEADDR on socket\n");
		}

		yes = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to set SO_BROADCAST on socket\n");
		}

		status = bind(fd, addr_it->ai_addr, addr_it->ai_addrlen);
		if(status < 0){
			close(fd);
			continue;
		}

		break;
	}

	freeaddrinfo(info);

	if(!addr_it){
		fprintf(stderr, "Failed to create listening socket for %s port %s\n", host, port);
		return -1;
	}
	return fd;
}

static int artnet_parse_addr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len){
	struct addrinfo* head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM
	};
	
	int error = getaddrinfo(host, port, &hints, &head);
	if(error || !head){
		fprintf(stderr, "Failed to parse address %s port %s: %s\n", host, port, gai_strerror(error));
		return 1;
	}

	memcpy(addr, head->ai_addr, head->ai_addrlen);
	*len = head->ai_addrlen;

	freeaddrinfo(head);
	return 0;
}

int init(){
	backend artnet = {
		.name = BACKEND_NAME,
		.conf = artnet_configure,
		.create = artnet_instance,
		.conf_instance = artnet_configure_instance,
		.channel = artnet_channel,
		.handle = artnet_set,
		.process = artnet_handle,
		.start = artnet_start,
		.shutdown = artnet_shutdown
	};

	//register backend
	if(mm_backend_register(artnet)){
		fprintf(stderr, "Failed to register ArtNet backend\n");
		return 1;
	}
	return 0;
}

static int artnet_configure(char* option, char* value){
	char* separator = value;
	if(!strcmp(option, "bind")){
		for(; *separator && *separator != ' '; separator++){
		}

		if(*separator){
			*separator = 0;
			separator++;
			free(bind_info.port);
			bind_info.port = strdup(separator);
		}

		free(bind_info.host);
		bind_info.host = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "net")){
		//configure default net
		default_net = strtoul(value, NULL, 10);
		return 0;
	}
	fprintf(stderr, "Unknown ArtNet backend option %s\n", option);
	return 1;
}

static instance* artnet_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	inst->impl = calloc(1, sizeof(artnet_instance_data));
	if(!inst->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	artnet_instance_data* data = (artnet_instance_data*) inst->impl;
	data->net = default_net;

	return inst;
}

static int artnet_configure_instance(instance* instance, char* option, char* value){
	char* separator;
	artnet_instance_data* data = (artnet_instance_data*) instance->impl;

	if(!strcmp(option, "net")){
		data->net = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "uni")){
		data->uni = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "output")){
		if(!strcmp(value, "true")){
			data->mode |= output;
		}
		else{
			data->mode &= ~output;
		}
		return 0;
	}
	else if(!strcmp(option, "dest")){
		for(separator = value; *separator && *separator != ' '; separator++){
		}

		if(!*separator){
			fprintf(stderr, "No port supplied in destination address\n");
			return 1;
		}

		*separator = 0;
		separator++;

		return artnet_parse_addr(value, separator, &data->dest_addr, &data->dest_len); 
	}

	fprintf(stderr, "Unknown ArtNet instance option %s\n", option);
	return 1;
}

static channel* artnet_channel(instance* instance, char* spec){
	unsigned channel = strtoul(spec, NULL, 10);
	if(channel > 512 || channel < 1){
		fprintf(stderr, "Invalid ArtNet channel %s\n", spec);
		return NULL;
	}
	return mm_channel(instance, channel - 1, 1);
}

static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t u, mark = 0;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;
	
	if(!(data->mode & output)){
		fprintf(stderr, "ArtNet instance %s not enabled for output\n", inst->name);
		return 0;
	}

	//FIXME maybe introduce minimum frame interval
	for(u = 0; u < num; u++){
		if(data->data.out[c[u]->ident] != (v[u].normalised * 255.0)){
			mark = 1;

			data->data.out[c[u]->ident] = v[u].normalised * 255.0;
		}
	}

	if(mark){
		//output frame
		artnet_pkt frame = {
			.magic = {'A', 'r', 't', '-', 'N', 'e', 't', 0x00},
			.opcode = htobe16(OpDmx),
			.version = htobe16(ARTNET_VERSION),
			.sequence = data->data.seq++,
			.port = 0,
			.universe = data->uni,
			.net = data->net,
			.length = htobe16(512),
			.data = {}
		};
		memcpy(frame.data, data->data.out, 512);

		if(sendto(artnet_fd, &frame, sizeof(frame), 0, (struct sockaddr*) &data->dest_addr, data->dest_len) < 0){
			fprintf(stderr, "Failed to output ArtNet frame for instance %s: %s\n", inst->name, strerror(errno));
		}
	}

	return 0;
}

static int artnet_handle(size_t num, managed_fd* fds){
	size_t u, p;
	ssize_t bytes_read;
	char recv_buf[ARTNET_RECV_BUF];
	artnet_instance_id inst_id = {
		.label = 0
	};
	instance* inst = NULL;
	channel* chan = NULL;
	channel_value val;
	artnet_instance_data* data;
	artnet_pkt* frame = (artnet_pkt*) recv_buf;
	if(!num){
		//early exit
		return 0;
	}

	for(u = 0; u < num; u++){
		do{
			bytes_read = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
			if(bytes_read > sizeof(artnet_hdr)){
				if(!memcmp(frame->magic, "Art-Net\0", 8) && be16toh(frame->opcode) == OpDmx){
					//find matching instance
					inst_id.fields.net = frame->net;
					inst_id.fields.uni = frame->universe;
					inst = mm_instance_find(BACKEND_NAME, inst_id.label);
					if(inst){
						data = (artnet_instance_data*) inst->impl;

						//read data, notify of changes
						for(p = 0; p < be16toh(frame->length); p++){
							if(frame->data[p] != data->data.in[p]){
								data->data.in[p] = frame->data[p];
								chan = mm_channel(inst, p, 0);
								val.raw.u64 = frame->data[p];
								val.normalised = (double)frame->data[p] / 255.0;
								if(chan && mm_channel_event(chan, val)){
									fprintf(stderr, "Failed to push ArtNet channel event to core\n");
									return 1;
								}
							}
						}
					}
				}
			}
		} while(bytes_read > 0);

		if(bytes_read < 0 && errno != EAGAIN){
			fprintf(stderr, "ArtNet failed to receive data: %s\n", strerror(errno));
		}

		if(bytes_read == 0){
			fprintf(stderr, "ArtNet listener closed\n");
			return 1;
		}
	}

	return 0;
}

static int artnet_start(){
	size_t n, u, p;
	int rv = 1i, flags;
	instance** inst = NULL;
	artnet_instance_data* data_a, *data_b;
	artnet_instance_id id = {
		.label = 0
	};

	if(!bind_info.host){
		bind_info.host = strdup("127.0.0.1");
	}

	if(!bind_info.port){
		bind_info.port = strdup("6454");
	}

	if(!bind_info.host || !bind_info.port){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if(!n){
		return 0;
	}

	for(u = 0; u < n; u++){
		data_a = (artnet_instance_data*) inst[u]->impl;
		//set instance identifier
		id.fields.net = data_a->net;
		id.fields.uni = data_a->uni;
		inst[u]->ident = id.label;

		//set destination address if not provided
		if(!data_a->dest_len){
			struct sockaddr_in bcast4 = {
				.sin_addr.s_addr = htobe32(-1),
				.sin_port = htobe16(strtoul(ARTNET_PORT, NULL, 10)),
				.sin_family = PF_INET
			};

			memcpy(&(data_a->dest_addr), &bcast4, sizeof(bcast4));
			data_a->dest_len = sizeof(bcast4);
		}

		//check for duplicate instances
		for(p = u + 1; p < n; p++){
			data_b = (artnet_instance_data*) inst[p]->impl;
			//FIXME might want to include destination in duplicate check
			if(data_a->net == data_b->net
					&& data_a->uni == data_b->uni){
				fprintf(stderr, "Universe specified multiple times, use one instance: %s - %s\n", inst[u]->name, inst[p]->name);
				goto bail;
			}
		}
	}

	//open socket
	artnet_fd = artnet_listener(bind_info.host, bind_info.port);
	if(artnet_fd < 0){
		fprintf(stderr, "Failed to open ArtNet listener socket\n");
		goto bail;
	}
	fprintf(stderr, "Listening for ArtNet data on %s port %s\n", bind_info.host, bind_info.port);
	
	//set nonblocking
	flags = fcntl(artnet_fd, F_GETFL, 0);
	if(fcntl(artnet_fd, F_SETFL, flags | O_NONBLOCK) < 0){
		fprintf(stderr, "Failed to set ArtNet input nonblocking\n");
		goto bail;
	}

	fprintf(stderr, "ArtNet backend registering 1 descriptors to core\n");
	if(mm_manage_fd(artnet_fd, BACKEND_NAME, 1, NULL)){
		goto bail;
	}

	//TODO parse all universe destinations
	rv = 0;
bail:
	free(inst);
	return rv;
}

static int artnet_shutdown(){
	size_t n, p;
	instance** inst = NULL;
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(p = 0; p < n; p++){
		free(inst[p]->impl);
	}
	free(inst);

	free(bind_info.host);
	free(bind_info.port);
	fprintf(stderr, "ArtNet backend shut down\n");
	return 0;
}
