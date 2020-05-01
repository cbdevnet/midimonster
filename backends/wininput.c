#define BACKEND_NAME "wininput"
#define DEBUG

#include <string.h>
#include "wininput.h"

MM_PLUGIN_API int init(){
	backend wininput = {
		.name = BACKEND_NAME,
		.conf = wininput_configure,
		.create = wininput_instance,
		.conf_instance = wininput_configure_instance,
		.channel = wininput_channel,
		.handle = wininput_set,
		.process = wininput_handle,
		.start = wininput_start,
		.shutdown = wininput_shutdown
	};

	if(sizeof(wininput_channel_ident) != sizeof(uint64_t)){
		LOG("Channel identification union out of bounds");
		return 1;
	}

	//register backend
	if(mm_backend_register(wininput)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int wininput_configure(char* option, char* value){
	LOG("The backend does not take any global configuration");
	return 1;
}

static int wininput_configure_instance(instance* inst, char* option, char* value){
	LOG("The backend does not take any instance configuration");
	return 0;
}

static int wininput_instance(instance* inst){
	wininput_instance_data* data = calloc(1, sizeof(wininput_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	inst->impl = data;
	return 0;
}

static channel* wininput_channel(instance* inst, char* spec, uint8_t flags){
	char* token = spec;
	wininput_channel_ident ident = {
		.label = 0
	};

	if(!strncmp(spec, "mouse.", 6)){
		//TODO wheel
		token += 6;
		ident.fields.type = mouse;
		if(!strcmp(token, "x")){
			ident.fields.channel = position;
		}
		else if(!strcmp(token, "y")){
			ident.fields.channel = position;
			ident.fields.control = 1;
		}
		else if(!strcmp(token, "lmb")){
			ident.fields.channel = button;
		}
		else if(!strcmp(token, "rmb")){
			ident.fields.channel = button;
			ident.fields.control = 1;
		}
		else if(!strcmp(token, "mmb")){
			ident.fields.channel = button;
			ident.fields.control = 2;
		}
		else if(!strcmp(token, "xmb1")){
			ident.fields.channel = button;
			ident.fields.control = 3;
		}
		else if(!strcmp(token, "xmb2")){
			ident.fields.channel = button;
			ident.fields.control = 4;
		}
		else{
			LOGPF("Unknown control %s", token);
			return NULL;
		}
	}
	else if(!strncmp(spec, "keyboard.", 9)){
		token += 9;
		//TODO
	}
	else{
		LOGPF("Unknown channel spec %s", spec);
	}

	if(ident.label){
		return mm_channel(inst, ident.label, 1);
	}
	return NULL;
}

static INPUT wininput_event_mouse(wininput_instance_data* data, uint8_t channel, uint8_t control, double value){
	DWORD flags_down = 0, flags_up = 0;
	INPUT ev = {
		.type = INPUT_MOUSE
	};

	if(channel == position){
		if(control){
			data->mouse.y = value * 0xFFFF;
		}
		else{
			data->mouse.x = value * 0xFFFF;
		}

		ev.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
		ev.mi.dx = data->mouse.x;
		ev.mi.dy = data->mouse.y;
	}
	if(channel == button){
		switch(control){
			case 0:
				flags_up |= MOUSEEVENTF_LEFTUP;
				flags_down |= MOUSEEVENTF_LEFTDOWN;
				break;
			case 1:
				flags_up |= MOUSEEVENTF_RIGHTUP;
				flags_down |= MOUSEEVENTF_RIGHTDOWN;
				break;
			case 2:
				flags_up |= MOUSEEVENTF_MIDDLEUP;
				flags_down |= MOUSEEVENTF_MIDDLEDOWN;
				break;
			case 3:
			case 4:
				ev.mi.mouseData = (control == 3) ? XBUTTON1 : XBUTTON2;
				flags_up |= MOUSEEVENTF_XUP;
				flags_down |= MOUSEEVENTF_XDOWN;
				break;
		}

		if(value > 0.9){
			ev.mi.dwFlags |= flags_down;
		}
		else{
			ev.mi.dwFlags |= flags_up;
		}
	}

	return ev;
}

static INPUT wininput_event_keyboard(wininput_instance_data* data, uint8_t channel, uint8_t control, double value){
	INPUT ev = {
		.type = INPUT_KEYBOARD
	};

	return ev;
}

static int wininput_set(instance* inst, size_t num, channel** c, channel_value* v){
	wininput_channel_ident ident = {
		.label = 0
	};
	wininput_instance_data* data = (wininput_instance_data*) inst->impl;
	size_t n = 0, offset = 0;
	INPUT events[500];

	//FIXME might want to coalesce mouse events
	if(num > sizeof(events) / sizeof(events[0])){
		LOGPF("Truncating output on %s to the last %" PRIsize_t " events, please notify the developers", inst->name, sizeof(events) / sizeof(events[0]));
		offset = num - sizeof(events) / sizeof(events[0]);
	}

	for(n = 0; n + offset < num; n++){
		ident.label = c[n + offset]->ident;
		if(ident.fields.type == mouse){
			events[n] = wininput_event_mouse(data, ident.fields.channel, ident.fields.control, v[n + offset].normalised);
		}
		else if(ident.fields.type == keyboard){
			events[n] = wininput_event_keyboard(data, ident.fields.channel, ident.fields.control, v[n + offset].normalised);
		}
		else{
			n--;
			offset++;
		}
	}

	if(n){
		offset = SendInput(n, events, sizeof(INPUT));
		if(offset != n){
			LOGPF("Output %" PRIsize_t " of %" PRIsize_t " events on %s", offset, n, inst->name);
		}
	}
	return 0;
}

static int wininput_handle(size_t num, managed_fd* fds){
	//TODO
	return 0;
}

static int wininput_start(size_t n, instance** inst){
	//TODO
	return 0;
}

static int wininput_shutdown(size_t n, instance** inst){
	size_t u;

	for(u = 0; u < n; u++){
		free(inst[u]->impl);
	}

	LOG("Backend shut down");
	return 0;
}
