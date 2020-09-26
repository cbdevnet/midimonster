#define BACKEND_NAME "atem"
#include <string.h>

#define DEBUG
#include "atem.h"
#include "libmmbackend.h"

#include "../tests/hexdump.c"
/* TODO
 *  audio control
 *  power information output channel
 *  assign source IDs from the data gathered by _handle_input_props
 *  tally output with multiple m/e's?
 *  output event deduplication
 *  colorgen rgb conversion
 *  check/simplify all atem_send payload_len parameters against the corresponding command length fields
 */

static uint64_t alive_timestamp = 0;

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

	if(sizeof(atem_channel_ident) != sizeof(uint64_t)){
		LOG("Channel identification union out of bounds");
		return 1;
	}

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
		0
	};

	if(payload_len){
		memcpy(tx.payload, payload, min(payload_len, ATEM_PAYLOAD_MAX));
	}

	//byteswap and bitmap the header
	tx.hdr.session = htobe16(data->txhdr.session);
	if(data->established){
		if(payload_len){
			data->txhdr.seqno++;
			tx.hdr.length = htobe16(ATEM_SEQUENCE_VALID | (sizeof(atem_hdr) + payload_len));
			tx.hdr.seqno = htobe16(data->txhdr.seqno);
		}
		else{
			tx.hdr.length = htobe16(ATEM_ACK_VALID | (sizeof(atem_hdr) + payload_len));
			tx.hdr.ack = htobe16(data->txhdr.ack);
		}
	}
	else{
		tx.hdr.length = htobe16(ATEM_HELLO | (sizeof(atem_hdr) + payload_len));
	}

	//DBGPF("Sending %" PRIsize_t " bytes, ack %d seqno %d", sizeof(atem_hdr) + payload_len, be16toh(tx.hdr.ack), be16toh(tx.hdr.seqno));
	if(mmbackend_send(data->fd, (uint8_t*) &tx, sizeof(atem_hdr) + payload_len)){
		LOGPF("Failed to send on instance %s", inst->name);
		return 1;
	}
	return 0;
}

static int atem_disconnect(instance* inst){
	atem_instance_data* data = (atem_instance_data*) inst->impl;
	free(data->bus);
	data->bus = NULL;
	data->buses = 0;
	data->established = 0;
	memset(&(data->txhdr), 0, sizeof(data->txhdr));
	//close(data->fd);
	//mm_manage_fd(data->fd, BACKEND_NAME, 0, NULL);
	//data->fd = -1;
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

	//set initial timestamp to be that of the connection for timeout purposes
	data->last_response = mm_timestamp();

	DBGPF("Sending HELLO on instance %s", inst->name);
	return atem_send(inst, hello_payload, sizeof(hello_payload));
}

static int atem_handle_version(instance* inst, size_t n, uint8_t* data){
	uint16_t* major = (uint16_t*) (data + 8);
	uint16_t* minor = (uint16_t*) (data + 10);

	LOGPF("Instance %s peer speaking protocol version %d.%d", inst->name, be16toh(*major), be16toh(*minor));
	return 0;
}

static int atem_handle_strings(instance* inst, size_t n, uint8_t* data){
	if(!memcmp(data + 4, "_pin", 4)){
		LOGPF("Instance %s product reports as %.*s", inst->name, (int) n - 8, data + 8);
	}
	else if(!memcmp(data + 4, "Warn", 4)){
		LOGPF("Instance %s warning: %.*s", inst->name, (int) n - 8, data + 8);
	}
	return 0;
}

static int atem_handle_topology(instance* inst, size_t n, uint8_t* data){
	if(!memcmp(data + 4, "_MeC", 4) && n >= 11){
		LOGPF("M/E %d has %d keyers (trailer %02X %02X)", data[8], data[9], data[10], data[11]);
	}
	else if(!memcmp(data + 4, "_top", 4)){
		LOGPF("Handling topology on %s", inst->name);
		LOGPF("  %d M/Es", data[8]);
		LOGPF("  %d Sources", data[8 + 1]);
		LOGPF("  %d Color Generators", data[8 + 2]);
		LOGPF("  %d AUX buses", data[8 + 3]);
		LOGPF("  %d DSKs", data[8 + 4]);
		LOGPF("  %d Stingers", data[8 + 5]);
		LOGPF("  %d DVEs", data[8 + 6]);
		//hex_dump(data + 8, n - 8);
	}
	else if(!memcmp(data + 4, "_mpl", 4)){
		LOGPF("Handling mediaplayer on %s", inst->name);
		//hex_dump(data + 8, n - 8);
	}
	else if(!memcmp(data + 4, "_MvC", 4)){
		LOGPF("Handling multiview on %s", inst->name);
		//hex_dump(data + 8, n - 8);
	}
	else if(!memcmp(data + 4, "_FAC", 4)){
		LOGPF("Handling audio config(?) on %s", inst->name);
		//hex_dump(data + 8, n - 8);
	}
	else if(!memcmp(data + 4, "_FEC", 4)){
		LOGPF("Handling equalizer config(?) on %s", inst->name);
		//hex_dump(data + 8, n - 8);
	}
	else if(!memcmp(data + 4, "_MAC", 4)){
		LOGPF("Handling macro config on %s", inst->name);
		//hex_dump(data + 8, n - 8);
	}
	return 0;
}

static int atem_handle_input_props(instance* inst, size_t n, uint8_t* payload){
	uint16_t* input = (uint16_t*) (payload + 8);
	atem_instance_data* data = (atem_instance_data*) inst->impl;
	size_t u;

	if(n < 36){
		LOG("Short input property report packet");
		return 0;
	}

	LOGPF("%s: Source %d (%.*s / %.*s), type %d, port type %d", inst->name, be16toh(*input),
			20, payload + 8 + 2,
			4, payload + 8 + 22,
			payload[8 + 26 + 6], payload[8 + 26 + 5]); //port type may also be on  + 3

	//try to find the source in the internal registry
	for(u = 0; u < data->buses; u++){
		if(data->bus[u].id == be16toh(*input)){
			DBGPF("The announced source %d was already registered", be16toh(*input));
			break;
		}
	}

	//if not yet known, register it
	if(u == data->buses){
		DBGPF("The announced source %d is not yet registered", be16toh(*input));
		data->bus = realloc(data->bus, (data->buses + 1) * sizeof(atem_bus));
		if(!data->bus){
			//FIXME this should really fail
			LOG("Failed to allocate memory");
			data->buses = 0;
			return 1;
		}
		data->bus[data->buses].id = be16toh(*input);
		data->bus[data->buses].type = payload[8 + 26 + 6];
		data->buses++;
	}

	//dump only the options
	//hex_dump(data + 8 + 26, n - 8 - 26);
	return 0;
}

static int atem_handle_time(instance* inst, size_t n, uint8_t* data){
	DBGPF("Time data for %.*s", 4, data + 4);
	//hex_dump(data + 8, n - 8);
	//TODO
	//12 2D 3A 0C 00 00 00 00
	//12 2D 3A 0C 00 00 00 00
	return 0;
}

static int atem_handle_tally_index(instance* inst, size_t n, uint8_t* data){
	size_t u = 0;
	uint16_t* inputs = (uint16_t*) (data + 8);

	//sanity checks
	if(n < 8 + 2 || n < 8 + 2 + be16toh(*inputs)){
		return 0;
	}

	//[u16 srces] [n * u8 tally] [u16 trailer]
	//trailers observed: 00 47 (initial sync), FF FF (changed), 00 01 (??)
	DBGPF("%s: Tally for %d inputs (trailer %02X %02X):", inst->name, be16toh(*inputs), data[n - 2], data[n - 1]);
	for(u = 0; u < be16toh(*inputs); u++){
		DBGPF("  Input %" PRIsize_t " Tally %d", u + 1, data[u + 8 + 2]);
	}

	//not sure why there are two tally reports, we currently only generate output from the source tally message
	return 0;
}

static int atem_handle_tally_source(instance* inst, size_t n, uint8_t* data){
	uint16_t* next_data = (uint16_t*) (data + 8);
	uint16_t sources = be16toh(*next_data);
	size_t u = 0;
	atem_channel_ident ident = {
		.fields.system = atem_input
	};
	channel* out = NULL;
	channel_value value = {
		0
	};

	if(n < 8 + 2 || n < 8 + 2 + sources * 3){
		return 0;
	}

	//FIXME is there an m/e identifier present in this message?
	//[u16 nsrc] [nsrc * [u16 id, u8 tally]] [u8 trailer]
	//trailers observed: 00 (initial sync), 66 (changed), 02 (keyer on/off)
	DBGPF("%s: Tally for %d sources (trailer %02X):", inst->name, sources, data[n - 1]);

	for(u = 0; u < sources; u++){
		next_data = (uint16_t*) (data + 8 + 2 + u * 3);
		ident.fields.subcontrol = be16toh(*next_data);
		DBGPF("  Source ID %d Tally %d", ident.fields.subcontrol, data[8 + 2 + u * 3 + 2]);

		//check for program tally
		value.normalised = 0.0;
		ident.fields.control = input_program;
		out = mm_channel(inst, ident.label, 0);
		if(out){
			if(data[8 + 2 + u * 3 + 2] & 1){
				value.normalised = 1.0;
			}
			mm_channel_event(out, value);
		}

		//check for preview tally
		value.normalised = 0.0;
		ident.fields.control = input_preview;
		out = mm_channel(inst, ident.label, 0);
		if(out){
			if(data[8 + 2 + u * 3 + 2] & 2){
				value.normalised = 1.0;
			}
			mm_channel_event(out, value);
		}

		//check for anything else
		if(data[8 + 2 + u * 3 + 2] & ~3){
			LOGPF("Unknown tally flags %d present for source %d", data[8 + 2 + u * 3 + 2], ident.fields.subcontrol);
		}
	}
	return 0;
}

static int atem_handle_preview(instance* inst, size_t n, uint8_t* data){
	uint16_t* source = (uint16_t*) (data + 8 + 2);
	LOGPF("Preview changed on %s, source is now %d", inst->name, be16toh(*source));
	//connect	00 0C [u16 src] 00 6D 70 6C
	//		00 06 [u16 src] 00 FF FF FF
	return 0;
}

static int atem_handle_program(instance* inst, size_t n, uint8_t* data){
	uint16_t* source = (uint16_t*) (data + 8 + 2);
	uint16_t* unknown = (uint16_t*) (data + 8);
	LOGPF("Program changed on %s, source is now %d (header %04X)", inst->name, be16toh(*source), be16toh(*unknown));
	//hex_dump(data + 8, n - 8);
	//connect	00 0C [u16 src]
	//		00 00 [u16 src]
	//between internals 00 06 [u16 src]
	return 0;
}

static int atem_handle_tbar(instance* inst, size_t n, uint8_t* data){
	//hex_dump(data + 8, n - 8);
	//00 01 1A 00 [04 4D] 00 00
	//00 01 1A 00 [04 9D] 00 00
	//00 01 1A 00 [04 EB] 00 00
	//00 01 19 00 [05 3A] 00 00
	//00 01 19 00 [05 88] 00 00
	//00 01 19 00 [05 D7] 00 00
	uint16_t* active = (uint16_t*) (data + 8);
	uint16_t* timing = (uint16_t*) (data + 10);
	uint16_t* position = (uint16_t*) (data + 12);
	uint16_t* transition = (uint16_t*) (data + 14);
	LOGPF("T-Bar moved on %s: active %d, time %04X, position %d, transition %04X", inst->name, be16toh(*active), be16toh(*timing), be16toh(*position), be16toh(*transition));
	return 0;
}

static int atem_handle_color(instance* inst, size_t n, uint8_t* data){
	uint16_t* hue = (uint16_t*) (data + 10);
	uint16_t* saturation = (uint16_t*) (data + 12);
	uint16_t* luma = (uint16_t*) (data + 14);
	LOGPF("Color change on %s colorgen %d: hue %d saturation %d luma %d", inst->name, data[8] + 1, be16toh(*hue), be16toh(*saturation), be16toh(*luma));
	//[#] 04 [u16 hue] [u16 sat] [u16 lum]
	return 0;
}

static int atem_handle_stream(instance* inst, size_t n, uint8_t* data){
	DBGPF("Stream status data on %s", inst->name);
	hex_dump(data + 8, n - 8);
	return 0;
}

static int atem_handle_aux(instance* inst, size_t n, uint8_t* data){
	uint16_t* source = (uint16_t*) (data + 8 + 2);
	LOGPF("AUX %d changed to source %d", data[8] + 1, be16toh(*source));
	return 0;
}

static int atem_handle_ignore(instance* inst, size_t n, uint8_t* data){
	return 0;
}

static int atem_process(instance* inst, atem_hdr* hdr, uint8_t* payload, size_t payload_len){
	atem_instance_data* data = (atem_instance_data*) inst->impl;
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

	data->last_response = mm_timestamp();
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

	if(hdr->length & ATEM_SEQUENCE_VALID){
		data->txhdr.ack = hdr->seqno;
		//DBGPF("Acknowledging command %d on %s", hdr->seqno, inst->name);
		return atem_send(inst, NULL, 0);
	}

	return 1;
}

static uint32_t atem_interval(){
	return ATEM_ALIVE_INTERVAL;
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

		LOGPF("Connected instance %s to %s", inst->name, host);
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

static int atem_channel_input(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	char* token = spec;
	//skip input.
	token += 6;
	ident->fields.control = input_preview;

	if(!strncmp(token, "black", 5)){
		token += 5;
		ident->fields.subcontrol = 0;
	}
	else if(!strncmp(token, "bars", 4)){
		token += 4;
		ident->fields.subcontrol = 1000;
	}
	else if(!strncmp(token, "multiview", 9)){
		//FIXME bigger models seem to support more than one multiview output, but i don't know how to address those yet
		token += 9;
		ident->fields.subcontrol = 9001;
	}
	else if(!strncmp(token, "program", 7)){
		//FIXME multi-m/e models will have multiple program/preview buses, not sure how they're adressed...
		token += 7;
		ident->fields.subcontrol = 10010;
	}
	else if(!strncmp(token, "preview", 7)){
		//FIXME multi-m/e models will have multiple program/preview buses, not sure how they're adressed...
		token += 7;
		ident->fields.subcontrol = 10011;
	}
	else if(!strncmp(token, "color", 5)){
		token += 5;
		ident->fields.subcontrol = strtoul(token, &token, 10);
		if(!ident->fields.subcontrol){
			LOGPF("Missing index for color generator input spec %s", spec);
			return 1;
		}
		//add input offset for color gens
		ident->fields.subcontrol += 2000;
	}
	else if(!strncmp(token, "in", 2)){
		token += 2;
		ident->fields.subcontrol = strtoul(token, &token, 10);
		if(!ident->fields.subcontrol){
			LOGPF("Missing index for input spec %s", spec);
			return 1;
		}
	}
	else if(!strncmp(token, "mp", 2)){
		token += 2;

		if(!strncmp(token, "key", 3)){
			token += 3;
			ident->fields.extra = 1;
		}

		ident->fields.subcontrol = strtoul(token, &token, 10);
		if(!ident->fields.subcontrol){
			LOGPF("Missing index for mediaplayer input spec %s", spec);
			return 1;
		}

		//FIXME mp1 seems to be 3010, however i'm unclear on how this increments for mp2 etc
		ident->fields.subcontrol *= 10;
		ident->fields.subcontrol += 3000 + ident->fields.extra;
		ident->fields.extra = 0;
	}
	else{
		LOGPF("Unknown input channel spec %s", spec);
		return 1;
	}

	if(*token == '.'){
		token++;
		if(!strcmp(token, "preview")){
			ident->fields.control = input_preview;
		}
		else if(!strcmp(token, "program")){
			ident->fields.control = input_program;
		}
		else if(!strncmp(token, "dsk", 3)){
			token += 3;
			ident->fields.control = input_dsk_key;

			if(!strncmp(token, "fill", 4)){
				token += 4;
				ident->fields.control = input_dsk_fill;
			}

			ident->fields.extra = strtoul(token, NULL, 10);
			if(!ident->fields.extra){
				LOGPF("Missing or erroneous keyer ID for spec %s", spec);
				return 1;
			}
		}
		else if(!strncmp(token, "usk", 3)){
			token += 3;
			ident->fields.control = input_usk_key;

			if(!strncmp(token, "fill", 4)){
				token += 4;
				ident->fields.control = input_usk_fill;
			}

			ident->fields.extra = strtoul(token, NULL, 10);
			if(!ident->fields.extra){
				LOGPF("Missing or erroneous keyer ID for spec %s", spec);
				return 1;
			}
		}
		else if(!strncmp(token, "aux", 3)){
			ident->fields.control = input_aux;
			ident->fields.extra = strtoul(token + 3, NULL, 10);
			if(!ident->fields.extra){
				LOGPF("Missing or erroneous aux ID for spec %s", spec);
				return 1;
			}
		}
		else{
			LOGPF("Unknown input action spec %s", spec);
			return 1;
		}
	}

	return 0;
}

static int atem_channel_mediaplayer(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//TODO
	return 1;
}

static int atem_channel_dsk(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//TODO
	return 1;
}

static int atem_channel_usk(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//TODO
	return 1;
}

static int atem_channel_colorgen(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//skip colorgen
	char* token = spec + 8;

	uint8_t generator = strtoul(token, &token, 10);
	if(!generator || *token != '.'){
		LOGPF("Invalid color generator spec %s", spec);
		return 1;
	}

	//skip dot
	token++;
	ident->fields.me = generator;

	if(!strcmp(token, "hue")){
		ident->fields.control = color_hue;
	}
	else if(!strcmp(token, "saturation")){
		ident->fields.control = color_saturation;
	}
	else if(!strcmp(token, "luminance")){
		ident->fields.control = color_luminance;
	}
	else{
		LOGPF("Unknown colorgen control %s", token);
		return 1;
	}

	return 0;
}

static int atem_channel_playout(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//skip playout.
	spec += 8;

	if(!strcmp(spec, "stream")){
		ident->fields.control = playout_stream;
	}
	else{
		LOGPF("Unknown playout control %s", spec);
		return 1;
	}

	return 0;
}

static int atem_channel_transition(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//skip transition.
	spec += 11;

	if(!strcmp(spec, "auto")){
		ident->fields.control = transition_auto;
	}
	else if(!strcmp(spec, "cut")){
		ident->fields.control = transition_cut;
	}
	else if(!strcmp(spec, "ftb")){
		ident->fields.control = transition_ftb;
	}
	else if(!strcmp(spec, "tbar")){
		ident->fields.control = transition_tbar;
	}
	else{
		LOGPF("Unknown transition channel spec %s", spec);
		return 1;
	}

	return 0;
}

static int atem_channel_audio(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags){
	//TODO
	return 1;
}

static channel* atem_channel(instance* inst, char* spec, uint8_t flags){
	char* token = spec;
	size_t n = 0;
	atem_channel_ident ident = {
		.label = 0
	};

	if(!strncmp(token, "me", 2)){
		ident.fields.me = strtoul(spec + 2, &token, 10);
		if(*token != '.' || !ident.fields.me){
			LOGPF("Failed to read M/E spec for %s", spec);
			return NULL;
		}
		token++;
		ident.fields.me--;
	}

	for(n = 0; n < atem_sentinel; n++){
		if(!strncmp(token, atem_systems[n].id, strlen(atem_systems[n].id))){
			//parse using subsystem parser
			if(atem_systems[n].parser){
				ident.fields.system = n;
				if(atem_systems[n].parser(inst, &ident, token, flags)){
					break;
				}
				return mm_channel(inst, ident.label, 1);
			}
			LOGPF("Failed to detect system of spec %s", spec);
			break;
		}
	}
	return NULL;
}

static int atem_control_transition(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v){
	uint8_t buffer[ATEM_PAYLOAD_MAX] = "";
	atem_command_hdr* hdr = (atem_command_hdr*) buffer;
	uint16_t* parameter = NULL;

	switch(ident->fields.control){
		case transition_cut:
			//TODO debounce this
			if(v->normalised > 0.9){
				hdr->length = htobe16(12);
				memcpy(hdr->command, "DCut", 4);
				hdr->me = ident->fields.me;
				//trailer bd b6 49
				return atem_send(inst, buffer, 12);
			}
			return 0;
		case transition_auto:
			//TODO debounce this
			if(v->normalised > 0.9){
				hdr->length = htobe16(12);
				memcpy(hdr->command, "DAut", 4);
				hdr->me = ident->fields.me;
				//trailer f9 1c b7
				return atem_send(inst, buffer, 12);
			}
			return 0;
		case transition_tbar:
			//TODO value range needs to be inverted after completion
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CTPs", 4);
			hdr->me = ident->fields.me;
			parameter = (uint16_t*) (buffer + sizeof(atem_command_hdr));
			*parameter = htobe16((uint16_t) (v->normalised * 10000));
			return atem_send(inst, buffer, 12);
		case transition_ftb:
			//TODO debounce this
			if(v->normalised > 0.9){
				hdr->length = htobe16(12);
				memcpy(hdr->command, "FtbA", 4);
				hdr->me = ident->fields.me;
				//trailer e5 ab 49
				return atem_send(inst, buffer, 12);
			}
			return 0;
	}
	return 1;
}

static int atem_control_input(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v){
	uint8_t buffer[ATEM_PAYLOAD_MAX] = "";
	atem_command_hdr* hdr = (atem_command_hdr*) buffer;
	uint16_t* parameter = (uint16_t*) (buffer + sizeof(atem_command_hdr));

	//all of these are oneshot keys, so bail out early
	if(v->normalised < 0.9){
		return 0;
	}

	switch(ident->fields.control){
		case input_preview:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CPvI", 4);
			hdr->me = ident->fields.me;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_program:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CPgI", 4);
			hdr->me = ident->fields.me;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_usk_fill:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CKeF", 4);
			hdr->me = ident->fields.me;
			hdr->reserved2 = ident->fields.extra - 1;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_usk_key:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CKeC", 4);
			hdr->me = ident->fields.me;
			hdr->reserved2 = ident->fields.extra - 1;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_dsk_fill:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CDsF", 4);
			hdr->me = ident->fields.extra - 1;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_dsk_key:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CDsC", 4);
			hdr->me = ident->fields.extra - 1;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
		case input_aux:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "CAuS", 4);
			hdr->me = 1; //seems to always be the same
			hdr->reserved2 = ident->fields.extra - 1;
			*parameter = htobe16(ident->fields.subcontrol);
			return atem_send(inst, buffer, 12);
	}
	return 1;
}

static int atem_control_playout(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v){
	uint8_t buffer[ATEM_PAYLOAD_MAX] = "";
	atem_command_hdr* hdr = (atem_command_hdr*) buffer;

	switch(ident->fields.control){
		case playout_stream:
			hdr->length = htobe16(12);
			memcpy(hdr->command, "StrR", 4);
			hdr->me = (v->normalised > 0.9) ? 1 : 0;
			return atem_send(inst, buffer, 14);
	}

	return 1;
}

static int atem_control_colorgen(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v){
	uint8_t buffer[ATEM_PAYLOAD_MAX] = "";
	atem_command_hdr* hdr = (atem_command_hdr*) buffer;
	uint16_t* hue = (uint16_t*) (buffer + sizeof(atem_command_hdr));
	uint16_t* saturation = (uint16_t*) (buffer + sizeof(atem_command_hdr) + 2);
	uint16_t* luma = (uint16_t*) (buffer + sizeof(atem_command_hdr) + 4);

	hdr->length = htobe16(16);
	memcpy(hdr->command, "CClV", 4);
	hdr->reserved2 = ident->fields.me - 1;

	switch(ident->fields.control){
		case color_hue:
			hdr->me = 1;
			*hue = htobe16((uint16_t) (v->normalised * 3599.0));
			break;
		case color_saturation:
			hdr->me = 1 << 1;
			*saturation = htobe16((uint16_t) (v->normalised * 1000.0));
			break;
		case color_luminance:
			hdr->me = 1 << 2;
			*luma = htobe16((uint16_t) (v->normalised * 1000.0));
			break;
		default:
			LOG("Unknown colorgen channel control");
			return 0;
	}

	return atem_send(inst, buffer, 18);
}

static int atem_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t n = 0;
	atem_channel_ident ident;

	for(n = 0; n < num; n++){
		ident.label = c[n]->ident;

		//sanity check
		if(ident.fields.system >= atem_sentinel){
			continue;
		}

		//handle input
		if(atem_systems[ident.fields.system].handler){
			atem_systems[ident.fields.system].handler(inst, &ident, c[n], v + n);
		}
	}
	return 0;
}

static int atem_handle(size_t num, managed_fd* fds){
	size_t n, u;
	instance** be_insts = NULL;
	instance* inst = NULL;
	atem_instance_data* data = NULL;
	ssize_t bytes;
	struct {
		atem_hdr hdr;
		uint8_t payload[ATEM_PAYLOAD_MAX];
	} rx = {
		0
	};

	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		bytes = recv(fds[u].fd, (uint8_t*) &rx, sizeof(rx), 0);
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
			//DBGPF("Received %" PRIsize_t " bytes (%u bytes indicated) for session %04X (ack %04X seqno %04X) on instance %s",
			//		bytes, ATEM_LENGTH(rx.hdr.length), rx.hdr.session, rx.hdr.ack, rx.hdr.seqno, inst->name);
			atem_process(inst, &rx.hdr, rx.payload, bytes - sizeof(atem_hdr));
		}
		else{
			DBGPF("Received %" PRIsize_t " bytes of short message data on instance %s", bytes, inst->name);
		}
	}

	//regularly check all instances for an active connection
	if(mm_timestamp() - alive_timestamp > ATEM_ALIVE_INTERVAL){
		//fetch all instances
		if(mm_backend_instances(BACKEND_NAME, &n, &be_insts)){
			LOG("Failed to fetch instance list");
			return 1;
		}

		DBGPF("Running alive check on %" PRIsize_t " instances", n);

		for(u = 0; u < n; u++){
			//check if they're alive
			data = (atem_instance_data*) be_insts[u]->impl;
			if(mm_timestamp() - data->last_response > ATEM_ALIVE_INTERVAL){
				//dis/reconnect if not
				atem_disconnect(be_insts[u]);
				atem_connect(be_insts[u]);
			}
		}

		free(be_insts);
		alive_timestamp = mm_timestamp();
	}
	return 0;
}

static int atem_start(size_t n, instance** inst){
	size_t u;
	atem_instance_data* data = NULL;
	alive_timestamp = mm_timestamp();

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
	size_t u = 0;
	atem_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (atem_instance_data*) inst[u]->impl;
		atem_disconnect(inst[u]);
		close(data->fd);
		data->fd = -1;
	}

	LOG("Backend shut down");
	return 0;
}
