#define BACKEND_NAME "atem"
#include <string.h>

#define DEBUG
#include "atem.h"
#include "libmmbackend.h"

//#include "../tests/hexdump.c"

typedef int (*atem_command_handler)(instance*, size_t, uint8_t*);
typedef struct {
	char command[4];
	atem_command_handler handler;
} atem_command_mapping;

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

	if(payload_len){
		memcpy(tx.payload, payload, min(payload_len, ATEM_PAYLOAD_MAX));
	}

	if(data->established){
		tx.hdr.length = htobe16(ATEM_RESPONSE_EXPECTED | (sizeof(atem_hdr) + payload_len));
		data->txhdr.seqno++;
	}
	else{
		tx.hdr.length = htobe16(ATEM_HELLO | (sizeof(atem_hdr) + payload_len));
	}

	tx.hdr.session = htobe16(tx.hdr.session);
	tx.hdr.ack = htobe16(tx.hdr.ack);
	tx.hdr.seqno = htobe16(tx.hdr.seqno);

	DBGPF("Sending %" PRIsize_t " bytes, seqno %d", sizeof(atem_hdr) + payload_len, be16toh(tx.hdr.seqno));
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

int atem_handle_time(instance* inst, size_t n, uint8_t* data){
	//TODO
	//12 2D 3A 0C 00 00 00 00
	//12 2D 3A 0C 00 00 00 00
	return 0;
}

int atem_handle_tally_index(instance* inst, size_t n, uint8_t* data){
	//LOGPF("Handling index tally on %s", inst->name);
	//hex_dump(data + 8, n - 8);
	//1g 3r -> 00 04 02 00 01 00 00 47
	//bit 0 -> active/red
	//bit 1 -> preview/green
	//[nsources u16] [c1 c2 c3 c4] [00 00]
	return 0;
}

int atem_handle_tally_source(instance* inst, size_t n, uint8_t* data){
	//LOGPF("Handling source tally on %s", inst->name);
	//hex_dump(data + 8, n - 8);
	return 0;
}

int atem_handle_preview(instance* inst, size_t n, uint8_t* data){
	//LOGPF("Preview changed on %s", inst->name);
	//1/connect -> 00 0C 00 01 00 6D 70 6C
	//2 -> 00 06 00 02 00 FF FF FF
	//1 -> 00 06 00 01 00 FF FF FF
	//[in x] -> 00 06 00 xx 00 FF FF FF
	//still -> 00 06 0B C2 00 FF FF FF
	//black -> x=0
	//colors?
	return 0;
}

int atem_handle_program(instance* inst, size_t n, uint8_t* data){
	//LOGPF("Program changed on %s", inst->name);
	//hex_dump(data + 8, n - 8);
	//1/connect -> 00 0C 00 01
	//2 -> 00 00 00 02
	//1 -> 00 00 00 01
	//still -> 00 00 0B C2
	//black -> 00 06 00 00
	//black -> 00 00 00 00
	//[00 XX] [src] 
	//xx = 06 between non-cam sources?
	return 0;
}

int atem_handle_tbar(instance* inst, size_t n, uint8_t* data){
	LOGPF("T-Bar moved on %s", inst->name);
	//hex_dump(data + 8, n - 8);
	return 0;
}

static atem_command_mapping atem_command_map[] = {
	{"Time", atem_handle_time},
	{"TlIn", atem_handle_tally_index},
	{"TlSr", atem_handle_tally_source},
	{"PrvI", atem_handle_preview},
	{"PrgI", atem_handle_program},
	{"TrPs", atem_handle_tbar}
};

static int atem_process(instance* inst, atem_hdr* hdr, uint8_t* payload, size_t payload_len){
	atem_instance_data* data = (atem_instance_data*) inst->impl;
	uint8_t payload_buffer[ATEM_PAYLOAD_MAX] = "";
	uint16_t* payload_u16 = NULL;
	char* payload_str = NULL;
	size_t offset = 0, n;

	if(data->txhdr.session && hdr->session != data->txhdr.session){
		LOGPF("Received data for unknown session %04X on %s (session %04X)", hdr->session, inst->name, data->txhdr.session);
		return 0;
	}
	else if(!data->txhdr.session && hdr->session){
		data->txhdr.session = hdr->session;
		LOGPF("Updated session on %s to %04X", inst->name, hdr->session);
	}

	if(hdr->length & ATEM_HELLO){
		LOGPF("Received hello from peer on instance %s", inst->name);
		data->established = 1;
		return atem_send(inst, NULL, 0);
	}

	//read commands if present
	for(offset = 0; offset + 8 < payload_len;){
		payload_u16 = (uint16_t*) (payload + offset);
		payload_str = (char*) (payload + offset + 4);

		if(offset + be16toh(*payload_u16) <= payload_len){
			//find a handler and call it
			for(n = 0; n < sizeof(atem_command_map) / sizeof(atem_command_map[0]); n++){
				if(!memcmp(atem_command_map[n].command, payload_str, 4)){
					atem_command_map[n].handler(inst, be16toh(*payload_u16), payload + offset);
					break;
				}
			}

			if(n == sizeof(atem_command_map) / sizeof(atem_command_map[0])){
				DBGPF("%d bytes of data for command %.*s, no handler found", be16toh(*payload_u16), 4, payload_str);
			}

			//advance to next command
			offset += be16toh(*payload_u16);
		}
		else{
			LOGPF("Short read on %s for command %.*s, %d indicated, %" PRIsize_t " left", inst->name, 4, payload_str, be16toh(*payload_u16), payload_len - offset);
			break;
		}
	}

	if(hdr->length & ATEM_RESPONSE_EXPECTED){
		data->txhdr.ack = hdr->seqno;
		DBGPF("Response expected, acknowledging packet %d on %s", hdr->seqno, inst->name);
		return atem_send(inst, NULL, 0);
	}

	return 1;
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
	ssize_t bytes;
	struct {
		atem_hdr hdr;
		uint8_t payload[ATEM_PAYLOAD_MAX];
	} rx = {
		0
	};

	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		bytes = recv(fds[u].fd, &rx, sizeof(rx), 0);
		if(bytes < 0){
			LOGPF("Failed to receive data for instance %s", inst->name);
			return 1;
		}

		if(bytes >= sizeof(atem_hdr)){
			//swap byteorder
			rx.hdr.length = be16toh(rx.hdr.length);
			rx.hdr.session = be16toh(rx.hdr.session);
			rx.hdr.ack = be16toh(rx.hdr.ack);
			rx.hdr.seqno = be16toh(rx.hdr.seqno);
			DBGPF("Received %" PRIsize_t " bytes (%u bytes indicated) for session %04X (ack %04X seqno %04X) on instance %s",
					bytes, ATEM_LENGTH(rx.hdr.length), rx.hdr.session, rx.hdr.ack, rx.hdr.seqno, inst->name);
			atem_process(inst, &rx.hdr, rx.payload, bytes - sizeof(atem_hdr));
		}
		else{
			DBGPF("Received %" PRIsize_t " bytes of short message data on instance %s", bytes, inst->name);
		}
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
