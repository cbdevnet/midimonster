#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
#include "lua.h"

#define BACKEND_NAME "lua"
#define LUA_REGISTRY_KEY "_midimonster_lua_instance"

static size_t timers = 0;
static lua_timer* timer = NULL;
static struct itimerspec timer_config = {
	0
};
static int timer_fd = -1;

int init(){
	backend lua = {
		.name = BACKEND_NAME,
		.conf = lua_configure,
		.create = lua_instance,
		.conf_instance = lua_configure_instance,
		.channel = lua_channel,
		.handle = lua_set,
		.process = lua_handle,
		.start = lua_start,
		.shutdown = lua_shutdown
	};

	//register backend
	if(mm_backend_register(lua)){
		fprintf(stderr, "Failed to register lua backend\n");
		return 1;
	}

	//create the timer to expire intervals
	timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if(timer_fd < 0){
		fprintf(stderr, "Failed to create timer for Lua backend\n");
		return 1;
	}
	return 0;
}

static int lua_update_timerfd(){
	uint64_t interval = 0, gcd, residual;
	size_t n = 0;

	//find the minimum for the lower interval bounds
	for(n = 0; n < timers; n++){
		if(timer[n].interval && (!interval || timer[n].interval < interval)){
			interval = timer[n].interval;
		}
	}

	//stop the timer
	if(!interval){
		memset(&timer_config, 0, sizeof(struct itimerspec));
	}
	//calculate gcd of all timers
	else{
		for(n = 0; n < timers; n++){
			if(timer[n].interval){
				//calculate gcd of current interval and this timers interval
				gcd = timer[n].interval;
				while(gcd){
					residual = interval % gcd;
					interval = gcd;
					gcd = residual;
				}
				//since we round everything, 10 is the lowest interval we get
				if(interval == 10){
					break;
				}
			}
		}

		timer_config.it_interval.tv_sec = interval / 1000;
		timer_config.it_interval.tv_nsec = (interval % 1000) * 1e6;
		timer_config.it_value.tv_nsec = 1;
	}

	//configure the new interval
	timerfd_settime(timer_fd, 0, &timer_config, NULL);
	return 0;
}

static int lua_callback_output(lua_State* interpreter){
	size_t n = 0;
	channel_value val;
	const char* channel_name = NULL;
	channel* channel = NULL;
	instance* inst = NULL;
	lua_instance_data* data = NULL;

	if(lua_gettop(interpreter) != 2){
		fprintf(stderr, "Lua output function called with %d arguments, expected 2 (string, number)\n", lua_gettop(interpreter));
		return 0;
	}

	//get instance pointer from registry
	lua_pushstring(interpreter, LUA_REGISTRY_KEY);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	inst = (instance*) lua_touserdata(interpreter, -1);
	data = (lua_instance_data*) inst->impl;

	//fetch function parameters
	channel_name = lua_tostring(interpreter, 1);
	val.normalised = clamp(luaL_checknumber(interpreter, 2), 1.0, 0.0);

	//find correct channel & output value
	for(n = 0; n < data->channels; n++){
		if(!strcmp(channel_name, data->channel_name[n])){
			channel = mm_channel(inst, n, 0);
			if(!channel){
				return 0;
			}
			mm_channel_event(channel, val);
			data->output[n] = val.normalised;
			return 0;
		}
	}

	fprintf(stderr, "Tried to set unknown channel %s.%s\n", inst->name, channel_name);
	return 0;
}

static int lua_callback_interval(lua_State* interpreter){
	size_t n = 0;
	instance* inst = NULL;
	lua_instance_data* data = NULL;
	uint64_t interval = 0;
	int reference = LUA_NOREF;

	if(lua_gettop(interpreter) != 2){
		fprintf(stderr, "Lua output function called with %d arguments, expected 2 (string, number)\n", lua_gettop(interpreter));
		return 0;
	}

	//get instance pointer from registry
	lua_pushstring(interpreter, LUA_REGISTRY_KEY);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	inst = (instance*) lua_touserdata(interpreter, -1);
	data = (lua_instance_data*) inst->impl;

	//fetch and round the interval
	interval = luaL_checkinteger(interpreter, 2);
	if(interval % 10 < 5){
		interval -= interval % 10;
	}
	else{
		interval += (10 - (interval % 10));
	}

	//push the function again
	lua_pushvalue(interpreter, 1);
	if(lua_gettable(interpreter, LUA_REGISTRYINDEX) == LUA_TNUMBER){
		//already interval'd
		reference = luaL_checkinteger(interpreter, 4);
	}
	else if(interval){
		//get a reference to the function
		lua_pushvalue(interpreter, 1);
		reference = luaL_ref(interpreter, LUA_REGISTRYINDEX);

		//the function indexes the reference
		lua_pushvalue(interpreter, 1);
		lua_pushinteger(interpreter, reference);
		lua_settable(interpreter, LUA_REGISTRYINDEX);
	}

	//find matching timer
	for(n = 0; n < timers; n++){
		if(timer[n].reference == reference && timer[n].interpreter == interpreter){
			break;
		}
	}

	if(n < timers){
		//set new interval
		timer[n].interval = interval;
		timer[n].delta = 0;
	}
	else if(interval){
		//append new timer
		timer = realloc(timer, (timers + 1) * sizeof(lua_timer));
		if(!timer){
			fprintf(stderr, "Failed to allocate memory\n");
			timers = 0;
			return 0;
		}
		timer[timers].interval = interval;
		timer[timers].delta = 0;
		timer[timers].interpreter = interpreter;
		timer[timers].reference = reference;
		timers++;
	}

	//recalculate timerspec
	lua_update_timerfd();
	return 0;
}

static int lua_callback_value(lua_State* interpreter, uint8_t input){
	size_t n = 0;
	instance* inst = NULL;
	lua_instance_data* data = NULL;
	const char* channel_name = NULL;

	if(lua_gettop(interpreter) != 1){
		fprintf(stderr, "Lua get_value function called with %d arguments, expected 1 (string)\n", lua_gettop(interpreter));
		return 0;
	}

	//get instance pointer from registry
	lua_pushstring(interpreter, LUA_REGISTRY_KEY);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	inst = (instance*) lua_touserdata(interpreter, -1);
	data = (lua_instance_data*) inst->impl;

	//fetch argument
	channel_name = lua_tostring(interpreter, 1);

	//find correct channel & return value
	for(n = 0; n < data->channels; n++){
		if(!strcmp(channel_name, data->channel_name[n])){
			lua_pushnumber(data->interpreter, (input) ? data->input[n] : data->output[n]);
			return 1;
		}
	}

	fprintf(stderr, "Tried to get unknown channel %s.%s\n", inst->name, channel_name);
	return 0;
}

static int lua_callback_input_value(lua_State* interpreter){
	return lua_callback_value(interpreter, 1);
}

static int lua_callback_output_value(lua_State* interpreter){
	return lua_callback_value(interpreter, 0);
}

static int lua_configure(char* option, char* value){
	fprintf(stderr, "The lua backend does not take any global configuration\n");
	return 1;
}

static int lua_configure_instance(instance* inst, char* option, char* value){
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//load a lua file into the interpreter
	if(!strcmp(option, "script")){
		if(luaL_dofile(data->interpreter, value)){
			fprintf(stderr, "Failed to load lua source file %s for instance %s: %s\n", value, inst->name, lua_tostring(data->interpreter, -1));
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "Unknown configuration parameter %s for lua backend\n", option);
	return 1;
}

static instance* lua_instance(){
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	lua_instance_data* data = calloc(1, sizeof(lua_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	//load the interpreter
	data->interpreter = luaL_newstate();
	if(!data->interpreter){
		fprintf(stderr, "Failed to initialize LUA\n");
		free(data);
		return NULL;
	}
	luaL_openlibs(data->interpreter);

	//register lua interface functions
	lua_register(data->interpreter, "output", lua_callback_output);
	lua_register(data->interpreter, "interval", lua_callback_interval);
	lua_register(data->interpreter, "input_value", lua_callback_input_value);
	lua_register(data->interpreter, "output_value", lua_callback_output_value);

	//store instance pointer to the lua state
	lua_pushstring(data->interpreter, LUA_REGISTRY_KEY);
	lua_pushlightuserdata(data->interpreter, (void *) inst);
	lua_settable(data->interpreter, LUA_REGISTRYINDEX);

	inst->impl = data;
	return inst;
}

static channel* lua_channel(instance* inst, char* spec){
	size_t u;
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//find matching channel
	for(u = 0; u < data->channels; u++){
		if(!strcmp(spec, data->channel_name[u])){
			break;
		}
	}

	//allocate new channel
	if(u == data->channels){
		data->channel_name = realloc(data->channel_name, (u + 1) * sizeof(char*));
		data->reference = realloc(data->reference, (u + 1) * sizeof(int));
		data->input = realloc(data->input, (u + 1) * sizeof(double));
		data->output = realloc(data->output, (u + 1) * sizeof(double));
		if(!data->channel_name || !data->reference || !data->input || !data->output){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

		data->reference[u] = LUA_NOREF;
		data->input[u] = data->output[u] = 0.0;
		data->channel_name[u] = strdup(spec);
		if(!data->channel_name[u]){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}
		data->channels++;
	}

	return mm_channel(inst, u, 1);
}

static int lua_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t n = 0;
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//handle all incoming events
	for(n = 0; n < num; n++){
		data->input[c[n]->ident] = v[n].normalised;
		//call lua channel handlers if present
		if(data->reference[n] != LUA_NOREF){
			lua_rawgeti(data->interpreter, LUA_REGISTRYINDEX, data->reference[c[n]->ident]);
			lua_pushnumber(data->interpreter, v[n].normalised);
			if(lua_pcall(data->interpreter, 1, 0, 0) != LUA_OK){
				fprintf(stderr, "Failed to call handler for %s.%s: %s\n", inst->name, data->channel_name[c[n]->ident], lua_tostring(data->interpreter, -1));
				lua_pop(data->interpreter, 1);
			}
		}
	}
	return 0;
}

static int lua_handle(size_t num, managed_fd* fds){
	uint8_t read_buffer[100];
	uint64_t delta = timer_config.it_interval.tv_sec * 1000 + timer_config.it_interval.tv_nsec / 1e6;
	size_t n;

	if(!num){
		return 0;
	}

	//read the timer iteration to acknowledge the fd
	if(read(timer_fd, read_buffer, sizeof(read_buffer)) < 0){
		fprintf(stderr, "Failed to read from Lua timer: %s\n", strerror(errno));
		return 1;
	}

	//add delta to all active timers
	for(n = 0; n < timers; n++){
		if(timer[n].interval){
			timer[n].delta += delta;
			//call lua function if timer expired
			if(timer[n].delta >= timer[n].interval){
				timer[n].delta %= timer[n].interval;
				lua_rawgeti(timer[n].interpreter, LUA_REGISTRYINDEX, timer[n].reference);
				lua_pcall(timer[n].interpreter, 0, 0, 0);
			}
		}
	}
	return 0;
}

static int lua_start(){
	size_t n, u, p;
	instance** inst = NULL;
	lua_instance_data* data = NULL;

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	//resolve channels to their handler functions
	for(u = 0; u < n; u++){
		data = (lua_instance_data*) inst[u]->impl;
		for(p = 0; p < data->channels; p++){
			//exclude reserved names
			if(strcmp(data->channel_name[p], "output")
					&& strcmp(data->channel_name[p], "input_value")
					&& strcmp(data->channel_name[p], "output_value")
					&& strcmp(data->channel_name[p], "interval")){
				lua_getglobal(data->interpreter, data->channel_name[p]);
				data->reference[p] = luaL_ref(data->interpreter, LUA_REGISTRYINDEX);
				if(data->reference[p] == LUA_REFNIL){
					data->reference[p] = LUA_NOREF;
				}
			}
		}
	}

	free(inst);
	if(!n){
		return 0;
	}

	//register the timer with the core
	fprintf(stderr, "Lua backend registering 1 descriptor to core\n");
	if(mm_manage_fd(timer_fd, BACKEND_NAME, 1, NULL)){
		return 1;
	}
	return 0;
}

static int lua_shutdown(){
	size_t n, u, p;
	instance** inst = NULL;
	lua_instance_data* data = NULL;

	//fetch all instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(u = 0; u < n; u++){
		data = (lua_instance_data*) inst[u]->impl;
		//stop the interpreter
		lua_close(data->interpreter);
		//cleanup channel data
		for(p = 0; p < data->channels; p++){
			free(data->channel_name[p]);
		}
		free(data->channel_name);
		free(data->reference);
		free(data->input);
		free(data->output);
		free(inst[u]->impl);
	}

	free(inst);
	//free module-global data
	free(timer);
	timer = NULL;
	timers = 0;
	close(timer_fd);
	timer_fd = -1;

	fprintf(stderr, "Lua backend shut down\n");
	return 0;
}
