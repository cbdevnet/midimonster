#define BACKEND_NAME "mqtt"
//#define DEBUG

#include <string.h>
#include <time.h>
#include <math.h>

#include "libmmbackend.h"
#include "mqtt.h"

static uint64_t last_maintenance = 0;
/* according to spec 2.2.2.2 */
static struct {
	uint8_t property;
	uint8_t storage;
} property_lengths[] = {
	{0x01, STORAGE_U8},
	{0x02, STORAGE_U32},
	{0x03, STORAGE_PREFIXED},
	{0x08, STORAGE_PREFIXED},
	{0x09, STORAGE_PREFIXED},
	{0x0B, STORAGE_VARINT},
	{0x11, STORAGE_U32},

	{0x12, STORAGE_PREFIXED},
	{0x13, STORAGE_U16},
	{0x15, STORAGE_PREFIXED},
	{0x16, STORAGE_PREFIXED},
	{0x17, STORAGE_U8},
	{0x18, STORAGE_U32},
	{0x19, STORAGE_U8},
	{0x1A, STORAGE_PREFIXED},
	{0x1C, STORAGE_PREFIXED},
	{0x1F, STORAGE_PREFIXED},
	{0x21, STORAGE_U16},
	{0x22, STORAGE_U16},
	{0x23, STORAGE_U16},
	{0x24, STORAGE_U8},
	{0x25, STORAGE_U8},
	{0x26, STORAGE_PREFIXPAIR},
	{0x27, STORAGE_U32},
	{0x28, STORAGE_U8},
	{0x29, STORAGE_U8},
	{0x2A, STORAGE_U8}
};

/*
 * TODO
 *	* proper RETAIN handling
 *	* TLS
 *	* JSON subchannels
 */

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
	char clientid[24] = "";

	snprintf(clientid, sizeof(clientid), "MIDIMonster-%d-%s", (uint32_t) time(NULL), inst->name);
	return mmbackend_strdup(&(data->client_id), clientid);
}

static size_t mqtt_pop_varint(uint8_t* buffer, size_t len, uint32_t* result){
	size_t value = 0, offset = 0;
	do {
		if(offset >= len){
			return 0;
		}

		value |= (buffer[offset] & 0x7F) << (7 * offset);
		offset++;
	} while(buffer[offset - 1] & 0x80);

	if(result){
		*result = value;
	}
	return offset;
}

static size_t mqtt_pop_property(uint8_t* buffer, size_t bytes){
	size_t length = 0, u;

	if(bytes){
		for(u = 0; u < sizeof(property_lengths)/sizeof(property_lengths[0]); u++){
			if(property_lengths[u].property == buffer[0]){
				switch(property_lengths[u].storage){
					case STORAGE_U8:
						return 2;
					case STORAGE_U16:
						return 3;
					case STORAGE_U32:
						return 5;
					case STORAGE_VARINT:
						return mqtt_pop_varint(buffer + 1, bytes - 1, NULL) + 1;
					case STORAGE_PREFIXED:
						if(bytes >= 3){
							return ((buffer[1] << 8) | buffer[2]) + 1;
						}
						//best-effort guess
						return 3;
					case STORAGE_PREFIXPAIR:
						if(bytes >= 3){
							length = ((buffer[1] << 8) | buffer[2]);
							if(bytes >= length + 5){
								return (1 + 2 + length + 2 + ((buffer[length + 3] << 8) | buffer[length + 4]));
							}
							return length + 3;
						}
						//best-effort guess
						return 5;
				}
			}
		}
	}

	LOGPF("Storage class for property %02X was unknown", buffer[0]);
	return 1;
}

static size_t mqtt_push_varint(size_t value, size_t maxlen, uint8_t* buffer){
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

static size_t mqtt_pop_utf8(uint8_t* buffer, size_t buffer_length, char** data){
	size_t length = 0;
	*data = NULL;

	if(buffer_length < 2){
		return 0;
	}

	length = (buffer[0] << 8) | buffer[1];
	if(buffer_length >= length + 2){
		*data = (char*) buffer + 2;
	}
	return length;
}

static void mqtt_disconnect(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	size_t u;

	data->last_control = 0;

	//reset aliases as they can not be reused across sessions
	data->server_max_alias = 0;
	data->current_alias = 1;
	for(u = 0; u < data->nchannels; u++){
		data->channel[u].topic_alias_sent = 0;
		data->channel[u].topic_alias_rcvd = 0;
	}

	//unmanage the fd
	mm_manage_fd(data->fd, BACKEND_NAME, 0, NULL);

	close(data->fd);
	data->fd = -1;
}

static int mqtt_transmit(instance* inst, uint8_t type, size_t vh_length, uint8_t* vh, size_t payload_length, uint8_t* payload){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	uint8_t fixed_header[5];
	size_t offset = 0;

	//how in the world is it a _fixed_ header if it contains a variable length integer? eh...
	fixed_header[offset++] = type;
	offset += mqtt_push_varint(vh_length + payload_length, sizeof(fixed_header) - offset, fixed_header + offset);

	if(mmbackend_send(data->fd, fixed_header, offset)
			|| (vh && vh_length && mmbackend_send(data->fd, vh, vh_length))
			|| (payload && payload_length && mmbackend_send(data->fd, payload, payload_length))){
		LOGPF("Failed to transmit control message for %s, assuming connection failure", inst->name);
		mqtt_disconnect(inst);
		return 1;
	}

	data->last_control = mm_timestamp();
	return 0;
}

static int mqtt_configure(char* option, char* value){
	LOG("This backend does not take global configuration");
	return 1;
}

static int mqtt_reconnect(instance* inst){
	uint8_t variable_header[MQTT_BUFFER_LENGTH] = {0x00, 0x04, 'M', 'Q', 'T', 'T', MQTT_VERSION_DEFAULT, 0x00 /*flags*/, ((MQTT_KEEPALIVE * 2) >> 8) & 0xFF, (MQTT_KEEPALIVE * 2) & 0xFF};
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

	LOGPF("Connecting instance %s to host %s port %s (TLS: %s, Authentication: %s, Protocol: %s)",
			inst->name, data->host,
			data->port ? data->port : (data->tls ? MQTT_TLS_PORT : MQTT_PORT),
			data->tls ? "yes " : "no",
			(data->user || data->password) ? "yes" : "no",
			(data->mqtt_version == 0x05) ? "v5" : "v3.1.1");

	data->fd = mmbackend_socket(data->host,
			data->port ? data->port : (data->tls ? MQTT_TLS_PORT : MQTT_PORT),
			SOCK_STREAM, 0, 0, 1);

	if(data->fd < 0){
		//retry later
		return 1;
	}

	//prepare CONNECT message header
	variable_header[6] = data->mqtt_version;
	variable_header[7] = 0x02 /*clean start*/ | (data->user ? 0x80 : 0x00) | (data->user ? 0x40 : 0x00);

	if(data->mqtt_version == 0x05){ //mqtt v5 has additional options
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
	}

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

static int mqtt_configure_channel(instance* inst, char* option, char* value){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	char* next_token = NULL;
	channel* configure = NULL;
	uint8_t mark = 0;
	mqtt_channel_value config = {
		0
	};

	if(!strncmp(value, "range ", 6)){
		//we support min > max for range configurations
		value += 6;

		config.min = strtod(value, &next_token);
		if(value == next_token){
			LOGPF("Failed to parse range preconfiguration for topic %s.%s", inst->name, option);
			return 1;
		}

		config.max = strtod(next_token, &value);
		if(value == next_token){
			LOGPF("Failed to parse range preconfiguration for topic %s.%s", inst->name, option);
			return 1;
		}
	}
	else if(!strncmp(value, "discrete ", 9)){
		value += 9;

		for(; *value && isspace(*value); value++){
		}
		if(value[0] == '!'){
			mark = 1;
			value++;
		}
		config.min = clamp(strtod(value, &next_token), 1.0, 0.0);
		value = next_token;

		for(; *value && isspace(*value); value++){
		}
		if(value[0] == '!'){
			mark = 2;
			value++;
		}

		config.max = clamp(strtod(value, &next_token), 1.0, 0.0);
		value = next_token;
		if(config.max < config.min){
			LOGPF("Discrete topic configuration for %s.%s has invalid limit ordering", inst->name, option);
			return 1;
		}

		for(; *value && isspace(*value); value++){
		}

		config.discrete = strdup(value);
		config.normal = mark ? ((mark == 1) ? config.min : config.max) : (config.min + (config.max - config.min) / 2);
	}
	else{
		LOGPF("Unknown instance configuration option or invalid preconfiguration %s on instance %s", option, inst->name);
		return 1;
	}

	configure = mqtt_channel(inst, option, 0);
	if(!configure
			//if configuring scale, no other config is possible
			|| (!config.discrete && data->channel[configure->ident].values)
			//if configuring discrete, the previous one can't be a a scale
			|| (config.discrete && data->channel[configure->ident].values && !data->channel[configure->ident].value[0].discrete)){
		LOGPF("Failed to configure topic %s.%s", inst->name, option);
		free(config.discrete);
		return 1;
	}

	data->channel[configure->ident].value = realloc(data->channel[configure->ident].value, (data->channel[configure->ident].values + 1) * sizeof(mqtt_channel_value));
	if(!data->channel[configure->ident].value){
		LOG("Failed to allocate memory");
		return 1;
	}

	DBGPF("Configuring value on %s.%s: min %f max %f normal %f discrete %s", inst->name, option, config.min, config.max, config.normal, config.discrete ? config.discrete : "-");
	data->channel[configure->ident].value[data->channel[configure->ident].values] = config;
	data->channel[configure->ident].values++;
	DBGPF("Value configuration for %s.%s now at %" PRIsize_t " entries", inst->name, option, data->channel[configure->ident].values);
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
	else if(!strcmp(option, "protocol")){
		data->mqtt_version = MQTT_VERSION_DEFAULT;
		if(!strcmp(value, "3.1.1")){
			data->mqtt_version = 4;
		}
		return 0;
	}

	//try to register as channel preconfig
	return mqtt_configure_channel(inst, option, value);
}

static int mqtt_push_subscriptions(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	uint8_t variable_header[3] = {0};
	uint8_t payload[MQTT_BUFFER_LENGTH];
	size_t u, subs = 0, payload_offset = 0;

	//FIXME might want to aggregate multiple subscribes into one packet
	for(u = 0; u < data->nchannels; u++){
		payload_offset = 0;
		if(data->channel[u].flags & mmchannel_input){
			DBGPF("Subscribing %s.%s, channel %" PRIsize_t ", flags %d", inst->name, data->channel[u].topic, u, data->channel[u].flags);
			variable_header[0] = (data->packet_identifier >> 8) & 0xFF;
			variable_header[1] = (data->packet_identifier) & 0xFF;

			payload_offset += mqtt_push_utf8(payload + payload_offset, sizeof(payload) - payload_offset, data->channel[u].topic);
			payload[payload_offset++] = (data->mqtt_version == 0x05) ? MQTT5_NO_LOCAL : 0;

			data->packet_identifier++;
			//zero is not a valid packet identifier
			if(!data->packet_identifier){
				data->packet_identifier++;
			}

			mqtt_transmit(inst, MSG_SUBSCRIBE, data->mqtt_version == 0x05 ? 3 : 2, variable_header, payload_offset, payload);
			subs++;
		}
	}

	LOGPF("Subscribed %" PRIsize_t " channels on %s", subs, inst->name);
	return 0;
}

static int mqtt_instance(instance* inst){
	mqtt_instance_data* data = calloc(1, sizeof(mqtt_instance_data));

	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->fd = -1;
	data->mqtt_version = MQTT_VERSION_DEFAULT;
	data->packet_identifier = 1;
	data->current_alias = 1;
	inst->impl = data;

	if(mqtt_generate_instanceid(inst)){
		return 1;
	}
	return 0;
}

static channel* mqtt_channel(instance* inst, char* spec, uint8_t flags){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	size_t u;

	//check spec for compliance
	if(strchr(spec, '+') || strchr(spec, '#')){
		LOGPF("Invalid character in channel specification %s", spec);
		return NULL;
	}

	//find matching channel
	for(u = 0; u < data->nchannels; u++){
		if(!strcmp(spec, data->channel[u].topic)){
			data->channel[u].flags |= flags;
			DBGPF("Reusing existing channel %" PRIsize_t " for spec %s.%s, flags are now %02X", u, inst->name, spec, data->channel[u].flags);
			break;
		}
	}

	//allocate new channel
	if(u == data->nchannels){
		data->channel = realloc(data->channel, (data->nchannels + 1) * sizeof(mqtt_channel_data));
		if(!data->channel){
			LOG("Failed to allocate memory");
			return NULL;
		}

		data->channel[u].topic = strdup(spec);
		data->channel[u].topic_alias_sent = 0;
		data->channel[u].topic_alias_rcvd = 0;
		data->channel[u].flags = flags;
		data->channel[u].values = 0;
		data->channel[u].value = NULL;

		if(!data->channel[u].topic){
			LOG("Failed to allocate memory");
			return NULL;
		}

		DBGPF("Allocated channel %" PRIsize_t " for spec %s.%s, flags are %02X", u, inst->name, spec, data->channel[u].flags);
		data->nchannels++;
	}

	return mm_channel(inst, u, 1);
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
		else if(data->last_control && mm_timestamp() - data->last_control >= MQTT_KEEPALIVE * 1000){
			//send keepalive ping requests
			mqtt_transmit(inst[u], MSG_PINGREQ, 0, NULL, 0, NULL);
		}
	}

	free(inst);
	return 0;
}

static int mqtt_deserialize(instance* inst, channel* output, mqtt_channel_data* input, char* buffer, size_t length){
	char* next_token = NULL, conversion_buffer[1024] = {0};
	channel_value val;
	double range, raw;
	size_t u;
	//FIXME implement json subchannels

	//unconfigured channel
	if(!input->values){
		//the original buffer is the result of an unterminated receive, move it over
		memcpy(conversion_buffer, buffer, length);
		val.normalised = clamp(strtod(conversion_buffer, &next_token), 1.0, 0.0);
		if(conversion_buffer == next_token){
			LOGPF("Failed to parse incoming data for %s.%s", inst->name, input->topic);
			return 1;
		}
	}
	//ranged channel
	else if(!input->value[0].discrete){
		memcpy(conversion_buffer, buffer, length);
		raw = clamp(strtod(conversion_buffer, &next_token), max(input->value[0].max, input->value[0].min), min(input->value[0].max, input->value[0].min));
		if(conversion_buffer == next_token){
			LOGPF("Failed to parse incoming data for %s.%s", inst->name, input->topic);
			return 1;
		}
		range = fabs(input->value[0].max - input->value[0].min);
		val.normalised = (raw - input->value[0].min) / range;
		if(input->value[0].max < input->value[0].min){
			val.normalised = fabs(val.normalised);
		}
	}
	else{
		for(u = 0; u < input->values; u++){
			if(length == strlen(input->value[u].discrete)
					&& !strncmp(input->value[u].discrete, buffer, length)){
				val.normalised = input->value[u].normal;
				break;
			}
		}

		if(u == input->values){
			LOGPF("Failed to parse incoming data for %s.%s, no matching discrete token", inst->name, input->topic);
			return 1;
		}
	}

	val.normalised = clamp(val.normalised, 1.0, 0.0);
	mm_channel_event(output, val);
	return 0;
}

static size_t mqtt_serialize(instance* inst, mqtt_channel_data* input, char* output, size_t length, double value){
	double range;
	size_t u, invert = 0;

	//unconfigured channel
	if(!input->values){
		return snprintf(output, length, "%f", value);
	}
	//ranged channel
	else if(!input->value[0].discrete){
		range = fabs(input->value[0].max - input->value[0].min);
		if(input->value[0].max < input->value[0].min){
			invert = 1;
		}
		return snprintf(output, length, "%f", (value * range) * (invert ? -1 : 1) + input->value[0].min);
	}
	else{
		for(u = 0; u < input->values; u++){
			if(input->value[u].min <= value
					&& input->value[u].max >= value){
				memcpy(output, input->value[u].discrete, min(strlen(input->value[u].discrete), length));
				return min(strlen(input->value[u].discrete), length);
			}
		}
	}

	LOGPF("No discrete value on %s.%s defined for normalized value %f", inst->name, input->topic, value);
	return 0;
}

static int mqtt_set(instance* inst, size_t num, channel** c, channel_value* v){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	uint8_t variable_header[MQTT_BUFFER_LENGTH];
	uint8_t payload[MQTT_BUFFER_LENGTH], alias_assigned = 0;
	size_t vh_length = 0, payload_length = 0, u;

	for(u = 0; u < num; u++){
		vh_length = payload_length = alias_assigned = 0;

		if(data->mqtt_version == 0x05){
			if(data->channel[c[u]->ident].topic_alias_sent){
				//push zero-length topic
				variable_header[vh_length++] = 0;
				variable_header[vh_length++] = 0;
			}
			else{
				//push topic
				vh_length += mqtt_push_utf8(variable_header + vh_length, sizeof(variable_header) - vh_length, data->channel[c[u]->ident].topic);
				//generate topic alias if possible
				if(data->current_alias <= data->server_max_alias){
					data->channel[c[u]->ident].topic_alias_sent = data->current_alias++;
					DBGPF("Assigned outbound topic alias %" PRIu16 " to topic %s.%s", data->channel[c[u]->ident].topic_alias_sent, inst->name, data->channel[c[u]->ident].topic);

					alias_assigned = 1;
				}
			}

			//push property length
			variable_header[vh_length++] = (data->channel[c[u]->ident].topic_alias_sent) ? 5 : 2;

			//push payload type (0x01)
			variable_header[vh_length++] = 0x01;
			variable_header[vh_length++] = 1;

			if(data->channel[c[u]->ident].topic_alias_sent){
				//push topic alias (0x23)
				variable_header[vh_length++] = 0x23;
				variable_header[vh_length++] = (data->channel[c[u]->ident].topic_alias_sent >> 8) & 0xFF;
				variable_header[vh_length++] = data->channel[c[u]->ident].topic_alias_sent & 0xFF;
			}

			payload_length = mqtt_serialize(inst, data->channel + c[u]->ident, (char*) (payload + 2), sizeof(payload) - 2, v[u].normalised);
			if(payload_length){
				payload[0] = (payload_length >> 8) & 0xFF;
				payload[1] = payload_length & 0xFF;
				payload_length += 2;
			}
		}
		else{
			//push topic
			vh_length += mqtt_push_utf8(variable_header + vh_length, sizeof(variable_header) - vh_length, data->channel[c[u]->ident].topic);
			if(data->mqtt_version == 0x05){
				//push property length
				variable_header[vh_length++] = 2;

				//push payload type (0x01)
				variable_header[vh_length++] = 0x01;
				variable_header[vh_length++] = 1;
			}
			payload_length = mqtt_serialize(inst, data->channel + c[u]->ident, (char*) payload, sizeof(payload), v[u].normalised);
		}

		if(payload_length){
			DBGPF("Transmitting %" PRIsize_t " bytes for %s", payload_length, inst->name);
			mqtt_transmit(inst, MSG_PUBLISH, vh_length, variable_header, payload_length, payload);
		}
		else if(alias_assigned){
			//undo alias assignment
			data->channel[c[u]->ident].topic_alias_sent = 0;
			data->current_alias--;
		}
	}

	return 0;
}

static int mqtt_handle_publish(instance* inst, uint8_t type, uint8_t* variable_header, size_t length){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	char* topic = NULL, *payload = NULL;
	channel* changed = NULL;
	uint8_t qos = (type & 0x06) >> 1, content_utf8 = 0;
	uint16_t topic_alias = 0;
	uint32_t property_length = 0;
	size_t u = data->nchannels, property_offset, payload_offset, payload_length;
	size_t topic_length = mqtt_pop_utf8(variable_header, length, &topic);

	property_offset = payload_offset = topic_length + 2 + ((qos > 0) ? 2 : 0);
	if(data->mqtt_version == 0x05){
		//read properties length
		payload_offset += mqtt_pop_varint(variable_header + property_offset, length - property_offset, &property_length);
		payload_offset += property_length;

		property_offset += mqtt_pop_varint(variable_header + property_offset, length - property_offset, NULL);
		//parse properties
		while(property_offset < payload_offset && property_offset < length){
			DBGPF("Property %02X at offset %" PRIsize_t " of %" PRIu32, variable_header[property_offset], property_offset, property_length);

			//read payload format indicator
			if(variable_header[property_offset] == 0x01 && property_offset < length - 1){
				content_utf8 = variable_header[property_offset + 1];
			}
			//read topic alias
			else if(variable_header[property_offset] == 0x23 && property_offset < length - 2){
				topic_alias = (variable_header[property_offset + 1] << 8) | variable_header[property_offset + 2];
			}

			property_offset += mqtt_pop_property(variable_header + property_offset, length - property_offset);
		}
	}

	//match via topic alias
	if(!topic_length && topic_alias){
		for(u = 0; u < data->nchannels; u++){
			if(data->channel[u].topic_alias_rcvd == topic_alias){
				break;
			}
		}
	}
	//match via topic
	else if(topic_length){
		for(u = 0; u < data->nchannels; u++){
			if(!strncmp(data->channel[u].topic, topic, topic_length)){
				break;
			}
		}

		if(topic_alias){
			data->channel[u].topic_alias_rcvd = topic_alias;
		}
	}

	if(content_utf8){
		payload_length = mqtt_pop_utf8(variable_header + payload_offset, length - payload_offset, &payload);
	}
	else{
		payload_length = length - payload_offset;
		payload = (char*) (variable_header + payload_offset);
	}

	if(u != data->nchannels && payload_length && payload){
		DBGPF("Received PUBLISH for %s.%s, QoS %d, payload length %" PRIsize_t, inst->name, data->channel[u].topic, qos, payload_length);
		changed = mm_channel(inst, u, 0);
		if(changed){
			mqtt_deserialize(inst, changed, data->channel + u, payload, payload_length);
		}
	}
	return 0;
}

static int mqtt_handle_connack(instance* inst, uint8_t type, uint8_t* variable_header, size_t length){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	size_t property_offset = 2;

	if(length >= 2){
		if(variable_header[1]){
			if(variable_header[1] == 1 && data->mqtt_version == 0x05){
				LOGPF("Connection on %s was rejected for protocol incompatibility, downgrading to protocol 3.1.1", inst->name);
				data->mqtt_version = 0x04;
				return 0;
			}
			LOGPF("Connection on %s was rejected, reason code %d", inst->name, variable_header[1]);
			mqtt_disconnect(inst);
			return 0;
		}

		//parse response properties if present
		if(data->mqtt_version == 0x05){
			property_offset += mqtt_pop_varint(variable_header + property_offset, length - property_offset, NULL);
			while(property_offset < length){
				DBGPF("Property %02X at offset %" PRIsize_t " of %" PRIsize_t, variable_header[property_offset], property_offset, length);

				//read maximum topic alias
				if(variable_header[property_offset] == 0x22){
					data->server_max_alias = (variable_header[property_offset + 1] << 8) | variable_header[property_offset + 2];
					DBGPF("Connection supports maximum connection alias %" PRIu16, data->server_max_alias);
				}

				property_offset += mqtt_pop_property(variable_header + property_offset, length - property_offset);
			}
		}

		LOGPF("Connection on %s established", inst->name);
		return mqtt_push_subscriptions(inst);
	}

	LOGPF("Received malformed CONNACK on %s", inst->name);
	return 1;
}

static int mqtt_handle_message(instance* inst, uint8_t type, uint8_t* variable_header, size_t length){
	switch(type){
		case MSG_CONNACK:
			return mqtt_handle_connack(inst, type, variable_header, length);
		case MSG_PINGRESP:
		case MSG_SUBACK:
			//ignore most responses
			//FIXME error check SUBACK
			break;
		default:
			if((type & 0xF0) == MSG_PUBLISH){
				return mqtt_handle_publish(inst, type, variable_header, length);
			}
			LOGPF("Unhandled MQTT message type 0x%02X on %s", type, inst->name);
	}
	return 0;
}

static int mqtt_handle_fd(instance* inst){
	mqtt_instance_data* data = (mqtt_instance_data*) inst->impl;
	ssize_t bytes_read = 0, bytes_left = sizeof(data->receive_buffer) - data->receive_offset;
	uint32_t message_length = 0, header_length = 0;

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
	data->receive_offset += bytes_read;

	while(data->receive_offset >= 2){
		//check for complete message
		header_length = mqtt_pop_varint(data->receive_buffer + 1, data->receive_offset - 1, &message_length);
		if(header_length && data->receive_offset >= message_length + header_length + 1){
			DBGPF("Received complete message of %" PRIu32 " bytes, total received %" PRIsize_t ", payload %" PRIu32 ", message type %02X", message_length + header_length + 1, data->receive_offset, message_length, data->receive_buffer[0]);
			if(mqtt_handle_message(inst, data->receive_buffer[0], data->receive_buffer + header_length + 1, message_length)){
				//TODO handle failures properly
			}

			//remove handled message
			if(data->receive_offset > message_length + header_length + 1){
				memmove(data->receive_buffer, data->receive_buffer + message_length + header_length + 1, data->receive_offset - (message_length + header_length + 1));
			}
			data->receive_offset -= message_length + header_length + 1;
		}
		else{
			break;
		}
	}

	return 0;
}

static int mqtt_handle(size_t num, managed_fd* fds){
	size_t n = 0;

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
	size_t u = 0, fds = 0;

	for(u = 0; u < n; u++){
		switch(mqtt_reconnect(inst[u])){
			case 1:
				LOGPF("Failed to connect to host for instance %s, will be retried", inst[u]->name);
				break;
			case 2:
				LOGPF("Failed to connect to host for instance %s, aborting", inst[u]->name);
				return 1;
			default:
				fds++;
				break;
		}
	}
	LOGPF("Registered %" PRIsize_t " descriptors to core", fds);

	//initialize maintenance timer
	last_maintenance = mm_timestamp();
	return 0;
}

static int mqtt_shutdown(size_t n, instance** inst){
	size_t u, p, v;
	mqtt_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (mqtt_instance_data*) inst[u]->impl;
		mqtt_disconnect(inst[u]);

		for(p = 0; p < data->nchannels; p++){
			for(v = 0; v < data->channel[p].values; v++){
				free(data->channel[p].value[v].discrete);
			}
			free(data->channel[p].value);
			free(data->channel[p].topic);
		}
		free(data->channel);
		free(data->host);
		free(data->port);
		free(data->user);
		free(data->password);
		free(data->client_id);

		free(inst[u]->impl);
		inst[u]->impl = NULL;
	}

	LOG("Backend shut down");
	return 0;
}
