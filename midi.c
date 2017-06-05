#include <string.h>
#include <alsa/asoundlib.h>
#include "midi.h"

#define BACKEND_NAME "midi"
static snd_seq_t* sequencer = NULL;

/*
 * TODO
 * 	Optionally send note-off messages
 */

enum /*_midi_channel_type*/ {
	none = 0,
	note,
	cc,
	sysmsg
};

int midi_init(){
	backend midi = {
		.name = BACKEND_NAME,
		.conf = midi_configure,
		.create = midi_instance,
		.conf_instance = midi_configure_instance,
		.channel = midi_channel,
		.handle = midi_set,
		.process = midi_handle,
		.start = midi_start,
		.shutdown = midi_shutdown
	};

	if(snd_seq_open(&sequencer, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
		fprintf(stderr, "Failed to open ALSA sequencer\n");
		return 1;
	}

	//register backend
	if(mm_backend_register(midi)){
		fprintf(stderr, "Failed to register MIDI backend\n");
		return 1;
	}

	snd_seq_nonblock(sequencer, 1);		
		
	fprintf(stderr, "MIDI client ID is %d\n", snd_seq_client_id(sequencer));
	return 0;
}

static int midi_configure(char* option, char* value){
	if(!strcmp(option, "name")){
		if(snd_seq_set_client_name(sequencer, value) < 0){
			fprintf(stderr, "Failed to set MIDI client name to %s\n", value);
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown MIDI backend option %s\n", option);
	return 1;
}

static instance* midi_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	inst->impl = calloc(1, sizeof(midi_instance_data));
	if(!inst->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return inst;
}

static int midi_configure_instance(instance* instance, char* option, char* value){
	midi_instance_data* data = (midi_instance_data*) instance->impl;

	//FIXME maybe allow connecting more than one device
	if(!strcmp(option, "read")){
		//connect input device
		if(data->read){
			fprintf(stderr, "Port already connected to an input device\n");
			return 1;
		}
		data->read = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "write")){
		//connect output device
		if(data->write){
			fprintf(stderr, "Port already connected to an output device\n");
			return 1;
		}
		data->write = strdup(value);
		return 0;
	}

	fprintf(stderr, "Unknown MIDI instance option %s\n", option);
	return 1;
}

static channel* midi_channel(instance* instance, char* spec){
	union {
		struct {
			uint8_t pad[5];
			uint8_t type;
			uint8_t channel;
			uint8_t control;
		} fields;
		uint64_t label;
	} ident = {
		.label = 0
	};

	char* channel;

	if(!strncmp(spec, "cc", 2)){
		ident.fields.type = cc;
		channel = spec + 2;
	}
	else if(!strncmp(spec, "note", 4)){
		ident.fields.type = note;
		channel = spec + 4;
	}
	else{
		fprintf(stderr, "Unknown MIDI channel specification %s\n", spec);
		return NULL;
	}

	ident.fields.channel = strtoul(channel, &channel, 10);

	//FIXME test this
	if(ident.fields.channel > 16){
		fprintf(stderr, "Channel out of range in channel spec %s\n", spec);
		return NULL;
	}

	if(*channel != '.'){
		fprintf(stderr, "Need MIDI channel specification of form channel.control, had %s\n", spec);
		return NULL;
	}
	channel++;

	ident.fields.control = strtoul(channel, NULL, 10);

	if(ident.label){
		return mm_channel(instance, ident.label, 1);
	}

	return NULL;
}

static int midi_set(size_t num, channel* c, channel_value* v){
	//TODO
	return 1;
}

static int midi_handle(size_t num, int* fd, void** data){
	//TODO
	return 1;
}

static int midi_start(){
	size_t n, p;
	int nfds, rv = 1;
	struct pollfd* pfds = NULL;
	instance** inst = NULL;
	midi_instance_data* data = NULL;
	snd_seq_addr_t addr;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	//create all ports
	for(p = 0; p < n; p++){
		data = (midi_instance_data*) inst[p]->impl;
		data->port = snd_seq_create_simple_port(sequencer, inst[p]->name, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE | SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC);

		//make connections
		if(data->write){
			fprintf(stderr, "Connecting output of instance %s to MIDI device %s\n", inst[p]->name, data->write);
			if(snd_seq_parse_address(sequencer, &addr, data->write) == 0){
				snd_seq_connect_to(sequencer, data->port, addr.client, addr.port);
			}
			else{
				fprintf(stderr, "Failed to get destination device address: %s\n", data->write);
			}
			free(data->write);
			data->write = NULL;
		}

		if(data->read){
			fprintf(stderr, "Connecting input from MIDI device %s to instance %s\n", data->read, inst[p]->name);
			if(snd_seq_parse_address(sequencer, &addr, data->read) == 0){
				snd_seq_connect_from(sequencer, data->port, addr.client, addr.port);
			}
			else{
				fprintf(stderr, "Failed to get source device address: %s\n", data->read);
			}
			free(data->read);
			data->read = NULL;
		}
	}

	//register all fds to core
	nfds = snd_seq_poll_descriptors_count(sequencer, POLLIN | POLLOUT);	
	pfds = calloc(nfds, sizeof(struct pollfd));
	if(!pfds){
		fprintf(stderr, "Failed to allocate memory\n");
		goto bail;
	}
	nfds = snd_seq_poll_descriptors(sequencer, pfds, nfds, POLLIN | POLLOUT);	

	fprintf(stderr, "Registering %d descriptors to core\n", nfds);
	for(p = 0; p < nfds; p++){
		mm_manage_fd(pfds[p].fd, BACKEND_NAME, 1, NULL);
	}

	rv = 0;

bail:
	free(pfds);
	free(inst);
	return rv;
}

static int midi_shutdown(){
	size_t n, p;
	instance** inst = NULL;
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(p = 0; p < n; p++){
		free(inst[p]->impl);
	}
	free(inst);

	//close midi
	snd_seq_close(sequencer);
	sequencer = NULL;

	//free configuration cache
	snd_config_update_free_global();

	fprintf(stderr, "MIDI backend shut down\n");
	return 0;
}
