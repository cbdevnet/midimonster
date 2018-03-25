#include <string.h>
#include <unistd.h>

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

static int rtpmidi_configure(char* option, char* value){
	if(!strcmp(option, "mdns-name")){
		if(cfg.mdns_name){
			free(cfg.mdns_name);
		}

		cfg.mdns_name = strdup(value);
		if(!cfg.mdns_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		//TODO this should create the mdns broadcaster and responder socket
		return 0;
	}
	else if(!strcmp(option, "bind")){
		//TODO open listening control and data fds
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
	//TODO
	return 1;
}

static int rtpmidi_start(){
	//TODO
	return 1;
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
