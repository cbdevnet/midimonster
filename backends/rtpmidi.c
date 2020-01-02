#define BACKEND_NAME "rtpmidi"
#define DEBUG

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "libmmbackend.h"
#include "rtpmidi.h"

//TODO learn peer ssrcs
//TODO default session join?
//TODO default mode?
//TODO internal loop mode

static struct /*_rtpmidi_global*/ {
	int mdns_fd;
	char* mdns_name;
	uint8_t detect;
	uint64_t last_service;

	size_t announces;
	rtpmidi_announce* announce;
} cfg = {
	.mdns_fd = -1,
	.mdns_name = NULL,
	.detect = 0,
	.last_service = 0,

	.announces = 0,
	.announce = NULL
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
		LOG("Channel identification union out of bounds");
		return 1;
	}

	if(mm_backend_register(rtpmidi)){
		LOG("Failed to register backend");
		return 1;
	}

	return 0;
}

static int rtpmidi_configure(char* option, char* value){
	char* host = NULL, *port = NULL;

	if(!strcmp(option, "mdns-name")){
		if(cfg.mdns_name){
			LOG("Duplicate mdns-name assignment");
			return 1;
		}

		cfg.mdns_name = strdup(value);
		if(!cfg.mdns_name){
			LOG("Failed to allocate memory");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "mdns-bind")){
		if(cfg.mdns_fd >= 0){
			LOG( "Only one mDNS discovery bind is supported");
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port, NULL);

		if(!host){
			LOGPF("Not a valid mDNS bind address: %s", value);
			return 1;
		}

		cfg.mdns_fd = mmbackend_socket(host, (port ? port : RTPMIDI_MDNS_PORT), SOCK_DGRAM, 1, 1);
		if(cfg.mdns_fd < 0){
			LOGPF("Failed to bind mDNS interface: %s", value);
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

	LOGPF("Unknown backend configuration option %s", option);
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
			LOGPF("Failed to fetch data port information: %s", strerror(errno));
			return 1;
		}

		snprintf(control_port, sizeof(control_port), "%d", be16toh(((struct sockaddr_in*)&sock_addr)->sin_port) - 1);
		data->control_fd = mmbackend_socket(host, control_port, SOCK_DGRAM, 1, 0);
		if(data->control_fd < 0){
			LOGPF("Failed to bind control port %s", control_port);
			return 1;
		}
	}

	return 0;
}

static char* rtpmidi_type_name(uint8_t type){
	switch(type){
		case note:
			return "note";
		case cc:
			return "cc";
		case pressure:
			return "pressure";
		case aftertouch:
			return "aftertouch";
		case pitchbend:
			return "pitch";
	}
	return "unknown";
}

static int rtpmidi_push_peer(rtpmidi_instance_data* data, struct sockaddr_storage sock_addr, socklen_t sock_len, uint8_t learned, uint8_t connected){
	size_t u, p = data->peers;

	for(u = 0; u < data->peers; u++){
		//check whether the peer is already in the list
		if(data->peer[u].active
				&& sock_len == data->peer[u].dest_len
				&& !memcmp(&data->peer[u].dest, &sock_addr, sock_len)){
			//if yes, update connection flag (but not learned flag because that doesn't change)
			data->peer[u].connected = connected;
			return 0;
		}

		if(!data->peer[u].active){
			p = u;
		}
	}

	if(p == data->peers){
		data->peer = realloc(data->peer, (data->peers + 1) * sizeof(rtpmidi_peer));
		if(!data->peer){
			LOG("Failed to allocate memory");
			data->peers = 0;
			return 1;
		}
		data->peers++;
		DBGPF("Extending peer registry to %" PRIsize_t " entries", data->peers);
	}

	data->peer[p].active = 1;
	data->peer[p].learned = learned;
	data->peer[p].connected = connected;
	data->peer[p].dest = sock_addr;
	data->peer[p].dest_len = sock_len;
	return 0;
}

static int rtpmidi_push_invite(instance* inst, char* peer){
	size_t u, p;

	//check whether the instance is already in the announce list
	for(u = 0; u < cfg.announces; u++){
		if(cfg.announce[u].inst == inst){
			break;
		}
	}

	//add to the announce list
	if(u == cfg.announces){
		cfg.announce = realloc(cfg.announce, (cfg.announces + 1) * sizeof(rtpmidi_announce));
		if(!cfg.announce){
			LOG("Failed to allocate memory");
			cfg.announces = 0;
			return 1;
		}

		cfg.announce[u].inst = inst;
		cfg.announce[u].invites = 0;
		cfg.announce[u].invite = NULL;

		cfg.announces++;
	}

	//check whether the peer is already in the invite list
	for(p = 0; p < cfg.announce[u].invites; p++){
		if(!strcmp(cfg.announce[u].invite[p], peer)){
			return 0;
		}
	}

	//extend the invite list
	cfg.announce[u].invite = realloc(cfg.announce[u].invite, (cfg.announce[u].invites + 1) * sizeof(char*));
	if(!cfg.announce[u].invite){
		LOG("Failed to allocate memory");
		cfg.announce[u].invites = 0;
		return 1;
	}

	//append the new invitee
	cfg.announce[u].invite[p] = strdup(peer);
	if(!cfg.announce[u].invite[p]){
		LOG("Failed to allocate memory");
		return 1;
	}

	cfg.announce[u].invites++;
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
		LOGPF("Unknown instance mode %s for instance %s", value, inst->name);
		return 1;
	}
	else if(!strcmp(option, "ssrc")){
		data->ssrc = strtoul(value, NULL, 0);
		if(!data->ssrc){
			LOGPF("Random SSRC will be generated for instance %s", inst->name);
		}
		return 0;
	}
	else if(!strcmp(option, "bind")){
		if(data->mode == unconfigured){
			LOGPF("Please specify mode for instance %s before setting bind host", inst->name);
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port, NULL);

		if(!host){
			LOGPF("Could not parse bind host specification %s for instance %s", value, inst->name);
			return 1;
		}

		return rtpmidi_bind_instance(data, host, port);
	}
	else if(!strcmp(option, "learn")){
		if(data->mode != direct){
			LOG("'learn' option is only valid for direct mode instances");
			return 1;
		}
		data->learn_peers = 0;
		if(!strcmp(value, "true")){
			data->learn_peers = 1;
		}
		return 0;
	}
	else if(!strcmp(option, "peer")){
		if(data->mode == unconfigured){
			LOGPF("Please specify mode for instance %s before configuring peers", inst->name);
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port, NULL);
		if(!host || !port){
			LOGPF("Invalid peer %s configured on instance %s", value, inst->name);
			return 1;
		}

		if(mmbackend_parse_sockaddr(host, port, &sock_addr, &sock_len)){
			LOGPF("Failed to resolve peer %s on instance %s", value, inst->name);
			return 1;
		}

		//apple peers are specified using the control port, but we want to store the data port as peer
		if(data->mode == apple){
			((struct sockaddr_in*) &sock_addr)->sin_port = be16toh(htobe16(((struct sockaddr_in*) &sock_addr)->sin_port) + 1);
		}

		return rtpmidi_push_peer(data, sock_addr, sock_len, 0, 0);
	}
	else if(!strcmp(option, "session")){
		if(data->mode != apple){
			LOG("'session' option is only valid for apple mode instances");
			return 1;
		}
		free(data->session_name);
		data->session_name = strdup(value);
		if(!data->session_name){
			LOG("Failed to allocate memory");
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "invite")){
		if(data->mode != apple){
			LOG("'invite' option is only valid for apple mode instances");
			return 1;
		}

		return rtpmidi_push_invite(inst, value);
	}
	else if(!strcmp(option, "join")){
		if(data->mode != apple){
			LOG("'join' option is only valid for apple mode instances");
			return 1;
		}
		free(data->accept);
		data->accept = strdup(value);
		if(!data->accept){
			LOG("Failed to allocate memory");
			return 1;
		}
		return 0;
	}

	LOGPF("Unknown instance configuration option %s on instance %s", option, inst->name);
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
		LOG("Failed to allocate memory");
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
		LOGPF("Invalid channel specification %s", spec);
		return NULL;
	}

	ident.fields.channel = strtoul(next_token, &next_token, 10);
	if(ident.fields.channel > 15){
		LOGPF("Channel out of range in channel spec %s", spec);
		return NULL;
	}

	if(*next_token != '.'){
		LOGPF("Channel specification %s does not conform to channel<X>.<control><Y>", spec);
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
		LOGPF("Unknown control type in spec %s", spec);
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
	rtp_header->timestamp = mm_timestamp() * 10; //just assume 100msec resolution because rfc4695 handwaves it
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
		if(data->peer[u].active && data->peer[u].connected){
			sendto(data->fd, frame, offset, 0, (struct sockaddr*) &data->peer[u].dest, data->peer[u].dest_len);
		}
	}

	return 0;
}

static int rtpmidi_handle_applemidi(instance* inst, int fd, uint8_t* frame, size_t bytes, struct sockaddr_storage* peer, socklen_t peer_len){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	uint8_t response[RTPMIDI_PACKET_BUFFER] = "";
	apple_command* command = (apple_command*) frame;
	char* session_name = (char*) frame + sizeof(apple_command);
	size_t n, u;

	command->command = be16toh(command->command);

	//check command version (except for clock sync and receiver feedback)
	if(command->command != apple_sync && command->command != apple_feedback
			&& be32toh(command->version) != 2){
		LOGPF("Invalid AppleMIDI command version %" PRIu32 " on instance %s", be32toh(command->version), inst->name);
		return 0;
	}

	if(command->command == apple_invite){
		//check session name
		for(n = sizeof(apple_command); n < bytes; n++){
			if(!frame[n]){
				break;
			}

			if(!isprint(frame[n])){
				session_name = NULL;
				break;
			}
		}

		//unterminated string
		if(n == bytes){
			session_name = NULL;
		}

		//FIXME if already in session, reject the invitation
		if(data->accept &&
				(!strcmp(data->accept, "*") || (session_name && !strcmp(session_name, data->accept)))){
			//accept the invitation
			LOGPF("Instance %s accepting invitation to session %s%s", inst->name, session_name ? session_name : "UNNAMED", (fd == data->control_fd) ? " (control)":"");
			//send accept message
			apple_command* accept = (apple_command*) response;
			accept->res1 = 0xFFFF;
			accept->command = htobe16(apple_accept);
			accept->version = htobe32(2);
			accept->token = command->token;
			accept->ssrc = htobe32(data->ssrc);
			//add local name to response
			//FIXME might want to use the session name in case it is set
			memcpy(response + sizeof(apple_command), cfg.mdns_name ? cfg.mdns_name : RTPMIDI_DEFAULT_NAME, strlen((cfg.mdns_name ? cfg.mdns_name : RTPMIDI_DEFAULT_NAME)) + 1);
			sendto(fd, response, sizeof(apple_command) + strlen(cfg.mdns_name ? cfg.mdns_name : RTPMIDI_DEFAULT_NAME) + 1, 0, (struct sockaddr*) peer, peer_len);

			//push peer
			if(fd != data->control_fd){
				return rtpmidi_push_peer(data, *peer, peer_len, 1, 1);
			}
			return 0;
		}
		else{
			//send reject message
			LOGPF("Instance %s rejecting invitation to session %s", inst->name, session_name ? session_name : "UNNAMED");
			apple_command reject = {
				.res1 = 0xFFFF,
				.command = htobe16(apple_reject),
				.version = htobe32(2),
				.token = command->token,
				.ssrc = htobe32(data->ssrc)
			};
			sendto(fd, (uint8_t*) &reject, sizeof(apple_command), 0, (struct sockaddr*) peer, peer_len);
		}
		return 0;
	}
	else if(command->command == apple_accept){
		if(fd != data->control_fd){
			LOGPF("Instance %s negotiated new peer", inst->name);
			return rtpmidi_push_peer(data, *peer, peer_len, 1, 1);
			//FIXME store ssrc, start timesync
		}
		else{
			//send invite on data fd
			LOGPF("Instance %s peer accepted on control port, inviting data port", inst->name);
			//FIXME limit max length of session name
			apple_command* invite = (apple_command*) response;
			invite->res1 = 0xFFFF;
			invite->command = htobe16(apple_invite);
			invite->version = htobe32(2);
			invite->token = command->token;
			invite->ssrc = htobe32(data->ssrc);
			memcpy(response + sizeof(apple_command), data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME, strlen((data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME)) + 1);
			//calculate data port
			((struct sockaddr_in*) peer)->sin_port = be16toh(htobe16(((struct sockaddr_in*) peer)->sin_port) + 1);
			sendto(data->fd, response, sizeof(apple_command) + strlen(data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME) + 1, 0, (struct sockaddr*) peer, peer_len);
		}
		return 0;
	}
	else if(command->command == apple_reject){
		//just ignore this for now and retry the invitation
	}
	else if(command->command == apple_leave){
		//remove peer from list - this comes in on the control port, but we need to remove the data port...
		((struct sockaddr_in*) peer)->sin_port = be16toh(htobe16(((struct sockaddr_in*) peer)->sin_port) + 1);
		for(u = 0; u < data->peers; u++){
			if(data->peer[u].dest_len == peer_len
					&& !memcmp(&data->peer[u].dest, peer, peer_len)){
				LOGPF("Instance %s removed peer", inst->name);
				//learned peers are marked inactive, configured peers are marked unconnected
				if(data->peer[u].learned){
					data->peer[u].active = 0;
				}
				else{
					data->peer[u].connected = 0;
				}
			}
		}
		return 0;
	}
	else if(command->command == apple_sync){
		//respond with sync answer
		memcpy(response, frame, bytes);
		apple_sync_frame* sync = (apple_sync_frame*) response;
		DBGPF("Incoming sync on instance %s (%d)", inst->name, sync->count);
		sync->command = htobe16(apple_sync);
		sync->ssrc = htobe32(data->ssrc);
		switch(sync->count){
			case 0:
				//this happens if we're a participant
				sync->count++;
				sync->timestamp[1] = htobe64(mm_timestamp() * 10);
				break;
			case 1:
				//this happens if we're an initiator
				sync->count++;
				sync->timestamp[2] = htobe64(mm_timestamp() * 10);
				break;
			default:
				//ignore this one
				return 0;
		}

		sendto(fd, response, sizeof(apple_sync_frame), 0, (struct sockaddr*) peer, peer_len);
		return 0;
	}
	else if(command->command == apple_feedback){
		//TODO store this somewhere to properly update the recovery journal
		LOGPF("Feedback on instance %s", inst->name);
		return 0;
	}
	else{
		LOGPF("Unknown AppleMIDI session command %04X", command->command);
	}

	return 0;
}

static int rtpmidi_parse(instance* inst, uint8_t* frame, size_t bytes){
	uint16_t length = 0;
	size_t offset = 1, decode_time = 0, command_bytes = 0;
	uint8_t midi_status = 0;
	rtpmidi_channel_ident ident;
	channel_value val;
	channel* chan = NULL;

	if(!bytes){
		LOGPF("No command section in data on instance %s", inst->name);
		return 1;
	}

	//calculate midi command section length
	length = frame[0] & 0x0F;
	if(frame[0] & 0x80){
		//extended header
		if(bytes < 2){
			LOGPF("Short command section (%" PRIsize_t " bytes) on %s, missing extended header", bytes, inst->name);
			return 1;
		}
		length <<= 8;
		length |= frame[1];
		offset = 2;
	}

	command_bytes = offset + length;
	DBGPF("%u/%" PRIsize_t " bytes of command section on %s, %s header, %s initial dtime",
			length, bytes, inst->name,
			(frame[0] & 0x80) ? "extended" : "normal",
			(frame[0] & 0x20) ? "has" : "no");

	if(command_bytes > bytes){
		LOGPF("Short command section on %s, indicated %" PRIsize_t ", had %" PRIsize_t, inst->name, command_bytes, bytes);
		return 1;
	}

	if(frame[0] & 0x20){
		decode_time = 1;
	}

	do{
		//decode (and ignore) delta-time
		if(decode_time){
			for(; offset < command_bytes && frame[offset] & 0x80; offset++){
			}
			offset++;
		}

		//section 3 of rfc6295 states that the first dtime as well as the last command may be omitted
		//this may make sense on a low-speed serial line, but on a network... come on.
		if(offset >= command_bytes){
			break;
		}

		//check for a status byte
		//TODO filter sysex
		if(frame[offset] & 0x80){
			midi_status = frame[offset];
			offset++;
		}

		//having variable encoding in each and every component is super annoying to check for...
		if(offset >= command_bytes){
			break;
		}

		ident.label = 0;
		ident.fields.type = midi_status & 0xF0;
		ident.fields.channel = midi_status & 0x0F;

		//single byte command
		if(ident.fields.type == aftertouch){
			ident.fields.control = 0;
			val.normalised = (double) frame[offset] / 127.0;
			offset++;
		}
		//two-byte command
		else{
			offset++;
			if(offset >= command_bytes){
				break;
			}

			if(ident.fields.type == pitchbend){
				ident.fields.control = 0;
				val.normalised = (double)((frame[offset] << 7) | frame[offset - 1]) / 16384.0;
			}
			else{
				ident.fields.control = frame[offset - 1];
				val.normalised = (double) frame[offset] / 127.0;
			}

			//fix-up note off events
			if(ident.fields.type == 0x80){
				ident.fields.type = note;
				val.normalised = 0;
			}

			offset++;
		}

		DBGPF("Decoded command type %02X channel %d control %d value %f",
				ident.fields.type, ident.fields.channel, ident.fields.control, val.normalised);

		if(cfg.detect){
			if(ident.fields.type == pitchbend || ident.fields.type == aftertouch){
				LOGPF("Incoming data on channel %s.ch%d.%s, value %f",
						inst->name, ident.fields.channel,
						rtpmidi_type_name(ident.fields.type), val.normalised);
			}
			else{
				LOGPF("Incoming data on channel %s.ch%d.%s%d, value %f",
						inst->name, ident.fields.channel,
						rtpmidi_type_name(ident.fields.type),
						ident.fields.control, val.normalised);
			}
		}

		//push event
		chan = mm_channel(inst, ident.label, 0);
		if(chan){
			mm_channel_event(chan, val);
		}

		decode_time = 1;
	} while(offset < command_bytes);

	return 0;
}

static int rtpmidi_handle_data(instance* inst){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	uint8_t frame[RTPMIDI_PACKET_BUFFER] = "";
	struct sockaddr_storage sock_addr;
	socklen_t sock_len = sizeof(sock_addr);
	rtpmidi_header* rtp_header = (rtpmidi_header*) frame;
	ssize_t bytes_recv = recvfrom(data->fd, frame, sizeof(frame), 0, (struct sockaddr*) &sock_addr, &sock_len);
	size_t u;

	//TODO receive until EAGAIN
	if(bytes_recv < 0){
		LOGPF("Failed to receive for instance %s", inst->name);
		return 1;
	}

	if(bytes_recv < sizeof(rtpmidi_header)){
		LOGPF("Skipping short packet on instance %s", inst->name);
		return 0;
	}

	//FIXME might want to filter data input from sources that are not registered peers
	if(data->mode == apple && rtp_header->vpxcc == 0xFF && rtp_header->mpt == 0xFF){
		return rtpmidi_handle_applemidi(inst, data->fd, frame, bytes_recv, &sock_addr, sock_len);
	}
	else if(rtp_header->vpxcc != RTPMIDI_HEADER_MAGIC || RTPMIDI_GET_TYPE(rtp_header->mpt) != RTPMIDI_HEADER_TYPE){
		LOGPF("Frame with invalid header magic on %s", inst->name);
		return 0;
	}

	//parse data
	if(rtpmidi_parse(inst, frame + sizeof(rtpmidi_header), bytes_recv - sizeof(rtpmidi_header))){
		//returning errors here fails the core loop, so just return 0 to have some logging
		return 0;
	}

	//try to learn peers
	if(data->learn_peers){
		for(u = 0; u < data->peers; u++){
			if(data->peer[u].active
					&& data->peer[u].dest_len == sock_len
					&& !memcmp(&data->peer[u].dest, &sock_addr, sock_len)){
				break;
			}
		}

		if(u == data->peers){
			LOGPF("Learned new peer on %s", inst->name);
			return rtpmidi_push_peer(data, sock_addr, sock_len, 1, 1);
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
		LOGPF("Failed to receive on control socket for instance %s", inst->name);
		return 1;
	}

	//the shortest applemidi packet is still larger than the rtpmidi header, so use that as bar
	if(bytes_recv < sizeof(rtpmidi_header)){
		LOGPF("Skipping short packet on control socket of instance %s", inst->name);
		return 0;
	}

	if(data->mode == apple && frame[0] == 0xFF && frame[1] == 0xFF){
		return rtpmidi_handle_applemidi(inst, data->control_fd, frame, bytes_recv, &sock_addr, sock_len);
	}

	LOGPF("Unknown session protocol frame received on instance %s", inst->name);
	return 0;
}

static int rtpmidi_service(){
	size_t n, u, p;
	instance** inst = NULL;
	rtpmidi_instance_data* data = NULL;
	uint8_t frame[RTPMIDI_PACKET_BUFFER] = "";
	struct sockaddr_storage control_peer;

	//prepare commands
	apple_sync_frame sync = {
		.res1 = 0xFFFF,
		.command = htobe16(apple_sync),
		.ssrc = 0,
		.count = 0,
		.timestamp = {
			mm_timestamp() * 10
		}
	};
	apple_command* invite = (apple_command*) &frame;
	invite->res1 = 0xFFFF;
	invite->command = htobe16(apple_invite);
	invite->version = htobe32(2);
	invite->token = ((uint32_t) rand()) << 16 | rand();

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		LOG("Failed to fetch instances");
		return 1;
	}

	//mdns discovery
	if(cfg.mdns_fd >= 0){
		//TODO send applemidi discovery packets
	}

	for(u = 0; u < n; u++){
		data = (rtpmidi_instance_data*) inst[u]->impl;

		if(data->mode == apple){
			for(p = 0; p < data->peers; p++){
				if(data->peer[p].active && data->peer[p].connected){
					//apple sync
					DBGPF("Instance %s initializing sync on peer %" PRIsize_t, inst[u]->name, p);
					sync.ssrc = htobe32(data->ssrc);
					//calculate remote control port from data port
					memcpy(&control_peer, &(data->peer[u].dest), sizeof(control_peer));
					((struct sockaddr_in*) &control_peer)->sin_port = be16toh(htobe16(((struct sockaddr_in*) &control_peer)->sin_port) - 1);

					sendto(data->control_fd, &sync, sizeof(apple_sync_frame), 0, (struct sockaddr*) &control_peer, data->peer[u].dest_len);
				}
				else if(data->peer[p].active && !data->peer[p].learned && (mm_timestamp() / 1000) % 10 == 0){
					//try to invite pre-defined unconnected applemidi peers
					DBGPF("Instance %s inviting configured peer %" PRIsize_t, inst[u]->name, p);
					invite->ssrc = htobe32(data->ssrc);
					//calculate remote control port from data port
					memcpy(&control_peer, &(data->peer[u].dest), sizeof(control_peer));
					((struct sockaddr_in*) &control_peer)->sin_port = be16toh(htobe16(((struct sockaddr_in*) &control_peer)->sin_port) - 1);
					//append session name to packet
					memcpy(frame + sizeof(apple_command), data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME, strlen((data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME)) + 1);

					sendto(data->control_fd, invite, sizeof(apple_command) + strlen((data->session_name ? data->session_name : RTPMIDI_DEFAULT_NAME)) + 1, 0, (struct sockaddr*) &control_peer, data->peer[u].dest_len);
				}
			}
		}
	}

	free(inst);
	return 0;
}

static int rtpmidi_handle(size_t num, managed_fd* fds){
	size_t u;
	int rv = 0;
	instance* inst = NULL;
	rtpmidi_instance_data* data = NULL;

	//handle service tasks (mdns, clock sync, peer connections)
	if(mm_timestamp() - cfg.last_service > RTPMIDI_SERVICE_INTERVAL){
		DBGPF("Performing service tasks, delta %" PRIu64, mm_timestamp() - cfg.last_service);
		if(rtpmidi_service()){
			return 1;
		}
		cfg.last_service = mm_timestamp();
	}

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
				rv |= rtpmidi_handle_control(inst);
			}
			else{
				LOG("Signaled for unknown descriptor");
			}
		}
	}

	return rv;
}

static int rtpmidi_start(size_t n, instance** inst){
	size_t u, p, fds = 0;
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
			LOG("Failed to register mDNS socket with core");
			return 1;
		}
		fds++;
	}
	else{
		LOG("No mDNS discovery interface bound, AppleMIDI session discovery disabled");
	}

	for(u = 0; u < n; u++){
		data = (rtpmidi_instance_data*) inst[u]->impl;
		//check whether instances are explicitly configured to a mode
		if(data->mode == unconfigured){
			LOGPF("Instance %s is missing a mode configuration", inst[u]->name);
			return 1;
		}

		//generate random ssrc's
		if(!data->ssrc){
			data->ssrc = ((uint32_t) rand()) << 16 | rand();
		}

		//if not bound, bind to default
		if(data->fd < 0 && rtpmidi_bind_instance(data, RTPMIDI_DEFAULT_HOST, NULL)){
			LOGPF("Failed to bind default sockets for instance %s", inst[u]->name);
			return 1;
		}

		//mark configured peers on direct instances as connected so output is sent
		//apple mode instances go through the session negotiation before marking peers as active
		if(data->mode == direct){
			for(p = 0; p < data->peers; p++){
				data->peer[p].connected = 1;
			}
		}

		//register fds to core
		if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u]) || (data->control_fd >= 0 && mm_manage_fd(data->control_fd, BACKEND_NAME, 1, inst[u]))){
			LOGPF("Failed to register descriptor for instance %s with core", inst[u]->name);
			return 1;
		}
		fds += (data->control_fd >= 0) ? 2 : 1;
	}

	LOGPF("Registered %" PRIsize_t " descriptors to core", fds);
	return 0;
}

static int rtpmidi_shutdown(size_t n, instance** inst){
	rtpmidi_instance_data* data = NULL;
	size_t u, p;

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

		free(data->accept);
		data->accept = NULL;

		free(data->peer);
		data->peer = NULL;
		data->peers = 0;

		free(inst[u]->impl);
		inst[u]->impl = NULL;
	}

	for(u = 0; u < cfg.announces; u++){
		for(p = 0; p < cfg.announce[u].invites; p++){
			free(cfg.announce[u].invite[p]);
		}
		free(cfg.announce[u].invite);
	}
	free(cfg.announce);
	cfg.announce = NULL;
	cfg.announces = 0;

	free(cfg.mdns_name);
	cfg.mdns_name = NULL;
	if(cfg.mdns_fd >= 0){
		close(cfg.mdns_fd);
	}

	LOG("Backend shut down");
	return 0;
}
