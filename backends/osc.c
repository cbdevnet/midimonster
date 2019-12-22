#define BACKEND_NAME "osc"

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

static struct {
	uint8_t detect;
} osc_global_config = {
	.detect = 0
};

MM_PLUGIN_API int init(){
	backend osc = {
		.name = BACKEND_NAME,
		.conf = osc_configure,
		.create = osc_instance,
		.conf_instance = osc_configure_instance,
		.channel = osc_map_channel,
		.handle = osc_set,
		.process = osc_handle,
		.start = osc_start,
		.shutdown = osc_shutdown
	};

	if(sizeof(osc_channel_ident) != sizeof(uint64_t)){
		LOG("Channel identification union out of bounds");
		return 1;
	}

	//register backend
	if(mm_backend_register(osc)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static size_t osc_data_length(osc_parameter_type t){
	//binary representation lengths for osc data types
	switch(t){
		case int32:
		case float32:
			return 4;
		case int64:
		case double64:
			return 8;
		default:
			LOGPF("Invalid OSC format specifier %c", t);
			return 0;
	}
}

static inline void osc_defaults(osc_parameter_type t, osc_parameter_value* max, osc_parameter_value* min){
	//data type default ranges
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
			LOG("Invalid OSC type, not setting any sane defaults");
			return;
	}
}

static inline osc_parameter_value osc_parse(osc_parameter_type t, uint8_t* data){
	//read value from binary representation
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
			LOG("Invalid OSC type passed to parsing routine");
	}
	return v;
}

static inline int osc_deparse(osc_parameter_type t, osc_parameter_value v, uint8_t* data){
	//write value to binary representation
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
			LOG("Invalid OSC type passed to parsing routine");
			return 1;
	}
	return 0;
}

static inline osc_parameter_value osc_parse_value_spec(osc_parameter_type t, char* value){
	//read value from string
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
			LOG("Invalid OSC type passed to value parser");
	}
	return v;
}

static inline channel_value osc_parameter_normalise(osc_parameter_type t, osc_parameter_value min, osc_parameter_value max, osc_parameter_value cur){
	//normalise osc value wrt given min/max
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
			LOG("Invalid OSC type passed to interpolation routine (normalise)");
	}

	//clamp to range
	v.normalised = clamp(v.normalised, 1.0, 0.0);
	return v;
}

static inline osc_parameter_value osc_parameter_denormalise(osc_parameter_type t, osc_parameter_value min, osc_parameter_value max, channel_value cur){
	//convert normalised value to osc value wrt given min/max
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
			LOG("Invalid OSC type passed to interpolation routine (denormalise)");
	}

	return v;
}

static int osc_path_validate(char* path, uint8_t allow_patterns){
	//validate osc path or pattern
	char illegal_chars[] = " #,";
	char pattern_chars[] = "?[]{}*";
	size_t u, c;
	uint8_t square_open = 0, curly_open = 0;
	
	if(path[0] != '/'){
		LOGPF("%s is not a valid OSC path: Missing root /", path);
		return 1;
	}

	for(u = 0; u < strlen(path); u++){
		for(c = 0; c < sizeof(illegal_chars); c++){
			if(path[u] == illegal_chars[c]){
				LOGPF("%s is not a valid OSC path: Illegal '%c' at %" PRIsize_t, path, illegal_chars[c], u);
				return 1;
			}
		}

		if(!isgraph(path[u])){
			LOGPF("%s is not a valid OSC path: Illegal '%c' at %" PRIsize_t, path, pattern_chars[c], u);
			return 1;
		}

		if(!allow_patterns){
			for(c = 0; c < sizeof(pattern_chars); c++){
				if(path[u] == pattern_chars[c]){
					LOGPF("%s is not a valid OSC path: Illegal '%c' at %" PRIsize_t, path, pattern_chars[c], u);
					return 1;
				}
			}
		}

		switch(path[u]){
			case '{':
				if(square_open || curly_open){
					LOGPF("%s is not a valid OSC path: Illegal '%c' at %" PRIsize_t, path, pattern_chars[c], u);
					return 1;
				}
				curly_open = 1;
				break;
			case '[':
				if(square_open || curly_open){
					LOGPF("%s is not a valid OSC path: Illegal '%c' at %" PRIsize_t, path, pattern_chars[c], u);
					return 1;
				}
				square_open = 1;
				break;
			case '}':
				curly_open = 0;
				break;
			case ']':
				square_open = 0;
				break;
			case '/':
				if(square_open || curly_open){
					LOGPF("%s is not a valid OSC path: Pattern across part boundaries", path);
					return 1;
				}
		}
	}

	if(square_open || curly_open){
		LOGPF("%s is not a valid OSC path: Unterminated pattern expression", path);
		return 1;
	}
	return 0;
}

static int osc_path_match(char* pattern, char* path){
	size_t u, p = 0, match_begin, match_end;
	uint8_t match_any = 0, inverted, match;

	for(u = 0; u < strlen(path); u++){
		switch(pattern[p]){
			case '/':
				if(match_any){
					for(; path[u] && path[u] != '/'; u++){
					}
				}
				if(path[u] != '/'){
					return 0;
				}
				match_any = 0;
				p++;
				break;
			case '?':
				match_any = 0;
				p++;
				break;
			case '*':
				match_any = 1;
				p++;
				break;
			case '[':
				inverted = (pattern[p + 1] == '!') ? 1 : 0;
				match_end = match_begin = inverted ? p + 2 : p + 1;
				match = 0;
				for(; pattern[match_end] != ']'; match_end++){
					if(pattern[match_end] == path[u]){
						match = 1;
						break;
					}

					if(pattern[match_end + 1] == '-' && pattern[match_end + 2] != ']'){
						if((pattern[match_end] > pattern[match_end + 2] 
									&& path[u] >= pattern[match_end + 2]
									&& path[u] <= pattern[match_end])
								|| (pattern[match_end] <= pattern[match_end + 2]
									&& path[u] >= pattern[match_end]
									&& path[u] <= pattern[match_end + 2])){
							match = 1;
							break;
						}
						match_end += 2;
					}

					if(pattern[match_end + 1] == ']' && match_any && !match
							&& path[u + 1] && path[u + 1] != '/'){
						match_end = match_begin - 1;
						u++;
					}
				}

				if(match == inverted){
					return 0;
				}

				match_any = 0;
				//advance to end of pattern
				for(; pattern[p] != ']'; p++){
				}
				p++;
				break;
			case '{':
				for(match_begin = p + 1; pattern[match_begin] != '}'; match_begin++){
					//find end
					for(match_end = match_begin; pattern[match_end] != ',' && pattern[match_end] != '}'; match_end++){
					}

					if(!strncmp(path + u, pattern + match_begin, match_end - match_begin)){
						//advance pattern
						for(; pattern[p] != '}'; p++){
						}
						p++;
						//advance path
						u += match_end - match_begin - 1;
						break;
					}

					if(pattern[match_end] == '}'){
						//retry with next if in match_any
						if(match_any && path[u + 1] && path[u + 1] != '/'){
							u++;
							match_begin = p;
							continue;
						}
						return 0;
					}
					match_begin = match_end;
				}
				match_any = 0;
				break;
			case 0:
				if(match_any){
					for(; path[u] && path[u] != '/'; u++){
					}
				}
				if(path[u]){
					return 0;
				}
				break;
			default:
				if(match_any){
					for(; path[u] && path[u] != '/' && path[u] != pattern[p]; u++){
					}
				}
				if(pattern[p] != path[u]){
					return 0;
				}
				p++;
				break;
		}
	}
	return 1;
}

static int osc_configure(char* option, char* value){
	if(!strcmp(option, "detect")){
		osc_global_config.detect = 1;
		if(!strcmp(value, "off")){
			osc_global_config.detect = 0;
		}
		return 0;
	}

	LOGPF("Unknown backend configuration parameter %s", option);
	return 1;
}

static int osc_register_pattern(osc_instance_data* data, char* pattern_path, char* configuration){
	size_t u, pattern;
	char* format = NULL, *token = NULL;

	if(osc_path_validate(pattern_path, 1)){
		LOGPF("Not a valid OSC pattern: %s", pattern_path);
		return 1;
	}

	//tokenize configuration
	format = strtok(configuration, " ");
	if(!format || strlen(format) < 1){
		LOGPF("Not a valid format specification for OSC pattern %s", pattern_path);
		return 1;
	}

	//create pattern
	data->pattern = realloc(data->pattern, (data->patterns + 1) * sizeof(osc_channel));
	if(!data->pattern){
		LOG("Failed to allocate memory");
		return 1;
	}
	pattern = data->patterns;

	data->pattern[pattern].params = strlen(format);
	data->pattern[pattern].path = strdup(pattern_path);
	data->pattern[pattern].type = calloc(strlen(format), sizeof(osc_parameter_type));
	data->pattern[pattern].max = calloc(strlen(format), sizeof(osc_parameter_value));
	data->pattern[pattern].min = calloc(strlen(format), sizeof(osc_parameter_value));

	if(!data->pattern[pattern].path
			|| !data->pattern[pattern].type
			|| !data->pattern[pattern].max
			|| !data->pattern[pattern].min){
		//this should fail config parsing and thus call the shutdown function,
		//which should properly free the rest of the data
		LOG("Failed to allocate memory");
		return 1;
	}

	//check format validity and store min/max values
	for(u = 0; u < strlen(format); u++){
		if(!osc_data_length(format[u])){
			LOGPF("Invalid format specifier %c for pattern %s", format[u], pattern_path);
			return 1;
		}

		data->pattern[pattern].type[u] = format[u];

		//parse min/max values
		token = strtok(NULL, " ");
		if(!token){
			LOGPF("Missing minimum specification for parameter %" PRIsize_t " of OSC pattern %s", u, pattern_path);
			return 1;
		}
		data->pattern[pattern].min[u] = osc_parse_value_spec(format[u], token);

		token = strtok(NULL, " ");
		if(!token){
			LOGPF("Missing maximum specification for parameter %" PRIsize_t " of OSC pattern %s", u, pattern_path);
			return 1;
		}
		data->pattern[pattern].max[u] = osc_parse_value_spec(format[u], token);
	}

	data->patterns++;
	return 0;
}

static int osc_configure_instance(instance* inst, char* option, char* value){
	osc_instance_data* data = (osc_instance_data*) inst->impl;
	char* host = NULL, *port = NULL, *fd_opts = NULL;

	if(!strcmp(option, "root")){
		if(osc_path_validate(value, 0)){
			LOGPF("Not a valid OSC root: %s", value);
			return 1;
		}

		if(data->root){
			free(data->root);
		}
		data->root = strdup(value);

		if(!data->root){
			LOG("Failed to allocate memory");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "bind")){
		mmbackend_parse_hostspec(value, &host, &port, &fd_opts);
		if(!host || !port){
			LOGPF("Invalid bind address for instance %s", inst->name);
			return 1;
		}

		//this requests a socket with SO_BROADCAST set, whether this is useful functionality for OSC is up for debate
		data->fd = mmbackend_socket(host, port, SOCK_DGRAM, 1, 1);
		if(data->fd < 0){
			LOGPF("Failed to bind for instance %s", inst->name);
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

		mmbackend_parse_hostspec(value, &host, &port, NULL);
		if(!host || !port){
			LOGPF("Invalid destination address for instance %s", inst->name);
			return 1;
		}

		if(mmbackend_parse_sockaddr(host, port, &data->dest, &data->dest_len)){
			LOGPF("Failed to parse destination address for instance %s", inst->name);
			return 1;
		}
		return 0;
	}
	else if(*option == '/'){
		return osc_register_pattern(data, option, value);
	}

	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static instance* osc_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	osc_instance_data* data = calloc(1, sizeof(osc_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return NULL;
	}

	data->fd = -1;
	inst->impl = data;
	return inst;
}

static channel* osc_map_channel(instance* inst, char* spec, uint8_t flags){
	size_t u, p;
	osc_instance_data* data = (osc_instance_data*) inst->impl;
	osc_channel_ident ident = {
		.label = 0
	};

	//check spec for correctness
	if(osc_path_validate(spec, 0)){
		return NULL;
	}

	//parse parameter offset
	if(strrchr(spec, ':')){
		ident.fields.parameter = strtoul(strrchr(spec, ':') + 1, NULL, 10);
		*(strrchr(spec, ':')) = 0;
	}

	//find matching channel
	for(u = 0; u < data->channels; u++){
		if(!strcmp(spec, data->channel[u].path)){
			break;
		}
	}

	//allocate new channel
	if(u == data->channels){
		for(p = 0; p < data->patterns; p++){
			if(osc_path_match(data->pattern[p].path, spec)){
				break;
			}
		}

		data->channel = realloc(data->channel, (u + 1) * sizeof(osc_channel));
		if(!data->channel){
			LOG("Failed to allocate memory");
			return NULL;
		}

		memset(data->channel + u, 0, sizeof(osc_channel));
		data->channel[u].path = strdup(spec);
		if(p != data->patterns){
			LOGPF("Matched pattern %s for %s", data->pattern[p].path, spec);
			data->channel[u].params = data->pattern[p].params;
			//just reuse the pointers from the pattern
			data->channel[u].type = data->pattern[p].type;
			data->channel[u].max = data->pattern[p].max;
			data->channel[u].min = data->pattern[p].min;

			//these are per channel
			data->channel[u].in = calloc(data->channel[u].params, sizeof(osc_parameter_value));
			data->channel[u].out = calloc(data->channel[u].params, sizeof(osc_parameter_value));
		}
		else if(data->patterns){
			LOGPF("No pattern match found for %s", spec);
		}

		if(!data->channel[u].path
				|| (data->channel[u].params && (!data->channel[u].in || !data->channel[u].out))){
			LOG("Failed to allocate memory");
			return NULL;
		}
		data->channels++;
	}

	ident.fields.channel = u;
	return mm_channel(inst, ident.label, 1);
}

static int osc_output_channel(instance* inst, size_t channel){
	osc_instance_data* data = (osc_instance_data*) inst->impl;
	uint8_t xmit_buf[OSC_XMIT_BUF] = "", *format = NULL;
	size_t offset = 0, p;

	//fix destination rport if required
	if(data->forced_rport){
		//cheating a bit because both IPv4 and IPv6 have the port at the same offset
		struct sockaddr_in* sockadd = (struct sockaddr_in*) &(data->dest);
		sockadd->sin_port = htobe16(data->forced_rport);
	}

	//determine minimum packet size
	if(osc_align((data->root ? strlen(data->root) : 0) + strlen(data->channel[channel].path) + 1) + osc_align(data->channel[channel].params + 2)  >= sizeof(xmit_buf)){
		LOGPF("Insufficient buffer size for transmitting channel %s.%s", inst->name, data->channel[channel].path);
		return 1;
	}

	//copy osc target path
	if(data->root){
		memcpy(xmit_buf, data->root, strlen(data->root));
		offset += strlen(data->root);
	}
	
	memcpy(xmit_buf + offset, data->channel[channel].path, strlen(data->channel[channel].path));
	offset += strlen(data->channel[channel].path) + 1;
	offset = osc_align(offset);

	//get format string offset, initialize
	format = xmit_buf + offset;
	offset += osc_align(data->channel[channel].params + 2);
	*format = ',';
	format++;

	for(p = 0; p < data->channel[channel].params; p++){
		//write format specifier
		format[p] = data->channel[channel].type[p];

		//write data
		if(offset + osc_data_length(data->channel[channel].type[p]) >= sizeof(xmit_buf)){
			LOGPF("Insufficient buffer size for transmitting channel %s.%s at parameter %" PRIsize_t, inst->name, data->channel[channel].path, p);
			return 1;
		}

		osc_deparse(data->channel[channel].type[p],
				data->channel[channel].out[p],
				xmit_buf + offset);
		offset += osc_data_length(data->channel[channel].type[p]);
	}

	//output packet
	if(sendto(data->fd, xmit_buf, offset, 0, (struct sockaddr*) &(data->dest), data->dest_len) < 0){
		LOGPF("Failed to transmit packet: %s", strerror(errno));
	}
	return 0;
}

static int osc_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t evt = 0, mark = 0;
	int rv = 0;
	osc_channel_ident ident = {
		.label = 0
	};
	osc_parameter_value current;

	if(!num){
		return 0;
	}

	osc_instance_data* data = (osc_instance_data*) inst->impl;
	if(!data->dest_len){
		LOGPF("Instance %s does not have a destination, output is disabled (%" PRIsize_t " channels)", inst->name, num);
		return 0;
	}

	for(evt = 0; evt < num; evt++){
		ident.label = c[evt]->ident;

		//sanity check
		if(ident.fields.channel >= data->channels
				|| ident.fields.parameter >= data->channel[ident.fields.channel].params){
			LOG("Channel identifier out of range");
			return 1;
		}

		//if the format is unknown, don't output
		if(!data->channel[ident.fields.channel].params){
			LOGPF("Channel %s.%s requires format specification for output", inst->name, data->channel[ident.fields.channel].path);
			continue;
		}

		//only output on change
		current = osc_parameter_denormalise(data->channel[ident.fields.channel].type[ident.fields.parameter],
				data->channel[ident.fields.channel].min[ident.fields.parameter],
				data->channel[ident.fields.channel].max[ident.fields.parameter],
				v[evt]);
		if(memcmp(&current, &data->channel[ident.fields.channel].out[ident.fields.parameter], sizeof(current))){
			//update current value
			data->channel[ident.fields.channel].out[ident.fields.parameter] = current;
			//mark channel
			data->channel[ident.fields.channel].mark = 1;
			mark = 1;
		}
	}
	
	if(mark){
		//output all marked channels
		for(evt = 0; !rv && evt < num; evt++){
			ident.label = c[evt]->ident;
			if(data->channel[ident.fields.channel].mark){
				rv |= osc_output_channel(inst, ident.fields.channel);
				data->channel[ident.fields.channel].mark = 0;
			}
		}
	}
	return rv;
}

static int osc_process_packet(instance* inst, char* local_path, char* format, uint8_t* payload, size_t payload_len){
	osc_instance_data* data = (osc_instance_data*) inst->impl;
	size_t c, p, offset = 0;
	osc_parameter_value min, max, cur;
	channel_value evt;
	osc_channel_ident ident = {
		.label = 0
	};
	channel* chan = NULL;

	if(payload_len % 4){
		LOGPF("Invalid packet, data length %" PRIsize_t, payload_len);
		return 0;
	}

	for(c = 0; c < data->channels; c++){
		if(!strcmp(local_path, data->channel[c].path)){
			ident.fields.channel = c;
			//unconfigured input should work without errors (using default limits)
			if(data->channel[c].params && strlen(format) != data->channel[c].params){
				LOGPF("Message %s.%s had format %s, internal representation has %" PRIsize_t " parameters", inst->name, local_path, format, data->channel[c].params);
				continue;
			}

			for(p = 0; p < strlen(format); p++){
				ident.fields.parameter = p;
				if(data->channel[c].params){
					max = data->channel[c].max[p];
					min = data->channel[c].min[p];
				}
				else{
					osc_defaults(format[p], &max, &min);
				}
				cur = osc_parse(format[p], payload + offset);
				if(!data->channel[c].params || memcmp(&cur, &data->channel[c].in, sizeof(cur))){
					evt = osc_parameter_normalise(format[p], min, max, cur);
					chan = mm_channel(inst, ident.label, 0);
					if(chan){
						mm_channel_event(chan, evt);
					}
				}

				//skip to next parameter data
				offset += osc_data_length(format[p]);
				//TODO check offset against payload length
			}
		}
	}

	return 0;
}

static int osc_handle(size_t num, managed_fd* fds){
	size_t fd;
	char recv_buf[OSC_RECV_BUF];
	instance* inst = NULL;
	osc_instance_data* data = NULL;
	ssize_t bytes_read = 0;
	char* osc_fmt = NULL;
	char* osc_local = NULL;
	uint8_t* osc_data = NULL;

	for(fd = 0; fd < num; fd++){
		inst = (instance*) fds[fd].impl;
		if(!inst){
			LOG("Signaled for unknown FD");
			continue;
		}

		data = (osc_instance_data*) inst->impl;

		do{
			if(data->learn){
				data->dest_len = sizeof(data->dest);
				bytes_read = recvfrom(fds[fd].fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr*) &(data->dest), &(data->dest_len));
			}
			else{
				bytes_read = recv(fds[fd].fd, recv_buf, sizeof(recv_buf), 0);
			}

			if(bytes_read <= 0){
				break;
			}

			if(data->root && strncmp(recv_buf, data->root, min(bytes_read, strlen(data->root)))){
				//ignore packet for different root
				continue;
			}
			osc_local = recv_buf + (data->root ? strlen(data->root) : 0);

			osc_fmt = recv_buf + osc_align(strlen(recv_buf) + 1);
			if(*osc_fmt != ','){
				//invalid format string
				LOGPF("Invalid format string in packet for instance %s", inst->name);
				continue;
			}
			osc_fmt++;

			if(osc_global_config.detect){
				LOGPF("Incoming data: Path %s.%s Format %s", inst->name, osc_local, osc_fmt);
			}

			//FIXME check supplied data length
			osc_data = (uint8_t*) osc_fmt + (osc_align(strlen(osc_fmt) + 2) - 1);

			if(osc_process_packet(inst, osc_local, osc_fmt, osc_data, bytes_read - (osc_data - (uint8_t*) recv_buf))){
				return 1;
			}
		} while(bytes_read > 0);

		#ifdef _WIN32
		if(bytes_read < 0 && WSAGetLastError() != WSAEWOULDBLOCK){
		#else
		if(bytes_read < 0 && errno != EAGAIN){
		#endif
			LOGPF("Failed to receive data for instance %s: %s", inst->name, strerror(errno));
		}

		if(bytes_read == 0){
			LOGPF("Descriptor for instance %s closed", inst->name);
			return 1;
		}
	}

	return 0;
}

static int osc_start(size_t n, instance** inst){
	size_t u, fds = 0;
	osc_instance_data* data = NULL;

	//update instance identifiers
	for(u = 0; u < n; u++){
		data = (osc_instance_data*) inst[u]->impl;

		if(data->fd >= 0){
			inst[u]->ident = data->fd;
			if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u])){
				LOGPF("Failed to register descriptor for instance %s", inst[u]->name);
				return 1;
			}
			fds++;
		}
		else{
			inst[u]->ident = -1;
		}
	}

	LOGPF("Registered %" PRIsize_t " descriptors to core", fds);
	return 0;
}

static int osc_shutdown(size_t n, instance** inst){
	size_t u, c;
	osc_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (osc_instance_data*) inst[u]->impl;
		for(c = 0; c < data->channels; c++){
			free(data->channel[c].path);
			free(data->channel[c].in);
			free(data->channel[c].out);
		}
		free(data->channel);
		for(c = 0; c < data->patterns; c++){
			free(data->pattern[c].path);
			free(data->pattern[c].type);
			free(data->pattern[c].min);
			free(data->pattern[c].max);
		}
		free(data->pattern);

		free(data->root);
		if(data->fd >= 0){
			close(data->fd);
		}
		data->fd = -1;
		data->channels = 0;
		data->patterns = 0;
		free(inst[u]->impl);
	}

	LOG("Backend shut down");
	return 0;
}
