#include <string.h>
#ifndef _WIN32
	#define MM_API __attribute__((visibility ("default")))
#else
	#define MM_API __attribute__((dllexport))
#endif

#define BACKEND_NAME "core/be"
#include "midimonster.h"
#include "backend.h"

static struct {
	size_t n;
	backend* backends;
	instance*** instances;
} registry = {
	.n = 0
};

//the global channel store was converted from a naive list to a hashmap of lists for performance reasons
static struct {
	//channelstore hash is set up for 256 buckets
	size_t n[256];
	channel** entry[256];
} channels = {
	.n = {
		0
	}
};

static size_t channelstore_hash(instance* inst, uint64_t ident){
	uint64_t repr = ((uint64_t) inst) ^ ident;
	return (repr ^ (repr >> 8) ^ (repr >> 16) ^ (repr >> 24) ^ (repr >> 32)) & 0xFF;
}

int backends_handle(size_t nfds, managed_fd* fds){
	size_t u, p, n;
	int rv = 0;
	managed_fd xchg;

	for(u = 0; u < registry.n && !rv; u++){
		n = 0;

		for(p = 0; p < nfds; p++){
			if(fds[p].backend == registry.backends + u){
				xchg = fds[n];
				fds[n] = fds[p];
				fds[p] = xchg;
				n++;
			}
		}

		//handle if there is data ready or the backend has active instances for polling
		if(n || registry.instances[u]){
			DBGPF("Notifying backend %s of %" PRIsize_t " waiting FDs", registry.backends[u].name, n);
			rv |= registry.backends[u].process(n, fds);
			if(rv){
				fprintf(stderr, "Backend %s failed to handle input\n", registry.backends[u].name);
			}
		}
	}
	return rv;
}

int backends_notify(size_t nev, channel** c, channel_value* v){
	size_t u, p, n;
	int rv = 0;
	channel_value xval;
	channel* xchnl = NULL;

	for(u = 0; u < nev && !rv; u = n){
		//sort for this instance
		n = u + 1;
		for(p = u + 1; p < nev; p++){
			if(c[p]->instance == c[u]->instance){
				xval = v[p];
				xchnl = c[p];

				v[p] = v[n];
				c[p] = c[n];

				v[n] = xval;
				c[n] = xchnl;
				n++;
			}
		}

		//TODO eliminate duplicates
		DBGPF("Calling handler for instance %s with %" PRIsize_t " events", c[u]->instance->name, n - u);
		rv |= c[u]->instance->backend->handle(c[u]->instance, n - u, c + u, v + u);
	}

	return 0;
}

MM_API channel* mm_channel(instance* inst, uint64_t ident, uint8_t create){
	size_t u, bucket = channelstore_hash(inst, ident);
	DBGPF("\tSearching for inst %" PRIu64 " ident %" PRIu64, (uint64_t) inst, ident);
	for(u = 0; u < channels.n[bucket]; u++){
		DBGPF("\tBucket %" PRIsize_t " entry %" PRIsize_t " inst %" PRIu64 " ident %" PRIu64, bucket, u, (uint64_t) channels.entry[bucket][u]->instance, channels.entry[bucket][u]->ident);
		if(channels.entry[bucket][u]->instance == inst
				&& channels.entry[bucket][u]->ident == ident){
			DBGPF("Requested channel %" PRIu64 " on instance %s already exists, reusing (bucket %" PRIsize_t ", %" PRIsize_t " search steps)\n", ident, inst->name, bucket, u);
			return channels.entry[bucket][u];
		}
	}

	if(!create){
		DBGPF("Requested unknown channel %" PRIu64 " (bucket %" PRIsize_t ") on instance %s", ident, bucket, inst->name);
		return NULL;
	}

	DBGPF("Creating previously unknown channel %" PRIu64 " on instance %s, bucket %" PRIsize_t, ident, inst->name, bucket);
	channels.entry[bucket] = realloc(channels.entry[bucket], (channels.n[bucket] + 1) * sizeof(channel*));
	if(!channels.entry[bucket]){
		fprintf(stderr, "Failed to allocate memory\n");
		channels.n[bucket] = 0;
		return NULL;
	}

	channels.entry[bucket][channels.n[bucket]] = calloc(1, sizeof(channel));
	if(!channels.entry[bucket][channels.n[bucket]]){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	channels.entry[bucket][channels.n[bucket]]->instance = inst;
	channels.entry[bucket][channels.n[bucket]]->ident = ident;
	return channels.entry[bucket][(channels.n[bucket]++)];
}

MM_API void mm_channel_update(channel* chan, uint64_t ident){
	size_t bucket = channelstore_hash(chan->instance, chan->ident), new_bucket = channelstore_hash(chan->instance, ident);
	size_t u;

	DBGPF("Updating identifier for inst %" PRIu64 " ident %" PRIu64 " (bucket %" PRIsize_t " to %" PRIsize_t ") to %" PRIu64, (uint64_t) chan->instance, chan->ident, bucket, new_bucket, ident);

	if(bucket == new_bucket){
		chan->ident = ident;
		return;
	}

	for(u = 0; u < channels.n[bucket]; u++){
		if(channels.entry[bucket][u]->instance == chan->instance
				&& channels.entry[bucket][u]->ident == chan->ident){
			break;
		}
	}

	if(u == channels.n[bucket]){
		DBGPF("Failed to find channel to update in bucket %" PRIsize_t, bucket);
		return;
	}

	DBGPF("Removing channel from slot %" PRIsize_t " of %" PRIsize_t " of bucket %" PRIsize_t, u, channels.n[bucket], bucket);
	//remove channel from old bucket
	for(; u < channels.n[bucket] - 1; u++){
		channels.entry[bucket][u] = channels.entry[bucket][u + 1];
	}

	//add to new bucket
	channels.entry[new_bucket] = realloc(channels.entry[new_bucket], (channels.n[new_bucket] + 1) * sizeof(channel*));
	if(!channels.entry[new_bucket]){
		fprintf(stderr, "Failed to allocate memory\n");
		channels.n[new_bucket] = 0;
		return;
	}

	channels.entry[new_bucket][channels.n[new_bucket]] = chan;
	chan->ident = ident;
	channels.n[bucket]--;
	channels.n[new_bucket]++;
}

instance* mm_instance(backend* b){
	size_t u = 0, n = 0;

	for(u = 0; u < registry.n; u++){
		if(registry.backends + u == b){
			//count existing instances
			for(n = 0; registry.instances[u] && registry.instances[u][n]; n++){
			}

			//extend
			registry.instances[u] = realloc(registry.instances[u], (n + 2) * sizeof(instance*));
			if(!registry.instances[u]){
				fprintf(stderr, "Failed to allocate memory\n");
				return NULL;
			}
			//sentinel
			registry.instances[u][n + 1] = NULL;
			registry.instances[u][n] = calloc(1, sizeof(instance));
			if(!registry.instances[u][n]){
				fprintf(stderr, "Failed to allocate memory\n");
			}
			registry.instances[u][n]->backend = b;
			return registry.instances[u][n];
		}
	}

	//this should never happen
	return NULL;
}

MM_API instance* mm_instance_find(char* name, uint64_t ident){
	size_t b = 0;
	instance** iter = NULL;
	for(b = 0; b < registry.n; b++){
		if(!strcmp(registry.backends[b].name, name)){
			for(iter = registry.instances[b]; iter && *iter; iter++){
				if((*iter)->ident == ident){
					return *iter;
				}
			}
		}
	}

	return NULL;
}

MM_API int mm_backend_instances(char* name, size_t* ninst, instance*** inst){
	size_t b = 0, i = 0;
	if(!ninst || !inst){
		return 1;
	}

	for(b = 0; b < registry.n; b++){
		if(!strcmp(registry.backends[b].name, name)){
			//count instances
			for(i = 0; registry.instances[b] && registry.instances[b][i]; i++){
			}

			*ninst = i;
			if(!i){
				*inst = NULL;
				return 0;
			}

			*inst = calloc(i, sizeof(instance*));
			if(!*inst){
				fprintf(stderr, "Failed to allocate memory\n");
				return 1;
			}

			memcpy(*inst, registry.instances[b], i * sizeof(instance*));
			return 0;
		}
	}
	return 1;
}

backend* backend_match(char* name){
	size_t u;
	for(u = 0; u < registry.n; u++){
		if(!strcmp(registry.backends[u].name, name)){
			return registry.backends + u;
		}
	}
	return NULL;
}

instance* instance_match(char* name){
	size_t u;
	instance** iter = NULL;
	for(u = 0; u < registry.n; u++){
		for(iter = registry.instances[u]; iter && *iter; iter++){
			if(!strcmp(name, (*iter)->name)){
				return *iter;
			}
		}
	}
	return NULL;
}

struct timeval backend_timeout(){
	size_t u;
	uint32_t res, secs = 1, msecs = 0;

	for(u = 0; u < registry.n; u++){
		//only call interval if backend has instances
		if(registry.instances[u] && registry.backends[u].interval){
			res = registry.backends[u].interval();
			if(res && (res / 1000) < secs){
				DBGPF("Updating interval to %" PRIu32 " msecs by request from %s", res, registry.backends[u].name);
				secs = res / 1000;
				msecs = res % 1000;
			}
			else if(res && res / 1000 == secs && (res % 1000) < msecs){
				DBGPF("Updating interval to %" PRIu32 " msecs by request from %s", res, registry.backends[u].name);
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
		registry.backends = realloc(registry.backends, (registry.n + 1) * sizeof(backend));
		registry.instances = realloc(registry.instances, (registry.n + 1) * sizeof(instance**));
		if(!registry.backends || !registry.instances){
			fprintf(stderr, "Failed to allocate memory\n");
			registry.n = 0;
			return 1;
		}
		registry.backends[registry.n] = b;
		registry.instances[registry.n] = NULL;
		registry.n++;

		fprintf(stderr, "Registered backend %s\n", b.name);
		return 0;
	}
	return 1;
}

int backends_start(){
	int rv = 0, current;
	instance** inst = NULL;
	size_t n, u;

	for(u = 0; u < registry.n; u++){
		//skip backends without instances
		if(!registry.instances[u]){
			continue;
		}

		//fetch list of instances
		if(mm_backend_instances(registry.backends[u].name, &n, &inst)){
			fprintf(stderr, "Failed to fetch instance list for initialization of backend %s\n", registry.backends[u].name);
			return 1;
		}

		//start the backend
		current = registry.backends[u].start(n, inst);
		if(current){
			fprintf(stderr, "Failed to start backend %s\n", registry.backends[u].name);
		}

		//clean up
		free(inst);
		inst = NULL;
		rv |= current;
	}
	return rv;
}

static void channels_free(){
	size_t u, p;
	for(u = 0; u < sizeof(channels.n) / sizeof(channels.n[0]); u++){
		DBGPF("Cleaning up channel registry bucket %" PRIsize_t " with %" PRIsize_t " channels", u, channels.n[u]);
		for(p = 0; p < channels.n[u]; p++){
			DBGPF("Destroying channel %" PRIu64 " on instance %s", channels.entry[u][p]->ident, channels.entry[u][p]->instance->name);
			//call the channel_free function if the backend supports it
			if(channels.entry[u][p]->impl && channels.entry[u][p]->instance->backend->channel_free){
				channels.entry[u][p]->instance->backend->channel_free(channels.entry[u][p]);
			}
			free(channels.entry[u][p]);
		}
		free(channels.entry[u]);
		channels.entry[u] = NULL;
		channels.n[u] = 0;
	}
}

int backends_stop(){
	size_t u, n;
	instance** inst = NULL;

	//channels before instances to support proper shutdown procedures
	channels_free();

	//shut down the registry
	for(u = 0; u < registry.n; u++){
		//fetch list of instances
		if(mm_backend_instances(registry.backends[u].name, &n, &inst)){
			fprintf(stderr, "Failed to fetch instance list for shutdown of backend %s\n", registry.backends[u].name);
			inst = NULL;
			n = 0;
		}

		registry.backends[u].shutdown(n, inst);
		free(inst);
		inst = NULL;

		//free instances
		for(inst = registry.instances[u]; inst && *inst; inst++){
			free((*inst)->name);
			(*inst)->name = NULL;
			(*inst)->backend = NULL;
			free(*inst);
		}
		free(registry.instances[u]);
		registry.instances[u] = NULL;
	}

	free(registry.backends);
	free(registry.instances);
	registry.n = 0;
	return 0;
}
