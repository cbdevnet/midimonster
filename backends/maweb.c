#define BACKEND_NAME "maweb"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef MAWEB_NO_LIBSSL
#include <openssl/md5.h>
#endif

#include "libmmbackend.h"
#include "maweb.h"

#define WS_LEN(a) ((a) & 0x7F)
#define WS_OP(a) ((a) & 0x0F)
#define WS_FLAG_FIN 0x80
#define WS_FLAG_MASK 0x80

static uint64_t last_keepalive = 0;
static uint64_t update_interval = 50;
static uint64_t last_update = 0;
static uint64_t updates_inflight = 0;

static maweb_command_key cmdline_keys[] = {
	{"PREV", 109, 0, 1}, {"SET", 108, 1, 0, 1}, {"NEXT", 110, 0, 1},
	{"TIME", 58, 1, 1}, {"EDIT", 55, 1, 1}, {"UPDATE", 57, 1, 1},
	{"OOPS", 53, 1, 1}, {"ESC", 54, 1, 1}, {"CLEAR", 105, 1, 1},
	{"0", 86, 1, 1}, {"1", 87, 1, 1}, {"2", 88, 1, 1},
	{"3", 89, 1, 1}, {"4", 90, 1, 1}, {"5", 91, 1, 1},
	{"6", 92, 1, 1}, {"7", 93, 1, 1}, {"8", 94, 1, 1},
	{"9", 95, 1, 1}, {"PUNKT", 98, 1, 1}, {"ENTER", 106, 1, 1},
	{"PLUS", 96, 1, 1}, {"MINUS", 97, 1, 1}, {"THRU", 102, 1, 1},
	{"IF", 103, 1, 1}, {"AT", 104, 1, 1}, {"FULL", 99, 1, 1},
	{"MA", 68, 0, 1}, {"HIGH", 100, 1, 1, 1}, {"SOLO", 101, 1, 1, 1},
	{"SELECT", 42, 1, 1}, {"OFF", 43, 1, 1}, {"ON", 46, 1, 1},
	{"ASSIGN", 63, 1, 1}, {"LABEL", 0, 1, 1},
	{"COPY", 73, 1, 1}, {"DELETE", 69, 1, 1}, {"STORE", 59, 1, 1},
	{"GOTO", 56, 1, 1}, {"PAGE", 70, 1, 1}, {"MACRO", 71, 1, 1},
	{"PRESET", 72, 1, 1}, {"SEQU", 74, 1, 1}, {"CUE", 75, 1, 1},
	{"EXEC", 76, 1, 1}, {"FIXTURE", 83, 1, 1}, {"GROUP", 84, 1, 1},
	{"GO_MINUS", 10, 1, 1}, {"PAUSE", 9, 1, 1}, {"GO_PLUS", 11, 1, 1},

	{"FIXTURE_CHANNEL", 0, 1, 1}, {"FIXTURE_GROUP_PRESET", 0, 1, 1},
	{"EXEC_CUE", 0, 1, 1}, {"STORE_UPDATE", 0, 1, 1}, {"PROG_ONLY", 0, 1, 1, 1},
	{"SPECIAL_DIALOGUE", 0, 1, 1},
	{"ODD", 0, 1, 1}, {"EVEN", 0, 1, 1},
	{"WINGS", 0, 1, 1}, {"RESET", 0, 1, 1},
	//gma2 internal only
	{"CHPGPLUS", 3}, {"CHPGMINUS", 4},
	{"FDPGPLUS", 5}, {"FDPGMINUS", 6},
	{"BTPGPLUS", 7}, {"BTPGMINUS", 8},
	{"X1", 12}, {"X2", 13}, {"X3", 14},
	{"X4", 15}, {"X5", 16}, {"X6", 17},
	{"X7", 18}, {"X8", 19}, {"X9", 20},
	{"X10", 21}, {"X11", 22}, {"X12", 23},
	{"X13", 24}, {"X14", 25}, {"X15", 26},
	{"X16", 27}, {"X17", 28}, {"X18", 29},
	{"X19", 30}, {"X20", 31},
	{"V1", 120}, {"V2", 121}, {"V3", 122},
	{"V4", 123}, {"V5", 124}, {"V6", 125},
	{"V7", 126}, {"V8", 127}, {"V9", 128},
	{"V10", 129},
	{"NIPPLE", 40},
	{"TOOLS", 119}, {"SETUP", 117}, {"BACKUP", 117},
	{"BLIND", 60}, {"FREEZE", 61}, {"PREVIEW", 62},
	{"FIX", 41}, {"TEMP", 44}, {"TOP", 45},
	{"VIEW", 66}, {"EFFECT", 67}, {"CHANNEL", 82},
	{"MOVE", 85}, {"BLACKOUT", 65},
	{"PLEASE", 106},
	{"LIST", 32}, {"USER1", 33}, {"USER2", 34},
	{"ALIGN", 64}, {"HELP", 116},
	{"UP", 107}, {"DOWN", 111},
	{"FASTREVERSE", 47}, {"LEARN", 48}, {"FASTFORWARD", 49},
	{"GO_MINUS_SMALL", 50}, {"PAUSE_SMALL", 51}, {"GO_PLUS_SMALL", 52}
};

MM_PLUGIN_API int init(){
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

	//register backend
	if(mm_backend_register(maweb)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static ssize_t maweb_channel_index(maweb_instance_data* data, maweb_channel_type type, uint16_t page, uint16_t index){
	size_t n;
	for(n = 0; n < data->channels; n++){
		if(data->channel[n].type == type
				&& data->channel[n].page == page
				&& data->channel[n].index == index){
			return n;
		}
	}
	return -1;
}

static int channel_comparator(const void* raw_a, const void* raw_b){
	maweb_channel_data* a = (maweb_channel_data*) raw_a;
	maweb_channel_data* b = (maweb_channel_data*) raw_b;

	//this needs to take into account command line channels
	//they need to be sorted last so that the channel poll logic works properly
	if(a->page != b->page){
		return a->page - b->page;
	}
	//execs and their components are sorted by index first, type second
	if(a->type < cmdline && b->type < cmdline){
		if(a->index != b->index){
			return a->index - b->index;
		}
		return a->type - b->type;
	}
	//if either one is not an exec, sort by type first, index second
	if(a->type != b->type){
		return a->type - b->type;
	}
	return a->index - b->index;
}

static uint32_t maweb_interval(){
	return update_interval - (last_update % update_interval);
}

static int maweb_configure(char* option, char* value){
	if(!strcmp(option, "interval")){
		update_interval = strtoul(value, NULL, 10);
		return 0;
	}

	LOGPF("Unknown backend configuration option %s", option);
	return 1;
}

static int maweb_configure_instance(instance* inst, char* option, char* value){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	char* host = NULL, *port = NULL, *fd_opts = NULL;

	if(!strcmp(option, "host")){
		mmbackend_parse_hostspec(value, &host, &port, &fd_opts);
		if(!host){
			LOGPF("Invalid host specified for instance %s", inst->name);
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
		uint8_t password_hash[MD5_DIGEST_LENGTH];

		MD5((uint8_t*) value, strlen(value), (uint8_t*) password_hash);
		data->pass = realloc(data->pass, (2 * MD5_DIGEST_LENGTH + 1) * sizeof(char));
		for(n = 0; n < MD5_DIGEST_LENGTH; n++){
			snprintf(data->pass + 2 * n, 3, "%02x", password_hash[n]);
		}
		return 0;
		#else
		LOG("This build only supports the default password");
		return 1;
		#endif
	}
	else if(!strcmp(option, "cmdline")){
		if(!strcmp(value, "console")){
			data->cmdline = cmd_console;
		}
		else if(!strcmp(value, "remote")){
			data->cmdline = cmd_remote;
		}
		else if(!strcmp(value, "downgrade")){
			data->cmdline = cmd_downgrade;
		}
		else{
			LOGPF("Unknown commandline mode %s for instance %s", value, inst->name);
			return 1;
		}
		return 0;
	}

	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static instance* maweb_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	maweb_instance_data* data = calloc(1, sizeof(maweb_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return NULL;
	}

	data->fd = -1;
	data->buffer = calloc(MAWEB_RECV_CHUNK, sizeof(uint8_t));
	if(!data->buffer){
		LOG("Failed to allocate memory");
		free(data);
		return NULL;
	}
	data->allocated = MAWEB_RECV_CHUNK;

	inst->impl = data;
	return inst;
}

static channel* maweb_channel(instance* inst, char* spec, uint8_t flags){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	maweb_channel_data chan = {
		0
	};
	char* next_token = NULL;
	channel* channel_ref = NULL;
	size_t n;

	if(!strncmp(spec, "page", 4)){
		chan.page = strtoul(spec + 4, &next_token, 10);
		if(*next_token != '.'){
			LOGPF("Failed to parse channel spec %s: Missing separator", spec);
			return NULL;
		}

		next_token++;
		if(!strncmp(next_token, "fader", 5)){
			chan.type = exec_fader;
			next_token += 5;
		}
		else if(!strncmp(next_token, "upper", 5)){
			chan.type = exec_upper;
			next_token += 5;
		}
		else if(!strncmp(next_token, "lower", 5)){
			chan.type = exec_lower;
			next_token += 5;
		}
		else if(!strncmp(next_token, "flash", 5)){
			chan.type = exec_button;
			next_token += 5;
		}
		else if(!strncmp(next_token, "button", 6)){
			chan.type = exec_button;
			next_token += 6;
		}
		chan.index = strtoul(next_token, NULL, 10);
	}
	else{
		for(n = 0; n < sizeof(cmdline_keys) / sizeof(maweb_command_key); n++){
			if(!strcmp(spec, cmdline_keys[n].name)){
				if((data->cmdline == cmd_remote && !cmdline_keys[n].press && !cmdline_keys[n].release)
							|| (data->cmdline == cmd_console && !cmdline_keys[n].lua)){
					LOGPF("Key %s does not work with the current commandline mode for instance %s", spec, inst->name);
					return NULL;
				}

				chan.type = cmdline;
				chan.index = n + 1;
				chan.page = 1;
				break;
			}
		}
	}

	if(chan.type && chan.index && chan.page){
		//actually, those are zero-indexed...
		chan.index--;
		chan.page--;

		if(maweb_channel_index(data, chan.type, chan.page, chan.index) == -1){
			data->channel = realloc(data->channel, (data->channels + 1) * sizeof(maweb_channel_data));
			if(!data->channel){
				LOG("Failed to allocate memory");
				return NULL;
			}
			data->channel[data->channels] = chan;
			data->channels++;
		}

		channel_ref = mm_channel(inst, maweb_channel_index(data, chan.type, chan.page, chan.index), 1);
		data->channel[maweb_channel_index(data, chan.type, chan.page, chan.index)].chan = channel_ref;
		return channel_ref;
	}

	LOGPF("Failed to parse channel spec %s", spec);
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
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	size_t exec_blocks = json_obj_offset(payload, (metatype == 2) ? "executorBlocks" : "bottomButtons"), offset, block = 0, control;
	int64_t exec_index = json_obj_int(payload, "iExec", 191);
	ssize_t channel_index;
	channel_value evt;

	if(!exec_blocks){
		if(metatype == 3){
			//ignore unused buttons
			return 0;
		}
		LOGPF("Missing exec block data on exec %" PRIu64 ".%" PRIu64, page, exec_index);
		return 1;
	}

	//the bottomButtons key has an additional subentry
	if(metatype == 3){
		exec_blocks += json_obj_offset(payload + exec_blocks, "items");
	}

	//iterate over executor blocks
	for(offset = json_array_offset(payload + exec_blocks, block); offset; offset = json_array_offset(payload + exec_blocks, block)){
		control = exec_blocks + offset + json_obj_offset(payload + exec_blocks + offset, "fader");

		channel_index = maweb_channel_index(data, exec_fader, page - 1, exec_index);
		if(channel_index >= 0){
			if(!data->channel[channel_index].input_blocked){
				evt.normalised = json_obj_double(payload + control, "v", 0.0);
				if(evt.normalised != data->channel[channel_index].in){
					mm_channel_event(mm_channel(inst, channel_index, 0), evt);
					data->channel[channel_index].in = evt.normalised;
				}
			}
			else{
				//block input immediately after channel set to prevent feedback loops
				data->channel[channel_index].input_blocked--;
			}
		}

		channel_index = maweb_channel_index(data, exec_button, page - 1, exec_index);
		if(channel_index >= 0){
			if(!data->channel[channel_index].input_blocked){
				evt.normalised = json_obj_int(payload, "isRun", 0);
				if(evt.normalised != data->channel[channel_index].in){
					mm_channel_event(mm_channel(inst, channel_index, 0), evt);
					data->channel[channel_index].in = evt.normalised;
				}
			}
			else{
				data->channel[channel_index].input_blocked--;
			}
		}

		DBGPF("Page %" PRIu64 " exec %" PRIu64 " value %f running %" PRIu64, page, exec_index, json_obj_double(payload + control, "v", 0.0), json_obj_int(payload, "isRun", 0));
		exec_index++;
		block++;
	}

	return 0;
}

static int maweb_process_playbacks(instance* inst, int64_t page, char* payload, size_t payload_length){
	size_t base_offset = json_obj_offset(payload, "itemGroups"), group_offset, subgroup_offset, item_offset;
	uint64_t group = 0, subgroup, item, metatype;

	if(!page){
		LOG("Received playbacks for invalid page");
		return 0;
	}

	if(!base_offset){
		LOG("Playback data missing item key");
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
	DBGPF("Playback message processing done, %" PRIu64 " updates inflight", updates_inflight);
	return 0;
}

static int maweb_request_playbacks(instance* inst){
	maweb_instance_data* data = (maweb_instance_data*) inst->impl;
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	int rv = 0;

	char item_indices[1024] = "[300,400,500]", item_counts[1024] = "[16,16,16]", item_types[1024] = "[3,3,3]";
	size_t page_index = 0, view = 3, channel = 0, offsets[3], channel_offset, channels;

	if(updates_inflight){
		LOGPF("Skipping update request, %" PRIu64 " updates still inflight", updates_inflight);
		return 0;
	}

	//only request faders and buttons
	for(channel = 0; channel < data->channels && data->channel[channel].type < cmdline; channel++){
		offsets[0] = offsets[1] = offsets[2] = 1;
		page_index = data->channel[channel].page;
		//poll logic differs between the consoles because reasons
		//don't quote me on this section
		if(data->peer_type == peer_dot2){
			//blocks 0, 100 & 200 have 21 execs and need to be queried from fader view
			view = (data->channel[channel].index >= 300) ? 3 : 2;

			for(channel_offset = 1; channel + channel_offset <= data->channels
					&& data->channel[channel + channel_offset].type < cmdline; channel_offset++){
				channels = channel + channel_offset - 1;
				//find end for this exec block
				for(; channel + channel_offset < data->channels; channel_offset++){
					if(data->channel[channel + channel_offset].page != page_index
							|| (data->channel[channels].index / 100) != (data->channel[channel + channel_offset].index / 100)){
						break;
					}
				}

				//add request block for the exec block
				offsets[0] += snprintf(item_indices + offsets[0], sizeof(item_indices) - offsets[0], "%d,", data->channel[channels].index);
				offsets[1] += snprintf(item_counts + offsets[1], sizeof(item_counts) - offsets[1], "%d,", data->channel[channel + channel_offset - 1].index - data->channel[channels].index + 1);
				offsets[2] += snprintf(item_types + offsets[2], sizeof(item_types) - offsets[2], "%d,", (data->channel[channels].index < 100) ? 2 : 3);

				//send on last channel, page boundary, metamode boundary
				if(channel + channel_offset >= data->channels
						|| data->channel[channel + channel_offset].page != page_index
						|| (data->channel[channel].index < 300) != (data->channel[channel + channel_offset].index < 300)){
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
			view = (data->channel[channel].index >= 100) ? 3 : 2;
			snprintf(item_types, sizeof(item_types), "[%" PRIsize_t "]", view);
			//this channel must be included, so it must be in range for the first startindex
			snprintf(item_indices, sizeof(item_indices), "[%d]", (data->channel[channel].index / 5) * 5);

			//find end of exec block
			for(channel_offset = 1; channel + channel_offset < data->channels
					&& data->channel[channel].page == data->channel[channel + channel_offset].page
					&& data->channel[channel].index / 100 == data->channel[channel + channel_offset].index / 100; channel_offset++){
			}

			//gma execs are grouped in blocks of 5
			channels = data->channel[channel + channel_offset - 1].index - (data->channel[channel].index / 5) * 5;
			snprintf(item_counts, sizeof(item_indices), "[%" PRIsize_t "]", ((channels / 5) * 5 + 5));
		}

		DBGPF("Poll range first %d: %d.%d last %d: %d.%d next %d: %d.%d",
				data->channel[channel].type, data->channel[channel].page, data->channel[channel].index,
				data->channel[channel + channel_offset - 1].type, data->channel[channel + channel_offset - 1].page, data->channel[channel + channel_offset - 1].index,
				data->channel[channel + channel_offset].type, data->channel[channel + channel_offset].page, data->channel[channel + channel_offset].index);

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
		DBGPF("Poll request: %s", xmit_buffer);
		updates_inflight++;
	}

	DBGPF("Poll request handling done, %" PRIu64 " updates requested", updates_inflight);
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
				LOG("Login successful");
				data->login = 1;
			}
			else{
				LOG("Login failed");
				data->login = 0;
			}
		}
		if(!strncmp(field, "playbacks", 9)){
			if(maweb_process_playbacks(inst, json_obj_int(payload, "iPage", 0), payload, payload_length)){
				LOG("Failed to handle/request input data");
			}
			return 0;
		}
	}

	DBGPF("Incoming message (%" PRIsize_t "): %s", payload_length, payload);
	if(json_obj(payload, "session") == JSON_NUMBER){
		data->session = json_obj_int(payload, "session", data->session);
		if(data->session < 0){
				LOG("Login failed");
				data->login = 0;
				return 0;
		}
		LOGPF("Session id is now %" PRId64, data->session);
	}

	if(json_obj_bool(payload, "forceLogin", 0)){
		LOG("Sending user credentials");
		snprintf(xmit_buffer, sizeof(xmit_buffer),
				"{\"requestType\":\"login\",\"username\":\"%s\",\"password\":\"%s\",\"session\":%" PRIu64 "}",
				(data->peer_type == peer_dot2) ? "remote" : data->user, data->pass ? data->pass : MAWEB_DEFAULT_PASSWORD, data->session);
		maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
	}
	if(json_obj(payload, "status") && json_obj(payload, "appType")){
		LOG("Connection established");
		field = json_obj_str(payload, "appType", NULL);
		if(!strncmp(field, "dot2", 4)){
			data->peer_type = peer_dot2;
			//the dot2 can't handle lua commands
			data->cmdline = cmd_remote;
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
		LOG("Failed to communicate with peer");
		return 1;
	}

	//register new fd
	if(mm_manage_fd(data->fd, BACKEND_NAME, 1, (void*) inst)){
		LOG("Failed to register FD");
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
					LOGPF("Invalid HTTP response for instance %s", inst->name);
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
				LOG("Failed to send pong");
			}
			return header_length + payload_length;
		default:
			LOGPF("Unhandled frame type %02X", WS_OP(data->buffer[0]));
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
			LOG("Failed to allocate memory");
			return 1;
		}
		data->allocated += MAWEB_RECV_CHUNK;
		bytes_left += MAWEB_RECV_CHUNK;
	}

	bytes_read = recv(data->fd, data->buffer + data->offset, bytes_left - 1, 0);
	if(bytes_read < 0){
		LOGPF("Failed to receive: %s", strerror(errno));
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
			LOG("Failed to handle incoming data");
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
	maweb_channel_data* chan = NULL;
	char xmit_buffer[MAWEB_XMIT_CHUNK];
	size_t n;

	if(num && !data->login){
		LOGPF("Instance %s can not send output, not logged in", inst->name);
		return 0;
	}

	for(n = 0; n < num; n++){
		//sanity check
		if(c[n]->ident >= data->channels){
			return 1;
		}
		chan = data->channel + c[n]->ident;

		//channel state tracking
		if(chan->out == v[n].normalised){
			continue;
		}
		chan->out = v[n].normalised;

		//i/o value space separation & feedback filtering for faders
		if(chan->type == exec_fader){
			chan->input_blocked = 1;
			chan->in = v[n].normalised;
		}

		switch(chan->type){
			case exec_fader:
				snprintf(xmit_buffer, sizeof(xmit_buffer),
						"{\"requestType\":\"playbacks_userInput\","
						"\"execIndex\":%d,"
						"\"pageIndex\":%d,"
						"\"faderValue\":%f,"
						"\"type\":1,"
						"\"session\":%" PRIu64
						"}", chan->index, chan->page, v[n].normalised, data->session);
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
						"}", chan->index, chan->page,
						(data->peer_type == peer_dot2 && chan->type == exec_upper) ? 0 : (chan->type - exec_button),
						(v[n].normalised > 0.9) ? "true" : "false",
						(v[n].normalised > 0.9) ? "false" : "true",
						data->session);
				break;
			case cmdline:
				if(cmdline_keys[chan->index].lua
						&& (data->cmdline == cmd_console || data->cmdline == cmd_downgrade)
						&& data->peer_type != peer_dot2){
					//push canbus events
					snprintf(xmit_buffer, sizeof(xmit_buffer),
							"{\"command\":\"LUA 'gma.canbus.hardkey(%d, %s, false)'\","
							"\"requestType\":\"command\","
							"\"session\":%" PRIu64
							"}", cmdline_keys[chan->index].lua,
							(v[n].normalised > 0.9) ? "true" : "false",
							data->session);
				}
				else if((cmdline_keys[chan->index].press || cmdline_keys[chan->index].release)
						&& (data->cmdline != cmd_console)){
					//send press/release events if required
					if((cmdline_keys[chan->index].press && v[n].normalised > 0.9)
							|| (cmdline_keys[chan->index].release && v[n].normalised < 0.9)){
						snprintf(xmit_buffer, sizeof(xmit_buffer),
								"{\"keyname\":\"%s\","
								"\"autoSubmit\":%s,"
								"\"value\":%d,"
								"\"session\":%" PRIu64
								"}", cmdline_keys[chan->index].name,
								cmdline_keys[chan->index].auto_submit ? "true" : "null",
								(v[n].normalised > 0.9) ? 1 : 0,
								data->session);
					}
					else{
						continue;
					}
				}
				else{
					LOGPF("Key %s not executed on %s due to mode mismatch",
							cmdline_keys[chan->index].name, inst->name);
					continue;
				}
				break;
			default:
				LOG("Control not yet implemented");
				return 1;
		}
		DBGPF("Command out %s", xmit_buffer);
		maweb_send_frame(inst, ws_text, (uint8_t*) xmit_buffer, strlen(xmit_buffer));
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
		LOG("Failed to fetch instance list");
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
		LOG( "Failed to fetch instance list");
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

static int maweb_start(size_t n, instance** inst){
	size_t u, p;
	maweb_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		//sort channels
		data = (maweb_instance_data*) inst[u]->impl;
		qsort(data->channel, data->channels, sizeof(maweb_channel_data), channel_comparator);

		//re-set channel identifiers
		for(p = 0; p < data->channels; p++){
			data->channel[p].chan->ident = p;
		}

		if(maweb_connect(inst[u])){
			LOGPF("Failed to open connection for instance %s", inst[u]->name);
			free(inst);
			return 1;
		}
	}

	LOGPF("Registering %" PRIsize_t " descriptors to core", n);

	//initialize timeouts
	last_keepalive = last_update = mm_timestamp();
	return 0;
}

static int maweb_shutdown(size_t n, instance** inst){
	size_t u;
	maweb_instance_data* data = NULL;

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

		free(data->channel);
		data->channel = NULL;
		data->channels = 0;

		free(inst[u]->impl);
	}

	LOG("Backend shut down");
	return 0;
}
