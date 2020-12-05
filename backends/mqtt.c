#define BACKEND_NAME "mqtt"
#define DEBUG

#include <string.h>

#include "libmmbackend.h"
#include "mqtt.h"

//TODO
// * Periodic connection retries

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

static int mqtt_parse_hostspec(instance* inst, char* hostspec){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	char* host = strchr(hostspec, '@'), *password = NULL, *port = NULL;

	//mqtt[s]://[username][:password]@host.domain[:port]
	if(!strncmp(hostspec, "mqtt://", 7)){
		hostspec += 7;
	}
	else if(!strncmp(hostspec, "mqtts://", 8)){
		data->tls = 1;
		hostspec += 8;
	}

	if(host){
		//parse credentials, separate out host spec
		*host = 0;
		host++;

		password = strchr(hostspec, ':');
		if(password){
			//password supplied, store
			*password = 0;
			password++;
			mmbackend_strdup(&(data->password), password);
		}

		//store username
		mmbackend_strdup(&(data->user), hostspec);
	}
	else{
		host = hostspec;
	}

	//parse port if supplied
	port = strchr(host, ':');
	if(port){
		*port = 0;
		port++;
		mmbackend_strdup(&(data->port), port);
	}

	mmbackend_strdup(&(data->host), host);
	return 0;
}

static size_t mqtt_varint_decode(uint8_t* buffer, uint32_t* result){
	size_t value = 0, offset = 0;
	do {
		value |= (buffer[offset] & 0x7F) << (7 * offset);
		offset++;
	} while(buffer[offset - 1] & 0x80);
	return 0;
}

static size_t mqtt_varint_encode(size_t value, size_t maxlen, uint8_t* buffer){
	//implementation conforming to spec 1.5.5
	size_t offset = 0;
	do {
		buffer[offset] = value % 128;
		value = value / 128;
		if(value){
			buffer[offset] |= 0x80;
		}
		offset++;
	} while(value);
	return offset;
}

static void mqtt_disconnect(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;

	//unmanage the fd
	mm_manage_fd(data->fd, BACKEND_NAME, 0, NULL);

	close(data->fd);
	data->fd = -1;
}

static int mqtt_transmit(instance* inst, uint8_t type, size_t vh_length, uint8_t* vh, size_t payload_length, uint8_t* payload){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	uint8_t fixed_header[5];
	size_t offset = 0;

	fixed_header[offset++] = type;
	offset += mqtt_varint_encode(vh_length + payload_length, sizeof(fixed_header) - offset, fixed_header + offset);

	if(mmbackend_send(data->fd, fixed_header, offset)
			|| mmbackend_send(data->fd, vh, vh_length)
			|| mmbackend_send(data->fd, payload, payload_length)){
		LOGPF("Failed to transmit control message for %s, assuming connection failure", inst->name);
		mqtt_disconnect(inst);
		return 1;
	}

	return 0;
}

static int mqtt_configure(char* option, char* value){
	LOG("This backend does not take global configuration");
	return 1;
}

static int mqtt_reconnect(instance* inst){
	uint8_t variable_header[MQTT_BUFFER_LENGTH] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x05, 0x00 /*flags*/, (MQTT_KEEPALIVE >> 8) & 0xFF, MQTT_KEEPALIVE & 0xFF};
	uint8_t payload[MQTT_BUFFER_LENGTH];
	size_t vh_offset = 10, payload_offset = 0;
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;

	if(!data->host){
		LOGPF("No host specified for instance %s", inst->name);
		return 2;
	}

	if(data->fd >= 0){
		mqtt_disconnect(inst);
	}

	LOGPF("Connecting instance %s to host %s port %s (TLS: %s, Authentication: %s)",
			inst->name, data->host,
			data->port ? data->port : (data->tls ? MQTT_TLS_PORT : MQTT_PORT),
			data->tls ? "yes " : "no",
			(data->user || data->password) ? "yes" : "no");

	data->fd = mmbackend_socket(data->host,
			data->port ? data->port : (data->tls ? MQTT_TLS_PORT : MQTT_PORT),
			SOCK_STREAM, 0, 0, 1);

	if(data->fd < 0){
		//retry later
		return 1;
	}

	//prepare CONNECT message
	variable_header[7] = 0x02 /*clean start*/ | (data->user ? 0x80 : 0x00) | (data->user ? 0x40 : 0x00);
	//TODO set session expiry interval option
	//TODO re-use previos session on reconnect
	
	//push number of option bytes (as a varint, no less) before actually pushing the option data.
	//obviously someone thought saving 3 whole bytes in exchange for not being able to sequentially creating the package was smart..
	variable_header[vh_offset++] = 7;
	//push maximum packet size option
	variable_header[vh_offset++] = 0x27;
	variable_header[vh_offset++] = (MQTT_BUFFER_LENGTH >> 24) & 0xFF;
	variable_header[vh_offset++] = (MQTT_BUFFER_LENGTH >> 16) & 0xFF;
	variable_header[vh_offset++] = (MQTT_BUFFER_LENGTH >> 8) & 0xFF;
	variable_header[vh_offset++] = (MQTT_BUFFER_LENGTH) & 0xFF;
	//push topic alias maximum option
	variable_header[vh_offset++] = 0x22;
	variable_header[vh_offset++] = 0xFF;
	variable_header[vh_offset++] = 0xFF;

	//push client_id as utf8
	//payload_offset += mqtt_push_utf8();
	if(data->user){
		//push user name as utf8
	}
	if(data->password){
		//push password as binary
	}

	mqtt_transmit(inst, MSG_CONNECT, vh_offset, variable_header, payload_offset, payload);

	//register the fd
	if(mm_manage_fd(data->fd, BACKEND_NAME, 1, (void*) inst)){
		LOG("Failed to register FD");
		return 2;
	}

	return 0;
}

static int mqtt_configure_instance(instance* inst, char* option, char* value){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;

	if(!strcmp(option, "user")){
		mmbackend_strdup(&(data->user), value);
		return 0;
	}
	else if(!strcmp(option, "password")){
		mmbackend_strdup(&(data->password), value);
		return 0;
	}
	else if(!strcmp(option, "host")){
		if(mqtt_parse_hostspec(inst, value)){
			return 1;
		}
		return 0;
	}

	LOGPF("Unknown instance configuration option %s on instance %s", option, inst->name);
	return 1;
}

static int mqtt_instance(instance* inst){
	mqtt_instance_data* data = calloc(1, sizeof(mqtt_instance_data));

	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->fd = -1;
	inst->impl = data;
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
	LOG("Handling");
	//TODO
	return 0;
}

static int mqtt_start(size_t n, instance** inst){
	size_t u = 0;

	for(u = 0; u < n; u++){
		switch(mqtt_reconnect(inst[u])){
			case 1:
				LOGPF("Failed to connect to host for instance %s, will be retried", inst[u]->name);
				break;
			case 2:
				LOGPF("Failed to connect to host for instance %s, aborting", inst[u]->name);
				return 1;
			default:
				break;
		}
	}

	return 0;
}

static int mqtt_shutdown(size_t n, instance** inst){
	size_t u, p;
	mqtt_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (mqtt_instance_data*) inst[u]->impl;
		mqtt_disconnect(inst[u]);

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
