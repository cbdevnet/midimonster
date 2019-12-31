#define BACKEND_NAME "loopback"

#include <string.h>
#include "loopback.h"

MM_PLUGIN_API int init(){
	backend loopback = {
		.name = BACKEND_NAME,
		.conf = loopback_configure,
		.create = loopback_instance,
		.conf_instance = loopback_configure_instance,
		.channel = loopback_channel,
		.handle = loopback_set,
		.process = loopback_handle,
		.start = loopback_start,
		.shutdown = loopback_shutdown
	};

	//register backend
	if(mm_backend_register(loopback)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int loopback_configure(char* option, char* value){
	//intentionally ignored
	return 0;
}

static int loopback_configure_instance(instance* inst, char* option, char* value){
	//intentionally ignored
	return 0;
}

static instance* loopback_instance(){
	instance* i = mm_instance();
	if(!i){
		return NULL;
	}

	i->impl = calloc(1, sizeof(loopback_instance_data));
	if(!i->impl){
		LOG("Failed to allocate memory");
		return NULL;
	}

	return i;
}

static channel* loopback_channel(instance* inst, char* spec, uint8_t flags){
	size_t u;
	loopback_instance_data* data = (loopback_instance_data*) inst->impl;

	//find matching channel
	for(u = 0; u < data->n; u++){
		if(!strcmp(spec, data->name[u])){
			break;
		}
	}

	//allocate new channel
	if(u == data->n){
		data->name = realloc(data->name, (u + 1) * sizeof(char*));
		if(!data->name){
			LOG("Failed to allocate memory");
			return NULL;
		}

		data->name[u] = strdup(spec);
		if(!data->name[u]){
			LOG("Failed to allocate memory");
			return NULL;
		}
		data->n++;
	}

	return mm_channel(inst, u, 1);
}

static int loopback_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t n;
	for(n = 0; n < num; n++){
		mm_channel_event(c[n], v[n]);
	}
	return 0;
}

static int loopback_handle(size_t num, managed_fd* fds){
	//no events generated here
	return 0;
}

static int loopback_start(size_t n, instance** inst){
	return 0;
}

static int loopback_shutdown(size_t n, instance** inst){
	size_t u, p;
	loopback_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (loopback_instance_data*) inst[u]->impl;
		for(p = 0; p < data->n; p++){
			free(data->name[p]);
		}
		free(data->name);
		free(inst[u]->impl);
	}

	LOG("Backend shut down");
	return 0;
}
