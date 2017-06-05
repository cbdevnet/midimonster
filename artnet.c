#include <string.h>
#include "artnet.h"

#define BACKEND_NAME "artnet"
static uint8_t default_net = 0;
static struct {
	char* host;
	char* port;
} bind = {
	.host = NULL,
	.port = NULL
};

int artnet_init(){
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
	char* separator = value;
	if(!strcmp(option, "bind")){
		for(; *separator && *separator != ' '; separator++){
		}

		if(*separator){
			*separator = 0;
			separator++;
			free(bind.port);
			bind.port = strdup(separator);
		}

		free(bind.host);
		bind.host = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "net")){
		//configure default net
		default_net = strtoul(value, NULL, 10);
		return 0;
	}
	fprintf(stderr, "Unknown ArtNet backend option %s\n", option);
	return 1;
}

static instance* artnet_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	inst->impl = calloc(1, sizeof(artnet_instance_data));
	if(!inst->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return inst;
}

static int artnet_configure_instance(instance* instance, char* option, char* value){
	artnet_instance_data* data = (artnet_instance_data*) instance->impl;

	if(!strcmp(option, "net")){
		data->net = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "uni")){
		data->uni = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "output")){
		if(!strcmp(value, "true")){
			data->mode |= output;
		}
		else{
			data->mode &= ~output;
		}
		return 0;
	}

	fprintf(stderr, "Unknown ArtNet instance option %s\n", option);
	return 1;
}

static channel* artnet_channel(instance* instance, char* spec){
	unsigned channel = strtoul(spec, NULL, 10);
	if(channel > 512 || channel < 1){
		fprintf(stderr, "Invalid ArtNet channel %s\n", spec);
		return NULL;
	}
	return mm_channel(instance, channel, 1);
}

static int artnet_set(instance* inst, size_t num, channel* c, channel_value* v){
	//TODO
	return 1;
}

static int artnet_handle(size_t num, managed_fd* fds){
	//TODO
	return 0;
}

static int artnet_start(){
	if(!bind.host){
		bind.host = strdup("127.0.0.1");
	}

	if(!bind.port){
		bind.port = strdup("6454");
	}

	if(!bind.host || !bind.port){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//TODO allocate all active universes
	//TODO open socket
	fprintf(stderr, "Listening for ArtNet data on %s port %s\n", bind.host, bind.port);
	//TODO parse all universe destinations

	return 0;
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

	free(bind.host);
	free(bind.port);
	fprintf(stderr, "ArtNet backend shut down\n");
	return 0;
}
