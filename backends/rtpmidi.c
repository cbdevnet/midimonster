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

static struct /*_rtpmidi_global*/ {
	int mdns_fd;
	char* mdns_name;
	uint8_t detect;
} cfg = {
	.mdns_fd = -1,
	.mdns_name = NULL,
	.detect = 0
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
		fprintf(stderr, "Failed to register rtpmidi backend\n");
		return 1;
	}

	return 0;
}

static int rtpmidi_configure(char* option, char* value){
	char* host = NULL, *port = NULL;
	char next_port[10];

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

		if(mmbackend_parse_hostspec(value, &host, &port)){
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

static int rtpmidi_configure_instance(instance* inst, char* option, char* value){
	//TODO
	return 1;
}

static instance* rtpmidi_instance(){
	//TODO
	return NULL;
}

static channel* rtpmidi_channel(instance* inst, char* spec, uint8_t flags){
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

	//TODO if mdns name defined and no socket, bind default values

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
