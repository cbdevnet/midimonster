#include <string.h>
#include "midimonster.h"
#include "backend.h"

static size_t nbackends = 0;
static backend* backends = NULL;
static size_t ninstances = 0;
static instance** instances = NULL;
static size_t nchannels = 0;
static channel** channels = NULL;

int backends_handle(size_t nfds, managed_fd* fds){
	size_t u, p, n;
	int rv = 0;
	managed_fd xchg;

	for(u = 0; u < nbackends && !rv; u++){
		n = 0;

		for(p = 0; p < nfds; p++){
			if(fds[p].backend == backends + u){
				xchg = fds[n];
				fds[n] = fds[p];
				fds[p] = xchg;
				n++;
			}
		}

		DBGPF("Notifying backend %s of %zu waiting FDs\n", backends[u].name, n);
		rv |= backends[u].process(n, fds);
	}
	return rv;
}

int backends_notify(size_t nev, channel** c, channel_value* v){
	size_t u, p, n;
	int rv = 0;
	channel_value xval;
	channel* xchnl;

	//TODO eliminate duplicates
	for(u = 0; u < ninstances && !rv; u++){
		n = 0;

		for(p = 0; p < nev; p++){
			if(c[p]->instance == instances[u]){
				xval = v[n];
				xchnl = c[n];
				
				v[n] = v[p];
				c[n] = c[p];

				v[p] = xval;
				c[p] = xchnl;
				n++;
			}
		}

		DBGPF("Calling handler for instance %s with %zu events\n", instances[u]->name, n);
		rv |= instances[u]->backend->handle(instances[u], n, c, v);
	}

	return 0;
}

channel* mm_channel(instance* i, uint64_t ident, uint8_t create){
	size_t u;
	for(u = 0; u < nchannels; u++){
		if(channels[u]->instance == i && channels[u]->ident == ident){
			DBGPF("Requested channel %zu on instance %s already exists, reusing\n", ident, i->name);
			return channels[u];
		}
	}

	if(!create){
		DBGPF("Requested unknown channel %zu on instance %s\n", ident, i->name);
		return NULL;
	}

	DBGPF("Creating previously unknown channel %zu on instance %s\n", ident, i->name);
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

instance* mm_instance_find(char* name, uint64_t ident){
	size_t u;
	backend* b = backend_match(name);
	if(!b){
		return NULL;
	}

	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b && instances[u]->ident == ident){
			return instances[u];
		}
	}

	return NULL;
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
		DBGPF("Destroying channel %zu on instance %s\n", channels[u]->ident, channels[u]->instance->name);
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
	size_t u;
	uint32_t res, secs = 1, msecs = 0;

	for(u = 0; u < nbackends; u++){
		if(backends[u].interval){
			res = backends[u].interval();
			if((res / 1000) < secs){
				secs = res / 1000;
				msecs = res % 1000;
			}
			else if(res / 1000 == secs && (res % 1000) < msecs){
				msecs = res % 1000;
			}
		}
	}

	struct timeval tv = {
		secs,
		msecs
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
	int rv = 0, current;
	size_t u;
	for(u = 0; u < nbackends; u++){
		current = backends[u].start();
		if(current){
			fprintf(stderr, "Failed to start backend %s\n", backends[u].name);
		}
		rv |= current;
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
