#include <string.h>
#ifndef _WIN32
#define MM_API __attribute__((visibility ("default")))
#else
#define MM_API __attribute__((dllexport))
#endif
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

		DBGPF("Notifying backend %s of %lu waiting FDs\n", backends[u].name, n);
		rv |= backends[u].process(n, fds);
		if(rv){
			fprintf(stderr, "Backend %s failed to handle input\n", backends[u].name);
		}
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

		DBGPF("Calling handler for instance %s with %lu events\n", instances[u]->name, n);
		rv |= instances[u]->backend->handle(instances[u], n, c, v);
	}

	return 0;
}

MM_API channel* mm_channel(instance* inst, uint64_t ident, uint8_t create){
	size_t u;
	for(u = 0; u < nchannels; u++){
		if(channels[u]->instance == inst && channels[u]->ident == ident){
			DBGPF("Requested channel %lu on instance %s already exists, reusing\n", ident, inst->name);
			return channels[u];
		}
	}

	if(!create){
		DBGPF("Requested unknown channel %lu on instance %s\n", ident, inst->name);
		return NULL;
	}

	DBGPF("Creating previously unknown channel %lu on instance %s\n", ident, inst->name);
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

	channels[nchannels]->instance = inst;
	channels[nchannels]->ident = ident;
	return channels[nchannels++];
}

MM_API instance* mm_instance(){
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

MM_API instance* mm_instance_find(char* name, uint64_t ident){
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

MM_API int mm_backend_instances(char* name, size_t* ninst, instance*** inst){
	backend* b = backend_match(name);
	size_t n = 0, u;
	//count number of affected instances
	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b){
			n++;
		}
	}

	*ninst = n;

	if(!n){
		*inst = NULL;
		return 0;
	}

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
		DBGPF("Destroying channel %lu on instance %s\n", channels[u]->ident, channels[u]->instance->name);
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
		msecs * 1000
	};
	return tv;
}

MM_API int mm_backend_register(backend b){
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
	size_t n, u, p;
	instance** inst = NULL;

	for(u = 0; u < nbackends; u++){
		//only start backends that have instances
		for(p = 0; p < ninstances && instances[p]->backend != backends + u; p++){
		}

		//backend has no instances, skip the start call
		if(p == ninstances){
			continue;
		}
		
		//fetch list of instances
		if(mm_backend_instances(backends[u].name, &n, &inst)){
			fprintf(stderr, "Failed to fetch instance list for initialization of backend %s\n", backends[u].name);
			return 1;
		}

		//start the backend
		current = backends[u].start(n, inst);
		if(current){
			fprintf(stderr, "Failed to start backend %s\n", backends[u].name);
		}

		//clean up
		free(inst);
		inst = NULL;
		rv |= current;
	}
	return rv;
}

int backends_stop(){
	size_t u, n;
	instance** inst = NULL;

	for(u = 0; u < nbackends; u++){
		//fetch list of instances
		if(mm_backend_instances(backends[u].name, &n, &inst)){
			fprintf(stderr, "Failed to fetch instance list for shutdown of backend %s\n", backends[u].name);
			n = 0;
			inst = NULL;
		}

		backends[u].shutdown(n, inst);
		free(inst);
		inst = NULL;
	}

	free(backends);
	nbackends = 0;
	return 0;
}
