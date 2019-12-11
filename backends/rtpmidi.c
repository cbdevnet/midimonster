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

static int rtpmidi_configure_instance(instance* inst, char* option, char* value){
	rtpmidi_instance_data* data = (rtpmidi_instance_data*) inst->impl;
	char* host = NULL, *port = NULL;

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
		if(data->mode != direct){
			fprintf(stderr, "The rtpmidi 'peer' option is only valid for direct mode instances\n");
			return 1;
		}

		//TODO add peer
		return 0;
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

static int rtpmidi_start(size_t n, instance** inst){
	size_t u, fds = 0;
	int rv = 1;
	rtpmidi_instance_data* data = NULL;

	//if mdns name defined and no socket, bind default values
	if(cfg.mdns_name && cfg.mdns_fd < 0){
		cfg.mdns_fd = mmbackend_socket("::", RTPMIDI_MDNS_PORT, SOCK_DGRAM, 1, 1);
		if(cfg.mdns_fd < 0){
			return 1;
		}
	}

	//register mdns fd to core
	if(cfg.mdns_fd >= 0){
		if(mm_manage_fd(cfg.mdns_fd, BACKEND_NAME, 1, NULL)){
			fprintf(stderr, "rtpmidi failed to register mDNS socket with core\n");
			goto bail;
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
			goto bail;
		}

		//generate random ssrc's
		if(!data->ssrc){
			data->ssrc = rand() << 16 | rand();
		}

		//if not bound, bind to default
		if(data->fd < 0 && rtpmidi_bind_instance(data, "::", NULL)){
			fprintf(stderr, "Failed to bind default sockets for rtpmidi instance %s\n", inst[u]->name);
			goto bail;
		}

		//register fds to core
		if(mm_manage_fd(data->fd, BACKEND_NAME, 1, NULL) || (data->control_fd >= 0 && mm_manage_fd(data->control_fd, BACKEND_NAME, 1, NULL))){
			fprintf(stderr, "rtpmidi failed to register instance socket with core\n");
			goto bail;
		}
		fds += (data->control_fd >= 0) ? 2 : 1;
	}

	fprintf(stderr, "rtpmidi backend registered %" PRIsize_t " descriptors to core\n", fds);
	rv = 0;
bail:
	return rv;
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
