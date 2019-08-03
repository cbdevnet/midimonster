#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "libmmbackend.h"
#include "artnet.h"

#define MAX_FDS 255
#define BACKEND_NAME "artnet"

static uint8_t default_net = 0;
static size_t artnet_fds = 0;
static artnet_descriptor* artnet_fd = NULL;

static int artnet_listener(char* host, char* port){
	int fd;
	if(artnet_fds >= MAX_FDS){
		fprintf(stderr, "ArtNet backend descriptor limit reached\n");
		return -1;
	}

	fd = mmbackend_socket(host, port, SOCK_DGRAM, 1);
	if(fd < 0){
		return -1;
	}

	//store fd
	artnet_fd = realloc(artnet_fd, (artnet_fds + 1) * sizeof(artnet_descriptor));
	if(!artnet_fd){
		close(fd);
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}

	fprintf(stderr, "ArtNet backend interface %lu bound to %s port %s\n", artnet_fds, host, port);
	artnet_fd[artnet_fds].fd = fd;
	artnet_fd[artnet_fds].output_instances = 0;
	artnet_fd[artnet_fds].output_instance = NULL;
	artnet_fd[artnet_fds].last_frame = NULL;
	artnet_fds++;
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
	char* host = NULL, *port = NULL;
	if(!strcmp(option, "net")){
		//configure default net
		default_net = strtoul(value, NULL, 0);
		return 0;
	}
	else if(!strcmp(option, "bind")){
		mmbackend_parse_hostspec(value, &host, &port);

		if(!port){
			port = ARTNET_PORT;
		}

		if(!host){
			fprintf(stderr, "Not valid ArtNet bind address given\n");
			return 1;
		}

		if(artnet_listener(host, port)){
			fprintf(stderr, "Failed to bind ArtNet descriptor: %s\n", value);
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown ArtNet backend option %s\n", option);
	return 1;
}

static instance* artnet_instance(){
	artnet_instance_data* data = NULL;
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	data = calloc(1, sizeof(artnet_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	data->net = default_net;

	inst->impl = data;
	return inst;
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

		if(data->fd_index >= artnet_fds){
			fprintf(stderr, "Invalid interface configured for ArtNet instance %s\n", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "dest") || !strcmp(option, "destination")){
		mmbackend_parse_hostspec(value, &host, &port);

		if(!port){
			port = ARTNET_PORT;
		}

		if(!host){
			fprintf(stderr, "Not a valid ArtNet destination for instance %s\n", inst->name);
			return 1;
		}

		return mmbackend_parse_sockaddr(host, port, &data->dest_addr, &data->dest_len);
	}

	fprintf(stderr, "Unknown ArtNet option %s for instance %s\n", option, inst->name);
	return 1;
}

static channel* artnet_channel(instance* inst, char* spec){
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;
	char* spec_next = spec;
	unsigned chan_a = strtoul(spec, &spec_next, 10);
	unsigned chan_b = 0;

	//primary channel sanity check
	if(!chan_a || chan_a > 512){
		fprintf(stderr, "Invalid ArtNet channel specification %s\n", spec);
		return NULL;
	}
	chan_a--;

	//secondary channel setup
	if(*spec_next == '+'){
		chan_b = strtoul(spec_next + 1, NULL, 10);
		if(!chan_b || chan_b > 512){
			fprintf(stderr, "Invalid wide-channel spec %s\n", spec);
			return NULL;
		}
		chan_b--;

		//if mapped mode differs, bail
		if(IS_ACTIVE(data->data.map[chan_b]) && data->data.map[chan_b] != (MAP_FINE | chan_a)){
			fprintf(stderr, "Fine channel already mapped for ArtNet spec %s\n", spec);
			return NULL;
		}

		data->data.map[chan_b] = MAP_FINE | chan_a;
	}

	//check current map mode
	if(IS_ACTIVE(data->data.map[chan_a])){
		if((*spec_next == '+' && data->data.map[chan_a] != (MAP_COARSE | chan_b))
				|| (*spec_next != '+' && data->data.map[chan_a] != (MAP_SINGLE | chan_a))){
			fprintf(stderr, "Primary ArtNet channel already mapped at differing mode: %s\n", spec);
			return NULL;
		}
	}
	data->data.map[chan_a] = (*spec_next == '+') ? (MAP_COARSE | chan_b) : (MAP_SINGLE | chan_a);

	return mm_channel(inst, chan_a, 1);
}

static int artnet_transmit(instance* inst){
	size_t u;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;
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
		.data = {0}
	};
	memcpy(frame.data, data->data.out, 512);

	if(sendto(artnet_fd[data->fd_index].fd, (uint8_t*) &frame, sizeof(frame), 0, (struct sockaddr*) &data->dest_addr, data->dest_len) < 0){
		fprintf(stderr, "Failed to output ArtNet frame for instance %s: %s\n", inst->name, strerror(errno));
	}

	//update last frame timestamp
	for(u = 0; u < artnet_fd[data->fd_index].output_instances; u++){
		if(artnet_fd[data->fd_index].output_instance[u].label == inst->ident){
			artnet_fd[data->fd_index].last_frame[u] = mm_timestamp();
		}
	}
	return 0;
}

static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t u, mark = 0;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	if(!data->dest_len){
		fprintf(stderr, "ArtNet instance %s not enabled for output (%lu channel events)\n", inst->name, num);
		return 0;
	}

	//FIXME maybe introduce minimum frame interval
	for(u = 0; u < num; u++){
		if(IS_WIDE(data->data.map[c[u]->ident])){
			uint32_t val = v[u].normalised * ((double) 0xFFFF);
			//the primary (coarse) channel is the one registered to the core, so we don't have to check for that
			if(data->data.out[c[u]->ident] != ((val >> 8) & 0xFF)){
				mark = 1;
				data->data.out[c[u]->ident] = (val >> 8) & 0xFF;
			}

			if(data->data.out[MAPPED_CHANNEL(data->data.map[c[u]->ident])] != (val & 0xFF)){
				mark = 1;
				data->data.out[MAPPED_CHANNEL(data->data.map[c[u]->ident])] = val & 0xFF;
			}
		}
		else if(data->data.out[c[u]->ident] != (v[u].normalised * 255.0)){
			mark = 1;
			data->data.out[c[u]->ident] = v[u].normalised * 255.0;
		}
	}

	if(mark){
		return artnet_transmit(inst);
	}

	return 0;
}

static inline int artnet_process_frame(instance* inst, artnet_pkt* frame){
	size_t p, max_mark = 0;
	uint16_t wide_val = 0;
	channel* chan = NULL;
	channel_value val;
	artnet_instance_data* data = (artnet_instance_data*) inst->impl;

	if(be16toh(frame->length) > 512){
		fprintf(stderr, "Invalid ArtNet frame channel count\n");
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
			if(data->data.map[p] & MAP_FINE){
				chan = mm_channel(inst, MAPPED_CHANNEL(data->data.map[p]), 0);
			}
			else{
				chan = mm_channel(inst, p, 0);
			}

			if(!chan){
				fprintf(stderr, "Active channel %lu on %s not known to core\n", p, inst->name);
				return 1;
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
				fprintf(stderr, "Failed to push ArtNet channel event to core\n");
				return 1;
			}
		}
	}
	return 0;
}

static int artnet_handle(size_t num, managed_fd* fds){
	size_t u, c;
	uint64_t timestamp = mm_timestamp();
	ssize_t bytes_read;
	char recv_buf[ARTNET_RECV_BUF];
	artnet_instance_id inst_id = {
		.label = 0
	};
	instance* inst = NULL;
	artnet_pkt* frame = (artnet_pkt*) recv_buf;

	//transmit keepalive frames
	for(u = 0; u < artnet_fds; u++){
		for(c = 0; c < artnet_fd[u].output_instances; c++){
			if(timestamp - artnet_fd[u].last_frame[c] >= ARTNET_KEEPALIVE_INTERVAL){
				inst = mm_instance_find(BACKEND_NAME, artnet_fd[u].output_instance[c].label);
				if(inst){
					artnet_transmit(inst);
				}
			}
		}
	}

	if(!num){
		//early exit
		return 0;
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
						fprintf(stderr, "Failed to process ArtNet frame\n");
					}
				}
			}
		} while(bytes_read > 0);

		#ifdef _WIN32
		if(bytes_read < 0 && WSAGetLastError() != WSAEWOULDBLOCK){
		#else
		if(bytes_read < 0 && errno != EAGAIN){
		#endif
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
	int rv = 1;
	instance** inst = NULL;
	artnet_instance_data* data = NULL;
	artnet_instance_id id = {
		.label = 0
	};

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if(!n){
		free(inst);
		return 0;
	}

	if(!artnet_fds){
		free(inst);
		fprintf(stderr, "Failed to start ArtNet backend: no descriptors bound\n");
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
				fprintf(stderr, "Universe specified multiple times, use one instance: %s - %s\n", inst[u]->name, inst[p]->name);
				goto bail;
			}
		}

		//if enabled for output, add to keepalive tracking
		if(data->dest_len){
			artnet_fd[data->fd_index].output_instance = realloc(artnet_fd[data->fd_index].output_instance, (artnet_fd[data->fd_index].output_instances + 1) * sizeof(artnet_instance_id));
			artnet_fd[data->fd_index].last_frame = realloc(artnet_fd[data->fd_index].last_frame, (artnet_fd[data->fd_index].output_instances + 1) * sizeof(uint64_t));

			if(!artnet_fd[data->fd_index].output_instance || !artnet_fd[data->fd_index].last_frame){
				fprintf(stderr, "Failed to allocate memory\n");
				goto bail;
			}
			artnet_fd[data->fd_index].output_instance[artnet_fd[data->fd_index].output_instances] = id;
			artnet_fd[data->fd_index].last_frame[artnet_fd[data->fd_index].output_instances] = 0;

			artnet_fd[data->fd_index].output_instances++;
		}
	}

	fprintf(stderr, "ArtNet backend registering %lu descriptors to core\n", artnet_fds);
	for(u = 0; u < artnet_fds; u++){
		if(mm_manage_fd(artnet_fd[u].fd, BACKEND_NAME, 1, (void*) u)){
			goto bail;
		}
	}

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

	for(p = 0; p < artnet_fds; p++){
		close(artnet_fd[p].fd);
		free(artnet_fd[p].output_instance);
		free(artnet_fd[p].last_frame);
	}
	free(artnet_fd);

	fprintf(stderr, "ArtNet backend shut down\n");
	return 0;
}
