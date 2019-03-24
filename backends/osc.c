#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "libmmbackend.h"

#include "osc.h"

/*
 * TODO
 * ping method
 */

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

static inline int osc_deparse(osc_parameter_type t, osc_parameter_value v, uint8_t* data){
	uint64_t u64 = 0;
	uint32_t u32 = 0;
	switch(t){
		case int32:
		case float32:
			u32 = htobe32(v.i32);
			memcpy(data, &u32, sizeof(u32));
			break;
		case int64:
		case double64:
			u64 = htobe64(v.i64);
			memcpy(data, &u64, sizeof(u64));
			break;
		default:
			fprintf(stderr, "Invalid OSC type passed to parsing routine\n");
			return 1;
	}
	return 0;
}

static inline osc_parameter_value osc_parse_value_spec(osc_parameter_type t, char* value){
	osc_parameter_value v = {0};
	switch(t){
		case int32:
			v.i32 = strtol(value, NULL, 0);
			break;
		case float32:
			v.f = strtof(value, NULL);
			break;
		case int64:
			v.i64 = strtoll(value, NULL, 0);
			break;
		case double64:
			v.d = strtod(value, NULL);
			break;
		default:
			fprintf(stderr, "Invalid OSC type passed to value parser\n");
	}
	return v;
}

static inline channel_value osc_parameter_normalise(osc_parameter_type t, osc_parameter_value min, osc_parameter_value max, osc_parameter_value cur){
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

static inline osc_parameter_value osc_parameter_denormalise(osc_parameter_type t, osc_parameter_value min, osc_parameter_value max, channel_value cur){
	osc_parameter_value v = {0};

	union {
		uint32_t u32;
		float f32;
		uint64_t u64;
		double d64;
	} range;

	switch(t){
		case int32:
			range.u32 = max.i32 - min.i32;
			v.i32 = (range.u32 * cur.normalised) + min.i32;
			break;
		case float32:
			range.f32 = max.f - min.f;
			v.f = (range.f32 * cur.normalised) + min.f;
			break;
		case int64:
			range.u64 = max.i64 - min.i64;
			v.i64 = (range.u64 * cur.normalised) + min.i64;
			break;
		case double64:
			range.d64 = max.d - min.d;
			v.d = (range.d64 * cur.normalised) + min.d;
			break;
		default:
			fprintf(stderr, "Invalid OSC type passed to interpolation routine\n");
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
	evt = osc_parameter_normalise(fmt[info->param_index], min, max, cur);

	return mm_channel_event(c, evt);
}

static int osc_validate_path(char* path){
	if(path[0] != '/'){
		fprintf(stderr, "%s is not a valid OSC path: Missing root /\n", path);
		return 1;
	}
	return 0;
}

static int backend_configure(char* option, char* value){
	fprintf(stderr, "The OSC backend does not take any global configuration\n");
	return 1;
}

static int backend_configure_instance(instance* inst, char* option, char* value){
	osc_instance* data = (osc_instance*) inst->impl;
	char* host = NULL, *port = NULL, *token = NULL, *format = NULL;
	size_t u, p;

	if(!strcmp(option, "root")){
		if(osc_validate_path(value)){
			fprintf(stderr, "Not a valid OSC root: %s\n", value);
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
		mmbackend_parse_hostspec(value, &host, &port);
		if(!host || !port){
			fprintf(stderr, "Invalid bind address for instance %s\n", inst->name);
			return 1;
		}

		data->fd = mmbackend_socket(host, port, SOCK_DGRAM, 1);
		if(data->fd < 0){
			fprintf(stderr, "Failed to bind for instance %s\n", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "dest") || !strcmp(option, "destination")){
		if(!strncmp(value, "learn", 5)){
			data->learn = 1;

			//check if a forced port was provided
			if(value[5] == '@'){
				data->forced_rport = strtoul(value + 6, NULL, 0);
			}
			return 0;
		}

		mmbackend_parse_hostspec(value, &host, &port);
		if(!host || !port){
			fprintf(stderr, "Invalid destination address for instance %s\n", inst->name);
			return 1;
		}

		if(mmbackend_parse_sockaddr(host, port, &data->dest, &data->dest_len)){
			fprintf(stderr, "Failed to parse destination address for instance %s\n", inst->name);
			return 1;
		}
		return 0;
	}
	else if(*option == '/'){
		//pre-configure channel
		if(osc_validate_path(option)){
			fprintf(stderr, "Not a valid OSC path: %s\n", option);
			return 1;
		}

		for(u = 0; u < data->channels; u++){
			if(!strcmp(option, data->channel[u].path)){
				fprintf(stderr, "OSC channel %s already configured\n", option);
				return 1;
			}
		}

		//tokenize configuration
		format = strtok(value, " ");
		if(!format || strlen(format) < 1){
			fprintf(stderr, "Not a valid format for OSC path %s\n", option);
			return 1;
		}

		//check format validity, create subchannels
		for(p = 0; p < strlen(format); p++){
			if(!osc_data_length(format[p])){
				fprintf(stderr, "Invalid format specifier %c for path %s, ignoring\n", format[p], option);
				continue;
			}

			//register new sub-channel
			data->channel = realloc(data->channel, (data->channels + 1) * sizeof(osc_channel));
			if(!data->channel){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			memset(data->channel + data->channels, 0, sizeof(osc_channel));
			data->channel[data->channels].params = strlen(format);
			data->channel[data->channels].param_index = p;
			data->channel[data->channels].type = format[p];
			data->channel[data->channels].path = strdup(option);

			if(!data->channel[data->channels].path){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			//parse min/max values
			token = strtok(NULL, " ");
			if(!token){
				fprintf(stderr, "Missing minimum specification for parameter %zu of %s\n", p, option);
				return 1;
			}
			data->channel[data->channels].min = osc_parse_value_spec(format[p], token);

			token = strtok(NULL, " ");
			if(!token){
				fprintf(stderr, "Missing maximum specification for parameter %zu of %s\n", p, option);
				return 1;
			}
			data->channel[data->channels].max = osc_parse_value_spec(format[p], token);

			//allocate channel from core
			if(!mm_channel(inst, data->channels, 1)){
				fprintf(stderr, "Failed to register core channel\n");
				return 1;
			}

			//increase channel count
			data->channels++;
		}
		return 0;
	}

	fprintf(stderr, "Unknown configuration parameter %s for OSC backend\n", option);
	return 1;
}

static instance* backend_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	osc_instance* data = calloc(1, sizeof(osc_instance));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	data->fd = -1;
	inst->impl = data;
	return inst;
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
			//fprintf(stderr, "Reusing previously created channel %s parameter %zu\n", data->channel[u].path, data->channel[u].param_index);
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
	uint8_t xmit_buf[OSC_XMIT_BUF], *format = NULL;
	size_t evt = 0, off, members, p;
	if(!num){
		return 0;
	}

	osc_instance* data = (osc_instance*) inst->impl;
	if(!data->dest_len){
		fprintf(stderr, "OSC instance %s does not have a destination, output is disabled (%zu channels)\n", inst->name, num);
		return 0;
	}

	for(evt = 0; evt < num; evt++){
		off = c[evt]->ident;

		//sanity check
		if(off >= data->channels){
			fprintf(stderr, "OSC channel identifier out of range\n");
			return 1;
		}

		//if the format is unknown, don't output
		if(data->channel[off].type == not_set || data->channel[off].params == 0){
			fprintf(stderr, "OSC channel %s.%s requires format specification for output\n", inst->name, data->channel[off].path);
			continue;
		}

		//update current value
		data->channel[off].current = osc_parameter_denormalise(data->channel[off].type, data->channel[off].min, data->channel[off].max, v[evt]);
		//mark channel
		data->channel[off].mark = 1;
	}

	//fix destination rport if required
	if(data->forced_rport){
		//cheating a bit because both IPv4 and IPv6 have the port at the same offset
		struct sockaddr_in* sockadd = (struct sockaddr_in*) &(data->dest);
		sockadd->sin_port = htobe16(data->forced_rport);
	}

	//find all marked channels
	for(evt = 0; evt < data->channels; evt++){
		//zero output buffer
		memset(xmit_buf, 0, sizeof(xmit_buf));
		if(data->channel[evt].mark){
			//determine minimum packet size
			if(osc_align((data->root ? strlen(data->root) : 0) + strlen(data->channel[evt].path) + 1) + osc_align(data->channel[evt].params + 2)  >= sizeof(xmit_buf)){
				fprintf(stderr, "Insufficient buffer size for OSC transmitting channel %s.%s\n", inst->name, data->channel[evt].path);
				return 1;
			}

			off = 0;
			//copy osc target path
			if(data->root){
				memcpy(xmit_buf, data->root, strlen(data->root));
				off += strlen(data->root);
			}
			memcpy(xmit_buf + off, data->channel[evt].path, strlen(data->channel[evt].path));
			off += strlen(data->channel[evt].path) + 1;
			off = osc_align(off);

			//get format string offset, initialize
			format = xmit_buf + off;
			off += osc_align(data->channel[evt].params + 2);
			*format = ',';
			format++;

			//gather subchannels, unmark
			members = 0;
			for(p = 0; p < data->channels && members < data->channel[evt].params; p++){
				if(!strcmp(data->channel[evt].path, data->channel[p].path)){
					//unmark channel
					data->channel[p].mark = 0;

					//sanity check
					if(data->channel[p].param_index >= data->channel[evt].params){
						fprintf(stderr, "OSC channel %s.%s has multiple parameter offset definitions\n", inst->name, data->channel[evt].path);
						return 1;
					}

					//write format specifier
					format[data->channel[p].param_index] = data->channel[p].type;

					//write data
					//FIXME this currently depends on all channels being registered in the correct order, since it just appends data
					if(off + osc_data_length(data->channel[p].type) >= sizeof(xmit_buf)){
						fprintf(stderr, "Insufficient buffer size for OSC transmitting channel %s.%s at parameter %zu\n", inst->name, data->channel[evt].path, members);
						return 1;
					}

					osc_deparse(data->channel[p].type, data->channel[p].current, xmit_buf + off);
					off += osc_data_length(data->channel[p].type);
					members++;
				}
			}

			//output packet
			if(sendto(data->fd, xmit_buf, off, 0, (struct sockaddr*) &(data->dest), data->dest_len) < 0){
				fprintf(stderr, "Failed to transmit OSC packet: %s\n", strerror(errno));
			}
		}
	}
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
			if(data->learn){
				data->dest_len = sizeof(data->dest);
				bytes_read = recvfrom(fds[fd].fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr*) &(data->dest), &(data->dest_len));
			}
			else{
				bytes_read = recv(fds[fd].fd, recv_buf, sizeof(recv_buf), 0);
			}
			if(data->root && strncmp(recv_buf, data->root, min(bytes_read, strlen(data->root)))){
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
				if(!strcmp(osc_local, data->channel[c].path)){
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

	fprintf(stderr, "OSC backend registered %zu descriptors to core\n", fds);

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
		}
		free(data->channel);
		free(data->root);
		if(data->fd >= 0){
			close(data->fd);
		}
		data->fd = -1;
		data->channels = 0;
		free(inst[u]->impl);
	}

	free(inst);
	return 0;
}
