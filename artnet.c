#include <string.h>
#include "artnet.h"

size_t ninstances = 0;
instance* instances = NULL;

int artnet_init(){
	backend artnet = {
		.name = "artnet",
		.conf = artnet_configure,
		.create = artnet_instance,
		.conf_instance = artnet_configure_instance,
		.channel = artnet_channel,
		.handle = artnet_set,
		.process = artnet_handle,
		.shutdown = artnet_shutdown
	};

	//register backend
	if(!mm_backend_register(artnet)){
		fprintf(stderr, "Failed to register ArtNet backend\n");
		return 1;
	}
	return 0;
}

static int artnet_configure(char* option, char* value){
	fprintf(stderr, "ArtNet backend configured: %s -> %s\n", option, value);
	return 0;
}

static instance* artnet_instance(){
	fprintf(stderr, "Creating new ArtNet instance\n");
	instances = realloc(instances, (ninstances + 1) * sizeof(instance));
	if(!instances){
		fprintf(stderr, "Failed to allocate memory\n");
		ninstances = 0;
		return NULL;
	}
	memset(instances + ninstances, 0, sizeof(instance));
	ninstances++;
	return instances + (ninstances - 1);
}

static int artnet_configure_instance(instance* instance, char* option, char* value){
	fprintf(stderr, "ArtNet instance configured: %s -> %s\n", option, value);
	return 1;
}

static channel* artnet_channel(instance* instance, char* spec){
	fprintf(stderr, "Parsing ArtNet channelspec %s\n", spec);
	return NULL;
}

static int artnet_set(size_t num, channel* c, channel_value* v){
	return 1;
}

static int artnet_handle(size_t num, int* fd, void** data){
	return 1;
}

static int artnet_shutdown(){
	size_t u;

	for(u = 0; u < ninstances; u++){
		mm_instance_free(instances + u);
	}
	free(instances);
	ninstances = 0;

	fprintf(stderr, "ArtNet backend shut down\n");
	return 0;
}
