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
	return mm_instance();
}

static int midi_configure_instance(instance* instance, char* option, char* value){
	if(!strcmp(option, "device")){
		//open i/o device
		return 0;
	}
	else if(!strcmp(option, "port")){
		//create midi port
		return 0;
	}
	else if(!strcmp(option, "mode")){
		//configure open mode
		//FIXME needed?
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
		return mm_channel(instance, ident.label);
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
	return 0;
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
