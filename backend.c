#include <string.h>
#include "midimonster.h"
#include "backend.h"

static size_t nbackends = 0;
static backend* backends = NULL;
static size_t ninstances = 0;
static instance** instances = NULL;
static size_t nchannels = 0;
static channel** channels = NULL;

channel* mm_channel(instance* i, uint64_t ident, uint8_t create){
	size_t u;
	for(u = 0; u < nchannels; u++){
		if(channels[u]->instance == 0 && channels[u]->ident == ident){
			return channels[u];
		}
	}

	if(!create){
		return NULL;
	}

	channel** new_chan = realloc(channels, (nchannels + 1) * sizeof(channel*));
	if(!new_chan){
		fprintf(stderr, "Failed to allocate memory\n");
		nchannels = 0;
		return NULL;
	}

	channels = new_chan;
	channels[nchannels] = calloc(1, sizeof(channel));
	if(!channels[nchannels]){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	channels[nchannels]->instance = i;
	channels[nchannels]->ident = ident;
	return channels[nchannels++];
}

instance* mm_instance(){
	instance** new_inst = realloc(instances, (ninstances + 1) * sizeof(instance*));
	if(!new_inst){
		//TODO free
		fprintf(stderr, "Failed to allocate memory\n");
		ninstances = 0;
		return NULL;
	}
	instances = new_inst;
	instances[ninstances] = calloc(1, sizeof(instance));
	if(!instances[ninstances]){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return instances[ninstances++];
}

int mm_backend_instances(char* name, size_t* ninst, instance*** inst){
	backend* b = backend_match(name);
	size_t n = 0, u;
	//count number of affected instances
	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b){
			n++;
		}
	}

	*ninst = n;
	*inst = calloc(n, sizeof(instance*));
	if(!*inst){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	n = 0;
	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b){
			(*inst)[n] = instances[u];
			n++;
		}
	}
	return 0;
}

void instances_free(){
	size_t u;
	for(u = 0; u < ninstances; u++){
		free(instances[u]->name);
		instances[u]->name = NULL;
		instances[u]->backend = NULL;
		free(instances[u]);
		instances[u] = NULL;
	}
	free(instances);
	ninstances = 0;
}

void channels_free(){
	size_t u;
	for(u = 0; u < nchannels; u++){
		if(channels[u]->impl){
			channels[u]->instance->backend->channel_free(channels[u]);
		}
		free(channels[u]);
		channels[u] = NULL;
	}
	free(channels);
	nchannels = 0;
}

backend* backend_match(char* name){
	size_t u;
	for(u = 0; u < nbackends; u++){
		if(!strcmp(backends[u].name, name)){
			return backends + u;
		}
	}
	return NULL;
}

instance* instance_match(char* name){
	size_t u;
	for(u = 0; u < ninstances; u++){
		if(!strcmp(instances[u]->name, name)){
			return instances[u];
		}
	}
	return NULL;
}

struct timeval backend_timeout(){
	//TODO fetch minimum poll interval from backends
	struct timeval tv = {
		0,
		10000
	};

	return tv;
}

int mm_backend_register(backend b){
	if(!backend_match(b.name)){
		backends = realloc(backends, (nbackends + 1) * sizeof(backend));
		if(!backends){
			fprintf(stderr, "Failed to allocate memory\n");
			nbackends = 0;
			return 1;
		}
		backends[nbackends] = b;
		nbackends++;

		fprintf(stderr, "Registered backend %s\n", b.name);
		return 0;
	}
	return 1;
}

int backends_start(){
	int rv = 0;
	size_t u;
	for(u = 0; u < nbackends; u++){
		rv |= backends[u].start();
	}
	return rv;
}

int backends_stop(){
	size_t u;
	for(u = 0; u < nbackends; u++){
		backends[u].shutdown();
	}
	free(backends);
	nbackends = 0;
	return 0;
}
