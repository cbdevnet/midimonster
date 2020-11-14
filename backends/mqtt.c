#define BACKEND_NAME "mqtt"

#include <string.h>
#include "mqtt.h"

MM_PLUGIN_API int init(){
	backend mqtt = {
		.name = BACKEND_NAME,
		.conf = mqtt_configure,
		.create = mqtt_instance,
		.conf_instance = mqtt_configure_instance,
		.channel = mqtt_channel,
		.handle = mqtt_set,
		.process = mqtt_handle,
		.start = mqtt_start,
		.shutdown = mqtt_shutdown
	};

	//register backend
	if(mm_backend_register(mqtt)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static size_t mqtt_varint_decode(uint8_t* buffer, uint32_t* result){
	//TODO
	return 0;
}

static int mqtt_configure(char* option, char* value){
	LOG("This backend does not take global configuration");
	return 1;
}

static int mqtt_configure_instance(instance* inst, char* option, char* value){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	char* token = value;

	if(!strcmp(option, "user")){
		free(data->user);
		data->user = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "password")){
		free(data->password);
		data->user = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "host")){
		//mqtt url may be of the form
		//mqtt[s]://[username][:password]@host.domain[:port]
		token = strchr(value, ':');
		//TODO
	}

	LOGPF("Unknown instance configuration option %s on instance %s", option, inst->name);
	return 1;
}

static int mqtt_instance(instance* inst){
	//TODO
	return 0;
}

static channel* mqtt_channel(instance* inst, char* spec, uint8_t flags){
	//TODO
	return NULL;
}

static int mqtt_set(instance* inst, size_t num, channel** c, channel_value* v){
	//TODO
	return 0;
}

static int mqtt_handle(size_t num, managed_fd* fds){
	//TODO
	return 0;
}

static int mqtt_start(size_t n, instance** inst){
	//TODO
	return 0;
}

static int mqtt_shutdown(size_t n, instance** inst){
	size_t u, p;
	mqtt_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (mqtt_instance_data*) inst[u]->impl;
		for(p = 0; p < data->nchannels; p++){
			free(data->channel[p]);
		}
		free(data->channel);
		free(data->host);
		free(data->port);
		free(data->user);
		free(data->password);

		free(inst[u]->impl);
		inst[u]->impl = NULL;
	}

	LOG("Backend shut down");
	return 0;
}
