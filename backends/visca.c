#define BACKEND_NAME "visca"
#define DEBUG

#include <string.h>
#include <math.h>

#ifndef _WIN32
	#include <sys/ioctl.h>
	#include <asm/termbits.h>
#endif

#include "visca.h"
#include "libmmbackend.h"

/* TODO
 *	VISCA server
 *	Command output rate limiting / deduplication
 *	Inquiry
 *	Reconnect on connection close
 */

MM_PLUGIN_API int init(){
	backend ptz = {
		.name = BACKEND_NAME,
		.conf = ptz_configure,
		.create = ptz_instance,
		.conf_instance = ptz_configure_instance,
		.channel = ptz_channel,
		.handle = ptz_set,
		.process = ptz_handle,
		.start = ptz_start,
		.shutdown = ptz_shutdown
	};

	//register backend
	if(mm_backend_register(ptz)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static int ptz_configure(char* option, char* value){
	LOG("No backend configuration possible");
	return 1;
}

static int ptz_configure_instance(instance* inst, char* option, char* value){
	char* host = NULL, *port = NULL, *options = NULL;
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	uint8_t mode = 0;

	if(!strcmp(option, "id")){
		data->cam_address = strtoul(value, NULL, 10);
		return 0;
	}
	else if(!strcmp(option, "connect")){
		if(data->fd >= 0){
			LOGPF("Instance %s already connected", inst->name);
			return 1;
		}

		mmbackend_parse_hostspec(value, &host, &port, &options);
		if(!host || !port){
			LOGPF("Invalid destination address specified for instance %s", inst->name);
			return 1;
		}

		if(options && !strcmp(options, "udp")){
			mode = 1;
		}

		data->fd = mmbackend_socket(host, port, mode ? SOCK_DGRAM : SOCK_STREAM, 0, 0, 1);
		if(data->fd < 0){
			LOGPF("Failed to connect instance %s", inst->name);
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "device")){
		if(data->fd >= 0){
			LOGPF("Instance %s already connected", inst->name);
			return 1;
		}

		#ifdef _WIN32
		LOG("Direct device connections are not possible on Windows");
		return 1;
		#else

		struct termios2 device_config;

		options = strchr(value, ' ');
		if(options){
			//terminate port name
			*options = 0;
			options++;
		}

		data->fd = open(value, O_RDWR | O_NONBLOCK);
		if(data->fd < 0){
			LOGPF("Failed to connect instance %s to device %s", inst->name, value);
			return 1;
		}
		data->direct_device = 1;

		//configure baudrate
		if(options){
			//get current port config
			if(ioctl(data->fd, TCGETS2, &device_config)){
				LOGPF("Failed to get port configuration data for %s: %s", value, strerror(errno));
				return 0;
			}

			device_config.c_cflag &= ~CBAUD;
			device_config.c_cflag |= BOTHER;
			device_config.c_ispeed = strtoul(options, NULL, 10);
			device_config.c_ospeed = strtoul(options, NULL, 10);

			//set updated config
			if(ioctl(data->fd, TCSETS2, &device_config)){
				LOGPF("Failed to set port configuration data for %s: %s", value, strerror(errno));
			}
		}
		return 0;
		#endif
	}
	else if(!strcmp(option, "deadzone")){
		data->deadzone = strtod(value, NULL);
		return 0;
	}

	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static int ptz_instance(instance* inst){
	ptz_instance_data* data = calloc(1, sizeof(ptz_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	data->fd = -1;
	data->cam_address = 1;
	//start with maximum speeds
	data->panspeed = ptz_channels[panspeed].max;
	data->tiltspeed = ptz_channels[tiltspeed].max;
	//start with a bit of slack/deadzone in relative movement axes
	data->deadzone = 0.1;

	inst->impl = data;
	return 0;
}

static channel* ptz_channel(instance* inst, char* spec, uint8_t flags){
	uint64_t command = 0;

	if(flags & mmchannel_input){
		LOG("This backend currently only supports output channels");
		return NULL;
	}

	for(command = 0; command < sentinel; command++){
		if(!strncmp(spec, ptz_channels[command].name, strlen(ptz_channels[command].name))){
			break;
		}
	}

	DBGPF("Matched spec %s as %s", spec, ptz_channels[command].name ? ptz_channels[command].name : "sentinel");

	if(command == sentinel){
		LOGPF("Unknown channel spec %s", spec);
		return NULL;
	}

	//store the memory to be called above the command type
	if(command == call || command == store){
		command |= (strtoul(spec + strlen(ptz_channels[command].name), NULL, 10) << 8);
	}

	//store relative move direction
	else if(command == relmove){
		if(!strcmp(spec + strlen(ptz_channels[relmove].name), ".up")
				|| !strcmp(spec + strlen(ptz_channels[relmove].name), ".y")){
			command |= (rel_up << 8);
		}
		else if(!strcmp(spec + strlen(ptz_channels[relmove].name), ".left")
				|| !strcmp(spec + strlen(ptz_channels[relmove].name), ".x")){
			command |= (rel_left << 8);
		}

		if(!strcmp(spec + strlen(ptz_channels[relmove].name), ".down")
				|| !strcmp(spec + strlen(ptz_channels[relmove].name), ".y")){
			command |= (rel_down << 8);
		}
		else if(!strcmp(spec + strlen(ptz_channels[relmove].name), ".right")
				|| !strcmp(spec + strlen(ptz_channels[relmove].name), ".x")){
			command |= (rel_right << 8);
		}

		if(command >> 8 == 0){
			LOGPF("Could not parse relative movement command %s", spec);
			return NULL;
		}
	}

	return mm_channel(inst, command, 1);
}

static size_t ptz_set_pantilt(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;

	if(c->ident == pan){
		data->x = ((ptz_channels[pan].max - ptz_channels[pan].min) * v->normalised) + ptz_channels[pan].min - ptz_channels[pan].offset;
	}
	else{
		data->y = ((ptz_channels[tilt].max - ptz_channels[tilt].min) * v->normalised) + ptz_channels[tilt].min - ptz_channels[tilt].offset;
	}

	msg[4] = data->panspeed;
	msg[5] = data->tiltspeed;

	//either i'm doing this wrong or visca is just weird.
	msg[6] = ((data->x & 0xF000) >> 12);
	msg[7] = ((data->x & 0x0F00) >> 8);
	msg[8] = ((data->x & 0xF0) >> 4);
	msg[9] = (data->x & 0x0F);

	msg[10] = ((data->y & 0xF000) >> 12);
	msg[11] = ((data->y & 0x0F00) >> 8);
	msg[12] = ((data->y & 0xF0) >> 4);
	msg[13] = (data->y & 0x0F);

	return ptz_channels[pan].bytes;
}

static size_t ptz_set_ptspeed(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	if(c->ident == panspeed){
		data->panspeed = ((ptz_channels[panspeed].max - ptz_channels[panspeed].min) * v->normalised) + ptz_channels[panspeed].min - ptz_channels[panspeed].offset;
	}
	else{
		data->tiltspeed = ((ptz_channels[tiltspeed].max - ptz_channels[tiltspeed].min) * v->normalised) + ptz_channels[tiltspeed].min - ptz_channels[tiltspeed].offset;
	}

	return 0;
}

static size_t ptz_set_relmove(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;

	uint8_t direction = c->ident >> 8;
	double speed_factor = v->normalised;

	if(direction == rel_x
			|| direction == rel_y){
		//select only one move event
		direction &= (speed_factor > 0.5) ? (rel_up | rel_left) : (rel_down | rel_right);

		//scale event value to full axis
		speed_factor = fabs((speed_factor - 0.5) * 2);

		//clamp to deadzone
		speed_factor = (speed_factor < 2 * data->deadzone) ? 0 : speed_factor;
	}

	//clear modified axis
	if(direction & rel_x){
		data->relative_movement &= ~rel_x;
	}
	else{
		data->relative_movement &= ~rel_y;
	}

	if(speed_factor){
		data->relative_movement |= direction;
	}

	//set stored axis speed
	//TODO find a way to do relative axis speed via speed_factor, without overwriting a set absolute speed
	msg[4] = data->panspeed;
	msg[5] = data->tiltspeed;

	//update motor control from movement data
	msg[6] |= (data->relative_movement & (rel_left | rel_right)) >> 2;
	msg[7] |= data->relative_movement & (rel_up | rel_down);

	//stop motors if unset
	msg[6] = msg[6] ? msg[6] : 3;
	msg[7] = msg[7] ? msg[7] : 3;

	DBGPF("Moving axis %d with factor %f, total movement now %02X, commanding %d / %d, %d / %d",
			direction, speed_factor, data->relative_movement,
			msg[6], msg[4], msg[7], msg[5]);

	return ptz_channels[relmove].bytes;
}

static size_t ptz_set_zoom(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	uint16_t position = ((ptz_channels[zoom].max - ptz_channels[zoom].min) * v->normalised) + ptz_channels[zoom].min - ptz_channels[zoom].offset;
	msg[4] = ((position & 0xF000) >> 12);
	msg[5] = ((position & 0x0F00) >> 8);
	msg[6] = ((position & 0xF0) >> 4);
	msg[7] = (position & 0x0F);
	return ptz_channels[zoom].bytes;
}

static size_t ptz_set_focus(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	uint16_t position = ((ptz_channels[focus].max - ptz_channels[focus].min) * v->normalised) + ptz_channels[focus].min - ptz_channels[focus].offset;
	msg[4] = ((position & 0xF000) >> 12);
	msg[5] = ((position & 0x0F00) >> 8);
	msg[6] = ((position & 0xF0) >> 4);
	msg[7] = (position & 0x0F);
	return ptz_channels[focus].bytes;
}

static size_t ptz_set_focus_mode(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	msg[4] = (v->normalised > 0.9) ? 2 : 3;
	return ptz_channels[focus_mode].bytes;
}

static size_t ptz_set_wb_mode(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	msg[4] = (v->normalised > 0.9) ? 0 : 5;
	return ptz_channels[wb_mode].bytes;
}

static size_t ptz_set_wb(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	uint8_t command = c->ident & 0xFF;
	uint8_t value = ((ptz_channels[command].max - ptz_channels[command].min) * v->normalised) + ptz_channels[command].min - ptz_channels[command].offset;
	msg[6] = value >> 4;
	msg[7] = value & 0x0F;
	return ptz_channels[command].bytes;
}

static size_t ptz_set_memory(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	if(v->normalised < 0.9){
		return 0;
	}

	msg[5] = (c->ident >> 8);
	return ptz_channels[call].bytes;
}

static size_t ptz_set_memory_store(instance* inst, channel* c, channel_value* v, uint8_t* msg){
	if(v->normalised < 0.9){
		return 0;
	}

	msg[5] = (c->ident >> 8);
	return ptz_channels[store].bytes;
}

static int ptz_write_serial(int fd, uint8_t* data, size_t bytes){
	ssize_t total = 0, sent;

	while(total < bytes){
		sent = write(fd, data + total, bytes - total);
		if(sent < 0){
			LOGPF("Failed to write to serial port: %s", strerror(errno));
			return 1;
		}
		total += sent;
	}

	return 0;
}

static int ptz_set(instance* inst, size_t num, channel** c, channel_value* v){
	ptz_instance_data* data = (ptz_instance_data*) inst->impl;
	size_t n = 0, bytes = 0;
	uint8_t tx[VISCA_BUFFER_LENGTH] = "";
	uint8_t command = 0;

	for(n = 0; n < num; n++){
		bytes = 0;
		command = c[n]->ident & 0xFF;

		if(ptz_channels[command].bytes){
			memcpy(tx, ptz_channels[command].pattern, ptz_channels[command].bytes);
			//if no handler function set, assume a parameterless command and send verbatim
			bytes = ptz_channels[command].bytes;
		}
		tx[0] = 0x80 | (data->cam_address & 0xF);

		if(ptz_channels[command].set){
			bytes = ptz_channels[command].set(inst, c[n], v + n, tx);
		}

		if(data->direct_device && bytes && ptz_write_serial(data->fd, tx, bytes)){
			LOGPF("Failed to write %s command on instance %s", ptz_channels[command].name, inst->name);	
		}
		else if(!data->direct_device && bytes && mmbackend_send(data->fd, tx, bytes)){
			LOGPF("Failed to push %s command on instance %s", ptz_channels[command].name, inst->name);
		}
	}
	return 0;
}

static int ptz_handle(size_t num, managed_fd* fds){
	uint8_t recv_buf[VISCA_BUFFER_LENGTH];
	size_t u;
	ssize_t bytes_read;
	instance* inst = NULL;

	//read and ignore any responses for now
	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		bytes_read = recv(fds[u].fd, recv_buf, sizeof(recv_buf), 0);
		if(bytes_read <= 0){
			LOGPF("Failed to receive on signaled fd for instance %s", inst->name);
			//TODO handle failure
		}
		else{
			DBGPF("Ignored %" PRIsize_t " incoming bytes for instance %s", bytes_read, inst->name);
		}
	}

	return 0;
}

static int ptz_start(size_t n, instance** inst){
	size_t u, fds = 0;
	ptz_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (ptz_instance_data*) inst[u]->impl;
		if(data->fd >= 0){
			if(mm_manage_fd(data->fd, BACKEND_NAME, 1, inst[u])){
				LOGPF("Failed to register descriptor for instance %s", inst[u]->name);
				return 1;
			}
			fds++;
		}
	}

	LOGPF("Registered %" PRIsize_t " descriptors to core", fds);
	return 0;
}

static int ptz_shutdown(size_t n, instance** inst){
	size_t u;
	ptz_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (ptz_instance_data*) inst[u]->impl;
		if(data->fd >= 0){
			close(data->fd);
		}
		free(data);
		inst[u]->impl = NULL;
	}

	LOG("Backend shut down");
	return 0;
}
