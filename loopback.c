#include <string.h>
#include "loopback.h"

#define BACKEND_NAME "loopback"

int init(){
	backend loopback = {
		.name = BACKEND_NAME,
		.conf = backend_configure,
		.create = backend_instance,
		.conf_instance = backend_configure_instance,
		.channel = backend_channel,
		.handle = backend_set,
		.process = backend_handle,
		.start = backend_start,
		.shutdown = backend_shutdown
	};

	//register backend
	if(mm_backend_register(loopback)){
		fprintf(stderr, "Failed to register loopback backend\n");
		return 1;
	}
	return 0;
}

static int backend_configure(char* option, char* value){
	//intentionally ignored
	return 0;
}

static int backend_configure_instance(instance* instance, char* option, char* value){
	//intentionally ignored
	return 0;
}

static instance* backend_instance(){
	instance* i = mm_instance();
	if(!i){
		return NULL;
	}

	i->impl = calloc(1, sizeof(loopback_instance));
	if(!i->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return i;
}

static channel* backend_channel(instance* inst, char* spec){
	size_t u;
	loopback_instance* data = (loopback_instance*) inst->impl;

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
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

		data->name[u] = strdup(spec);
		if(!data->name[u]){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}
		data->n++;
	}

	return mm_channel(inst, u, 1);
}

static int backend_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t n;
	for(n = 0; n < num; n++){
		mm_channel_event(c[n], v[n]);
	}
	return 0;
}

static int backend_handle(size_t num, managed_fd* fds){
	//no events generated here
	return 0;
}

static int backend_start(){
	return 0;
}

static int backend_shutdown(){
	size_t n, u, p;
	instance** inst = NULL;
	loopback_instance* data = NULL;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (loopback_instance*) inst[u]->impl;
		for(p = 0; p < data->n; p++){
			free(data->name[p]);
		}
		free(data->name);
		free(inst[u]->impl);
	}

	free(inst);
	return 0;
}
