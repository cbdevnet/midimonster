#include <string.h>

#include "libmmbackend.h"
#include <mmsystem.h>

#include "winmidi.h"

#define BACKEND_NAME "winmidi"

static struct {
	uint8_t list_devices;
	int socket_pair[2];

	CRITICAL_SECTION push_events;
	volatile size_t events_alloc;
	volatile size_t events_active;
	volatile winmidi_event* event;
} backend_config = {
	.list_devices = 0,
	.socket_pair = {-1, -1}
};

//TODO detect option
//TODO receive feedback socket until EAGAIN

int init(){
	backend winmidi = {
		.name = BACKEND_NAME,
		.conf = winmidi_configure,
		.create = winmidi_instance,
		.conf_instance = winmidi_configure_instance,
		.channel = winmidi_channel,
		.handle = winmidi_set,
		.process = winmidi_handle,
		.start = winmidi_start,
		.shutdown = winmidi_shutdown
	};

	if(sizeof(winmidi_channel_ident) != sizeof(uint64_t)){
		fprintf(stderr, "winmidi channel identification union out of bounds\n");
		return 1;
	}

	//register backend
	if(mm_backend_register(winmidi)){
		fprintf(stderr, "Failed to register winmidi backend\n");
		return 1;
	}

	//initialize critical section
	InitializeCriticalSectionAndSpinCount(&backend_config.push_events, 4000);
	return 0;
}

static int winmidi_configure(char* option, char* value){
	if(!strcmp(option, "list")){
		backend_config.list_devices = 0;
		if(!strcmp(value, "on")){
			backend_config.list_devices = 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown winmidi backend option %s\n", option);
	return 1;
}

static int winmidi_configure_instance(instance* inst, char* option, char* value){
	winmidi_instance_data* data = (winmidi_instance_data*) inst->impl;
	if(!strcmp(option, "read")){
		if(data->read){
			fprintf(stderr, "winmidi instance %s already connected to an input device\n", inst->name);
			return 1;
		}
		data->read = strdup(value);
		return 0;
	}
	if(!strcmp(option, "write")){
		if(data->write){
			fprintf(stderr, "winmidi instance %s already connected to an otput device\n", inst->name);
			return 1;
		}
		data->write = strdup(value);
		return 0;
	}

	fprintf(stderr, "Unknown winmidi instance option %s\n", option);
	return 1;
}

static instance* winmidi_instance(){
	instance* i = mm_instance();
	if(!i){
		return NULL;
	}

	i->impl = calloc(1, sizeof(winmidi_instance_data));
	if(!i->impl){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return i;
}

static channel* winmidi_channel(instance* inst, char* spec){
	char* next_token = NULL;
	winmidi_channel_ident ident = {
		.label = 0
	};

	if(!strncmp(spec, "ch", 2)){
		next_token = spec + 2;
		if(!strncmp(spec, "channel", 7)){
			next_token = spec + 7;
		}
	}
	else{
		fprintf(stderr, "Unknown winmidi channel specification %s\n", spec);
		return NULL;
	}

	ident.fields.channel = strtoul(next_token, &next_token, 10);
	if(ident.fields.channel > 15){
		fprintf(stderr, "MIDI channel out of range in winmidi channel spec %s\n", spec);
		return NULL;
	}

	if(*next_token != '.'){
		fprintf(stderr, "winmidi channel specification %s does not conform to channel<X>.<control><Y>\n", spec);
		return NULL;
	}

	next_token++;

	if(!strncmp(next_token, "cc", 2)){
		ident.fields.type = cc;
		next_token += 2;
	}
	else if(!strncmp(next_token, "note", 4)){
		ident.fields.type = note;
		next_token += 4;
	}
	else if(!strncmp(next_token, "pressure", 8)){
		ident.fields.type = pressure;
		next_token += 8;
	}
	else if(!strncmp(next_token, "pitch", 5)){
		ident.fields.type = pitchbend;
	}
	else if(!strncmp(next_token, "aftertouch", 10)){
		ident.fields.type = aftertouch;
	}
	else{
		fprintf(stderr, "Unknown winmidi channel control type in %s\n", spec);
		return NULL;
	}

	ident.fields.control = strtoul(next_token, NULL, 10);

	if(ident.label){
		return mm_channel(inst, ident.label, 1);
	}
	return NULL;
}

static int winmidi_set(instance* inst, size_t num, channel** c, channel_value* v){
	winmidi_instance_data* data = (winmidi_instance_data*) inst->impl;
	winmidi_channel_ident ident = {
		.label = 0
	};
	union {
		struct {
			uint8_t status;
			uint8_t data1;
			uint8_t data2;
			uint8_t unused;
		} components;
		DWORD dword;
	} output = {
		.dword = 0
	};
	size_t u;

	//early exit
	if(!num){
		return 0;
	}

	if(!data->device_out){
		fprintf(stderr, "winmidi instance %s has no output device\n", inst->name);
		return 0;
	}

	for(u = 0; u < num; u++){
		ident.label = c[u]->ident;

		switch(ident.fields.type){
			case note:
				output.components.status = 0x90 | ident.fields.channel;
				output.components.data1 = ident.fields.control;
				output.components.data2 = v[u].normalised * 127.0;
				break;
			case cc:
				output.components.status = 0xB0 | ident.fields.channel;
				output.components.data1 = ident.fields.control;
				output.components.data2 = v[u].normalised * 127.0;
				break;
			case pressure:
				output.components.status = 0xA0 | ident.fields.channel;
				output.components.data1 = ident.fields.control;
				output.components.data2 = v[u].normalised * 127.0;
				break;
			case aftertouch:
				output.components.status = 0xD0 | ident.fields.channel;
				output.components.data1 = v[u].normalised * 127.0;
				output.components.data2 = 0;
				break;
			case pitchbend:
				output.components.status = 0xE0 | ident.fields.channel;
				output.components.data1 = ((int)(v[u].normalised * 32639.0)) & 0xFF;
				output.components.data2 = (((int)(v[u].normalised * 32639.0)) & 0xFF00) >> 8;
				break;
			default:
				fprintf(stderr, "Unknown winmidi channel type %d\n", ident.fields.type);
				continue;
		}

		midiOutShortMsg(data->device_out, output.dword);
	}
	
	return 0;
}

static int winmidi_handle(size_t num, managed_fd* fds){
	size_t u;
	ssize_t bytes = 0;
	char recv_buf[1024];
	channel* chan = NULL;
	if(!num){
		return 0;
	}

	//flush the feedback socket
	for(u = 0; u < num; u++){
		bytes += recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
	}

	//push queued events
	EnterCriticalSection(&backend_config.push_events);
	for(u = 0; u < backend_config.events_active; u++){
		chan = mm_channel(backend_config.event[u].inst, backend_config.event[u].channel.label, 0);
		if(chan){
			mm_channel_event(chan, backend_config.event[u].value);
		}
	}
	DBGPF("winmidi flushed %" PRIsize_t " wakeups, handled %" PRIsize_t " events\n", bytes, backend_config.events_active);
	backend_config.events_active = 0;
	LeaveCriticalSection(&backend_config.push_events);
	return 0;
}

static void CALLBACK winmidi_input_callback(HMIDIIN device, unsigned message, DWORD_PTR inst, DWORD param1, DWORD param2){
	winmidi_channel_ident ident = {
		.label = 0
	};
	channel_value val;
	union {
		struct {
			uint8_t status;
			uint8_t data1;
			uint8_t data2;
			uint8_t unused;
		} components;
		DWORD dword;
	} input = {
		.dword = 0
	};

	//callbacks may run on different threads, so we queue all events and alert the main thread via the feedback socket
	DBGPF("winmidi input callback on thread %ld\n", GetCurrentThreadId());

	switch(message){
		case MIM_MOREDATA:
			//processing too slow, do not immediately alert the main loop
		case MIM_DATA:
			//param1 has the message
			input.dword = param1;
			ident.fields.channel = input.components.status & 0x0F;
			switch(input.components.status & 0xF0){
				case 0x80:
					ident.fields.type = note;
					ident.fields.control = input.components.data1;
					val.normalised = 0.0;
					break;
				case 0x90:
					ident.fields.type = note;
					ident.fields.control = input.components.data1;
					val.normalised = (double) input.components.data2 / 127.0;
					break;
				case 0xA0:
					ident.fields.type = pressure;
					ident.fields.control = input.components.data1;
					val.normalised = (double) input.components.data2 / 127.0;
					break;
				case 0xB0:
					ident.fields.type = cc;
					ident.fields.control = input.components.data1;
					val.normalised = (double) input.components.data2 / 127.0;
					break;
				case 0xD0:
					ident.fields.type = aftertouch;
					ident.fields.control = 0;
					val.normalised = (double) input.components.data1 / 127.0;
					break;
				case 0xE0:
					ident.fields.type = pitchbend;
					ident.fields.control = 0;
					val.normalised = (double)((input.components.data2 << 8) | input.components.data1) / 32639.0;
					break;
				default:
					fprintf(stderr, "winmidi unhandled status byte %02X\n", input.components.status);
					return;
			}
			break;
		case MIM_LONGDATA:
			//sysex message, ignore
			return;
		case MIM_ERROR:
			//error in input stream
			fprintf(stderr, "winmidi warning: error in input stream\n");
			return;
		case MIM_OPEN:
		case MIM_CLOSE:
			//device opened/closed
			return;
		
	}

	DBGPF("winmidi incoming message type %d channel %d control %d value %f\n",
			ident.fields.type, ident.fields.channel, ident.fields.control, val.normalised);

	EnterCriticalSection(&backend_config.push_events);
	if(backend_config.events_alloc <= backend_config.events_active){
		backend_config.event = realloc((void*) backend_config.event, (backend_config.events_alloc + 1) * sizeof(winmidi_event));
		if(!backend_config.event){
			fprintf(stderr, "Failed to allocate memory\n");
			backend_config.events_alloc = 0;
			backend_config.events_active = 0;
			LeaveCriticalSection(&backend_config.push_events);
			return;
		}
		backend_config.events_alloc++;
	}
	backend_config.event[backend_config.events_active].inst = (instance*) inst;
	backend_config.event[backend_config.events_active].channel.label = ident.label;
	backend_config.event[backend_config.events_active].value = val;
	backend_config.events_active++;
	LeaveCriticalSection(&backend_config.push_events);

	if(message != MIM_MOREDATA){
		//alert the main loop
		send(backend_config.socket_pair[1], "w", 1, 0);
	}
}

static void CALLBACK winmidi_output_callback(HMIDIOUT device, unsigned message, DWORD_PTR inst, DWORD param1, DWORD param2){
	DBGPF("winmidi output callback on thread %ld\n", GetCurrentThreadId());
}

static int winmidi_match_input(char* prefix){
	MIDIINCAPS input_caps;
	unsigned inputs = midiInGetNumDevs();
	char* next_token = NULL;
	size_t n;

	if(!prefix){
		fprintf(stderr, "winmidi detected %u input devices\n", inputs);
	}
	else{
		n = strtoul(prefix, &next_token, 10);
		if(!(*next_token) && n < inputs){
			midiInGetDevCaps(n, &input_caps, sizeof(MIDIINCAPS));
			fprintf(stderr, "winmidi selected input device %s for ID %d\n", input_caps.szPname, n);
			return n;
		}
	}

	//find prefix match for input device
	for(n = 0; n < inputs; n++){
		midiInGetDevCaps(n, &input_caps, sizeof(MIDIINCAPS));
		if(!prefix){
			printf("\tID %d: %s\n", n, input_caps.szPname);
		}
		else if(!strncmp(input_caps.szPname, prefix, strlen(prefix))){
			fprintf(stderr, "winmidi selected input device %s for name %s\n", input_caps.szPname, prefix);
			return n;
		}
	}

	return -1;
}

static int winmidi_match_output(char* prefix){
	MIDIOUTCAPS output_caps;
	unsigned outputs = midiOutGetNumDevs();
	char* next_token = NULL;
	size_t n;

	if(!prefix){
		fprintf(stderr, "winmidi detected %u output devices\n", outputs);
	}
	else{
		n = strtoul(prefix, &next_token, 10);
		if(!(*next_token) && n < outputs){
			midiOutGetDevCaps(n, &output_caps, sizeof(MIDIOUTCAPS));
			fprintf(stderr, "winmidi selected output device %s for ID %d\n", output_caps.szPname, n);
			return n;
		}
	}

	//find prefix match for output device
	for(n = 0; n < outputs; n++){
		midiOutGetDevCaps(n, &output_caps, sizeof(MIDIOUTCAPS));
		if(!prefix){
			printf("\tID %d: %s\n", n, output_caps.szPname);
		}
		else if(!strncmp(output_caps.szPname, prefix, strlen(prefix))){
			fprintf(stderr, "winmidi selected output device %s for name %s\n", output_caps.szPname, prefix);
			return n;
		}
	}

	return -1;
}

static int winmidi_start(){
	size_t n = 0, p;
	int device, rv = -1;
	instance** inst = NULL;
	winmidi_instance_data* data = NULL;
	struct sockaddr_storage sockadd = {
		0
	};
	//this really should be a size_t but getsockname specifies int* for some reason
	int sockadd_len = sizeof(sockadd);
	char* error = NULL;
	DBGPF("winmidi main thread ID is %ld\n", GetCurrentThreadId());

	//fetch all instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	//no instances, we're done
	if(!n){
		free(inst);
		return 0;
	}

	//output device list if requested
	if(backend_config.list_devices){
		winmidi_match_input(NULL);
		winmidi_match_output(NULL);
	}

	//open the feedback sockets
	//for some reason the feedback connection fails to work on 'real' windows with ipv6
	backend_config.socket_pair[0] = mmbackend_socket("127.0.0.1", "0", SOCK_DGRAM, 1, 0);
	if(backend_config.socket_pair[0] < 0){
		fprintf(stderr, "winmidi failed to open feedback socket\n");
		return 1;
	}
	if(getsockname(backend_config.socket_pair[0], (struct sockaddr*) &sockadd, &sockadd_len)){
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error, 0, NULL);
		fprintf(stderr, "winmidi failed to query feedback socket information: %s\n", error);
		LocalFree(error);
		return 1;
	}
	//getsockname on 'real' windows may not set the address - works on wine, though
	switch(sockadd.ss_family){
		case AF_INET:
		case AF_INET6:
			((struct sockaddr_in*) &sockadd)->sin_family = AF_INET;
			((struct sockaddr_in*) &sockadd)->sin_addr.s_addr = htobe32(INADDR_LOOPBACK);
			break;
		//for some absurd reason 'real' windows announces the socket as AF_INET6 but rejects any connection unless its AF_INET
//		case AF_INET6:
//			((struct sockaddr_in6*) &sockadd)->sin6_addr = in6addr_any;
//			break;
		default:
			fprintf(stderr, "winmidi invalid feedback socket family\n");
			return 1;
	}
	DBGPF("winmidi feedback socket family %d port %d\n", sockadd.ss_family, be16toh(((struct sockaddr_in*)&sockadd)->sin_port));
	backend_config.socket_pair[1] = socket(sockadd.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if(backend_config.socket_pair[1] < 0 || connect(backend_config.socket_pair[1], (struct sockaddr*) &sockadd, sockadd_len)){
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error, 0, NULL);
		fprintf(stderr, "winmidi failed to connect to feedback socket: %s\n", error);
		LocalFree(error);
		return 1;
	}

	//set up instances and start input
	for(p = 0; p < n; p++){
		data = (winmidi_instance_data*) inst[p]->impl;
		inst[p]->ident = p;

		//connect input device if requested
		if(data->read){
			device = winmidi_match_input(data->read);
			if(device < 0){
				fprintf(stderr, "Failed to match input device %s for instance %s\n", data->read, inst[p]->name);
				goto bail;
			}
			if(midiInOpen(&(data->device_in), device, (DWORD_PTR) winmidi_input_callback, (DWORD_PTR) inst[p], CALLBACK_FUNCTION | MIDI_IO_STATUS) != MMSYSERR_NOERROR){
				fprintf(stderr, "Failed to open input device for instance %s\n", inst[p]->name);
				goto bail;
			}
			//start midi input callbacks
			midiInStart(data->device_in);
		}

		//connect output device if requested
		if(data->write){
			device = winmidi_match_output(data->write);
			if(device < 0){
				fprintf(stderr, "Failed to match output device %s for instance %s\n", data->read, inst[p]->name);
				goto bail;
			}
			if(midiOutOpen(&(data->device_out), device, (DWORD_PTR) winmidi_output_callback, (DWORD_PTR) inst[p], CALLBACK_FUNCTION) != MMSYSERR_NOERROR){
				fprintf(stderr, "Failed to open output device for instance %s\n", inst[p]->name);
				goto bail;
			}
		}
	}

	//register the feedback socket to the core
	fprintf(stderr, "winmidi backend registering 1 descriptor to core\n");
	if(mm_manage_fd(backend_config.socket_pair[0], BACKEND_NAME, 1, NULL)){
		goto bail;
	}

	rv = 0;
bail:
	free(inst);
	return rv;
}

static int winmidi_shutdown(){
	size_t n, u;
	instance** inst = NULL;
	winmidi_instance_data* data = NULL;

	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (winmidi_instance_data*) inst[u]->impl;
		free(data->read);
		data->read = NULL;
		free(data->write);
		data->write = NULL;

		if(data->device_in){
			midiInStop(data->device_in);
			midiInClose(data->device_in);
			data->device_in = NULL;
		}

		if(data->device_out){
			midiOutReset(data->device_out);
			midiOutClose(data->device_out);
			data->device_out = NULL;
		}
	}

	free(inst);
	closesocket(backend_config.socket_pair[0]);
	closesocket(backend_config.socket_pair[1]);

	EnterCriticalSection(&backend_config.push_events);
	free((void*) backend_config.event);
	backend_config.event = NULL;
	backend_config.events_alloc = 0;
	backend_config.events_active = 0;
	LeaveCriticalSection(&backend_config.push_events);
	DeleteCriticalSection(&backend_config.push_events);

	fprintf(stderr, "winmidi backend shut down\n");
	return 0;
}
