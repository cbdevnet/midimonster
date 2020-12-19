#define BACKEND_NAME "mqtt"
#define DEBUG

#include <string.h>
#include <time.h>

#include "libmmbackend.h"
#include "mqtt.h"

static uint64_t last_maintenance = 0;

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

static int mqtt_generate_instanceid(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	char clientid[23] = "";

	snprintf(clientid, sizeof(clientid), "MIDIMonster-%d-%s", (uint32_t) time(NULL), inst->name);
	return mmbackend_strdup(&(data->client_id), clientid);
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

static size_t mqtt_push_binary(uint8_t* buffer, size_t buffer_length, uint8_t* content, size_t length){
	if(buffer_length < length + 2 || length > 65535){
		LOG("Failed to push length-prefixed data blob, buffer size exceeded");
		return 0;
	}

	buffer[0] = (length >> 8) & 0xFF;
	buffer[1] = length & 0xFF;

	memcpy(buffer + 2, content, length);
	return length + 2;
}

static size_t mqtt_push_utf8(uint8_t* buffer, size_t buffer_length, char* content){
	//FIXME might want to validate the string for valid UTF-8
	return mqtt_push_binary(buffer, buffer_length, (uint8_t*) content, strlen(content));
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
	uint8_t variable_header[MQTT_BUFFER_LENGTH] = {0x00, 0x04, 'M', 'Q', 'T', 'T', MQTT_VERSION, 0x00 /*flags*/, (MQTT_KEEPALIVE >> 8) & 0xFF, MQTT_KEEPALIVE & 0xFF};
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

	//prepare CONNECT message flags
	variable_header[7] = 0x02 /*clean start*/ | (data->user ? 0x80 : 0x00) | (data->user ? 0x40 : 0x00);

	//TODO set session expiry interval option
	//TODO re-use previos session on reconnect

	//push number of option bytes (as a varint, no less) before actually pushing the option data.
	//obviously someone thought saving 3 whole bytes in exchange for not being able to sequentially creating the package was smart..
	variable_header[vh_offset++] = 8;
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

	//prepare CONNECT payload
	//push client id
	payload_offset += mqtt_push_utf8(payload + payload_offset, sizeof(payload) - payload_offset, data->client_id);
	if(data->user){
		payload_offset += mqtt_push_utf8(payload + payload_offset, sizeof(payload) - payload_offset, data->user);
	}
	if(data->password){
		payload_offset += mqtt_push_utf8(payload + payload_offset, sizeof(payload) - payload_offset, data->password);
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
	else if(!strcmp(option, "clientid")){
		if(strlen(value)){
			mmbackend_strdup(&(data->client_id), value);
			return 0;
		}
		else{
			return mqtt_generate_instanceid(inst);
		}
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

	if(mqtt_generate_instanceid(inst)){
		return 1;
	}
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

static int mqtt_maintenance(){
	size_t n, u;
	instance** inst = NULL;
	mqtt_instance_data* data = NULL;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		LOG("Failed to fetch instance list");
		return 1;
	}

	DBGPF("Running maintenance operations on %" PRIsize_t " instances", n);
	for(u = 0; u < n; u++){
       		data = (mqtt_instance_data*) inst[u]->impl;
		if(data->fd <= 0){
			if(mqtt_reconnect(inst[u]) >= 2){
				LOGPF("Failed to reconnect instance %s, terminating", inst[u]->name);
				free(inst);
				return 1;
			}
		}
	}

	free(inst);
	return 0;
}

static int mqtt_handle_fd(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	ssize_t bytes_read = 0, bytes_left = sizeof(data->receive_buffer) - data->receive_offset;

	bytes_read = recv(data->fd, data->receive_buffer + data->receive_offset, bytes_left, 0);
	if(bytes_read < 0){
		LOGPF("Failed to receive data on instance %s: %s", inst->name, mmbackend_socket_strerror(errno));
		return 1;
	}
	else if(bytes_read == 0){
		//disconnected, try to reconnect
		LOGPF("Instance %s disconnected, reconnection queued", inst->name);
		mqtt_disconnect(inst);
		return 1;
	}

	DBGPF("Instance %s, offset %" PRIsize_t ", read %" PRIsize_t " bytes", inst->name, data->receive_offset, bytes_read);

	return 0;
}

static int mqtt_handle(size_t num, managed_fd* fds){
	size_t n = 0;
	int rv = 0;

	for(n = 0; n < num; n++){
		if(mqtt_handle_fd((instance*) fds[n].impl) >= 2){
			//propagate critical failures
			return 1;
		}
	}

	//keepalive/reconnect processing
	if(last_maintenance && mm_timestamp() - last_maintenance >= MQTT_KEEPALIVE * 1000){
		if(mqtt_maintenance()){
			return 1;
		}
		last_maintenance = mm_timestamp();
	}

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

	//initialize maintenance timer
	last_maintenance = mm_timestamp();
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
