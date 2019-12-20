#define BACKEND_NAME "sacn"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#ifndef _WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "libmmbackend.h"
#include "sacn.h"

//upper limit imposed by using the fd index as 16-bit part of the instance id
#define MAX_FDS 4096

enum /*_sacn_fd_flags*/ {
	mcast_loop = 1
};

static struct /*_sacn_global_config*/ {
	uint8_t source_name[64];
	uint8_t cid[16];
	size_t fds;
	sacn_fd* fd;
	uint64_t last_announce;
} global_cfg = {
	.source_name = "MIDIMonster",
	.cid = {'M', 'I', 'D', 'I', 'M', 'o', 'n', 's', 't', 'e', 'r'},
	.fds = 0,
	.fd = NULL,
	.last_announce = 0
};

MM_PLUGIN_API int init(){
	backend sacn = {
		.name = BACKEND_NAME,
		.conf = sacn_configure,
		.create = sacn_instance,
		.conf_instance = sacn_configure_instance,
		.channel = sacn_channel,
		.handle = sacn_set,
		.process = sacn_handle,
		.start = sacn_start,
		.shutdown = sacn_shutdown
	};

	if(sizeof(sacn_instance_id) != sizeof(uint64_t)){
		LOG("Instance identification union out of bounds");
		return 1;
	}

	//register the backend
	if(mm_backend_register(sacn)){
		LOG("Failed to register backend");
		return 1;
	}

	return 0;
}

static int sacn_listener(char* host, char* port, uint8_t flags){
	int fd = -1, yes = 1;
	if(global_cfg.fds >= MAX_FDS){
		LOG("Descriptor limit reached");
		return -1;
	}

	fd = mmbackend_socket(host, port, SOCK_DGRAM, 1, 1);
	if(fd < 0){
		return -1;
	}

	//store fd
	global_cfg.fd = realloc(global_cfg.fd, (global_cfg.fds + 1) * sizeof(sacn_fd));
	if(!global_cfg.fd){
		close(fd);
		LOG("Failed to allocate memory");
		return -1;
	}

	LOGPF("Interface %" PRIsize_t " bound to %s port %s", global_cfg.fds, host, port);
	global_cfg.fd[global_cfg.fds].fd = fd;
	global_cfg.fd[global_cfg.fds].universes = 0;
	global_cfg.fd[global_cfg.fds].universe = NULL;
	global_cfg.fd[global_cfg.fds].last_frame = NULL;

	if(flags & mcast_loop){
		//set IP_MCAST_LOOP to allow local applications to receive output
		if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void*)&yes, sizeof(yes)) < 0){
			LOGPF("Failed to re-enable IP_MULTICAST_LOOP on socket: %s", strerror(errno));
		}
	}

	global_cfg.fds++;
	return 0;
}

static int sacn_configure(char* option, char* value){
	char* host = NULL, *port = NULL, *next = NULL;
	uint8_t flags = 0;
	size_t u;

	if(!strcmp(option, "name")){
		if(strlen(value) > 63){
			LOGPF("Invalid source name %s, limit is 63 characters", value);
			return 1;
		}

		memset(global_cfg.source_name, 0, sizeof(global_cfg.source_name));
		memcpy(global_cfg.source_name, value, strlen(value));
		return 0;
	}
	else if(!strcmp(option, "cid")){
		next = value;
		for(u = 0; u < sizeof(global_cfg.cid); u++){
			global_cfg.cid[u] = (strtoul(next, &next, 0) & 0xFF);
		}
	}
	else if(!strcmp(option, "bind")){
		mmbackend_parse_hostspec(value, &host, &port, &next);

		if(!host){
			LOG("No valid bind address provided");
			return 1;
		}

		if(next && !strncmp(next, "local", 5)){
			flags = mcast_loop;
		}

		if(sacn_listener(host, port ? port : SACN_PORT, flags)){
			LOGPF("Failed to bind descriptor: %s", value);
			return 1;
		}

		return 0;
	}

	LOGPF("Unknown backend configuration option %s", option);
	return 1;
}

static int sacn_configure_instance(instance* inst, char* option, char* value){
	sacn_instance_data* data = (sacn_instance_data*) inst->impl;
	char* host = NULL, *port = NULL, *next = NULL;
	size_t u;

	if(!strcmp(option, "universe")){
		data->uni = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "interface")){
		data->fd_index = strtoul(value, NULL, 10);

		if(data->fd_index >= global_cfg.fds){
			LOGPF("Configured interface index is out of range on instance %s", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "priority")){
		data->xmit_prio = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "destination")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);

		if(!host){
			LOGPF("No valid destination for instance %s", inst->name);
			return 1;
		}

		return mmbackend_parse_sockaddr(host, port ? port : SACN_PORT, &data->dest_addr, &data->dest_len);
	}
	else if(!strcmp(option, "from")){
		next = value;
		data->filter_enabled = 1;
		for(u = 0; u < sizeof(data->cid_filter); u++){
			data->cid_filter[u] = (strtoul(next, &next, 0) & 0xFF);
		}
		LOGPF("Enabled source CID filter for instance %s", inst->name);
		return 0;
	}
	else if(!strcmp(option, "unicast")){
		data->unicast_input = strtoul(value, NULL, 10);
		return 0;
	}

	LOGPF("Unknown instance configuration option %s for instance %s", option, inst->name);
	return 1;
}

static instance* sacn_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	inst->impl = calloc(1, sizeof(sacn_instance_data));
	if(!inst->impl){
		LOG("Failed to allocate memory");
		return NULL;
	}

	return inst;
}

static channel* sacn_channel(instance* inst, char* spec, uint8_t flags){
	sacn_instance_data* data = (sacn_instance_data*) inst->impl;
	char* spec_next = spec;

	unsigned chan_a = strtoul(spec, &spec_next, 10), chan_b = 0;
	
	//range check
	if(!chan_a || chan_a > 512){
		LOGPF("Channel out of range on instance %s: %s", inst->name, spec);
		return NULL;
	}
	chan_a--;

	//if wide channel, mark fine
	if(*spec_next == '+'){
		chan_b = strtoul(spec_next + 1, NULL, 10);
		if(!chan_b || chan_b > 512){
			LOGPF("Invalid wide-channel spec on instance %s: %s", inst->name, spec);
			return NULL;
		}
		chan_b--;

		//if already mapped, bail
		if(IS_ACTIVE(data->data.map[chan_b]) && data->data.map[chan_b] != (MAP_FINE | chan_a)){
			LOGPF("Fine channel %u already mapped on instance %s", chan_b, inst->name);
			return NULL;
		}

		data->data.map[chan_b] = MAP_FINE | chan_a;
	}

	//if already active, assert that nothing changes
	if(IS_ACTIVE(data->data.map[chan_a])){
		if((*spec_next == '+' && data->data.map[chan_a] != (MAP_COARSE | chan_b))
				|| (*spec_next != '+' && data->data.map[chan_a] != (MAP_SINGLE | chan_a))){
			LOGPF("Primary channel %u already mapped in another mode on instance %s", chan_a, inst->name);
			return NULL;
		}
	}

	data->data.map[chan_a] = (*spec_next == '+') ? (MAP_COARSE | chan_b) : (MAP_SINGLE | chan_a);
	return mm_channel(inst, chan_a, 1);
}

static int sacn_transmit(instance* inst){
	size_t u;
	sacn_instance_data* data = (sacn_instance_data*) inst->impl;
	sacn_data_pdu pdu = {
		.root = {
			.preamble_size = htobe16(0x10),
			.postamble_size = 0,
			.magic = { 0 }, //memcpy'd
			.flags = htobe16(0x7000 | 0x026e),
			.vector = htobe32(ROOT_E131_DATA),
			.sender_cid = { 0 }, //memcpy'd
			.frame_flags = htobe16(0x7000 | 0x0258),
			.frame_vector = htobe32(FRAME_E131_DATA)
		},
		.data = {
			.source_name = "", //memcpy'd
			.priority = data->xmit_prio,
			.sync_addr = 0,
			.sequence = data->data.last_seq++,
			.options = 0,
			.universe = htobe16(data->uni),
			.flags = htobe16(0x7000 | 0x0205),
			.vector = DMP_SET_PROPERTY,
			.format = 0xA1,
			.startcode_offset = 0,
			.address_increment = htobe16(1),
			.channels = htobe16(513),
			.data = { 0 } //memcpy'd
		}
	};

	memcpy(pdu.root.magic, SACN_PDU_MAGIC, sizeof(pdu.root.magic));
	memcpy(pdu.root.sender_cid, global_cfg.cid, sizeof(pdu.root.sender_cid));
	memcpy(pdu.data.source_name, global_cfg.source_name, sizeof(pdu.data.source_name));
	memcpy((((uint8_t*)pdu.data.data) + 1), data->data.out, 512);

	if(sendto(global_cfg.fd[data->fd_index].fd, (uint8_t*) &pdu, sizeof(pdu), 0, (struct sockaddr*) &data->dest_addr, data->dest_len) < 0){
		LOGPF("Failed to output frame for instance %s: %s", inst->name, strerror(errno));
	}

	//update last transmit timestamp
	for(u = 0; u < global_cfg.fd[data->fd_index].universes; u++){
		if(global_cfg.fd[data->fd_index].universe[u] == data->uni){
			global_cfg.fd[data->fd_index].last_frame[u] = mm_timestamp();
		}
	}
	return 0;
}

static int sacn_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t u, mark = 0;
	sacn_instance_data* data = (sacn_instance_data*) inst->impl;

	if(!num){
		return 0;
	}

	if(!data->xmit_prio){
		LOGPF("Instance %s not enabled for output (%" PRIsize_t " channel events)", inst->name, num);
		return 0;
	}

	for(u = 0; u < num; u++){
		if(IS_WIDE(data->data.map[c[u]->ident])){
			uint32_t val = v[u].normalised * ((double) 0xFFFF);

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

	//send packet if required
	if(mark){
		sacn_transmit(inst);
	}

	return 0;
}

static int sacn_process_frame(instance* inst, sacn_frame_root* frame, sacn_frame_data* data){
	size_t u, max_mark = 0;
	channel* chan = NULL;
	channel_value val;
	sacn_instance_data* inst_data = (sacn_instance_data*) inst->impl;

	//source filtering
	if(inst_data->filter_enabled && memcmp(inst_data->cid_filter, frame->sender_cid, 16)){
		return 0;
	}

	if(data->format != 0xa1
			|| data->startcode_offset
			|| be16toh(data->address_increment) != 1){
		LOGPF("Framing not supported for incoming data on instance %s\n", inst->name);
		return 1;
	}

	if(be16toh(data->channels) > 513){
		LOGPF("Invalid frame channel count %d on instance %s", be16toh(data->channels), inst->name);
		return 1;
	}

	//handle source priority (currently a 1-bit counter)
	if(inst_data->data.last_priority > data->priority){
		inst_data->data.last_priority = data->priority;
		return 0;
	}
	inst_data->data.last_priority = data->priority;

	//read data (except start code), mark changed channels
	for(u = 1; u < be16toh(data->channels); u++){
		if(IS_ACTIVE(inst_data->data.map[u - 1])
				&& data->data[u] != inst_data->data.in[u - 1]){
			inst_data->data.in[u - 1] = data->data[u];
			inst_data->data.map[u - 1] |= MAP_MARK;
			max_mark = u - 1;
		}
	}

	//generate events
	for(u = 0; u <= max_mark; u++){
		if(inst_data->data.map[u] & MAP_MARK){
			//unmark and get channel
			inst_data->data.map[u] &= ~MAP_MARK;
			if(inst_data->data.map[u] & MAP_FINE){
				chan = mm_channel(inst, MAPPED_CHANNEL(inst_data->data.map[u]), 0);
			}
			else{
				chan = mm_channel(inst, u, 0);
			}

			if(!chan){
				LOGPF("Active channel %" PRIsize_t " on %s not known to core", u, inst->name);
				return 1;
			}

			//generate value
			if(IS_WIDE(inst_data->data.map[u])){
				inst_data->data.map[MAPPED_CHANNEL(inst_data->data.map[u])] &= ~MAP_MARK;
				val.raw.u64 = (uint16_t) (inst_data->data.in[u] << ((inst_data->data.map[u] & MAP_COARSE) ? 8 : 0));
				val.raw.u64 |= (uint16_t) (inst_data->data.in[MAPPED_CHANNEL(inst_data->data.map[u])] << ((inst_data->data.map[u] & MAP_COARSE) ? 0 : 8));
				val.normalised = (double) val.raw.u64 / (double) 0xFFFF;
			}
			else{
				val.raw.u64 = inst_data->data.in[u];
				val.normalised = (double) val.raw.u64 / 255.0;
			}

			if(mm_channel_event(chan, val)){
				LOG("Failed to push event to core");
				return 1;
			}
		}
	}
	return 0;
}

static void sacn_discovery(size_t fd){
	size_t page = 0, pages = (global_cfg.fd[fd].universes / 512) + 1, universes;
	struct sockaddr_in discovery_dest = {
		.sin_family = AF_INET,
		.sin_port = htobe16(strtoul(SACN_PORT, NULL, 10)),
		.sin_addr.s_addr = htobe32(((uint32_t) 0xefff0000) | 64214)
	};

	sacn_discovery_pdu pdu = {
		.root = {
			.preamble_size = htobe16(0x10),
			.postamble_size = 0,
			.magic = { 0 }, //memcpy'd
			.flags = 0, //filled later
			.vector = htobe32(ROOT_E131_EXTENDED),
			.sender_cid = { 0 }, //memcpy'd
			.frame_flags = 0, //filled later
			.frame_vector = htobe32(FRAME_E131_DISCOVERY)
		},
		.data = {
			.source_name = "", //memcpy'd
			.flags = 0, //filled later
			.vector = htobe32(DISCOVERY_UNIVERSE_LIST),
			.page = 0, //filled later
			.max_page = pages - 1,
			.data = { 0 } //memcpy'd
		}
	};

	memcpy(pdu.root.magic, SACN_PDU_MAGIC, sizeof(pdu.root.magic));
	memcpy(pdu.root.sender_cid, global_cfg.cid, sizeof(pdu.root.sender_cid));
	memcpy(pdu.data.source_name, global_cfg.source_name, sizeof(pdu.data.source_name));

	for(; page < pages; page++){
		universes = (global_cfg.fd[fd].universes - page * 512 >= 512) ? 512 : (global_cfg.fd[fd].universes % 512);
		pdu.root.flags = htobe16(0x7000 | (104 + universes * sizeof(uint16_t)));
		pdu.root.frame_flags = htobe16(0x7000 | (82 + universes * sizeof(uint16_t)));
		pdu.data.flags = htobe16(0x7000 | (8 + universes * sizeof(uint16_t)));

		pdu.data.page = page;
		memcpy(pdu.data.data, global_cfg.fd[fd].universe + page * 512, universes * sizeof(uint16_t));

		if(sendto(global_cfg.fd[fd].fd, (uint8_t*) &pdu, sizeof(pdu) - (512 - universes) * sizeof(uint16_t), 0, (struct sockaddr*) &discovery_dest, sizeof(discovery_dest)) < 0){
			LOGPF("Failed to output universe discovery frame for interface %" PRIsize_t ": %s", fd, strerror(errno));
		}
	}
}

static int sacn_handle(size_t num, managed_fd* fds){
	size_t u, c;
	uint64_t timestamp = mm_timestamp();
	ssize_t bytes_read;
	char recv_buf[SACN_RECV_BUF];
	instance* inst = NULL;
	sacn_instance_id instance_id = {
		.label = 0
	};
	sacn_frame_root* frame = (sacn_frame_root*) recv_buf;
	sacn_frame_data* data = (sacn_frame_data*) (recv_buf + sizeof(sacn_frame_root));

	if(mm_timestamp() - global_cfg.last_announce > SACN_DISCOVERY_TIMEOUT){
		//send universe discovery pdu
		for(u = 0; u < global_cfg.fds; u++){
			if(global_cfg.fd[u].universes){
				sacn_discovery(u);
			}
		}
		global_cfg.last_announce = timestamp;
	}

	//check for keepalive frames
	for(u = 0; u < global_cfg.fds; u++){
		for(c = 0; c < global_cfg.fd[u].universes; c++){
			if(timestamp - global_cfg.fd[u].last_frame[c] >= SACN_KEEPALIVE_INTERVAL){
				instance_id.fields.fd_index = u;
				instance_id.fields.uni = global_cfg.fd[u].universe[c];
				inst = mm_instance_find(BACKEND_NAME, instance_id.label);
				if(inst){
					sacn_transmit(inst);
				}
			}
		}
	}

	//early exit
	if(!num){
		return 0;
	}

	for(u = 0; u < num; u++){
		do{
			bytes_read = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
			if(bytes_read > 0 && bytes_read > sizeof(sacn_frame_root)){
				if(!memcmp(frame->magic, SACN_PDU_MAGIC, 12)
						&& be16toh(frame->preamble_size) == 0x10
						&& frame->postamble_size == 0
						&& be32toh(frame->vector) == ROOT_E131_DATA
						&& be32toh(frame->frame_vector) == FRAME_E131_DATA
						&& data->vector == DMP_SET_PROPERTY){
					instance_id.fields.fd_index = ((uint64_t) fds[u].impl) & 0xFFFF;
					instance_id.fields.uni = be16toh(data->universe);
					inst = mm_instance_find(BACKEND_NAME, instance_id.label);
					if(inst && sacn_process_frame(inst, frame, data)){
						LOG("Failed to process frame");
					}
				}
			}
		} while(bytes_read > 0);

		#ifdef _WIN32
		if(bytes_read < 0 && WSAGetLastError() != WSAEWOULDBLOCK){
		#else
		if(bytes_read < 0 && errno != EAGAIN){
		#endif
			LOGPF("Failed to receive data: %s", strerror(errno));
		}

		if(bytes_read == 0){
			LOG("Listener closed");
			return 1;
		}
	}

	return 0;
}

static int sacn_start(size_t n, instance** inst){
	size_t u, p;
	int rv = 1;
	sacn_instance_data* data = NULL;
	sacn_instance_id id = {
		.label = 0
	};
	struct ip_mreq mcast_req = {
		.imr_interface = { INADDR_ANY }
	};
	struct sockaddr_in* dest_v4 = NULL;

	if(!global_cfg.fds){
		LOG("Failed to start, no descriptors bound");
		free(inst);
		return 1;
	}

	//update instance identifiers, join multicast groups
	for(u = 0; u < n; u++){
		data = (sacn_instance_data*) inst[u]->impl;
		id.fields.fd_index = data->fd_index;
		id.fields.uni = data->uni;
		inst[u]->ident = id.label;

		if(!data->uni){
			LOGPF("Please specify a universe on instance %s", inst[u]->name);
			goto bail;
		}

		//find duplicates
		for(p = 0; p < u; p++){
			if(inst[u]->ident == inst[p]->ident){
				LOGPF("Colliding instances, use one: %s - %s", inst[u]->name, inst[p]->name);
				goto bail;
			}
		}

		if(!data->unicast_input){
			mcast_req.imr_multiaddr.s_addr = htobe32(((uint32_t) 0xefff0000) | ((uint32_t) data->uni));
			if(setsockopt(global_cfg.fd[data->fd_index].fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (uint8_t*) &mcast_req, sizeof(mcast_req))){
				LOGPF("Failed to join Multicast group for universe %u on instance %s: %s", data->uni, inst[u]->name, strerror(errno));
			}
		}

		if(data->xmit_prio){
			//add to list of advertised universes for this fd
			global_cfg.fd[data->fd_index].universe = realloc(global_cfg.fd[data->fd_index].universe, (global_cfg.fd[data->fd_index].universes + 1) * sizeof(uint16_t));
			if(!global_cfg.fd[data->fd_index].universe){
				LOG("Failed to allocate memory");
				goto bail;
			}

			global_cfg.fd[data->fd_index].universe[global_cfg.fd[data->fd_index].universes] = data->uni;
			global_cfg.fd[data->fd_index].universes++;

			//generate multicast destination address if none set
			if(!data->dest_len){
				data->dest_len = sizeof(struct sockaddr_in);
				dest_v4 = (struct sockaddr_in*) (&data->dest_addr);
				dest_v4->sin_family = AF_INET;
				dest_v4->sin_port = htobe16(strtoul(SACN_PORT, NULL, 10));
				dest_v4->sin_addr = mcast_req.imr_multiaddr;
			}
		}
	}

	LOGPF("Registering %" PRIsize_t " descriptors to core", global_cfg.fds);
	for(u = 0; u < global_cfg.fds; u++){
		//allocate memory for storing last frame transmission timestamp
		global_cfg.fd[u].last_frame = calloc(global_cfg.fd[u].universes, sizeof(uint64_t));
		if(!global_cfg.fd[u].last_frame){
			LOG("Failed to allocate memory");
			goto bail;
		}
		if(mm_manage_fd(global_cfg.fd[u].fd, BACKEND_NAME, 1, (void*) u)){
			goto bail;
		}
	}

	rv = 0;
bail:
	return rv;
}

static int sacn_shutdown(size_t n, instance** inst){
	size_t p;

	for(p = 0; p < n; p++){
		free(inst[p]->impl);
	}

	for(p = 0; p < global_cfg.fds; p++){
		close(global_cfg.fd[p].fd);
		free(global_cfg.fd[p].universe);
		free(global_cfg.fd[p].last_frame);
	}
	free(global_cfg.fd);
	LOG("Backend shut down");
	return 0;
}
