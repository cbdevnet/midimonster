#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef MAWEB_NO_LIBSSL
#include <openssl/md5.h>
#endif

#include "libmmbackend.h"
#include "maweb.h"

#define BACKEND_NAME "maweb"
#define WS_LEN(a) ((a) & 0x7F)
#define WS_OP(a) ((a) & 0x0F)
#define WS_FLAG_FIN 0x80
#define WS_FLAG_MASK 0x80

static uint64_t last_keepalive = 0;

static char* cmdline_keys[] = {
	"SET",
	"PREV",
	"NEXT",
	"CLEAR",
	"FIXTURE_CHANNEL",
	"FIXTURE_GROUP_PRESET",
	"EXEC_CUE",
	"STORE_UPDATE",
	"OOPS",
	"ESC",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"PUNKT",
	"PLUS",
	"MINUS",
	"THRU",
	"IF",
	"AT",
	"FULL",
	"HIGH",
	"ENTER",
	"OFF",
	"ON",
	"ASSIGN",
	"LABEL",
	"COPY",
	"TIME",
	"PAGE",
	"MACRO",
	"DELETE",
	"GOTO",
	"GO_PLUS",
	"GO_MINUS",
	"PAUSE",
	"SELECT",
	"FIXTURE",
	"SEQU",
	"CUE",
	"PRESET",
	"EDIT",
	"UPDATE",
	"EXEC",
	"STORE",
	"GROUP",
	"PROG_ONLY",
	"SPECIAL_DIALOGUE",
	"SOLO",
	"ODD",
	"EVEN",
	"WINGS",
	"RESET",
	"MA",
	"layerMode",
	"featureSort",
	"fixtureSort",
	"channelSort",
	"hideName"
};

int init(){
	backend maweb = {
		.name = BACKEND_NAME,
		.conf = maweb_configure,
		.create = maweb_instance,
		.conf_instance = maweb_configure_instance,
		.channel = maweb_channel,
		.handle = maweb_set,
		.process = maweb_handle,
		.start = maweb_start,
		.shutdown = maweb_shutdown
	};

	if(sizeof(maweb_channel_ident) != sizeof(uint64_t)){
		fprintf(stderr, "maweb channel identification union out of bounds\n");
		return 1;
	}

	//register backend
	if(mm_backend_register(maweb)){
		fprintf(stderr, "Failed to register maweb backend\n");
		return 1;
	}
	return 0;
}

static int maweb_configure(char* option, char* value){
	fprintf(stderr, "The maweb backend does not take any global configuration\n");
	return 1;
}

static int maweb_configure_instance(instance* inst, char* option, char* value){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	char* host = NULL, *port = NULL;
	#ifndef MAWEB_NO_LIBSSL
	uint8_t password_hash[MD5_DIGEST_LENGTH];
	#endif

	if(!strcmp(option, "host")){
		mmbackend_parse_hostspec(value, &host, &port);
		if(!host){
			fprintf(stderr, "Invalid host specified for maweb instance %s\n", inst->name);
			return 1;
		}
		free(data->host);
		data->host = strdup(host);
		free(data->port);
		data->port = NULL;
		if(port){
			data->port = strdup(port);
		}
		return 0;
	}
	else if(!strcmp(option, "user")){
		free(data->user);
		data->user = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "password")){
		#ifndef MAWEB_NO_LIBSSL
		size_t n;
		MD5((uint8_t*) value, strlen(value), (uint8_t*) password_hash);
		data->pass = realloc(data->pass, (2 * MD5_DIGEST_LENGTH + 1) * sizeof(char));
		for(n = 0; n < MD5_DIGEST_LENGTH; n++){
			snprintf(data->pass + 2 * n, 3, "%02x", password_hash[n]);
		}
		return 0;
		#else
		fprintf(stderr, "This build of the maweb backend only supports the default password\n");
		return 1;
		#endif
	}

	fprintf(stderr, "Unknown configuration parameter %s for manet instance %s\n", option, inst->name);
	return 1;
}

static instance* maweb_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	maweb_instance_data* data = calloc(1, sizeof(maweb_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	data->fd = -1;
	data->buffer = calloc(MAWEB_RECV_CHUNK, sizeof(uint8_t));
	if(!data->buffer){
		fprintf(stderr, "Failed to allocate memory\n");
		free(data);
		return NULL;
	}
	data->allocated = MAWEB_RECV_CHUNK;

	inst->impl = data;
	return inst;
}

static channel* maweb_channel(instance* inst, char* spec){
	maweb_channel_ident ident = {
		.label = 0
	};
	char* next_token = NULL;
	size_t n;

	if(!strncmp(spec, "page", 4)){
		ident.fields.page = strtoul(spec + 4, &next_token, 10);
		if(*next_token != '.'){
			fprintf(stderr, "Failed to parse maweb channel spec %s: Missing separator\n", spec);
			return NULL;
		}

		next_token++;
		if(!strncmp(next_token, "fader", 5)){
			ident.fields.type = exec_fader;
			next_token += 5;
		}
		else if(!strncmp(next_token, "upper", 5)){
			ident.fields.type = exec_upper;
			next_token += 5;
		}
		else if(!strncmp(next_token, "lower", 5)){
			ident.fields.type = exec_lower;
			next_token += 5;
		}
		else if(!strncmp(next_token, "flash", 5)){
			ident.fields.type = exec_flash;
			next_token += 5;
		}
		else if(!strncmp(next_token, "button", 6)){
			ident.fields.type = exec_button;
			next_token += 6;
		}
		ident.fields.index = strtoul(next_token, NULL, 10);

		//fix up the identifiers for button execs
		if(ident.fields.index > 100){
			ident.fields.index -= 100;
		}
	}
	else{
		for(n = 0; n < sizeof(cmdline_keys) / sizeof(char*); n++){
			if(!strcmp(spec, cmdline_keys[n])){
				ident.fields.type = cmdline_button;
				ident.fields.index = n + 1;
				ident.fields.page = 1;
				break;
			}
		}
	}

	if(ident.fields.type && ident.fields.index && ident.fields.page
			&& ident.fields.index <= 90){
		//actually, those are zero-indexed...
		ident.fields.index--;
		ident.fields.page--;
		return mm_channel(inst, ident.label, 1);
	}
	fprintf(stderr, "Failed to parse maweb channel spec %s\n", spec);
	return NULL;
}

static int maweb_send_frame(instance* inst, maweb_operation op, uint8_t* payload, size_t len){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	uint8_t frame_header[MAWEB_FRAME_HEADER_LENGTH] = "";
	size_t header_bytes = 2;
	uint16_t* payload_len16 = (uint16_t*) (frame_header + 2);
	uint64_t* payload_len64 = (uint64_t*) (frame_header + 2);

	frame_header[0] = WS_FLAG_FIN | op;
	if(len <= 125){
		frame_header[1] = WS_FLAG_MASK | len;
	}
	else if(len <= 0xFFFF){
		frame_header[1] = WS_FLAG_MASK | 126;
		*payload_len16 = htobe16(len);
		header_bytes += 2;
	}
	else{
		frame_header[1] = WS_FLAG_MASK | 127;
		*payload_len64 = htobe64(len);
		header_bytes += 8;
	}
	//send a zero masking key because masking is stupid
	header_bytes += 4;

	if(mmbackend_send(data->fd, frame_header, header_bytes)
			|| mmbackend_send(data->fd, payload, len)){
		return 1;
	}

	return 0;
}

static int maweb_handle_message(instance* inst, char* payload, size_t payload_length){
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	char* field;
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;

	fprintf(stderr, "maweb message (%lu): %s\n", payload_length, payload);
	if(json_obj(payload, "session") == JSON_NUMBER){
		data->session = json_obj_int(payload, "session", data->session);
		fprintf(stderr, "maweb session id is now %ld\n", data->session);
	}

	if(json_obj_bool(payload, "forceLogin", 0)){
		fprintf(stderr, "maweb sending user credentials\n");
		snprintf(xmit_buffer, sizeof(xmit_buffer),
				"{\"requestType\":\"login\",\"username\":\"%s\",\"password\":\"%s\",\"session\":%ld}",
				data->user, data->pass, data->session);
		maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
	}

	if(json_obj(payload, "status") && json_obj(payload, "appType")){
		fprintf(stderr, "maweb connection established\n");
		field = json_obj_str(payload, "appType", NULL);
		if(!strncmp(field, "dot2", 4)){
			fprintf(stderr, "maweb peer detected as dot2, forcing user name 'remote'\n");
			free(data->user);
			data->user = strdup("remote");
		}
		maweb_send_frame(inst, ws_text, (uint8_t*) "{\"session\":0}", 13);
	}

	if(json_obj(payload, "responseType") == JSON_STRING){
		field = json_obj_str(payload, "responseType", NULL);
		if(!strncmp(field, "login", 5)){
			if(json_obj_bool(payload, "result", 0)){
				fprintf(stderr, "maweb login successful\n");
				data->login = 1;
			}
			else{
				fprintf(stderr, "maweb login failed\n");
				data->login = 0;
			}
		}
		else if(!strncmp(field, "getdata", 7)){
			//FIXME stupid keepalive logic
			snprintf(xmit_buffer, sizeof(xmit_buffer),
					"{\"requestType\":\"getdata\","
					"\"data\":\"set,clear,solo,high\","
					"\"realtime\":true,"
					"\"maxRequests\":10,"
					",\"session\":%ld}",
					data->session);
			maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
		}
	}

	return 0;
}

static int maweb_connect(instance* inst){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	if(!data->host){
		return 1;
	}

	//unregister old fd from core
	if(data->fd >= 0){
		mm_manage_fd(data->fd, BACKEND_NAME, 0, NULL);
	}

	data->fd = mmbackend_socket(data->host, data->port ? data->port : MAWEB_DEFAULT_PORT, SOCK_STREAM, 0, 0);
	if(data->fd < 0){
		return 1;
	}

	data->state = ws_new;
	if(mmbackend_send_str(data->fd, "GET /?ma=1 HTTP/1.1\r\n")
			|| mmbackend_send_str(data->fd, "Connection: Upgrade\r\n")
			|| mmbackend_send_str(data->fd, "Upgrade: websocket\r\n")
			|| mmbackend_send_str(data->fd, "Sec-WebSocket-Version: 13\r\n")
			//the websocket key probably should not be hardcoded, but this is not security critical
			//and the whole websocket 'accept key' dance is plenty stupid as it is
			|| mmbackend_send_str(data->fd, "Sec-WebSocket-Key: rbEQrXMEvCm4ZUjkj6juBQ==\r\n")
			|| mmbackend_send_str(data->fd, "\r\n")){
		fprintf(stderr, "maweb backend failed to communicate with peer\n");
		return 1;
	}

	//register new fd
	if(mm_manage_fd(data->fd, BACKEND_NAME, 1, (void*) inst)){
		fprintf(stderr, "maweb backend failed to register fd\n");
		return 1;
	}
	return 0;
}

static ssize_t maweb_handle_lines(instance* inst, ssize_t bytes_read){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	size_t n, begin = 0;

	for(n = 0; n < bytes_read - 2; n++){
		if(!strncmp((char*) data->buffer + data->offset + n, "\r\n", 2)){
			if(data->state == ws_new){
				if(!strncmp((char*) data->buffer, "HTTP/1.1 101", 12)){
					data->state = ws_http;
				}
				else{
					fprintf(stderr, "maweb received invalid HTTP response for instance %s\n", inst->name);
					return -1;
				}
			}
			else{
				//ignore all http stuff until the end of headers since we don't actually care...
				if(n == begin){
					data->state = ws_open;
				}
			}
			begin = n + 2;
		}
	}

	return begin;
}

static ssize_t maweb_handle_ws(instance* inst, ssize_t bytes_read){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	size_t header_length = 2;
	uint64_t payload_length = 0;
	uint16_t* payload_len16 = (uint16_t*) (data->buffer + 2);
	uint64_t* payload_len64 = (uint64_t*) (data->buffer + 2);
	uint8_t* payload = data->buffer + 2;
	uint8_t terminator_temp = 0;

	if(data->offset + bytes_read < 2){
		return 0;
	}

	//using varint as payload length is stupid, but some people seem to think otherwise...
	payload_length = WS_LEN(data->buffer[1]);
	switch(payload_length){
		case 126:
			if(data->offset + bytes_read < 4){
				return 0;
			}
			payload_length = htobe16(*payload_len16);
			payload = data->buffer + 4;
			header_length = 4;
			break;
		case 127:
			if(data->offset + bytes_read < 10){
				return 0;
			}
			payload_length = htobe64(*payload_len64);
			payload = data->buffer + 10;
			header_length = 10;
			break;
		default:
			break;
	}

	if(data->offset + bytes_read < header_length + payload_length){
		return 0;
	}

	switch(WS_OP(data->buffer[0])){
		case ws_text:
			//terminate message
			terminator_temp = payload[payload_length];
			payload[payload_length] = 0;
			if(maweb_handle_message(inst, (char*) payload, payload_length)){
				return data->offset + bytes_read;
			}
			payload[payload_length] = terminator_temp;
			break;
		case ws_ping:
			//answer server ping with a pong
			if(maweb_send_frame(inst, ws_pong, payload, payload_length)){
				fprintf(stderr, "maweb failed to send pong\n");
			}
			return header_length + payload_length;
		default:
			fprintf(stderr, "maweb encountered unhandled frame type %02X\n", WS_OP(data->buffer[0]));
			//this is somewhat dicey, it might be better to handle only header + payload length for known but unhandled types
			return data->offset + bytes_read;
	}

	return header_length + payload_length;
}

static int maweb_handle_fd(instance* inst){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	ssize_t bytes_read, bytes_left = data->allocated - data->offset, bytes_handled;

	if(bytes_left < 3){
		data->buffer = realloc(data->buffer, (data->allocated + MAWEB_RECV_CHUNK) * sizeof(uint8_t));
		if(!data->buffer){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		data->allocated += MAWEB_RECV_CHUNK;
		bytes_left += MAWEB_RECV_CHUNK;
	}

	bytes_read = recv(data->fd, data->buffer + data->offset, bytes_left - 1, 0);
	if(bytes_read < 0){
		fprintf(stderr, "maweb backend failed to receive: %s\n", strerror(errno));
		//TODO close, reopen
		return 1;
	}
	else if(bytes_read == 0){
		//client closed connection
		//TODO try to reopen
		return 0;
	}

	do{
		switch(data->state){
			case ws_new:
			case ws_http:
				bytes_handled = maweb_handle_lines(inst, bytes_read);
				break;
			case ws_open:
				bytes_handled = maweb_handle_ws(inst, bytes_read);
				break;
			case ws_closed:
				bytes_handled = data->offset + bytes_read;
				break;
		}

		if(bytes_handled < 0){
			bytes_handled = data->offset + bytes_read;
			//TODO close, reopen
			fprintf(stderr, "maweb failed to handle incoming data\n");
			return 1;
		}
		else if(bytes_handled == 0){
			 break;
		}

		memmove(data->buffer, data->buffer + bytes_handled, (data->offset + bytes_read) - bytes_handled);

		//FIXME this might be somewhat borked
		bytes_read -= data->offset;
		bytes_handled -= data->offset;
		bytes_read -= bytes_handled;
		data->offset = 0;
	} while(bytes_read > 0);

	data->offset += bytes_read;
	return 0;
}

static int maweb_set(instance* inst, size_t num, channel** c, channel_value* v){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	maweb_channel_ident ident;
	size_t n;

	if(num && !data->login){
		fprintf(stderr, "maweb instance %s can not send output, not logged in\n", inst->name);
		return 0;
	}

	for(n = 0; n < num; n++){
		ident.label = c[n]->ident;
		switch(ident.fields.type){
			case exec_fader:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"requestType\":\"playbacks_userInput\","
						"\"execIndex\":%d,"
						"\"pageIndex\":%d,"
						"\"faderValue\":%f,"
						"\"type\":1,"
						"\"session\":%ld"
						"}", ident.fields.index, ident.fields.page, v[n].normalised, data->session);
				fprintf(stderr, "maweb out %s\n", xmit_buffer);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			case exec_upper:
			case exec_lower:
			case exec_flash:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"requestType\":\"playbacks_userInput\","
						//"\"cmdline\":\"\","
						"\"execIndex\":%d,"
						"\"pageIndex\":%d,"
						"\"buttonId\":%d,"
						"\"pressed\":%s,"
						"\"released\":%s,"
						"\"type\":0,"
						"\"session\":%ld"
						"}", ident.fields.index, ident.fields.page,
						(exec_flash - ident.fields.type),
						(v[n].normalised > 0.9) ? "true" : "false",
						(v[n].normalised > 0.9) ? "false" : "true",
						data->session);
				fprintf(stderr, "maweb out %s\n", xmit_buffer);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			case exec_button:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"requestType\":\"playbacks_userInput\","
						//"\"cmdline\":\"\","
						"\"execIndex\":%d,"
						"\"pageIndex\":%d,"
						"\"buttonId\":%d,"
						"\"pressed\":%s,"
						"\"released\":%s,"
						"\"type\":0,"
						"\"session\":%ld"
						"}", ident.fields.index + 100,
						ident.fields.page,
						0,
						(v[n].normalised > 0.9) ? "true" : "false",
						(v[n].normalised > 0.9) ? "false" : "true",
						data->session);
				fprintf(stderr, "maweb out %s\n", xmit_buffer);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			case cmdline_button:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"keyname\":\"%s\","
						//"\"autoSubmit\":false,"
						"\"value\":%d"
						"}", cmdline_keys[ident.fields.index],
						(v[n].normalised > 0.9) ? 1 : 0);
				fprintf(stderr, "maweb out %s\n", xmit_buffer);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			default:
				fprintf(stderr, "maweb control not yet implemented\n");
				break;
		}
	}
	return 0;
}

static int maweb_keepalive(){
	size_t n, u;
	instance** inst = NULL;
	maweb_instance_data* data = NULL;
	char xmit_buffer[MAWEB_XMIT_CHUNK];

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	//send keep-alive messages for logged-in instances
	for(u = 0; u < n; u++){
		data = (maweb_instance_data*) inst[u]->impl;
		if(data->login){
			snprintf(xmit_buffer, sizeof(xmit_buffer), "{\"session\":%ld}", data->session);
			maweb_send_frame(inst[u], ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
		}
	}

	free(inst);
	return 0;
}

static int maweb_handle(size_t num, managed_fd* fds){
	size_t n = 0;
	int rv = 0;

	for(n = 0; n < num; n++){
		rv |= maweb_handle_fd((instance*) fds[n].impl);
	}

	if(last_keepalive && mm_timestamp() - last_keepalive >= MAWEB_CONNECTION_KEEPALIVE){
		rv |= maweb_keepalive();
		last_keepalive = mm_timestamp();
	}

	return rv;
}

static int maweb_start(){
	size_t n, u;
	instance** inst = NULL;

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		if(maweb_connect(inst[u])){
			fprintf(stderr, "Failed to open connection to MA Web Remote for instance %s\n", inst[u]->name);
			return 1;
		}
	}

	free(inst);
	if(!n){
		return 0;
	}

	fprintf(stderr, "maweb backend registering %lu descriptors to core\n", n);

	//initialize keepalive timeout
	last_keepalive = mm_timestamp();
	return 0;
}

static int maweb_shutdown(){
	size_t n, u;
	instance** inst = NULL;
	maweb_instance_data* data = NULL;

	//fetch all instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (maweb_instance_data*) inst[u]->impl;
		free(data->host);
		data->host = NULL;
		free(data->port);
		data->port = NULL;
		free(data->user);
		data->user = NULL;
		free(data->pass);
		data->pass = NULL;

		close(data->fd);
		data->fd = -1;

		free(data->buffer);
		data->buffer = NULL;

		data->offset = data->allocated = 0;
		data->state = ws_new;
	}

	free(inst);

	fprintf(stderr, "maweb backend shut down\n");
	return 0;
}
