#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "libmmbackend.h"
#include "rtpmidi.h"

#define BACKEND_NAME "rtpmidi"

static struct /*_rtpmidi_global*/ {
	int mdns_fd;
	char* mdns_name;
	uint8_t detect;
} cfg = {
	.mdns_fd = -1,
	.mdns_name = NULL,
	.detect = 0
};

MM_PLUGIN_API int init(){
	backend rtpmidi = {
		.name = BACKEND_NAME,
		.conf = rtpmidi_configure,
		.create = rtpmidi_instance,
		.conf_instance = rtpmidi_configure_instance,
		.channel = rtpmidi_channel,
		.handle = rtpmidi_set,
		.process = rtpmidi_handle,
		.start = rtpmidi_start,
		.shutdown = rtpmidi_shutdown
	};

	if(sizeof(rtpmidi_channel_ident) != sizeof(uint64_t)){
		fprintf(stderr, "rtpmidi channel identification union out of bounds\n");
		return 1;
	}

	if(mm_backend_register(rtpmidi)){
		fprintf(stderr, "Failed to register rtpmidi backend\n");
		return 1;
	}

	return 0;
}

static int rtpmidi_configure(char* option, char* value){
	char* host = NULL, *port = NULL;

	if(!strcmp(option, "mdns-name")){
		if(cfg.mdns_name){
			fprintf(stderr, "Duplicate mdns-name assignment\n");
			return 1;
		}

		cfg.mdns_name = strdup(value);
		if(!cfg.mdns_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "mdns-bind")){
		if(cfg.mdns_fd >= 0){
			fprintf(stderr, "Only one mDNS discovery bind is supported\n");
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port);

		if(!host){
			fprintf(stderr, "Not a valid mDNS bind address: %s\n", value);
			return 1;
		}

		cfg.mdns_fd = mmbackend_socket(host, (port ? port : RTPMIDI_MDNS_PORT), SOCK_DGRAM, 1, 1);
		if(cfg.mdns_fd < 0){
			fprintf(stderr, "Failed to bind mDNS interface: %s\n", value);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "detect")){
		cfg.detect = 0;
		if(!strcmp(value, "on")){
			cfg.detect = 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown rtpmidi backend option %s\n", option);
	return 1;
}

static int rtpmidi_bind_instance(rtpmidi_instance_data* data, char* host, char* port){
	struct sockaddr_storage sock_addr = {
		0
	};
	socklen_t sock_len = sizeof(sock_addr);
	char control_port[32];

	//bind to random port if none supplied
	data->fd = mmbackend_socket(host, port ? port : "0", SOCK_DGRAM, 1, 0);
	if(data->fd < 0){
		return 1;
	}

	//bind control port
	if(data->mode == apple){
		if(getsockname(data->fd, (struct sockaddr*) &sock_addr, &sock_len)){
			fprintf(stderr, "Failed to fetch data port information: %s\n", strerror(errno));
			return 1;
		}

		snprintf(control_port, sizeof(control_port), "%d", be16toh(((struct sockaddr_in*)&sock_addr)->sin_port) - 1);
		data->control_fd = mmbackend_socket(host, control_port, SOCK_DGRAM, 1, 0);
		if(data->control_fd < 0){
			fprintf(stderr, "Failed to bind control port %s\n", control_port);
			return 1;
		}
	}

	return 0;
}

static int rtpmidi_push_peer(rtpmidi_instance_data* data, struct sockaddr_storage sock_addr, socklen_t sock_len){
	size_t u;

	for(u = 0; u < data->peers; u++){
		//check whether the peer is already in the list
		if(sock_len == data->peer[u].dest_len && !memcmp(&data->peer[u].dest, &sock_addr, sock_len)){
			return 0;
		}
	}

	data->peer = realloc(data->peer, (data->peers + 1) * sizeof(rtpmidi_peer));
	if(!data->peer){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	data->peer[data->peers].dest = sock_addr;
	data->peer[data->peers].dest_len = sock_len;

	data->peers++;
	return 0;
}

static int rtpmidi_configure_instance(instance* inst, char* option, char* value){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	char* host = NULL, *port = NULL;
	struct sockaddr_storage sock_addr;
	socklen_t sock_len = sizeof(sock_addr);

	if(!strcmp(option, "mode")){
		if(!strcmp(value, "direct")){
			data->mode = direct;
			return 0;
		}
		else if(!strcmp(value, "apple")){
			data->mode = apple;
			return 0;
		}
		fprintf(stderr, "Unknown rtpmidi instance mode %s for instance %s\n", value, inst->name);
		return 1;
	}
	else if(!strcmp(option, "ssrc")){
		data->ssrc = strtoul(value, NULL, 0);
		if(!data->ssrc){
			fprintf(stderr, "Random SSRC will be generated for rtpmidi instance %s\n", inst->name);
		}
		return 0;
	}
	else if(!strcmp(option, "bind")){
		if(data->mode == unconfigured){
			fprintf(stderr, "Please specify mode for instance %s before setting bind host\n", inst->name);
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port);

		if(!host){
			fprintf(stderr, "Could not parse bind host specification %s for instance %s\n", value, inst->name);
			return 1;
		}

		return rtpmidi_bind_instance(data, host, port);
	}
	else if(!strcmp(option, "learn")){
		if(data->mode != direct){
			fprintf(stderr, "The rtpmidi 'learn' option is only valid for direct mode instances\n");
			return 1;
		}
		data->learn_peers = 0;
		if(!strcmp(value, "true")){
			data->learn_peers = 1;
		}
		return 0;
	}
	else if(!strcmp(option, "peer")){
		mmbackend_parse_hostspec(value, &host, &port);
		if(!host || !port){
			fprintf(stderr, "Invalid peer %s configured on rtpmidi instance %s\n", value, inst->name);
			return 1;
		}

		if(mmbackend_parse_sockaddr(host, port, &sock_addr, &sock_len)){
			fprintf(stderr, "Failed to resolve peer %s configured on rtpmidi instance %s\n", value, inst->name);
			return 1;
		}

		return rtpmidi_push_peer(data, sock_addr, sock_len);
	}
	else if(!strcmp(option, "session")){
		if(data->mode != apple){
			fprintf(stderr, "The rtpmidi 'session' option is only valid for apple mode instances\n");
			return 1;
		}
		free(data->session_name);
		data->session_name = strdup(value);
		if(!data->session_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "invite")){
		if(data->mode != apple){
			fprintf(stderr, "The rtpmidi 'invite' option is only valid for apple mode instances\n");
			return 1;
		}
		free(data->invite_peers);
		data->invite_peers = strdup(value);
		if(!data->invite_peers){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "join")){
		if(data->mode != apple){
			fprintf(stderr, "The rtpmidi 'join' option is only valid for apple mode instances\n");
			return 1;
		}
		free(data->invite_accept);
		data->invite_accept = strdup(value);
		if(!data->invite_accept){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown rtpmidi instance option %s\n", option);
	return 1;
}

static instance* rtpmidi_instance(){
	rtpmidi_instance_data* data = NULL;
	instance* inst = mm_instance();

	if(!inst){
		return NULL;
	}

	data = calloc(1, sizeof(rtpmidi_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}
	data->fd = -1;
	data->control_fd = -1;

	inst->impl = data;
	return inst;
}

static channel* rtpmidi_channel(instance* inst, char* spec, uint8_t flags){
	char* next_token = spec;
	rtpmidi_channel_ident ident = {
		.label = 0
	};

	if(!strncmp(spec, "ch", 2)){
		next_token += 2;
		if(!strncmp(spec, "channel", 7)){
			next_token = spec + 7;
		}
	}
	else{
		fprintf(stderr, "Invalid rtpmidi channel specification %s\n", spec);
		return NULL;
	}

	ident.fields.channel = strtoul(next_token, &next_token, 10);
	if(ident.fields.channel > 15){
		fprintf(stderr, "rtpmidi channel out of range in channel spec %s\n", spec);
		return NULL;
	}

	if(*next_token != '.'){
		fprintf(stderr, "rtpmidi channel specification %s does not conform to channel<X>.<control><Y>\n", spec);
		return NULL;
	}

	next_token++;

	if(!strncmp(next_token, "cc", 2)){
		ident.fields.type = cc;
		next_token += 2;
	}
	else if(!strncmp(next_token, "note", 4)){
		ident.fields.type = note;
		next_token += 4;
	}
	else if(!strncmp(next_token, "pressure", 8)){
		ident.fields.type = pressure;
		next_token += 8;
	}
	else if(!strncmp(next_token, "pitch", 5)){
		ident.fields.type = pitchbend;
	}
	else if(!strncmp(next_token, "aftertouch", 10)){
		ident.fields.type = aftertouch;
	}
	else{
		fprintf(stderr, "Unknown rtpmidi channel control type in spec %s\n", spec);
		return NULL;
	}

	ident.fields.control = strtoul(next_token, NULL, 10);

	if(ident.label){
		return mm_channel(inst, ident.label, 1);
	}
	return NULL;
}

static int rtpmidi_set(instance* inst, size_t num, channel** c, channel_value* v){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	uint8_t frame[RTPMIDI_PACKET_BUFFER] = "";
	rtpmidi_header* rtp_header = (rtpmidi_header*) frame;
	rtpmidi_command_header* command_header = (rtpmidi_command_header*) (frame + sizeof(rtpmidi_header));
	size_t offset = sizeof(rtpmidi_header) + sizeof(rtpmidi_command_header), u = 0;
	uint8_t* payload = frame + offset;
	rtpmidi_channel_ident ident;

	rtp_header->vpxcc = RTPMIDI_HEADER_MAGIC;
	//some receivers seem to have problems reading rfcs and interpreting the marker bit correctly
	rtp_header->mpt = (data->mode == apple ? 0 : 0x80) | RTPMIDI_HEADER_TYPE;
	rtp_header->sequence = htobe16(data->sequence++);
	rtp_header->timestamp = 0; //TODO calculate appropriate timestamps
	rtp_header->ssrc = htobe32(data->ssrc);

	//midi command section header
	//TODO enable the journal bit here
	command_header->flags = 0xA0; //extended length header, first entry in list has dtime

	//midi list
	for(u = 0; u < num; u++){
		ident.label = c[u]->ident;

		//encode timestamp
		payload[0] = 0;

		//encode midi command
		payload[1] = ident.fields.type | ident.fields.channel;
		payload[2] = ident.fields.control;
		payload[3] = v[u].normalised * 127.0;

		if(ident.fields.type == pitchbend){
			payload[2] = ((int)(v[u].normalised * 16384.0)) & 0x7F;
			payload[3] = (((int)(v[u].normalised * 16384.0)) >> 7) & 0x7F;
		}
		//channel-wide aftertouch is only 2 bytes
		else if(ident.fields.type == aftertouch){
			payload[2] = payload[3];
			payload -= 1;
			offset -= 1;
		}

		payload += 4;
		offset += 4;
	}

	//update command section length
	//FIXME this might overrun, might check the number of events at some point
	command_header->flags |= (((offset - sizeof(rtpmidi_header) - sizeof(rtpmidi_command_header)) & 0x0F00) >> 8);
	command_header->length = ((offset - sizeof(rtpmidi_header) - sizeof(rtpmidi_command_header)) & 0xFF);

	//TODO journal section

	for(u = 0; u < data->peers; u++){
		sendto(data->fd, frame, offset, 0, (struct sockaddr*) &data->peer[u].dest, data->peer[u].dest_len);
	}

	return 0;
}

static int rtpmidi_handle_applemidi(instance* inst, int fd, uint8_t* frame, size_t bytes, struct sockaddr_storage* peer, socklen_t peer_len){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	apple_command* command = (apple_command*) frame;
	size_t u;

	//find peer if already in list
	for(u = 0; u < data->peers; u++){
		if(data->peer[u].dest_len == peer_len
				&& !memcmp(&data->peer[u].dest, peer, peer_len)){
			break;
		}
	}

	if(!strncmp((char*) command->command, APPLEMIDI_INVITE, 2)){
		//TODO check whether the session is in the accept list
	}
	else if(!strncmp((char*) command->command, APPLEMIDI_ACCEPT, 2)){
		//TODO mark peer as in-session, start timesync
	}
	else if(!strncmp((char*) command->command, APPLEMIDI_REJECT, 2)){
		//TODO mark peer as rejected (or retry invitation)
	}
	else if(!strncmp((char*) command->command, APPLEMIDI_LEAVE, 2)){
		//TODO mark peer as disconnected, retry invitation
	}
	else if(!strncmp((char*) command->command, APPLEMIDI_SYNC, 2)){
		//TODO respond with sync answer
	}
	else if(!strncmp((char*) command->command, APPLEMIDI_FEEDBACK, 2)){
		//ignore
	}
	else{
		fprintf(stderr, "Unknown AppleMIDI session command %02X %02X\n", command->command[0], command->command[1]);
	}

	return 0;
}

static int rtpmidi_handle_data(instance* inst){
	size_t u;
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	uint8_t frame[RTPMIDI_PACKET_BUFFER] = "";
	struct sockaddr_storage sock_addr;
	socklen_t sock_len = sizeof(sock_addr);
	rtpmidi_header* rtp_header = (rtpmidi_header*) frame;
	ssize_t bytes_recv = recvfrom(data->fd, frame, sizeof(frame), 0, (struct sockaddr*) &sock_addr, &sock_len);

	//TODO receive until EAGAIN
	if(bytes_recv < 0){
		fprintf(stderr, "rtpmidi failed to receive for instance %s\n", inst->name);
		return 1;
	}

	if(bytes_recv < sizeof(rtpmidi_header)){
		fprintf(stderr, "Skipping short packet on rtpmidi instance %s\n", inst->name);
		return 0;
	}

	//FIXME might want to filter data input from sources that are not registered peers
	if(data->mode == apple && rtp_header->vpxcc == 0xFF && rtp_header->mpt == 0xFF){
		return rtpmidi_handle_applemidi(inst, data->fd, frame, bytes_recv, &sock_addr, sock_len);
	}
	else if(rtp_header->vpxcc != RTPMIDI_HEADER_MAGIC || RTPMIDI_GET_TYPE(rtp_header->mpt) != RTPMIDI_HEADER_TYPE){
		fprintf(stderr, "rtpmidi instance %s received frame with invalid header magic\n", inst->name);
		return 0;
	}

	//TODO parse data

	//try to learn peers
	if(data->learn_peers){
		for(u = 0; u < data->peers; u++){
			if(data->peer[u].dest_len == sock_len
					&& !memcmp(&data->peer[u].dest, &sock_addr, sock_len)){
				break;
			}
		}

		if(u == data->peers){
			fprintf(stderr, "rtpmidi instance %s learned new peer\n", inst->name);
			return rtpmidi_push_peer(data, sock_addr, sock_len);
		}
	}
	return 0;
}

static int rtpmidi_handle_control(instance* inst){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	uint8_t frame[RTPMIDI_PACKET_BUFFER] = "";
	struct sockaddr_storage sock_addr;
	socklen_t sock_len = sizeof(sock_addr);
	ssize_t bytes_recv = recvfrom(data->control_fd, frame, sizeof(frame), 0, (struct sockaddr*) &sock_addr, &sock_len);

	if(bytes_recv < 0){
		fprintf(stderr, "rtpmidi failed to receive for instance %s\n", inst->name);
		return 1;
	}

	//the shortest applemidi packet is still larger than the rtpmidi header, so use that as bar
	if(bytes_recv < sizeof(rtpmidi_header)){
		fprintf(stderr, "Skipping short packet on rtpmidi instance %s\n", inst->name);
		return 0;
	}

	if(data->mode == apple && frame[0] == 0xFF && frame[1] == 0xFF){
		return rtpmidi_handle_applemidi(inst, data->control_fd, frame, bytes_recv, &sock_addr, sock_len);
	}

	fprintf(stderr, "Unknown session protocol frame received on rtpmidi instance %s\n", inst->name);
	return 0;
}

static int rtpmidi_handle(size_t num, managed_fd* fds){
	size_t u;
	int rv = 0;
	instance* inst = NULL;
	rtpmidi_instance_data* data = NULL;

	//TODO handle mDNS discovery frames

	if(!num){
		return 0;
	}

	for(u = 0; u < num; u++){
		if(!fds[u].impl){
			//TODO handle mDNS discovery input
		}
		else{
			//handle rtp/control input
			inst = (instance*) fds[u].impl;
			data = (rtpmidi_instance_data*) inst->impl;
			if(fds[u].fd == data->fd){
				rv |= rtpmidi_handle_data(inst);
			}
			else if(fds[u].fd == data->control_fd){
				rv |=  rtpmidi_handle_control(inst);
			}
			else{
				fprintf(stderr, "rtpmidi signaled descriptor not recognized\n");
			}
		}
	}

	return rv;
}

static int rtpmidi_start(size_t n, instance** inst){
	size_t u, fds = 0;
	rtpmidi_instance_data* data = NULL;

	//if mdns name defined and no socket, bind default values
	if(cfg.mdns_name && cfg.mdns_fd < 0){
		cfg.mdns_fd = mmbackend_socket(RTPMIDI_DEFAULT_HOST, RTPMIDI_MDNS_PORT, SOCK_DGRAM, 1, 1);
		if(cfg.mdns_fd < 0){
			return 1;
		}
	}

	//register mdns fd to core
	if(cfg.mdns_fd >= 0){
		if(mm_manage_fd(cfg.mdns_fd, BACKEND_NAME, 1, NULL)){
			fprintf(stderr, "rtpmidi failed to register mDNS socket with core\n");
			return 1;
		}
		fds++;
	}
	else{
		fprintf(stderr, "No mDNS discovery interface bound, AppleMIDI session discovery disabled\n");
	}

	for(u = 0; u < n; u++){
		data = (rtpmidi_instance_data*) inst[u]->impl;
		//check whether instances are explicitly configured to a mode
		if(data->mode == unconfigured){
			fprintf(stderr, "rtpmidi instance %s is missing a mode configuration\n", inst[u]->name);
			return 1;
		}

		//generate random ssrc's
		if(!data->ssrc){
			data->ssrc = rand() << 16 | rand();
		}

		//if not bound, bind to default
		if(data->fd < 0 && rtpmidi_bind_instance(data, RTPMIDI_DEFAULT_HOST, NULL)){
			fprintf(stderr, "Failed to bind default sockets for rtpmidi instance %s\n", inst[u]->name);
			return 1;
		}

		//register fds to core
		if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u]) || (data->control_fd >= 0 && mm_manage_fd(data->control_fd, BACKEND_NAME, 1, inst[u]))){
			fprintf(stderr, "rtpmidi failed to register instance socket with core\n");
			return 1;
		}
		fds += (data->control_fd >= 0) ? 2 : 1;
	}

	fprintf(stderr, "rtpmidi backend registered %" PRIsize_t " descriptors to core\n", fds);
	return 0;
}

static int rtpmidi_shutdown(size_t n, instance** inst){
	rtpmidi_instance_data* data = NULL;
	size_t u;

	for(u = 0; u < n; u++){
		data = (rtpmidi_instance_data*) inst[u]->impl;
		if(data->fd >= 0){
			close(data->fd);
		}

		if(data->control_fd >= 0){
			close(data->control_fd);
		}

		free(data->session_name);
		data->session_name = NULL;

		free(data->invite_peers);
		data->invite_peers = NULL;

		free(data->invite_accept);
		data->invite_accept = NULL;

		free(data->peer);
		data->peer = NULL;
		data->peers = 0;

		free(inst[u]->impl);
		inst[u]->impl = NULL;
	}

	free(cfg.mdns_name);
	if(cfg.mdns_fd >= 0){
		close(cfg.mdns_fd);
	}

	return 0;
}
