#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jack.h"
#include <jack/midiport.h>
#include <jack/metadata.h>

#define BACKEND_NAME "jack"
#define JACKEY_SIGNAL_TYPE "http://jackaudio.org/metadata/signal-type"

#ifdef __APPLE__
	#ifndef PTHREAD_MUTEX_ADAPTIVE_NP
		#define PTHREAD_MUTEX_ADAPTIVE_NP PTHREAD_MUTEX_DEFAULT
	#endif
#endif

//FIXME pitchbend range is somewhat oob

static struct /*_mmjack_backend_cfg*/ {
	unsigned verbosity;
	volatile sig_atomic_t jack_shutdown;
} config = {
	.verbosity = 1,
	.jack_shutdown = 0
};

MM_PLUGIN_API int init(){
	backend mmjack = {
		.name = BACKEND_NAME,
		.conf = mmjack_configure,
		.create = mmjack_instance,
		.conf_instance = mmjack_configure_instance,
		.channel = mmjack_channel,
		.handle = mmjack_set,
		.process = mmjack_handle,
		.start = mmjack_start,
		.shutdown = mmjack_shutdown
	};

	if(sizeof(mmjack_channel_ident) != sizeof(uint64_t)){
		fprintf(stderr, "jack channel identification union out of bounds\n");
		return 1;
	}

	//register backend
	if(mm_backend_register(mmjack)){
		fprintf(stderr, "Failed to register jack backend\n");
		return 1;
	}
	return 0;
}

static void mmjack_message_print(const char* msg){
	fprintf(stderr, "JACK message: %s\n", msg);
}

static void mmjack_message_ignore(const char* msg){
}

static int mmjack_midiqueue_append(mmjack_port* port, mmjack_channel_ident ident, uint16_t value){
	//append events
	if(port->queue_len == port->queue_alloc){
		//extend the queue
		port->queue = realloc(port->queue, (port->queue_len + JACK_MIDIQUEUE_CHUNK) * sizeof(mmjack_midiqueue));
		if(!port->queue){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		port->queue_alloc += JACK_MIDIQUEUE_CHUNK;
	}

	port->queue[port->queue_len].ident.label = ident.label;
	port->queue[port->queue_len].raw = value;
	port->queue_len++;
	DBGPF("Appended event to queue for %s, now at %" PRIsize_t " entries\n", port->name, port->queue_len);
	return 0;
}

static int mmjack_process_midi(instance* inst, mmjack_port* port, size_t nframes, size_t* mark){
	void* buffer = jack_port_get_buffer(port->port, nframes);
	jack_nframes_t event_count = jack_midi_get_event_count(buffer);
	jack_midi_event_t event;
	jack_midi_data_t* event_data;
	mmjack_channel_ident ident;
	size_t u;
	uint16_t value;

	if(port->input){
		if(event_count){
			DBGPF("Reading %u MIDI events from jack port %s\n", event_count, port->name);
			for(u = 0; u < event_count; u++){
				ident.label = 0;
				//read midi data from stream
				jack_midi_event_get(&event, buffer, u);
				//ident.fields.port set on output in mmjack_handle_midi
				ident.fields.sub_channel = event.buffer[0] & 0x0F;
				ident.fields.sub_type = event.buffer[0] & 0xF0;
				ident.fields.sub_control = event.buffer[1];
				value = event.buffer[2];
				if(ident.fields.sub_type == 0x80){
					ident.fields.sub_type = midi_note;
					value = 0;
				}
				else if(ident.fields.sub_type == midi_pitchbend){
					ident.fields.sub_control = 0;
					value = event.buffer[1] | (event.buffer[2] << 7);
				}
				else if(ident.fields.sub_type == midi_aftertouch){
					ident.fields.sub_control = 0;
					value = event.buffer[1];
				}
				//append midi data
				mmjack_midiqueue_append(port, ident, value);
			}
			port->mark = 1;
			*mark = 1;
		}
	}
	else{
		//clear buffer
		jack_midi_clear_buffer(buffer);

		for(u = 0; u < port->queue_len; u++){
			//build midi event
			ident.label = port->queue[u].ident.label;
			event_data = jack_midi_event_reserve(buffer, u, (ident.fields.sub_type == midi_aftertouch) ? 2 : 3);
			if(!event_data){
				fprintf(stderr, "Failed to reserve MIDI stream data\n");
				return 1;
			}
			event_data[0] = ident.fields.sub_channel | ident.fields.sub_type;
			if(ident.fields.sub_type == midi_pitchbend){
				event_data[1] = port->queue[u].raw & 0x7F;
				event_data[2] = (port->queue[u].raw >> 7) & 0x7F;
			}
			else if(ident.fields.sub_type == midi_aftertouch){
				event_data[1] = port->queue[u].raw & 0x7F;
			}
			else{
				event_data[1] = ident.fields.sub_control;
				event_data[2] = port->queue[u].raw & 0x7F;
			}
		}

		if(port->queue_len){
			DBGPF("Wrote %" PRIsize_t " MIDI events to jack port %s\n", port->queue_len, port->name);
		}
		port->queue_len = 0;
	}
	return 0;
}

static int mmjack_process_cv(instance* inst, mmjack_port* port, size_t nframes, size_t* mark){
	jack_default_audio_sample_t* audio_buffer = jack_port_get_buffer(port->port, nframes);
	size_t u;

	if(port->input){
		//read updated data into the local buffer
		//FIXME maybe we don't want to always use the first sample...
		if((double) audio_buffer[0] != port->last){
			port->last = audio_buffer[0];
			port->mark = 1;
			*mark = 1;
		}
	}
	else{
		for(u = 0; u < nframes; u++){
			audio_buffer[u] = port->last;
		}
	}
	return 0;
}

static int mmjack_process(jack_nframes_t nframes, void* instp){
	instance* inst = (instance*) instp;
	mmjack_instance_data* data = (mmjack_instance_data*) inst->impl;
	size_t p, mark = 0;
	int rv = 0;

	//DBGPF("jack callback for %d frames on %s\n", nframes, inst->name);

	for(p = 0; p < data->ports; p++){
		pthread_mutex_lock(&data->port[p].lock);
		switch(data->port[p].type){
			case port_midi:
				//DBGPF("Handling MIDI port %s.%s\n", inst->name, data->port[p].name);
				rv |= mmjack_process_midi(inst, data->port + p, nframes, &mark);
				break;
			case port_cv:
				//DBGPF("Handling CV port %s.%s\n", inst->name, data->port[p].name);
				rv |= mmjack_process_cv(inst, data->port + p, nframes, &mark);
				break;
			default:
				fprintf(stderr, "Unhandled jack port type in processing callback\n");
				pthread_mutex_unlock(&data->port[p].lock);
				return 1;
		}
		pthread_mutex_unlock(&data->port[p].lock);
	}

	//notify the main thread
	if(mark){
		DBGPF("Notifying handler thread for jack instance %s\n", inst->name);
		send(data->fd, "c", 1, 0);
	}
	return rv;
}

static void mmjack_server_shutdown(void* inst){
	fprintf(stderr, "jack server shutdown notification\n");
	config.jack_shutdown = 1;
}

static int mmjack_configure(char* option, char* value){
	if(!strcmp(option, "debug")){
		if(!strcmp(value, "on")){
			config.verbosity |= 2;
			return 0;
		}
		config.verbosity &= ~2;
		return 0;
	}
	if(!strcmp(option, "errors")){
		if(!strcmp(value, "on")){
			config.verbosity |= 1;
			return 0;
		}
		config.verbosity &= ~1;
		return 0;
	}

	fprintf(stderr, "Unknown jack backend option %s\n", option);
	return 1;
}

static int mmjack_parse_portconfig(mmjack_port* port, char* spec){
	char* token = NULL;

	for(token = strtok(spec, " "); token; token = strtok(NULL, " ")){
		if(!strcmp(token, "in")){
			port->input = 1;
		}
		else if(!strcmp(token, "out")){
			port->input = 0;
		}
		else if(!strcmp(token, "midi")){
			port->type = port_midi;
		}
		else if(!strcmp(token, "osc")){
			port->type = port_osc;
		}
		else if(!strcmp(token, "cv")){
			port->type = port_cv;
		}
		else if(!strcmp(token, "max")){
			token = strtok(NULL, " ");
			if(!token){
				fprintf(stderr, "jack port %s configuration missing argument\n", port->name);
				return 1;
			}
			port->max = strtod(token, NULL);
		}
		else if(!strcmp(token, "min")){
			token = strtok(NULL, " ");
			if(!token){
				fprintf(stderr, "jack port %s configuration missing argument\n", port->name);
				return 1;
			}
			port->min = strtod(token, NULL);
		}
		else{
			fprintf(stderr, "Unknown jack channel configuration token %s on port %s\n", token, port->name);
			return 1;
		}
	}

	if(port->type == port_none){
		fprintf(stderr, "jack channel %s assigned no port type\n", port->name);
		return 1;
	}
	return 0;
}

static int mmjack_configure_instance(instance* inst, char* option, char* value){
	mmjack_instance_data* data = (mmjack_instance_data*) inst->impl;
	size_t p;

	if(!strcmp(option, "name")){
		if(data->client_name){
			free(data->client_name);
		}
		data->client_name = strdup(value);
		return 0;
	}
	else if(!strcmp(option, "server")){
		if(data->server_name){
			free(data->server_name);
		}
		data->server_name = strdup(value);
		return 0;
	}

	//register new port, first check for unique name
	for(p = 0; p < data->ports; p++){
		if(!strcmp(data->port[p].name, option)){
			fprintf(stderr, "jack instance %s has duplicate port %s\n", inst->name, option);
			return 1;
		}
	}
	if(strchr(option, '.')){
		fprintf(stderr, "Invalid jack channel spec %s.%s\n", inst->name, option);
	}

	//add port to registry
	//TODO for OSC ports we need to configure subchannels for each message
	data->port = realloc(data->port, (data->ports + 1) * sizeof(mmjack_port));
	if(!data->port){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	data->port[data->ports].name = strdup(option);
	if(!data->port[data->ports].name){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	if(mmjack_parse_portconfig(data->port + p, value)){
		return 1;
	}
	data->ports++;
	return 0;
}

static instance* mmjack_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	inst->impl = calloc(1, sizeof(mmjack_instance_data));
	if(!inst->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return inst;
}

static int mmjack_parse_midispec(mmjack_channel_ident* ident, char* spec){
	char* next_token = NULL;

	if(!strncmp(spec, "ch", 2)){
		next_token = spec + 2;
		if(!strncmp(spec, "channel", 7)){
			next_token = spec + 7;
		}
	}

	if(!next_token){
		fprintf(stderr, "Invalid jack MIDI spec %s\n", spec);
		return 1;
	}

	ident->fields.sub_channel = strtoul(next_token, &next_token, 10);
	if(ident->fields.sub_channel > 15){
		fprintf(stderr, "Invalid jack MIDI spec %s, channel out of range\n", spec);
		return 1;
	}

	if(*next_token != '.'){
		fprintf(stderr, "Invalid jack MIDI spec %s\n", spec);
		return 1;
	}

	next_token++;

	if(!strncmp(next_token, "cc", 2)){
		ident->fields.sub_type = midi_cc;
		next_token += 2;
	}
	else if(!strncmp(next_token, "note", 4)){
		ident->fields.sub_type = midi_note;
		next_token += 4;
	}
	else if(!strncmp(next_token, "pressure", 8)){
		ident->fields.sub_type = midi_pressure;
		next_token += 8;
	}
	else if(!strncmp(next_token, "pitch", 5)){
		ident->fields.sub_type = midi_pitchbend;
	}
	else if(!strncmp(next_token, "aftertouch", 10)){
		ident->fields.sub_type = midi_aftertouch;
	}
	else{
		fprintf(stderr, "Unknown jack MIDI control type in spec %s\n", spec);
		return 1;
	}

	ident->fields.sub_control = strtoul(next_token, NULL, 10);

	if(ident->fields.sub_type == midi_none
			|| ident->fields.sub_control > 127){
		fprintf(stderr, "Invalid jack MIDI spec %s\n", spec);
		return 1;
	}
	return 0;
}

static channel* mmjack_channel(instance* inst, char* spec, uint8_t flags){
	mmjack_instance_data* data = (mmjack_instance_data*) inst->impl;
	mmjack_channel_ident ident = {
		.label = 0
	};
	size_t u;

	for(u = 0; u < data->ports; u++){
		if(!strncmp(spec, data->port[u].name, strlen(data->port[u].name))
				&& (spec[strlen(data->port[u].name)] == '.' || spec[strlen(data->port[u].name)] == 0)){
			ident.fields.port = u;
			break;
		}
	}

	if(u == data->ports){
		fprintf(stderr, "jack port %s.%s not found\n", inst->name, spec);
		return NULL;
	}

	if(data->port[u].type == port_midi){
		//parse midi subspec
		if(!spec[strlen(data->port[u].name)]
				|| mmjack_parse_midispec(&ident, spec + strlen(data->port[u].name) + 1)){
			return NULL;
		}
	}
	else if(data->port[u].type == port_osc){
		//TODO parse osc subspec
	}

	return mm_channel(inst, ident.label, 1);
}

static int mmjack_set(instance* inst, size_t num, channel** c, channel_value* v){
	mmjack_instance_data* data = (mmjack_instance_data*) inst->impl;
	mmjack_channel_ident ident = {
		.label = 0
	};
	size_t u;
	double range;
	uint16_t value;

	for(u = 0; u < num; u++){
		ident.label = c[u]->ident;

		if(data->port[ident.fields.port].input){
			fprintf(stderr, "jack port %s.%s is an input port, no output is possible\n", inst->name, data->port[ident.fields.port].name);
			continue;
		}
		range = data->port[ident.fields.port].max - data->port[ident.fields.port].min;

		pthread_mutex_lock(&data->port[ident.fields.port].lock);
		switch(data->port[ident.fields.port].type){
			case port_cv:
				//scale value to given range
				data->port[ident.fields.port].last = (range * v[u].normalised) + data->port[ident.fields.port].min;
				DBGPF("CV port %s updated to %f\n", data->port[ident.fields.port].name, data->port[ident.fields.port].last);
				break;
			case port_midi:
				value = v[u].normalised * 127.0;
				if(ident.fields.sub_type == midi_pitchbend){
					value = ((uint16_t)(v[u].normalised * 16384.0));
				}
				if(mmjack_midiqueue_append(data->port + ident.fields.port, ident, value)){
					pthread_mutex_unlock(&data->port[ident.fields.port].lock);
					return 1;
				}
				break;
			default:
				fprintf(stderr, "No handler implemented for jack port type %s.%s\n", inst->name, data->port[ident.fields.port].name);
				break;
		}
		pthread_mutex_unlock(&data->port[ident.fields.port].lock);
	}

	return 0;
}

static void mmjack_handle_midi(instance* inst, size_t index, mmjack_port* port){
	size_t u;
	channel* chan = NULL;
	channel_value val;

	for(u = 0; u < port->queue_len; u++){
		port->queue[u].ident.fields.port = index;
		chan = mm_channel(inst, port->queue[u].ident.label, 0);
		if(chan){
			if(port->queue[u].ident.fields.sub_type == midi_pitchbend){
				val.normalised = ((double)port->queue[u].raw) / 16384.0;
			}
			else{
				val.normalised = ((double)port->queue[u].raw) / 127.0;
			}
			DBGPF("Pushing MIDI channel %d type %02X control %d value %f raw %d label %" PRIu64 "\n",
					port->queue[u].ident.fields.sub_channel,
					port->queue[u].ident.fields.sub_type,
					port->queue[u].ident.fields.sub_control,
					val.normalised,
					port->queue[u].raw,
					port->queue[u].ident.label);
			if(mm_channel_event(chan, val)){
				fprintf(stderr, "Failed to push MIDI event to core on jack port %s.%s\n", inst->name, port->name);
			}
		}
	}

	if(port->queue_len){
		DBGPF("Pushed %" PRIsize_t " MIDI events to core for jack port %s.%s\n", port->queue_len, inst->name, port->name);
	}
	port->queue_len = 0;
}

static void mmjack_handle_cv(instance* inst, size_t index, mmjack_port* port){
	mmjack_channel_ident ident = {
		.fields.port = index
	};
	double range;
	channel_value val;

	channel* chan = mm_channel(inst, ident.label, 0);
	if(!chan){
		//this might happen if a channel is registered but not mapped
		DBGPF("Failed to match jack CV channel %s.%s to core channel\n", inst->name, port->name);
		return;
	}

	//normalize value
	range = port->max - port->min;
	val.normalised = port->last - port->min;
	val.normalised /= range;
	val.normalised = clamp(val.normalised, 1.0, 0.0);
	DBGPF("Pushing CV channel %s value %f raw %f min %f max %f\n", port->name, val.normalised, port->last, port->min, port->max);
	if(mm_channel_event(chan, val)){
		fprintf(stderr, "Failed to push CV event to core for %s.%s\n", inst->name, port->name);
	}
}

static int mmjack_handle(size_t num, managed_fd* fds){
	size_t u, p;
	instance* inst = NULL;
	mmjack_instance_data* data = NULL;
	ssize_t bytes;
	uint8_t recv_buf[1024];

	if(num){
		for(u = 0; u < num; u++){
			inst = (instance*) fds[u].impl;
			data = (mmjack_instance_data*) inst->impl;
			bytes = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
			if(bytes < 0){
				fprintf(stderr, "Failed to receive on feedback socket for instance %s\n", inst->name);
				return 1;
			}

			for(p = 0; p < data->ports; p++){
				if(data->port[p].input && data->port[p].mark){
					pthread_mutex_lock(&data->port[p].lock);
					switch(data->port[p].type){
						case port_cv:
							mmjack_handle_cv(inst, p, data->port + p);
							break;
						case port_midi:
							mmjack_handle_midi(inst, p, data->port + p);
							break;
						default:
							fprintf(stderr, "Output handler not implemented for unknown jack channel type on %s.%s\n", inst->name, data->port[p].name);
							break;
					}

					data->port[p].mark = 0;
					pthread_mutex_unlock(&data->port[p].lock);
				}
			}
		}
	}
	
	if(config.jack_shutdown){
		fprintf(stderr, "JACK server disconnected\n");
		return 1;
	}
	return 0;
}

static int mmjack_start(size_t n, instance** inst){
	int rv = 1, feedback_fd[2];
	size_t u, p;
	pthread_mutexattr_t mutex_attr;
	mmjack_instance_data* data = NULL;
	jack_status_t error;

	//set jack logging functions
	jack_set_error_function(mmjack_message_ignore);
	if(config.verbosity & 1){
		jack_set_error_function(mmjack_message_print);
	}
	jack_set_info_function(mmjack_message_ignore);
	if(config.verbosity & 2){
		jack_set_info_function(mmjack_message_print);
	}

	//prepare mutex attributes because the initializer macro for adaptive mutexes is a GNU extension...
	if(pthread_mutexattr_init(&mutex_attr)
			|| pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ADAPTIVE_NP)){
		fprintf(stderr, "Failed to initialize mutex attributes\n");
		goto bail;
	}

	for(u = 0; u < n; u++){
		data = (mmjack_instance_data*) inst[u]->impl;

		//connect to the jack server
		data->client = jack_client_open(data->client_name ? data->client_name : JACK_DEFAULT_CLIENT_NAME,
				JackServerName | JackNoStartServer,
				&error,
				data->server_name ? data->server_name : JACK_DEFAULT_SERVER_NAME);

		if(!data->client){
			//TODO pretty-print failures
			fprintf(stderr, "jack backend failed to connect to server, return status %u\n", error);
			goto bail;
		}

		//set up the feedback fd
		if(socketpair(AF_LOCAL, SOCK_DGRAM, 0, feedback_fd)){
			fprintf(stderr, "Failed to create feedback socket pair\n");
			goto bail;
		}

		data->fd = feedback_fd[0];
		if(mm_manage_fd(feedback_fd[1], BACKEND_NAME, 1, inst[u])){
			fprintf(stderr, "jack backend failed to register feedback fd with core\n");
			goto bail;
		}

		//connect jack callbacks
		jack_set_process_callback(data->client, mmjack_process, inst[u]);
		jack_on_shutdown(data->client, mmjack_server_shutdown, inst[u]);

		fprintf(stderr, "jack instance %s assigned client name %s\n", inst[u]->name, jack_get_client_name(data->client));

		//create and initialize jack ports
		for(p = 0; p < data->ports; p++){
			if(pthread_mutex_init(&(data->port[p].lock), &mutex_attr)){
				fprintf(stderr, "Failed to create port mutex\n");
				goto bail;
			}

			data->port[p].port = jack_port_register(data->client,
					data->port[p].name,
					(data->port[p].type == port_cv) ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
					data->port[p].input ? JackPortIsInput : JackPortIsOutput,
					0);

			jack_set_property(data->client, jack_port_uuid(data->port[p].port), JACKEY_SIGNAL_TYPE, "CV", "text/plain");

			if(!data->port[p].port){
				fprintf(stderr, "Failed to create jack port %s.%s\n", inst[u]->name, data->port[p].name);
				goto bail;
			}
		}

		//do the thing
		if(jack_activate(data->client)){
			fprintf(stderr, "Failed to activate jack client for instance %s\n", inst[u]->name);
			goto bail;
		}
	}

	fprintf(stderr, "jack backend registered %" PRIsize_t " descriptors to core\n", n);
	rv = 0;
bail:
	pthread_mutexattr_destroy(&mutex_attr);
	return rv;
}

static int mmjack_shutdown(size_t n, instance** inst){
	size_t u, p;
	mmjack_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (mmjack_instance_data*) inst[u]->impl;

		//deactivate client to stop processing before free'ing channel data
		if(data->client){
			jack_deactivate(data->client);
		}

		//iterate and close ports
		for(p = 0; p < data->ports; p++){
			jack_remove_property(data->client, jack_port_uuid(data->port[p].port), JACKEY_SIGNAL_TYPE);
			if(data->port[p].port){
				jack_port_unregister(data->client, data->port[p].port);
			}
			free(data->port[p].name);
			data->port[p].name = NULL;

			free(data->port[p].queue);
			data->port[p].queue = NULL;
			data->port[p].queue_alloc = data->port[p].queue_len = 0;

			pthread_mutex_destroy(&data->port[p].lock);
		}

		//terminate jack connection
		if(data->client){
			jack_client_close(data->client);
		}

		//clean up instance data
		free(data->server_name);
		data->server_name = NULL;
		free(data->client_name);
		data->client_name = NULL;
		close(data->fd);
		data->fd = -1;

		free(inst[u]->impl);
	}

	fprintf(stderr, "jack backend shut down\n");
	return 0;
}
