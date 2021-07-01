#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#ifndef _WIN32
	#include <sys/select.h>
	#define MM_API __attribute__((visibility ("default")))
#else
	#define MM_API __attribute__((dllexport))
#endif

#define BACKEND_NAME "core/rt"
#define MM_SWAP_LIMIT 20
#include "midimonster.h"
#include "routing.h"
#include "backend.h"

/* Core-internal structures */
typedef struct /*_event_collection*/ {
	size_t alloc;
	size_t n;
	channel** channel;
	channel_value* value;
} event_collection;

typedef struct /*_mm_channel_mapping*/ {
	channel* from;
	size_t destinations;
	channel** to;
} channel_mapping;

static struct {
	//routing_hash is set up for 256 buckets
	size_t entries[256];
	channel_mapping* map[256];

	event_collection pool[2];
	event_collection* events;
} routing = {
	.events = routing.pool
};

static size_t routing_hash(channel* key){
	uint64_t repr = (uint64_t) key;
	//return 8bit hash for 256 buckets, not ideal but it works
	return (repr ^ (repr >> 8) ^ (repr >> 16) ^ (repr >> 24) ^ (repr >> 32)) & 0xFF;
}

int mm_map_channel(channel* from, channel* to){
	size_t u, m, bucket = routing_hash(from);

	//find existing source mapping
	for(u = 0; u < routing.entries[bucket]; u++){
		if(routing.map[bucket][u].from == from){
			break;
		}
	}

	//create new entry
	if(u == routing.entries[bucket]){
		routing.map[bucket] = realloc(routing.map[bucket], (routing.entries[bucket] + 1) * sizeof(channel_mapping));
		if(!routing.map[bucket]){
			routing.entries[bucket] = 0;
			LOG("Failed to allocate memory");
			return 1;
		}

		memset(routing.map[bucket] + routing.entries[bucket], 0, sizeof(channel_mapping));
		routing.entries[bucket]++;
		routing.map[bucket][u].from = from;
	}

	//check whether the target is already mapped
	for(m = 0; m < routing.map[bucket][u].destinations; m++){
		if(routing.map[bucket][u].to[m] == to){
			return 0;
		}
	}

	//add a mapping target
	routing.map[bucket][u].to = realloc(routing.map[bucket][u].to, (routing.map[bucket][u].destinations + 1) * sizeof(channel*));
	if(!routing.map[bucket][u].to){
		LOG("Failed to allocate memory");
		routing.map[bucket][u].destinations = 0;
		return 1;
	}

	routing.map[bucket][u].to[routing.map[bucket][u].destinations] = to;
	routing.map[bucket][u].destinations++;
	return 0;
}

MM_API int mm_channel_event(channel* c, channel_value v){
	size_t u, p, bucket = routing_hash(c);

	//find mapped channels
	for(u = 0; u < routing.entries[bucket]; u++){
		if(routing.map[bucket][u].from == c){
			break;
		}
	}

	if(u == routing.entries[bucket]){
		//target-only channel
		return 0;
	}

	//resize event structures to fit additional events
	if(routing.events->n + routing.map[bucket][u].destinations >= routing.events->alloc){
		routing.events->channel = realloc(routing.events->channel, (routing.events->alloc + routing.map[bucket][u].destinations) * sizeof(channel*));
		routing.events->value = realloc(routing.events->value, (routing.events->alloc + routing.map[bucket][u].destinations) * sizeof(channel_value));

		if(!routing.events->channel || !routing.events->value){
			LOG("Failed to allocate memory");
			routing.events->alloc = 0;
			routing.events->n = 0;
			return 1;
		}

		routing.events->alloc += routing.map[bucket][u].destinations;
	}

	//enqueue channel events
	//FIXME this might lead to one channel being mentioned multiple times in an apply call
	memcpy(routing.events->channel + routing.events->n, routing.map[bucket][u].to, routing.map[bucket][u].destinations * sizeof(channel*));
	for(p = 0; p < routing.map[bucket][u].destinations; p++){
		routing.events->value[routing.events->n + p] = v;
	}

	routing.events->n += routing.map[bucket][u].destinations;
	return 0;
}

void routing_stats(){
	size_t n = 0, u, max = 0;

	//count and report mappings
	for(u = 0; u < sizeof(routing.map) / sizeof(routing.map[0]); u++){
		n += routing.entries[u];
		max = max(max, routing.entries[u]);
	}

	LOGPF("Routing %" PRIsize_t " sources, largest bucket has %" PRIsize_t " entries",
			n, max);
}

int routing_iteration(){
	event_collection* secondary = NULL;
	size_t u, swaps = 0;

	//limit number of collector swaps per iteration to prevent complete deadlock
	while(routing.events->n && swaps < MM_SWAP_LIMIT){
		//swap primary and secondary event collectors
		DBGPF("Swapping event collectors, %" PRIsize_t " events in primary", routing.events->n);
		for(u = 0; u < sizeof(routing.pool) / sizeof(routing.pool[0]); u++){
			if(routing.events != routing.pool + u){
				secondary = routing.events;
				routing.events = routing.pool + u;
				break;
			}
		}

		//push collected events to target backends
		if(secondary->n && backends_notify(secondary->n, secondary->channel, secondary->value)){
			LOG("Backends failed to handle output");
			return 1;
		}

		//reset the event count
		secondary->n = 0;
	}

	if(swaps == MM_SWAP_LIMIT){
		LOG("Iteration swap limit hit, a backend may be configured to route events in an infinite loop");
	}

	return 0;
}

void routing_cleanup(){
	size_t u, n;

	for(u = 0; u < sizeof(routing.map) / sizeof(routing.map[0]); u++){
		for(n = 0; n < routing.entries[u]; n++){
			free(routing.map[u][n].to);
		}
		free(routing.map[u]);
		routing.map[u] = NULL;
		routing.entries[u] = 0;
	}

	for(u = 0; u < sizeof(routing.pool) / sizeof(routing.pool[0]); u++){
		free(routing.pool[u].channel);
		free(routing.pool[u].value);
		routing.pool[u].alloc = 0;
	}
}
