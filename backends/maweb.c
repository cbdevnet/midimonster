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

//TODO test using different pages simultaneously
//TODO test dot2 button virtual faders in fader view

static uint64_t last_keepalive = 0;
static uint64_t update_interval = 50;
static uint64_t last_update = 0;
static uint64_t updates_inflight = 0;

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
		.shutdown = maweb_shutdown,
		.interval = maweb_interval
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

static int channel_comparator(const void* raw_a, const void* raw_b){
	maweb_channel_ident* a = (maweb_channel_ident*) raw_a;
	maweb_channel_ident* b = (maweb_channel_ident*) raw_b;

	if(a->fields.page != b->fields.page){
		return a->fields.page - b->fields.page;
	}
	return a->fields.index - b->fields.index;
}

static uint32_t maweb_interval(){
	return update_interval - (last_update % update_interval);
}

static int maweb_configure(char* option, char* value){
	if(!strcmp(option, "interval")){
		update_interval = strtoul(value, NULL, 10);
		return 0;
	}

	fprintf(stderr, "Unknown maweb backend configuration option %s\n", option);
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
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
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
			ident.fields.type = exec_button;
			next_token += 5;
		}
		else if(!strncmp(next_token, "button", 6)){
			ident.fields.type = exec_button;
			next_token += 6;
		}
		ident.fields.index = strtoul(next_token, NULL, 10);
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

	if(ident.fields.type && ident.fields.index && ident.fields.page){
		//actually, those are zero-indexed...
		ident.fields.index--;
		ident.fields.page--;

		//check if the (exec/meta) channel is already known
		for(n = 0; n < data->input_channels; n++){
			if(data->input_channel[n].fields.page == ident.fields.page
					&& data->input_channel[n].fields.index == ident.fields.index){
				break;
			}
		}

		//FIXME only register channels that are mapped as outputs
		//only register exec channels for updates
		if(n == data->input_channels && ident.fields.type != cmdline_button){
			data->input_channel = realloc(data->input_channel, (data->input_channels + 1) * sizeof(maweb_channel_ident));
			if(!data->input_channel){
				fprintf(stderr, "Failed to allocate memory\n");
				return NULL;
			}
			data->input_channel[n].label = ident.label;
			data->input_channels++;
		}

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

static int maweb_process_playback(instance* inst, int64_t page, maweb_channel_type metatype, char* payload, size_t payload_length){
	size_t exec_blocks = json_obj_offset(payload, (metatype == 2) ? "executorBlocks" : "bottomButtons"), offset, block = 0, control;
	channel* chan = NULL;
	channel_value evt;
	maweb_channel_ident ident = {
		.fields.page = page - 1,
		.fields.index = json_obj_int(payload, "iExec", 191)
	};

	if(!exec_blocks){
		if(metatype == 3){
			//ignore unused buttons
			return 0;
		}
		fprintf(stderr, "maweb missing exec block data on exec %" PRIu64 ".%d\n", page, ident.fields.index);
		return 1;
	}

	//the bottomButtons key has an additional subentry
	if(metatype == 3){
		exec_blocks += json_obj_offset(payload + exec_blocks, "items");
	}

	//TODO detect unused faders
	//TODO state tracking for fader values / exec run state

	//iterate over executor blocks
	for(offset = json_array_offset(payload + exec_blocks, block); offset; offset = json_array_offset(payload + exec_blocks, block)){
		control = exec_blocks + offset + json_obj_offset(payload + exec_blocks + offset, "fader");
		ident.fields.type = exec_fader;
		chan = mm_channel(inst, ident.label, 0);
		if(chan){
			evt.normalised = json_obj_double(payload + control, "v", 0.0);
			mm_channel_event(chan, evt);
		}

		ident.fields.type = exec_button;
		chan = mm_channel(inst, ident.label, 0);
		if(chan){
			evt.normalised = json_obj_int(payload, "isRun", 0);
			mm_channel_event(chan, evt);
		}

		DBGPF("maweb page %" PRIu64 " exec %d value %f running %" PRIu64 "\n", page, ident.fields.index, json_obj_double(payload + control, "v", 0.0), json_obj_int(payload, "isRun", 0));
		ident.fields.index++;
		block++;
	}

	return 0;
}

static int maweb_process_playbacks(instance* inst, int64_t page, char* payload, size_t payload_length){
	size_t base_offset = json_obj_offset(payload, "itemGroups"), group_offset, subgroup_offset, item_offset;
	uint64_t group = 0, subgroup, item, metatype;

	if(!page){
		fprintf(stderr, "maweb received playbacks for invalid page\n");
		return 0;
	}

	if(!base_offset){
		fprintf(stderr, "maweb playback data missing item key\n");
		return 0;
	}

	//iterate .itemGroups
	for(group_offset = json_array_offset(payload + base_offset, group);
			group_offset;
			group_offset = json_array_offset(payload + base_offset, group)){
		metatype = json_obj_int(payload + base_offset + group_offset, "itemsType", 0);
		//iterate .itemGroups.items
		//FIXME this is problematic if there is no "items" key
		group_offset = group_offset + json_obj_offset(payload + base_offset + group_offset, "items");
		if(group_offset){
			subgroup = 0;
			group_offset += base_offset;
			for(subgroup_offset = json_array_offset(payload + group_offset, subgroup);
					subgroup_offset;
					subgroup_offset = json_array_offset(payload + group_offset, subgroup)){
				//iterate .itemGroups.items[n]
				item = 0;
				subgroup_offset += group_offset;
				for(item_offset = json_array_offset(payload + subgroup_offset, item);
						item_offset;
						item_offset = json_array_offset(payload + subgroup_offset, item)){
					maweb_process_playback(inst, page, metatype,
							payload + subgroup_offset + item_offset,
							payload_length - subgroup_offset - item_offset);
					item++;
				}
				subgroup++;
			}
		}
		group++;
	}
	updates_inflight--;
	DBGPF("maweb playback message processing done, %" PRIu64 " updates inflight\n", updates_inflight);
	return 0;
}

static int maweb_request_playbacks(instance* inst){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	int rv = 0;

	char item_indices[1024] = "[300,400,500]", item_counts[1024] = "[16,16,16]", item_types[1024] = "[3,3,3]";
	size_t page_index = 0, view = 3, channel = 0, offsets[3], channel_offset, channels;

	if(updates_inflight){
		fprintf(stderr, "maweb skipping update request, %" PRIu64 " updates still inflight\n", updates_inflight);
		return 0;
	}

	//don't quote me on this whole segment
	for(channel = 0; channel < data->input_channels; channel++){
		offsets[0] = offsets[1] = offsets[2] = 1;
		page_index = data->input_channel[channel].fields.page;
		if(data->peer_type == peer_dot2){
			//blocks 0, 100 & 200 have 21 execs and need to be queried from fader view
			view = (data->input_channel[channel].fields.index >= 300) ? 3 : 2;

			for(channel_offset = 1; channel + channel_offset <= data->input_channels; channel_offset++){
				channels = channel + channel_offset - 1;
				//find end for this exec block
				for(; channel + channel_offset < data->input_channels; channel_offset++){
					if(data->input_channel[channel + channel_offset].fields.page != page_index
							|| (data->input_channel[channels].fields.index / 100) != (data->input_channel[channel + channel_offset].fields.index / 100)){
						break;
					}
				}

				//add request block for the exec block
				offsets[0] += snprintf(item_indices + offsets[0], sizeof(item_indices) - offsets[0], "%d,", data->input_channel[channels].fields.index);
				offsets[1] += snprintf(item_counts + offsets[1], sizeof(item_counts) - offsets[1], "%d,", data->input_channel[channel + channel_offset - 1].fields.index - data->input_channel[channels].fields.index + 1);
				offsets[2] += snprintf(item_types + offsets[2], sizeof(item_types) - offsets[2], "%d,", (data->input_channel[channels].fields.index < 100) ? 2 : 3);

				//send on page boundary, metamode boundary, last channel
				if(channel + channel_offset >= data->input_channels
						|| data->input_channel[channel + channel_offset].fields.page != page_index
						|| (data->input_channel[channel].fields.index < 300) != (data->input_channel[channel + channel_offset].fields.index < 300)){
					break;
				}
			}

			//terminate arrays (overwriting the last array separator)
			offsets[0] += snprintf(item_indices + offsets[0] - 1, sizeof(item_indices) - offsets[0], "]");
			offsets[1] += snprintf(item_counts + offsets[1] - 1, sizeof(item_counts) - offsets[1], "]");
			offsets[2] += snprintf(item_types + offsets[2] - 1, sizeof(item_types) - offsets[2], "]");
		}
		else{
			//for the ma, the view equals the exec type requested (we can query all button execs from button view, all fader execs from fader view)
			view = (data->input_channel[channel].fields.index >= 100) ? 3 : 2;
			snprintf(item_types, sizeof(item_types), "[%" PRIsize_t "]", view);
			//this channel must be included, so it must be in range for the first startindex
			snprintf(item_indices, sizeof(item_indices), "[%d]", (data->input_channel[channel].fields.index / 5) * 5);

			for(channel_offset = 1; channel + channel_offset < data->input_channels
					&& data->input_channel[channel].fields.page == data->input_channel[channel + channel_offset].fields.page
					&& data->input_channel[channel].fields.index / 100 == data->input_channel[channel + channel_offset].fields.index / 100; channel_offset++){
			}

			channels = data->input_channel[channel + channel_offset - 1].fields.index - (data->input_channel[channel].fields.index / 5) * 5;

			snprintf(item_counts, sizeof(item_indices), "[%" PRIsize_t "]", ((channels / 5) * 5 + 5));
		}

		//advance base channel
		channel += channel_offset - 1;

		//send current request
		snprintf(xmit_buffer, sizeof(xmit_buffer),
				"{"
				"\"requestType\":\"playbacks\","
				"\"startIndex\":%s,"
				"\"itemsCount\":%s,"
				"\"pageIndex\":%" PRIsize_t ","
				"\"itemsType\":%s,"
				"\"view\":%" PRIsize_t ","
				"\"execButtonViewMode\":2,"	//extended
				"\"buttonsViewMode\":0,"	//get vfader for button execs
				"\"session\":%" PRIu64
				"}",
				item_indices,
				item_counts,
				page_index,
				item_types,
				view,
				data->session);
		rv |= maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
		DBGPF("maweb poll request: %s\n", xmit_buffer);
		updates_inflight++;
	}

	return rv;
}

static int maweb_handle_message(instance* inst, char* payload, size_t payload_length){
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	char* field;
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;

	//query this early to save on unnecessary parser passes with stupid-huge data messages
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
		if(!strncmp(field, "playbacks", 9)){
			if(maweb_process_playbacks(inst, json_obj_int(payload, "iPage", 0), payload, payload_length)){
				fprintf(stderr, "maweb failed to handle/request input data\n");
			}
			return 0;
		}
	}

	DBGPF("maweb message (%" PRIsize_t "): %s\n", payload_length, payload);
	if(json_obj(payload, "session") == JSON_NUMBER){
		data->session = json_obj_int(payload, "session", data->session);
		fprintf(stderr, "maweb session id is now %" PRIu64 "\n", data->session);
	}

	if(json_obj_bool(payload, "forceLogin", 0)){
		fprintf(stderr, "maweb sending user credentials\n");
		snprintf(xmit_buffer, sizeof(xmit_buffer),
				"{\"requestType\":\"login\",\"username\":\"%s\",\"password\":\"%s\",\"session\":%" PRIu64 "}",
				(data->peer_type == peer_dot2) ? "remote" : data->user, data->pass, data->session);
		maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
	}
	if(json_obj(payload, "status") && json_obj(payload, "appType")){
		fprintf(stderr, "maweb connection established\n");
		field = json_obj_str(payload, "appType", NULL);
		if(!strncmp(field, "dot2", 4)){
			data->peer_type = peer_dot2;
		}
		else if(!strncmp(field, "gma2", 4)){
			data->peer_type = peer_ma2;
		}
		maweb_send_frame(inst, ws_text, (uint8_t*) "{\"session\":0}", 13);
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

	for(n = 0; n < bytes_read - 1; n++){
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

	return data->offset + begin;
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
			data->offset = 0;
			//TODO close, reopen
			fprintf(stderr, "maweb failed to handle incoming data\n");
			return 1;
		}
		else if(bytes_handled == 0){
			 break;
		}

		memmove(data->buffer, data->buffer + bytes_handled, (data->offset + bytes_read) - bytes_handled);

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
						"\"session\":%" PRIu64
						"}", ident.fields.index, ident.fields.page, v[n].normalised, data->session);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			case exec_upper:
			case exec_lower:
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
						"\"session\":%" PRIu64
						"}", ident.fields.index, ident.fields.page,
						(data->peer_type == peer_dot2 && ident.fields.type == exec_upper) ? 0 : (ident.fields.type - exec_button),
						(v[n].normalised > 0.9) ? "true" : "false",
						(v[n].normalised > 0.9) ? "false" : "true",
						data->session);
				maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
				break;
			case cmdline_button:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"keyname\":\"%s\","
						//"\"autoSubmit\":false,"
						"\"value\":%d"
						"}", cmdline_keys[ident.fields.index],
						(v[n].normalised > 0.9) ? 1 : 0);
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
			snprintf(xmit_buffer, sizeof(xmit_buffer), "{\"session\":%" PRIu64 "}", data->session);
			maweb_send_frame(inst[u], ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
		}
	}

	free(inst);
	return 0;
}

static int maweb_poll(){
	size_t n, u;
	instance** inst = NULL;
	maweb_instance_data* data = NULL;

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	//send data polls for logged-in instances
	for(u = 0; u < n; u++){
		data = (maweb_instance_data*) inst[u]->impl;
		if(data->login){
			maweb_request_playbacks(inst[u]);
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

	//FIXME all keepalive processing allocates temporary buffers, this might an optimization target
	if(last_keepalive && mm_timestamp() - last_keepalive >= MAWEB_CONNECTION_KEEPALIVE){
		rv |= maweb_keepalive();
		last_keepalive = mm_timestamp();
	}

	if(last_update && mm_timestamp() - last_update >= update_interval){
		rv |= maweb_poll();
		last_update = mm_timestamp();
	}

	return rv;
}

static int maweb_start(){
	size_t n, u;
	instance** inst = NULL;
	maweb_instance_data* data = NULL;

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		//sort channels
		data = (maweb_instance_data*) inst[u]->impl;
		qsort(data->input_channel, data->input_channels, sizeof(maweb_channel_ident), channel_comparator);

		if(maweb_connect(inst[u])){
			fprintf(stderr, "Failed to open connection to MA Web Remote for instance %s\n", inst[u]->name);
			free(inst);
			return 1;
		}
	}

	free(inst);
	if(!n){
		return 0;
	}

	fprintf(stderr, "maweb backend registering %" PRIsize_t " descriptors to core\n", n);

	//initialize timeouts
	last_keepalive = last_update = mm_timestamp();
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

		free(data->input_channel);
		data->input_channel = NULL;
		data->input_channels = 0;
	}

	free(inst);

	fprintf(stderr, "maweb backend shut down\n");
	return 0;
}
