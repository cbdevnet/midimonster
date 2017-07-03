#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include "osc.h"

#define osc_align(a) ((((a) / 4) + (((a) % 4) ? 1 : 0)) * 4)
#define BACKEND_NAME "osc"

int init(){
	backend osc = {
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

	//register backend
	if(mm_backend_register(osc)){
		fprintf(stderr, "Failed to register OSC backend\n");
		return 1;
	}
	return 0;
}

static size_t osc_data_length(osc_parameter_type t){
	switch(t){
		case int32:
		case float32:
			return 4;
		case int64:
		case double64:
			return 8;
		default:
			fprintf(stderr, "Invalid OSC format specified %c\n", t);
			return 0;
	}
}

static inline void osc_defaults(osc_parameter_type t, osc_parameter_value* max, osc_parameter_value* min){
	memset(max, 0, sizeof(osc_parameter_value));
	memset(min, 0, sizeof(osc_parameter_value));
	switch(t){
		case int32:
			max->i32 = 255;
			return;
		case float32:
			max->f = 1.0;
			return;
		case int64:
			max->i64 = 1024;
			return;
		case double64:
			max->d = 1.0;
			return;
		default:
			fprintf(stderr, "Invalid OSC type, not setting any sane defaults\n");
			return;
	}
}

static inline osc_parameter_value osc_parse(osc_parameter_type t, uint8_t* data){
	osc_parameter_value v = {0};
	switch(t){
		case int32:
		case float32:
			v.i32 = be32toh(*((int32_t*) data));
			break;
		case int64:
		case double64:
			v.i64 = be64toh(*((int64_t*) data));
			break;
		default:
			fprintf(stderr, "Invalid OSC type passed to parsing routine\n");
	}
	return v;
}

static inline channel_value osc_parameter_interpolate(osc_parameter_type t, osc_parameter_value min, osc_parameter_value max, osc_parameter_value cur){
	channel_value v = {
		.raw = {0},
		.normalised = 0
	};
	
	union {
		uint32_t u32;
		float f32;
		uint64_t u64;
		double d64;
	} range;

	switch(t){
		case int32:
			range.u32 = max.i32 - min.i32;
			v.raw.u64 = cur.i32 - min.i32;
			v.normalised = v.raw.u64 / range.u32;
			break;
		case float32:
			range.f32 = max.f - min.f;
			v.raw.dbl = cur.f - min.f;
			v.normalised = v.raw.dbl / range.f32;
			break;
		case int64:
			range.u64 = max.i64 - min.i64;
			v.raw.u64 = cur.i64 - min.i64;
			v.normalised = v.raw.u64 / range.u64;
			break;
		case double64:
			range.d64 = max.d - min.d;
			v.raw.dbl = cur.d - min.d;
			v.normalised = v.raw.dbl / range.d64;
			break;
		default:
			fprintf(stderr, "Invalid OSC type passed to interpolation routine\n");
	}

	//fix overshoot
	if(v.normalised > 1.0){
		v.normalised = 1.0;
	}
	else if(v.normalised < 0.0){
		v.normalised = 0.0;
	}

	return v;
}

static int osc_generate_event(channel* c, osc_channel* info, char* fmt, uint8_t* data, size_t data_len){
	size_t p, off = 0;
	if(!c || !info){
		return 0;
	}

	osc_parameter_value min, max, cur;
	channel_value evt;

	if(!fmt || !data || data_len % 4 || !*fmt){
		fprintf(stderr, "Invalid OSC packet, data length %zu\n", data_len);
		return 1;
	}

	//find offset for this parameter
	for(p = 0; p < info->param_index; p++){
		off += osc_data_length(fmt[p]);
	}

	if(info->type != not_set){
		max = info->max;
		min = info->min;
	}
	else{
		osc_defaults(fmt[info->param_index], &max, &min);
	}

	cur = osc_parse(fmt[info->param_index], data + off);
	evt = osc_parameter_interpolate(fmt[info->param_index], min, max, cur);

	return mm_channel_event(c, evt);
}

static int osc_validate_path(char* path){
	if(path[0] != '/'){
		fprintf(stderr, "%s is not a valid OSC path: Missing root /\n", path);
		return 1;
	}
	return 0;
}

static int osc_separate_hostspec(char* in, char** host, char** port){
	size_t u;

	if(!in || !host || !port){
		return 1;
	}

	for(u = 0; in[u] && !isspace(in[u]); u++){
	}

	//guess
	*host = in;

	if(in[u]){
		in[u] = 0;
		*port = in + u + 1;
	}
	else{
		//no port given
		*port = NULL;
	}
	return 0;
}

static int osc_listener(char* host, char* port){
	int fd = -1, status, yes = 1, flags;
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

		yes = 0;
		if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to unset IP_MULTICAST_LOOP option: %s\n", strerror(errno));
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

	//set nonblocking
	flags = fcntl(fd, F_GETFL, 0);
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){
		fprintf(stderr, "Failed to set OSC descriptor nonblocking\n");
		return -1;
	}

	return fd;
}

static int osc_parse_addr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len){
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

static int backend_configure(char* option, char* value){
	fprintf(stderr, "The OSC backend does not take any global configuration\n");
	return 1;
}

static int backend_configure_instance(instance* inst, char* option, char* value){
	osc_instance* data = (osc_instance*) inst->impl;
	char* host = NULL, *port = NULL;

	if(!strcmp(option, "root")){
		if(osc_validate_path(value)){
			return 1;
		}

		if(data->root){
			free(data->root);
		}
		data->root = strdup(value);

		if(!data->root){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "bind")){
		if(osc_separate_hostspec(value, &host, &port)){
			fprintf(stderr, "Invalid bind address for instance %s\n", inst->name);
			return 1;
		}

		data->fd = osc_listener(host, port);
		if(data->fd < 0){
			fprintf(stderr, "Failed to bind for instance %s\n", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "dest")){
		if(osc_separate_hostspec(value, &host, &port)){
			fprintf(stderr, "Invalid destination address for instance %s\n", inst->name);
			return 1;
		}

		if(osc_parse_addr(host, port, &data->dest, &data->dest_len)){
			fprintf(stderr, "Failed to parse destination address for instance %s\n", inst->name);
			return 1;
		}
		return 0;
	}

	//TODO channel configuration
	//create path.INDEX channels
	fprintf(stderr, "Unknown configuration parameter %s for OSC backend\n", option);
	return 1;
}

static instance* backend_instance(){
	instance* i = mm_instance();
	osc_instance* data = calloc(1, sizeof(osc_instance));
	data->fd = -1;

	if(!i || !data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	i->impl = data;
	return i;
}

static channel* backend_channel(instance* inst, char* spec){
	size_t u;
	osc_instance* data = (osc_instance*) inst->impl;
	size_t param_index = 0;

	//check spec for correctness
	if(osc_validate_path(spec)){
		return NULL;
	}

	//parse parameter offset
	if(strrchr(spec, ':')){
		param_index = strtoul(strrchr(spec, ':') + 1, NULL, 10);
		*(strrchr(spec, ':')) = 0;
	}

	//find matching channel
	for(u = 0; u < data->channels; u++){
		if(!strcmp(spec, data->channel[u].path) && data->channel[u].param_index == param_index){
			break;
		}
	}

	//allocate new channel
	if(u == data->channels){
		data->channel = realloc(data->channel, (u + 1) * sizeof(osc_channel));
		if(!data->channel){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

		memset(data->channel + u, 0, sizeof(osc_channel));
		data->channel[u].param_index = param_index;
		data->channel[u].path = strdup(spec);

		if(!data->channel[u].path){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}
		data->channels++;
	}

	return mm_channel(inst, u, 1);
}

static int backend_set(instance* inst, size_t num, channel** c, channel_value* v){
	osc_instance* data = (osc_instance*) inst->impl;
	if(!data->dest_len){
		fprintf(stderr, "OSC instance %s does not have a destination, output is disabled\n", inst->name);
		return 0;
	}

	//TODO aggregate dot-channels
	return 0;
}

static int backend_handle(size_t num, managed_fd* fds){
	size_t fd;
	char recv_buf[OSC_RECV_BUF];
	instance* inst = NULL;
	osc_instance* data = NULL;
	ssize_t bytes_read = 0;
	size_t c;
	char* osc_fmt = NULL;
	char* osc_local = NULL;
	uint8_t* osc_data = NULL;

	for(fd = 0; fd < num; fd++){
		inst = (instance*) fds[fd].impl;
		if(!inst){
			fprintf(stderr, "OSC backend signaled for unknown fd\n");
			continue;
		}

		data = (osc_instance*) inst->impl;

		do{
			bytes_read = recv(fds[fd].fd, recv_buf, sizeof(recv_buf), 0);
			if(data->root && strncmp(recv_buf, data->root, strlen(data->root))){
				//ignore packet for different root
				continue;
			}
			osc_local = recv_buf + (data->root ? strlen(data->root) : 0);

			if(bytes_read < 0){
				break;
			}

			osc_fmt = recv_buf + osc_align(strlen(recv_buf) + 1);
			if(*osc_fmt != ','){
				//invalid format string
				fprintf(stderr, "Invalid OSC format string in packet\n");
				continue;
			}
			osc_fmt++;

			osc_data = (uint8_t*) osc_fmt + (osc_align(strlen(osc_fmt) + 2) - 1);
			//FIXME check supplied data length

			for(c = 0; c < data->channels; c++){
				//FIXME implement proper OSC path match
				//prefix match
				if(!strncmp(osc_local, data->channel[c].path, strlen(data->channel[c].path)) && strlen(data->channel[c].path) == strlen(osc_local)){
					if(strlen(osc_fmt) > data->channel[c].param_index){
						//fprintf(stderr, "Taking parameter %zu of %s (%s), %zd bytes, data offset %zu\n", data->channel[c].param_index, recv_buf, osc_fmt, bytes_read, (osc_data - (uint8_t*)recv_buf));
						if(osc_generate_event(mm_channel(inst, c, 0), data->channel + c, osc_fmt, osc_data, bytes_read - (osc_data - (uint8_t*) recv_buf))){
							fprintf(stderr, "Failed to generate OSC channel event\n");
						}
					}
				}
			}
		} while(bytes_read > 0);

		if(bytes_read < 0 && errno != EAGAIN){
			fprintf(stderr, "OSC failed to receive data for instance %s: %s\n", inst->name, strerror(errno));
		}

		if(bytes_read == 0){
			fprintf(stderr, "OSC descriptor for instance %s closed\n", inst->name);
			return 1;
		}
	}

	return 0;
}

static int backend_start(){
	size_t n, u, fds = 0;
	instance** inst = NULL;
	osc_instance* data = NULL;

	//fetch all instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if(!n){
		free(inst);
		return 0;
	}

	//update instance identifiers
	for(u = 0; u < n; u++){
		data = (osc_instance*) inst[u]->impl;

		if(data->fd >= 0){
			inst[u]->ident = data->fd;
			if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u])){
				fprintf(stderr, "Failed to register OSC descriptor for instance %s\n", inst[u]->name);
				free(inst);
				return 1;
			}
			fds++;
		}
		else{
			inst[u]->ident = -1;
		}
	}

	fprintf(stderr, "OSC backend registering %zu descriptors to core\n", fds);

	free(inst);
	return 0;
}

static int backend_shutdown(){
	size_t n, u, c;
	instance** inst = NULL;
	osc_instance* data = NULL;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (osc_instance*) inst[u]->impl;
		for(c = 0; c < data->channels; c++){
			free(data->channel[c].path);
			free(data->channel[c].param);
		}
		free(data->channel);
		free(data->root);
		close(data->fd);
		data->fd = -1;
		data->channels = 0;
		free(inst[u]->impl);
	}

	free(inst);
	return 0;
}
