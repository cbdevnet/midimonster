#define BACKEND_NAME "artnet"

#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "libmmbackend.h"
#include "artnet.h"

#define MAX_FDS 255

static struct {
	uint32_t next_frame;
	uint8_t default_net;
	size_t fds;
	artnet_descriptor* fd;
	uint8_t detect;
} global_cfg = {
	0
};

static int artnet_listener(char* host, char* port){
	int fd;
	if(global_cfg.fds >= MAX_FDS){
		LOG("Backend descriptor limit reached");
		return -1;
	}

	fd = mmbackend_socket(host, port, SOCK_DGRAM, 1, 1, 1);
	if(fd < 0){
		return -1;
	}

	//store fd
	global_cfg.fd = realloc(global_cfg.fd, (global_cfg.fds + 1) * sizeof(artnet_descriptor));
	if(!global_cfg.fd){
		close(fd);
		global_cfg.fds = 0;
		LOG("Failed to allocate memory");
		return -1;
	}

	LOGPF("Interface %" PRIsize_t " bound to %s port %s", global_cfg.fds, host, port);
	global_cfg.fd[global_cfg.fds].fd = fd;
	global_cfg.fd[global_cfg.fds].output_instances = 0;
	global_cfg.fd[global_cfg.fds].output_instance = NULL;
	global_cfg.fds++;
	return 0;
}

MM_PLUGIN_API int init(){
	backend artnet = {
		.name = BACKEND_NAME,
		.conf = artnet_configure,
		.create = artnet_instance,
		.conf_instance = artnet_configure_instance,
		.channel = artnet_channel,
		.handle = artnet_set,
		.process = artnet_handle,
		.start = artnet_start,
		.interval = artnet_interval,
		.shutdown = artnet_shutdown
	};

	if(sizeof(artnet_instance_id) != sizeof(uint64_t)){
		LOG("Instance identification union out of bounds");
		return 1;
	}

	//register backend
	if(mm_backend_register(artnet)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static uint32_t artnet_interval(){
	if(global_cfg.next_frame){
		return global_cfg.next_frame;
	}
	return ARTNET_KEEPALIVE_INTERVAL;
}

static int artnet_configure(char* option, char* value){
	char* host = NULL, *port = NULL, *fd_opts = NULL;
	if(!strcmp(option, "net")){
		//configure default net
		global_cfg.default_net = strtoul(value, NULL, 0);
		return 0;
	}
	else if(!strcmp(option, "bind")){
		mmbackend_parse_hostspec(value, &host, &port, &fd_opts);

		if(!host){
			LOGPF("%s is not a valid bind address", value);
			return 1;
		}

		if(artnet_listener(host, (port ? port : ARTNET_PORT))){
			LOGPF("Failed to bind descriptor: %s", value);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "detect")){
		global_cfg.detect = 0;
		if(!strcmp(value, "on")){
			global_cfg.detect = 1;
		}
		else if(!strcmp(value, "verbose")){
			global_cfg.detect = 2;
		}
		return 0;
	}

	LOGPF("Unknown backend option %s", option);
	return 1;
}

static int artnet_instance(instance* inst){
	artnet_instance_data* data = calloc(1, sizeof(artnet_instance_data));
	size_t u;

	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->net = global_cfg.default_net;
	for(u = 0; u < sizeof(data->data.channel) / sizeof(channel); u++){
		data->data.channel[u].ident = u;
		data->data.channel[u].instance = inst;
	}

	inst->impl = data;
	return 0;
}

static int artnet_configure_instance(instance* inst, char* option, char* value){
	char* host = NULL, *port = NULL;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	if(!strcmp(option, "net")){
		data->net = strtoul(value, NULL, 0);
		return 0;
	}
	else if(!strcmp(option, "uni") || !strcmp(option, "universe")){
		data->uni = strtoul(value, NULL, 0);
		return 0;
	}
	else if(!strcmp(option, "iface") || !strcmp(option, "interface")){
		data->fd_index = strtoul(value, NULL, 0);

		if(data->fd_index >= global_cfg.fds){
			LOGPF("Invalid interface configured for instance %s", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "dest") || !strcmp(option, "destination")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);

		if(!host){
			LOGPF("Not a valid destination for instance %s: %s", inst->name, value);
			return 1;
		}

		return mmbackend_parse_sockaddr(host, port ? port : ARTNET_PORT, &data->dest_addr, &data->dest_len);
	}
	else if(!strcmp(option, "realtime")){
		data->realtime = strtoul(value, NULL, 10);
		return 0;
	}

	LOGPF("Unknown instance option %s for instance %s", option, inst->name);
	return 1;
}

static channel* artnet_channel(instance* inst, char* spec, uint8_t flags){
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;
	char* spec_next = spec;
	unsigned chan_a = strtoul(spec, &spec_next, 10);
	unsigned chan_b = 0;

	//primary channel sanity check
	if(!chan_a || chan_a > 512){
		LOGPF("Invalid channel specification %s", spec);
		return NULL;
	}
	chan_a--;

	//check output capabilities
	if((flags & mmchannel_output) && !data->dest_len){
		LOGPF("Channel %s.%s mapped for output, but instance is not configured for output (missing destination)", inst->name, spec);
	}

	//secondary channel setup
	if(*spec_next == '+'){
		chan_b = strtoul(spec_next + 1, NULL, 10);
		if(!chan_b || chan_b > 512){
			LOGPF("Invalid wide-channel specification %s", spec);
			return NULL;
		}
		chan_b--;

		//if mapped mode differs, bail
		if(IS_ACTIVE(data->data.map[chan_b]) && data->data.map[chan_b] != (MAP_FINE | chan_a)){
			LOGPF("Fine channel already mapped for spec %s", spec);
			return NULL;
		}

		data->data.map[chan_b] = MAP_FINE | chan_a;
	}

	//check current map mode
	if(IS_ACTIVE(data->data.map[chan_a])){
		if((*spec_next == '+' && data->data.map[chan_a] != (MAP_COARSE | chan_b))
				|| (*spec_next != '+' && data->data.map[chan_a] != (MAP_SINGLE | chan_a))){
			LOGPF("Primary channel already mapped at differing mode: %s", spec);
			return NULL;
		}
	}
	data->data.map[chan_a] = (*spec_next == '+') ? (MAP_COARSE | chan_b) : (MAP_SINGLE | chan_a);

	return data->data.channel + chan_a;
}

static int artnet_transmit(instance* inst, artnet_output_universe* output){
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	//build output frame
	artnet_pkt frame = {
		.magic = {'A', 'r', 't', '-', 'N', 'e', 't', 0x00},
		.opcode = htobe16(OpDmx),
		.version = htobe16(ARTNET_VERSION),
		.sequence = data->data.seq++,
		.port = 0,
		.universe = data->uni,
		.net = data->net,
		.length = htobe16(512),
		.data = {0}
	};
	memcpy(frame.data, data->data.out, 512);

	if(sendto(global_cfg.fd[data->fd_index].fd, (uint8_t*) &frame, sizeof(frame), 0, (struct sockaddr*) &data->dest_addr, data->dest_len) < 0){
		#ifdef _WIN32
		if(WSAGetLastError() != WSAEWOULDBLOCK){
		#else
		if(errno != EAGAIN){
		#endif
			LOGPF("Failed to output frame for instance %s: %s", inst->name, mmbackend_socket_strerror(errno));
			return 1;
		}
		//reschedule frame output
		output->mark = 1;
		if(!global_cfg.next_frame || global_cfg.next_frame > ARTNET_SYNTHESIZE_MARGIN){
			global_cfg.next_frame = ARTNET_SYNTHESIZE_MARGIN;
		}
		return 0;
	}

	//update last frame timestamp
	output->last_frame = mm_timestamp();
	output->mark = 0;
	return 0;
}

static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v){
	uint32_t frame_delta = 0;
	size_t u, mark = 0, channel_offset = 0;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	if(!data->dest_len){
		LOGPF("Instance %s not enabled for output (%" PRIsize_t " channel events)", inst->name, num);
		return 0;
	}

	for(u = 0; u < num; u++){
		channel_offset = c[u]->ident;
		if(IS_WIDE(data->data.map[channel_offset])){
			uint32_t val = v[u].normalised * ((double) 0xFFFF);
			//the primary (coarse) channel is the one registered to the core, so we don't have to check for that
			if(data->data.out[channel_offset] != ((val >> 8) & 0xFF)){
				mark = 1;
				data->data.out[channel_offset] = (val >> 8) & 0xFF;
			}

			if(data->data.out[MAPPED_CHANNEL(data->data.map[channel_offset])] != (val & 0xFF)){
				mark = 1;
				data->data.out[MAPPED_CHANNEL(data->data.map[channel_offset])] = val & 0xFF;
			}
		}
		else if(data->data.out[channel_offset] != (v[u].normalised * 255.0)){
			mark = 1;
			data->data.out[channel_offset] = v[u].normalised * 255.0;
		}
	}

	if(mark){
		//find output control data for the instance
		for(u = 0; u < global_cfg.fd[data->fd_index].output_instances; u++){
			if(global_cfg.fd[data->fd_index].output_instance[u].label == inst->ident){
				break;
			}
		}

		if(!data->realtime){
			frame_delta = mm_timestamp() - global_cfg.fd[data->fd_index].output_instance[u].last_frame;

			//check output rate limit, request next frame
			if(frame_delta < ARTNET_FRAME_TIMEOUT){
				global_cfg.fd[data->fd_index].output_instance[u].mark = 1;
				if(!global_cfg.next_frame || global_cfg.next_frame > (ARTNET_FRAME_TIMEOUT - frame_delta)){
					global_cfg.next_frame = (ARTNET_FRAME_TIMEOUT - frame_delta);
				}
				return 0;
			}
		}
		return artnet_transmit(inst, global_cfg.fd[data->fd_index].output_instance + u);
	}

	return 0;
}

static inline int artnet_process_frame(instance* inst, artnet_pkt* frame){
	size_t p, max_mark = 0;
	uint16_t wide_val = 0;
	channel* chan = NULL;
	channel_value val;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	if(!data->last_input && global_cfg.detect){
		LOGPF("Valid data on instance %s (Net %d Universe %d): %d channels", inst->name, data->net, data->uni, be16toh(frame->length));
	}
	data->last_input = mm_timestamp();

	if(be16toh(frame->length) > 512){
		LOGPF("Invalid frame channel count: %d", be16toh(frame->length));
		return 1;
	}

	//read data, mark update channels
	for(p = 0; p < be16toh(frame->length); p++){
		if(IS_ACTIVE(data->data.map[p]) && frame->data[p] != data->data.in[p]){
			data->data.in[p] = frame->data[p];
			data->data.map[p] |= MAP_MARK;
			max_mark = p;
		}
	}

	//generate events
	for(p = 0; p <= max_mark; p++){
		if(data->data.map[p] & MAP_MARK){
			data->data.map[p] &= ~MAP_MARK;
			chan = data->data.channel + p;
			if(data->data.map[p] & MAP_FINE){
				chan = data->data.channel + MAPPED_CHANNEL(data->data.map[p]);
			}

			if(IS_WIDE(data->data.map[p])){
				data->data.map[MAPPED_CHANNEL(data->data.map[p])] &= ~MAP_MARK;
				wide_val = data->data.in[p] << ((data->data.map[p] & MAP_COARSE) ? 8 : 0);
				wide_val |= data->data.in[MAPPED_CHANNEL(data->data.map[p])] << ((data->data.map[p] & MAP_COARSE) ? 0 : 8);

				val.raw.u64 = wide_val;
				val.normalised = (double) wide_val / (double) 0xFFFF;
			}
			else{
				//single channel
				val.raw.u64 = data->data.in[p];
				val.normalised = (double) data->data.in[p] / 255.0;
			}

			if(mm_channel_event(chan, val)){
				LOG("Failed to push channel event to core");
				return 1;
			}
		}
	}
	return 0;
}

static int artnet_handle(size_t num, managed_fd* fds){
	size_t u, c;
	uint64_t timestamp = mm_timestamp();
	uint32_t synthesize_delta = 0;
	ssize_t bytes_read;
	char recv_buf[ARTNET_RECV_BUF];
	artnet_instance_id inst_id = {
		.label = 0
	};
	instance* inst = NULL;
	artnet_pkt* frame = (artnet_pkt*) recv_buf;

	//transmit keepalive & synthesized frames
	global_cfg.next_frame = 0;
	for(u = 0; u < global_cfg.fds; u++){
		for(c = 0; c < global_cfg.fd[u].output_instances; c++){
			synthesize_delta = timestamp - global_cfg.fd[u].output_instance[c].last_frame;
			if((global_cfg.fd[u].output_instance[c].mark
						&& synthesize_delta >= ARTNET_FRAME_TIMEOUT + ARTNET_SYNTHESIZE_MARGIN) //synthesize next frame
					|| synthesize_delta >= ARTNET_KEEPALIVE_INTERVAL){ //keepalive timeout
				inst = mm_instance_find(BACKEND_NAME, global_cfg.fd[u].output_instance[c].label);
				if(inst){
					artnet_transmit(inst, global_cfg.fd[u].output_instance + c);
				}
			}

			//update next_frame
			if(global_cfg.fd[u].output_instance[c].mark
					&& (!global_cfg.next_frame || global_cfg.next_frame > ARTNET_FRAME_TIMEOUT + ARTNET_SYNTHESIZE_MARGIN - synthesize_delta)){
				global_cfg.next_frame = ARTNET_FRAME_TIMEOUT + ARTNET_SYNTHESIZE_MARGIN - synthesize_delta;
			}
		}
	}

	for(u = 0; u < num; u++){
		do{
			bytes_read = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
			if(bytes_read > 0 && bytes_read > sizeof(artnet_hdr)){
				if(!memcmp(frame->magic, "Art-Net\0", 8) && be16toh(frame->opcode) == OpDmx){
					//find matching instance
					inst_id.fields.fd_index = ((uint64_t) fds[u].impl) & 0xFF;
					inst_id.fields.net = frame->net;
					inst_id.fields.uni = frame->universe;
					inst = mm_instance_find(BACKEND_NAME, inst_id.label);
					if(inst && artnet_process_frame(inst, frame)){
						LOG("Failed to process frame");
					}
					else if(!inst && global_cfg.detect > 1){
						LOGPF("Received data for unconfigured universe %d (net %d) on descriptor %" PRIu64, frame->universe, frame->net, (((uint64_t) fds[u].impl) & 0xFF));
					}
				}
			}
		} while(bytes_read > 0);

		#ifdef _WIN32
		if(bytes_read < 0 && WSAGetLastError() != WSAEWOULDBLOCK){
		#else
		if(bytes_read < 0 && errno != EAGAIN){
		#endif
			LOGPF("Failed to receive data: %s", mmbackend_socket_strerror(errno));
		}

		if(bytes_read == 0){
			LOG("Listener closed");
			return 1;
		}
	}

	return 0;
}

static int artnet_start(size_t n, instance** inst){
	size_t u, p;
	int rv = 1;
	artnet_instance_data* data = NULL;
	artnet_instance_id id = {
		.label = 0
	};

	if(!global_cfg.fds){
		LOG("Failed to start backend: no descriptors bound");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (artnet_instance_data*) inst[u]->impl;
		//set instance identifier
		id.fields.fd_index = data->fd_index;
		id.fields.net = data->net;
		id.fields.uni = data->uni;
		inst[u]->ident = id.label;

		//check for duplicates
		for(p = 0; p < u; p++){
			if(inst[u]->ident == inst[p]->ident){
				LOGPF("Universe specified multiple times, use one instance: %s - %s", inst[u]->name, inst[p]->name);
				goto bail;
			}
		}

		//if enabled for output, add to keepalive tracking
		if(data->dest_len){
			global_cfg.fd[data->fd_index].output_instance = realloc(global_cfg.fd[data->fd_index].output_instance, (global_cfg.fd[data->fd_index].output_instances + 1) * sizeof(artnet_output_universe));

			if(!global_cfg.fd[data->fd_index].output_instance){
				LOG("Failed to allocate memory");
				goto bail;
			}
			global_cfg.fd[data->fd_index].output_instance[global_cfg.fd[data->fd_index].output_instances].label = id.label;
			global_cfg.fd[data->fd_index].output_instance[global_cfg.fd[data->fd_index].output_instances].last_frame = 0;
			global_cfg.fd[data->fd_index].output_instance[global_cfg.fd[data->fd_index].output_instances].mark = 0;

			global_cfg.fd[data->fd_index].output_instances++;
		}
	}

	LOGPF("Registering %" PRIsize_t " descriptors to core", global_cfg.fds);
	for(u = 0; u < global_cfg.fds; u++){
		if(mm_manage_fd(global_cfg.fd[u].fd, BACKEND_NAME, 1, (void*) u)){
			goto bail;
		}
	}

	rv = 0;
bail:
	return rv;
}

static int artnet_shutdown(size_t n, instance** inst){
	size_t p;

	for(p = 0; p < n; p++){
		free(inst[p]->impl);
	}

	for(p = 0; p < global_cfg.fds; p++){
		close(global_cfg.fd[p].fd);
		free(global_cfg.fd[p].output_instance);
	}
	free(global_cfg.fd);
	global_cfg.fd = NULL;
	global_cfg.fds = 0;

	LOG("Backend shut down");
	return 0;
}
