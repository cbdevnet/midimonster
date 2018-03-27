#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "rtpmidi.h"

#define BACKEND_NAME "rtpmidi"

/** rtpMIDI backend w/ AppleMIDI support
 *
 * Global configuration
 * 	bind = 0.0.0.0
 * 	apple-bind = 0.0.0.1
 * 	mdns-bind = 0.0.0.0
 * 	mdns-name = mdns-name
 * 
 * Instance configuration
 * 	interface = 0
 * 	(opt) ssrc = X
 *
 * 	apple-session = session-name
 * 	apple-invite = invite-peer
 * 	apple-allow = *
 * or
 * 	connect = 
 * 	reply-any = 1
 */

static struct /*_rtpmidi_global*/ {
	int mdns_fd;
	char* mdns_name;
	size_t nfds;
	rtpmidi_fd* fds;
} cfg = {
	.mdns_fd = -1,
	.mdns_name = NULL,
	.nfds = 0,
	.fds = NULL
};

int init(){
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

	if(mm_backend_register(rtpmidi)){
		fprintf(stderr, "Failed to register rtpMIDI backend\n");
		return 1;
	}

	return 0;
}

static int rtpmidi_listener(char* host, char* port){
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
		fprintf(stderr, "Failed to set rtpMIDI descriptor nonblocking\n");
		close(fd);
		return -1;
	}
	return 0;
}

static int rtpmidi_parse_addr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len){
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

static int rtpmidi_separate_hostspec(char* in, char** host, char** port, char* default_port){
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
		*port = default_port;
	}
	return 0;
}

static int rtpmidi_configure(char* option, char* value){
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


		//TODO create mdns broadcast/responder socket
	}
	else if(!strcmp(option, "bind")){
		//TODO open listening data fd for raw rtpmidi instance
	}
	else if(!strcmp(option, "apple-bind")){
		//TODO open control and data fd for applemidi session/rtpmidi+apple-extensions instance
	}

	fprintf(stderr, "Unknown rtpMIDI backend option %s\n", option);
	return 1;
}

static int rtpmidi_configure_instance(instance* inst, char* option, char* value){
	//TODO
	return 1;
}

static instance* rtpmidi_instance(){
	//TODO
	return NULL;
}

static channel* rtpmidi_channel(instance* inst, char* spec){
	//TODO
	return NULL;
}

static int rtpmidi_set(instance* inst, size_t num, channel** c, channel_value* v){
	//TODO
	return 1;
}

static int rtpmidi_handle(size_t num, managed_fd* fds){
	//TODO handle discovery

	if(!num){
		return 0;
	}

	//TODO
	return 1;
}

static int rtpmidi_start(){
	size_t n, u, p;
	int rv = 1;
	instance** inst = NULL;
	rtpmidi_instance_data* data = NULL;

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	if(!n){
		free(inst);
		return 0;
	}

	if(!cfg.nfds){
		fprintf(stderr, "Failed to start rtpMIDI backend: no descriptors bound\n");
		goto bail;
	}

	if(cfg.mdns_fd < 0){
		fprintf(stderr, "No mDNS discovery interface bound, APPLEMIDI session support disabled\n");
	}

	//TODO initialize all instances

	rv = 0;
bail:
	free(inst);
	return rv;
}

static int rtpmidi_shutdown(){
	size_t u;

	free(cfg.mdns_name);
	if(cfg.mdns_fd >= 0){
		close(cfg.mdns_fd);
	}

	for(u = 0; u < cfg.nfds; u++){
		if(cfg.fds[u].data >= 0){
			close(cfg.fds[u].data);
		}
		if(cfg.fds[u].control >= 0){
			close(cfg.fds[u].control);
		}
	}
	return 0;
}
