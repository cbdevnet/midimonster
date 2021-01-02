#define BACKEND_NAME "midi"

#include <string.h>
#include <alsa/asoundlib.h>
#include "midi.h"

static char* sequencer_name = NULL;
static snd_seq_t* sequencer = NULL;

enum /*_midi_channel_type*/ {
	none = 0,
	note,
	cc,
	pressure,
	aftertouch,
	pitchbend,
	rpn,
	nrpn
};

static struct {
	uint8_t detect;
} midi_config = {
	.detect = 0
};

MM_PLUGIN_API int init(){
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

	if(sizeof(midi_channel_ident) != sizeof(uint64_t)){
		LOG("Channel identification union out of bounds");
		return 1;
	}

	//register backend
	if(mm_backend_register(midi)){
		LOG("Failed to register backend");
		return 1;
	}

	return 0;
}

static int midi_configure(char* option, char* value){
	if(!strcmp(option, "name")){
		free(sequencer_name);
		sequencer_name = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "detect")){
		midi_config.detect = 1;
		if(!strcmp(value, "off")){
			midi_config.detect = 0;
		}
		return 0;
	}

	LOGPF("Unknown backend option %s", option);
	return 1;
}

static int midi_instance(instance* inst){
	inst->impl = calloc(1, sizeof(midi_instance_data));
	if(!inst->impl){
		LOG("Failed to allocate memory");
		return 1;
	}

	return 0;
}

static int midi_configure_instance(instance* inst, char* option, char* value){
	midi_instance_data* data = (midi_instance_data*) inst->impl;

	//FIXME maybe allow connecting more than one device
	if(!strcmp(option, "read")){
		//connect input device
		if(data->read){
			LOGPF("Instance %s was already connected to an input device", inst->name);
			return 1;
		}
		data->read = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "write")){
		//connect output device
		if(data->write){
			LOGPF("Instance %s was already connected to an output device", inst->name);
			return 1;
		}
		data->write = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "epn-tx")){
		data->epn_tx_short = 0;
		if(!strcmp(value, "short")){
			data->epn_tx_short = 1;
		}
		return 0;
	}

	LOGPF("Unknown instance configuration option %s on instance %s", option, inst->name);
	return 1;
}

static channel* midi_channel(instance* inst, char* spec, uint8_t flags){
	midi_channel_ident ident = {
		.label = 0
	};

	char* channel = NULL;
	if(!strncmp(spec, "ch", 2)){
		channel = spec + 2;
		if(!strncmp(spec, "channel", 7)){
			channel = spec + 7;
		}
	}

	if(!channel){
		LOGPF("Invalid channel specification %s", spec);
		return NULL;
	}

	ident.fields.channel = strtoul(channel, &channel, 10);
	if(ident.fields.channel > 15){
		LOGPF("MIDI channel out of range in spec %s", spec);
		return NULL;
	}

	if(*channel != '.'){
		LOGPF("Need specification of form channel<X>.<control><Y>, had %s", spec);
		return NULL;
	}
	//skip the period
	channel++;

	if(!strncmp(channel, "cc", 2)){
		ident.fields.type = cc;
		channel += 2;
	}
	else if(!strncmp(channel, "note", 4)){
		ident.fields.type = note;
		channel += 4;
	}
	else if(!strncmp(channel, "pressure", 8)){
		ident.fields.type = pressure;
		channel += 8;
	}
	else if(!strncmp(channel, "rpn", 3)){
		ident.fields.type = rpn;
		channel += 3;
	}
	else if(!strncmp(channel, "nrpn", 4)){
		ident.fields.type = nrpn;
		channel += 4;
	}
	else if(!strncmp(channel, "pitch", 5)){
		ident.fields.type = pitchbend;
	}
	else if(!strncmp(channel, "aftertouch", 10)){
		ident.fields.type = aftertouch;
	}
	else{
		LOGPF("Unknown control type in %s", spec);
		return NULL;
	}

	ident.fields.control = strtoul(channel, NULL, 10);

	if(ident.label){
		return mm_channel(inst, ident.label, 1);
	}

	return NULL;
}

static void midi_tx(int port, uint8_t type, uint8_t channel, uint8_t control, uint16_t value){
	snd_seq_event_t ev;

	snd_seq_ev_clear(&ev);
	snd_seq_ev_set_source(&ev, port);
	snd_seq_ev_set_subs(&ev);
	snd_seq_ev_set_direct(&ev);

	switch(type){
		case note:
			snd_seq_ev_set_noteon(&ev, channel, control, value);
			break;
		case cc:
			snd_seq_ev_set_controller(&ev, channel, control, value);
			break;
		case pressure:
			snd_seq_ev_set_keypress(&ev, channel, control, value);
			break;
		case pitchbend:
			snd_seq_ev_set_pitchbend(&ev, channel, value);
			break;
		case aftertouch:
			snd_seq_ev_set_chanpress(&ev, channel, value);
			break;
	}

	snd_seq_event_output(sequencer, &ev);
}

static int midi_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t u;
	midi_instance_data* data = (midi_instance_data*) inst->impl;
	midi_channel_ident ident = {
		.label = 0
	};

	for(u = 0; u < num; u++){
		ident.label = c[u]->ident;

		switch(ident.fields.type){
			case rpn:
			case nrpn:
				//transmit parameter number
				midi_tx(data->port, cc, ident.fields.channel, (ident.fields.type == rpn) ? 101 : 99, (ident.fields.control >> 7) & 0x7F);
				midi_tx(data->port, cc, ident.fields.channel, (ident.fields.type == rpn) ? 100 : 98, ident.fields.control & 0x7F);
				//transmit parameter value
				midi_tx(data->port, cc, ident.fields.channel, 6, (((uint16_t) (v[u].normalised * 16383.0)) >> 7) & 0x7F);
				midi_tx(data->port, cc, ident.fields.channel, 38, ((uint16_t) (v[u].normalised * 16383.0)) & 0x7F);

				if(!data->epn_tx_short){
					//clear active parameter
					midi_tx(data->port, cc, ident.fields.channel, 101, 127);
					midi_tx(data->port, cc, ident.fields.channel, 100, 127);
				}
				break;
			case pitchbend:
				midi_tx(data->port, ident.fields.type, ident.fields.channel, ident.fields.control, (v[u].normalised * 16383.0) - 8192);
				break;
			default:
				midi_tx(data->port, ident.fields.type, ident.fields.channel, ident.fields.control, v[u].normalised * 127.0);
		}
	}

	snd_seq_drain_output(sequencer);
	return 0;
}

static char* midi_type_name(uint8_t type){
	switch(type){
		case none:
			return "none";
		case note:
			return "note";
		case cc:
			return "cc";
		case rpn:
			return "rpn";
		case nrpn:
			return "nrpn";
		case pressure:
			return "pressure";
		case aftertouch:
			return "aftertouch";
		case pitchbend:
			return "pitch";
	}
	return "unknown";
}

static int midi_handle(size_t num, managed_fd* fds){
	snd_seq_event_t* ev = NULL;
	instance* inst = NULL;
	channel* changed = NULL;
	channel_value val;
	char* event_type = NULL;
	midi_channel_ident ident = {
		.label = 0
	};

	if(!num){
		return 0;
	}

	while(snd_seq_event_input(sequencer, &ev) > 0){
		event_type = NULL;
		ident.label = 0;

		ident.fields.channel = ev->data.note.channel;
		ident.fields.control = ev->data.note.note;
		val.normalised = (double) ev->data.note.velocity / 127.0;

		//TODO (n)rpn RX
		switch(ev->type){
			case SND_SEQ_EVENT_NOTEON:
			case SND_SEQ_EVENT_NOTEOFF:
			case SND_SEQ_EVENT_NOTE:
				ident.fields.type = note;
				if(ev->type == SND_SEQ_EVENT_NOTEOFF){
   					val.normalised = 0;
				}
				break;
			case SND_SEQ_EVENT_KEYPRESS:
				ident.fields.type = pressure;
				break;
			case SND_SEQ_EVENT_CHANPRESS:
				ident.fields.type = aftertouch;
				ident.fields.channel = ev->data.control.channel;
				val.normalised = (double) ev->data.control.value / 127.0;
				break;
			case SND_SEQ_EVENT_PITCHBEND:
				ident.fields.type = pitchbend;
				ident.fields.channel = ev->data.control.channel;
				val.normalised = ((double) ev->data.control.value + 8192) / 16383.0;
				break;
			case SND_SEQ_EVENT_CONTROLLER:
				ident.fields.type = cc;
				ident.fields.channel = ev->data.control.channel;
				ident.fields.control = ev->data.control.param;
				val.normalised = (double) ev->data.control.value / 127.0;
				break;
			default:
				LOG("Ignored event of unsupported type");
				continue;
		}

		event_type = midi_type_name(ident.fields.type);
		inst = mm_instance_find(BACKEND_NAME, ev->dest.port);
		if(!inst){
			//FIXME might want to return failure
			LOG("Delivered event did not match any instance");
			continue;
		}

		changed = mm_channel(inst, ident.label, 0);
		if(changed){
			if(mm_channel_event(changed, val)){
				free(ev);
				return 1;
			}
		}

		if(midi_config.detect && event_type){
			if(ident.fields.type == pitchbend || ident.fields.type == aftertouch){
				LOGPF("Incoming data on channel %s.ch%d.%s", inst->name, ident.fields.channel, event_type);
			}
			else{
				LOGPF("Incoming data on channel %s.ch%d.%s%d", inst->name, ident.fields.channel, event_type, ident.fields.control);
			}
		}
	}
	free(ev);
	return 0;
}

static int midi_start(size_t n, instance** inst){
	size_t p;
	int nfds, rv = 1;
	struct pollfd* pfds = NULL;
	midi_instance_data* data = NULL;
	snd_seq_addr_t addr;

	//connect to the sequencer
	if(snd_seq_open(&sequencer, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
		LOG("Failed to open ALSA sequencer");
		goto bail;
	}

	snd_seq_nonblock(sequencer, 1);
	LOGPF("Client ID is %d", snd_seq_client_id(sequencer));

	//update the sequencer client name
	if(snd_seq_set_client_name(sequencer, sequencer_name ? sequencer_name : "MIDIMonster") < 0){
		LOGPF("Failed to set client name to %s", sequencer_name);
		goto bail;
	}

	//create all ports
	for(p = 0; p < n; p++){
		data = (midi_instance_data*) inst[p]->impl;
		data->port = snd_seq_create_simple_port(sequencer, inst[p]->name, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE | SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
		inst[p]->ident = data->port;

		//make connections
		if(data->write){
			if(snd_seq_parse_address(sequencer, &addr, data->write) == 0){
				LOGPF("Connecting output of instance %s to device %s (%d:%d)", inst[p]->name, data->write, addr.client, addr.port);
				snd_seq_connect_to(sequencer, data->port, addr.client, addr.port);
			}
			else{
				LOGPF("Failed to get destination device address: %s", data->write);
			}
			free(data->write);
			data->write = NULL;
		}

		if(data->read){
			if(snd_seq_parse_address(sequencer, &addr, data->read) == 0){
				LOGPF("Connecting input from device %s to instance %s (%d:%d)", data->read, inst[p]->name, addr.client, addr.port);
				snd_seq_connect_from(sequencer, data->port, addr.client, addr.port);
			}
			else{
				LOGPF("Failed to get source device address: %s", data->read);
			}
			free(data->read);
			data->read = NULL;
		}
	}

	//register all fds to core
	nfds = snd_seq_poll_descriptors_count(sequencer, POLLIN | POLLOUT);
	pfds = calloc(nfds, sizeof(struct pollfd));
	if(!pfds){
		LOG("Failed to allocate memory");
		goto bail;
	}
	nfds = snd_seq_poll_descriptors(sequencer, pfds, nfds, POLLIN | POLLOUT);

	LOGPF("Registering %d descriptors to core", nfds);
	for(p = 0; p < nfds; p++){
		if(mm_manage_fd(pfds[p].fd, BACKEND_NAME, 1, NULL)){
			goto bail;
		}
	}

	rv = 0;

bail:
	free(pfds);
	return rv;
}

static int midi_shutdown(size_t n, instance** inst){
	size_t p;
	midi_instance_data* data = NULL;

	for(p = 0; p < n; p++){
		data = (midi_instance_data*) inst[p]->impl;
		free(data->read);
		free(data->write);
		data->read = NULL;
		data->write = NULL;
		free(inst[p]->impl);
	}

	//close midi
	if(sequencer){
		snd_seq_close(sequencer);
		sequencer = NULL;
	}

	//free configuration cache
	snd_config_update_free_global();

	free(sequencer_name);
	sequencer_name = NULL;

	LOG("Backend shut down");
	return 0;
}
