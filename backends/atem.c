#define BACKEND_NAME "atem"
#include <string.h>

#define DEBUG
#include "atem.h"
#include "libmmbackend.h"

MM_PLUGIN_API int init(){
	backend atem = {
		.name = BACKEND_NAME,
		.conf = atem_configure,
		.create = atem_instance,
		.conf_instance = atem_configure_instance,
		.channel = atem_channel,
		.handle = atem_set,
		.process = atem_handle,
		.start = atem_start,
		.shutdown = atem_shutdown,
		.interval = atem_interval
	};

	//register backend
	if(mm_backend_register(atem)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int atem_send(instance* inst, uint8_t* payload, size_t payload_len){
	atem_instance_data* data = (atem_instance_data*) inst->impl;

	struct {
		atem_hdr hdr;
		uint8_t payload[ATEM_PAYLOAD_MAX];
	} tx = {
		.hdr = data->txhdr
	};

	memcpy(tx.payload, payload, min(payload_len, ATEM_PAYLOAD_MAX));

	if(data->txhdr.session == 0){
		//set hello bit
		tx.hdr.length = htobe16(0x1000 | (sizeof(atem_hdr) + payload_len));
	}

	DBGPF("Sending %" PRIsize_t " bytes", sizeof(atem_hdr) + payload_len);
	if(mmbackend_send(data->fd, (uint8_t*) &tx, sizeof(atem_hdr) + payload_len)){
		LOGPF("Failed to send on instance %s", inst->name);
		return 1;
	}
	return 0;
}

static int atem_connect(instance* inst){
	atem_instance_data* data = (atem_instance_data*) inst->impl;

	if(data->fd < 0){
		LOGPF("No host configured on instance %s", inst->name);
		return 1;
	}

	uint8_t hello_payload[8] = {
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};

	return atem_send(inst, hello_payload, sizeof(hello_payload));
}

static uint32_t atem_interval(){
	//TODO
	return 0;
}

static int atem_configure(char* option, char* value){
	LOG("No backend configuration possible");
	return 1;
}

static int atem_configure_instance(instance* inst, char* option, char* value){
	char* host = NULL, *port = NULL;
	atem_instance_data* data = (atem_instance_data*) inst->impl;

	if(!strcmp(option, "host")){
		mmbackend_parse_hostspec(value, &host, &port, NULL);

		if(!host){
			LOGPF("%s is not a valid address", value);
			return 1;
		}

		data->fd = mmbackend_socket(host, port ? port : ATEM_DEFAULT_PORT, SOCK_DGRAM, 0, 0, 1);
		if(data->fd < 0){
			LOGPF("Failed to connect to host %s", value);
			return 1;
		}
		return 0;
	}

	return 0;
}

static int atem_instance(instance* inst){
	atem_instance_data* data = calloc(1, sizeof(atem_instance_data));
	inst->impl = data;
	if(!inst->impl){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->fd = -1;
	return 0;
}

static channel* atem_channel(instance* inst, char* spec, uint8_t flags){
	//TODO
	return NULL;
}

static int atem_set(instance* inst, size_t num, channel** c, channel_value* v){
	//TODO
	return 0;
}

static int atem_handle(size_t num, managed_fd* fds){
	size_t u;
	instance* inst = NULL;
	atem_instance_data* data = NULL;
	ssize_t bytes;
	uint8_t recv_buf[sizeof(atem_hdr) + ATEM_PAYLOAD_MAX];

	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		data = (atem_instance_data*) inst->impl;
		bytes = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
		if(bytes < 0){
			LOGPF("Failed to receive data for instance %s", inst->name);
			return 1;
		}

		DBGPF("Received %" PRIsize_t " bytes of data on instance %s", bytes, inst->name);
	}
	return 0;
}

static int atem_start(size_t n, instance** inst){
	size_t u;
	atem_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (atem_instance_data*) inst[u]->impl;

		//connect the instance
		if(atem_connect(inst[u])){
			return 1;
		}

		if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u])){
			return 1;
		}
	}

	LOGPF("Registered %" PRIsize_t " descriptors to core", n);
	return 0;
}

static int atem_shutdown(size_t n, instance** inst){
	//TODO
	LOG("Backend shut down");
	return 0;
}
