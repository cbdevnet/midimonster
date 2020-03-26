#define BACKEND_NAME "lua"

#include "lua.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef MMBACKEND_LUA_TIMERFD
#include <sys/timerfd.h>
#endif

#define LUA_REGISTRY_KEY "_midimonster_lua_instance"
#define LUA_REGISTRY_CURRENT_CHANNEL "_midimonster_lua_channel"
#define LUA_REGISTRY_CURRENT_THREAD "_midimonster_lua_thread"

static size_t timers = 0;
static lua_timer* timer = NULL;
uint64_t timer_interval = 0;
#ifdef MMBACKEND_LUA_TIMERFD
static int timer_fd = -1;
#endif
static uint64_t last_timestamp = 0;

static size_t threads = 0;
static lua_thread* thread = NULL;

MM_PLUGIN_API int init(){
	backend lua = {
		#ifndef MMBACKEND_LUA_TIMERFD
		.interval = lua_interval,
		#endif
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
		LOG("Failed to register backend");
		return 1;
	}

	#ifdef MMBACKEND_LUA_TIMERFD
	//create the timer to expire intervals
	timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if(timer_fd < 0){
		LOG("Failed to create timer");
		return 1;
	}
	#endif
	return 0;
}

#ifndef MMBACKEND_LUA_TIMERFD
static uint32_t lua_interval(){
	size_t n = 0;
	uint64_t next_timer = 1000;

	if(timer_interval){
		for(n = 0; n < timers; n++){
			if(timer[n].interval && timer[n].interval - timer[n].delta < next_timer){
				next_timer = timer[n].interval - timer[n].delta;
			}
		}
		DBGPF("Next timer fires in %" PRIu32, next_timer);
		return next_timer;
	}
	return 1000;
}
#endif

static int lua_update_timerfd(){
	uint64_t interval = 0, gcd, residual;
	size_t n = 0;
	#ifdef MMBACKEND_LUA_TIMERFD
	struct itimerspec timer_config = {
		{0}
	};
	#endif

	//find the minimum for the lower interval bounds
	for(n = 0; n < timers; n++){
		if(timer[n].interval && (!interval || timer[n].interval < interval)){
			interval = timer[n].interval;
		}
	}

	for(n = 0; n < threads; n++){
		if(thread[n].timeout && (!interval || thread[n].timeout < interval)){
			interval = thread[n].timeout;
		}
	}
	DBGPF("Recalculating timers, minimum is %" PRIu64, interval);

	//calculate gcd of all timers if any are active
	if(interval){
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
				if(interval <= 10){
					interval = 10;
					break;
				}
			}
		}

		#ifdef MMBACKEND_LUA_TIMERFD
		timer_config.it_interval.tv_sec = timer_config.it_value.tv_sec = interval / 1000;
		timer_config.it_interval.tv_nsec = timer_config.it_value.tv_nsec = (interval % 1000) * 1e6;
		#endif
	}

	if(interval == timer_interval){
		DBGPF("Keeping interval at %" PRIu64, interval);
		return 0;
	}

	#ifdef MMBACKEND_LUA_TIMERFD
	//configure the new interval, 0.0 disarms the timer
	DBGPF("Reconfiguring timerfd to %" PRIu64 ".%" PRIu64, timer_config.it_interval.tv_sec, timer_config.it_interval.tv_nsec);
	timerfd_settime(timer_fd, 0, &timer_config, NULL);
	#endif
	timer_interval = interval;
	return 0;
}

static void lua_thread_resume(size_t current_thread){
	//push coroutine reference
	lua_pushstring(thread[current_thread].thread, LUA_REGISTRY_CURRENT_THREAD);
	lua_pushnumber(thread[current_thread].thread, current_thread);
	lua_settable(thread[current_thread].thread, LUA_REGISTRYINDEX);

	//call thread main
	DBGPF("Resuming thread %" PRIsize_t " on %s", current_thread, thread[current_thread].instance->name);
	if(lua_resume(thread[current_thread].thread, NULL, 0) != LUA_YIELD){
		DBGPF("Thread %" PRIsize_t " on %s terminated", current_thread, thread[current_thread].instance->name);
		thread[current_thread].timeout = 0;
	}

	//remove coroutine reference
	lua_pushstring(thread[current_thread].thread, LUA_REGISTRY_CURRENT_THREAD);
	lua_pushnil(thread[current_thread].thread);
	lua_settable(thread[current_thread].thread, LUA_REGISTRYINDEX);
}

static instance* lua_fetch_instance(lua_State* interpreter){
	instance* inst = NULL;

	//get instance pointer from registry
	lua_pushstring(interpreter, LUA_REGISTRY_KEY);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	inst = (instance*) lua_touserdata(interpreter, -1);
	lua_pop(interpreter, 1);
	return inst;
}

static int lua_callback_thread(lua_State* interpreter){
	instance* inst = lua_fetch_instance(interpreter);
	size_t u = threads;
	if(lua_gettop(interpreter) != 1){
		LOGPF("Thread function called with %d arguments, expected function", lua_gettop(interpreter));
		return 0;
	}

	luaL_checktype(interpreter, 1, LUA_TFUNCTION);

	//make space for a new thread
	thread = realloc(thread, (threads + 1) * sizeof(lua_thread));
	if(!thread){
		threads = 0;
		LOG("Failed to allocate memory");
		return 0;
	}
	threads++;

	thread[u].thread = lua_newthread(interpreter);
	thread[u].instance = inst;
	thread[u].timeout = 0;
	thread[u].reference = luaL_ref(interpreter, LUA_REGISTRYINDEX);

	DBGPF("Registered thread %" PRIsize_t " on %s", threads, inst->name);

	//push thread main
	luaL_checktype(interpreter, 1, LUA_TFUNCTION);
	lua_pushvalue(interpreter, 1);
	lua_xmove(interpreter, thread[u].thread, 1);

	lua_thread_resume(u);
	lua_update_timerfd();
	return 0;
}

static int lua_callback_sleep(lua_State* interpreter){
	uint64_t timeout = 0;
	size_t current_thread = threads;
	if(lua_gettop(interpreter) != 1){
		LOGPF("Sleep function called with %d arguments, expected number", lua_gettop(interpreter));
		return 0;
	}

	timeout = luaL_checkinteger(interpreter, 1);

	lua_pushstring(interpreter, LUA_REGISTRY_CURRENT_THREAD);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);

	current_thread = luaL_checkinteger(interpreter, -1);

	if(current_thread < threads){
		DBGPF("Yielding for %" PRIu64 "msec on thread %" PRIsize_t, timeout, current_thread);
		thread[current_thread].timeout = timeout;
		lua_yield(interpreter, 0);
	}
	return 0;
}

static int lua_callback_output(lua_State* interpreter){
	size_t n = 0;
	channel_value val;
	const char* channel_name = NULL;
	instance* inst = lua_fetch_instance(interpreter);
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	if(lua_gettop(interpreter) != 2){
		LOGPF("Output function called with %d arguments, expected 2 (string, number)", lua_gettop(interpreter));
		return 0;
	}

	//fetch function parameters
	channel_name = lua_tostring(interpreter, 1);
	val.normalised = clamp(luaL_checknumber(interpreter, 2), 1.0, 0.0);

	//if not started yet, create any requested channels so scripts may set them at load time
	if(!last_timestamp && channel_name){
		lua_channel(inst, (char*) channel_name, mmchannel_output);
	}

	//find correct channel & output value
	for(n = 0; n < data->channels; n++){
		if(!strcmp(channel_name, data->channel[n].name)){
			data->channel[n].out = val.normalised;
			if(!last_timestamp){
				data->channel[n].mark = 1;
			}
			else{
				mm_channel_event(mm_channel(inst, n, 0), val);
			}
			return 0;
		}
	}

	LOGPF("Tried to set unknown channel %s.%s", inst->name, channel_name);
	return 0;
}

static int lua_callback_cleanup_handler(lua_State* interpreter){
	instance* inst = lua_fetch_instance(interpreter);
	lua_instance_data* data = (lua_instance_data*) inst->impl;
	int current_handler = data->cleanup_handler;

	if(lua_gettop(interpreter) != 1){
		LOGPF("Cleanup handler function called with %d arguments, expected 1 (function)", lua_gettop(interpreter));
		return 0;
	}

	luaL_checktype(interpreter, 1, LUA_TFUNCTION);

	data->cleanup_handler = luaL_ref(interpreter, LUA_REGISTRYINDEX);
	if(current_handler == LUA_NOREF){
		lua_pushnil(interpreter);
		return 1;
	}
	lua_rawgeti(interpreter, LUA_REGISTRYINDEX, current_handler);
	luaL_unref(interpreter, LUA_REGISTRYINDEX, current_handler);
	return 1;
}

static int lua_callback_interval(lua_State* interpreter){
	size_t n = 0;
	uint64_t interval = 0;
	int reference = LUA_NOREF;

	if(lua_gettop(interpreter) != 2){
		LOGPF("Interval function called with %d arguments, expected 2 (function, number)", lua_gettop(interpreter));
		return 0;
	}

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
		DBGPF("Updating interval to %" PRIu64 " msec", interval);
	}
	else if(interval){
		//get a reference to the function
		lua_pushvalue(interpreter, 1);
		reference = luaL_ref(interpreter, LUA_REGISTRYINDEX);

		//the function indexes the reference
		lua_pushvalue(interpreter, 1);
		lua_pushinteger(interpreter, reference);
		lua_settable(interpreter, LUA_REGISTRYINDEX);

		DBGPF("Registered interval with %" PRIu64 " msec", interval);
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
			LOG("Failed to allocate memory");
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
	const char* channel_name = NULL;
	instance* inst = lua_fetch_instance(interpreter);
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	if(lua_gettop(interpreter) != 1){
		LOGPF("get_value function called with %d arguments, expected 1 (string)", lua_gettop(interpreter));
		return 0;
	}

	//fetch argument
	channel_name = lua_tostring(interpreter, 1);

	//find correct channel & return value
	for(n = 0; n < data->channels; n++){
		if(!strcmp(channel_name, data->channel[n].name)){
			lua_pushnumber(interpreter, (input) ? data->channel[n].in : data->channel[n].out);
			return 1;
		}
	}

	LOGPF("Tried to get unknown channel %s.%s", inst->name, channel_name);
	return 0;
}

static int lua_callback_input_value(lua_State* interpreter){
	return lua_callback_value(interpreter, 1);
}

static int lua_callback_output_value(lua_State* interpreter){
	return lua_callback_value(interpreter, 0);
}

static int lua_callback_input_channel(lua_State* interpreter){
	lua_pushstring(interpreter, LUA_REGISTRY_CURRENT_CHANNEL);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	return 1;
}

static int lua_callback_timestamp(lua_State* interpreter){
	lua_pushnumber(interpreter, mm_timestamp());
	return 1;
}

static int lua_configure(char* option, char* value){
	LOG("No backend configuration possible");
	return 1;
}

static int lua_configure_instance(instance* inst, char* option, char* value){
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//load a lua file into the interpreter
	if(!strcmp(option, "script") || !strcmp(option, "source")){
		if(luaL_dofile(data->interpreter, value)){
			LOGPF("Failed to load source file %s for instance %s: %s", value, inst->name, lua_tostring(data->interpreter, -1));
			return 1;
		}
		return 0;
	}
	else if(!strcmp(option, "default-handler")){
		free(data->default_handler);
		data->default_handler = strdup(value);
		return 0;
	}

	LOGPF("Unknown instance configuration parameter %s for instance %s", option, inst->name);
	return 1;
}

static int lua_instance(instance* inst){
	lua_instance_data* data = calloc(1, sizeof(lua_instance_data));
	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	//load the interpreter
	data->interpreter = luaL_newstate();
	data->cleanup_handler = LUA_NOREF;
	if(!data->interpreter){
		LOG("Failed to initialize interpreter");
		free(data);
		return 1;
	}
	luaL_openlibs(data->interpreter);

	//register lua interface functions
	lua_register(data->interpreter, "output", lua_callback_output);
	lua_register(data->interpreter, "interval", lua_callback_interval);
	lua_register(data->interpreter, "input_value", lua_callback_input_value);
	lua_register(data->interpreter, "output_value", lua_callback_output_value);
	lua_register(data->interpreter, "input_channel", lua_callback_input_channel);
	lua_register(data->interpreter, "timestamp", lua_callback_timestamp);
	lua_register(data->interpreter, "thread", lua_callback_thread);
	lua_register(data->interpreter, "sleep", lua_callback_sleep);
	lua_register(data->interpreter, "cleanup_handler", lua_callback_cleanup_handler);

	//store instance pointer to the lua state
	lua_pushstring(data->interpreter, LUA_REGISTRY_KEY);
	lua_pushlightuserdata(data->interpreter, (void *) inst);
	lua_settable(data->interpreter, LUA_REGISTRYINDEX);

	inst->impl = data;
	return 0;
}

static channel* lua_channel(instance* inst, char* spec, uint8_t flags){
	size_t u;
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//find matching channel
	for(u = 0; u < data->channels; u++){
		if(!strcmp(spec, data->channel[u].name)){
			break;
		}
	}

	//allocate new channel
	if(u == data->channels){
		data->channel = realloc(data->channel, (data->channels + 1) * sizeof(lua_channel_data));
		if(!data->channel){
			LOG("Failed to allocate memory");
			data->channels = 0;
			return NULL;
		}

		//initialize new channel
		memset(data->channel + u, 0, sizeof(lua_channel_data));
		data->channel[u].name = strdup(spec);
		if(!data->channel[u].name){
			LOG("Failed to allocate memory");
			return NULL;
		}
		data->channels++;
	}

	return mm_channel(inst, u, 1);
}

static int lua_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t n = 0, ident;
	lua_instance_data* data = (lua_instance_data*) inst->impl;

	//handle all incoming events
	for(n = 0; n < num; n++){
		ident = c[n]->ident;
		data->channel[ident].in = v[n].normalised;
		//call lua channel handlers if present
		if(data->channel[ident].reference != LUA_NOREF){
			//push the channel name
			lua_pushstring(data->interpreter, LUA_REGISTRY_CURRENT_CHANNEL);
			lua_pushstring(data->interpreter, data->channel[ident].name);
			lua_settable(data->interpreter, LUA_REGISTRYINDEX);

			lua_rawgeti(data->interpreter, LUA_REGISTRYINDEX, data->channel[ident].reference);
			lua_pushnumber(data->interpreter, v[n].normalised);
			if(lua_pcall(data->interpreter, 1, 0, 0) != LUA_OK){
				LOGPF("Failed to call handler for %s.%s: %s", inst->name, data->channel[ident].name, lua_tostring(data->interpreter, -1));
				lua_pop(data->interpreter, 1);
			}
		}
	}

	//clear the channel name
	lua_pushstring(data->interpreter, LUA_REGISTRY_CURRENT_CHANNEL);
	lua_pushnil(data->interpreter);
	lua_settable(data->interpreter, LUA_REGISTRYINDEX);
	return 0;
}

static int lua_handle(size_t num, managed_fd* fds){
	uint64_t delta = timer_interval;
	size_t n;

	#ifdef MMBACKEND_LUA_TIMERFD
	uint8_t read_buffer[100];
	if(!num){
		return 0;
	}

	//read the timer iteration to acknowledge the fd
	if(read(timer_fd, read_buffer, sizeof(read_buffer)) < 0){
		LOGPF("Failed to read timer: %s", strerror(errno));
		return 1;
	}
	#else
	delta = mm_timestamp() - last_timestamp;
	last_timestamp = mm_timestamp();
	#endif

	//no timers active
	if(!timer_interval){
		return 0;
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
				DBGPF("Calling interval timer function %" PRIsize_t, n);
			}
		}
	}

	//check for threads to wake up
	for(n = 0; n < threads; n++){
		if(thread[n].timeout && delta >= thread[n].timeout){
			lua_thread_resume(n);
			lua_update_timerfd();
		}
		else if(thread[n].timeout){
			thread[n].timeout -= delta;
		}
	}
	return 0;
}

static int lua_resolve_symbol(lua_State* interpreter, char* symbol){
	int reference = LUA_REFNIL;

	//exclude reserved names
	if(!strcmp(symbol, "output")
			|| !strcmp(symbol, "thread")
			|| !strcmp(symbol, "sleep")
			|| !strcmp(symbol, "input_value")
			|| !strcmp(symbol, "output_value")
			|| !strcmp(symbol, "input_channel")
			|| !strcmp(symbol, "timestamp")
			|| !strcmp(symbol, "cleanup_handler")
			|| !strcmp(symbol, "interval")){
		return LUA_NOREF;
	}

	lua_getglobal(interpreter, symbol);
	reference = luaL_ref(interpreter, LUA_REGISTRYINDEX);
	if(reference == LUA_REFNIL){
		return LUA_NOREF;
	}
	return reference;
}

static int lua_start(size_t n, instance** inst){
	size_t u, p;
	lua_instance_data* data = NULL;
	int default_handler;
	channel_value v;

	//resolve channels to their handler functions
	for(u = 0; u < n; u++){
		data = (lua_instance_data*) inst[u]->impl;
		default_handler = LUA_NOREF;

		//try to resolve default handler if given
		if(data->default_handler){
			default_handler = lua_resolve_symbol(data->interpreter, data->default_handler);
			if(default_handler == LUA_NOREF){
				LOGPF("Failed to resolve default handler %s on %s", data->default_handler, inst[u]->name);
			}
		}

		for(p = 0; p < data->channels; p++){
			data->channel[p].reference = default_handler;
			if(!data->default_handler){
				data->channel[p].reference = lua_resolve_symbol(data->interpreter, data->channel[p].name);
			}
			//push initial values
			if(data->channel[p].mark){
				v.normalised = data->channel[p].out;
				mm_channel_event(mm_channel(inst[u], p, 0), v);
			}
		}
	}

	#ifdef MMBACKEND_LUA_TIMERFD
	//register the timer with the core
	LOG("Registering 1 descriptor to core");
	if(mm_manage_fd(timer_fd, BACKEND_NAME, 1, NULL)){
		return 1;
	}
	#endif
	last_timestamp = mm_timestamp();
	return 0;
}

static int lua_shutdown(size_t n, instance** inst){
	size_t u, p;
	lua_instance_data* data = NULL;

	for(u = 0; u < n; u++){
		data = (lua_instance_data*) inst[u]->impl;

		//call cleanup function if one is registered
		if(data->cleanup_handler != LUA_NOREF){
			lua_rawgeti(data->interpreter, LUA_REGISTRYINDEX, data->cleanup_handler);
			lua_pcall(data->interpreter, 0, 0, 0);
		}

		//stop the interpreter
		lua_close(data->interpreter);
		//cleanup channel data
		for(p = 0; p < data->channels; p++){
			free(data->channel[p].name);
		}
		free(data->channel);
		free(data->default_handler);
		free(inst[u]->impl);
	}

	//free module-global data
	free(timer);
	timer = NULL;
	timers = 0;
	free(thread);
	thread = NULL;
	threads = 0;
	#ifdef MMBACKEND_LUA_TIMERFD
	close(timer_fd);
	timer_fd = -1;
	#endif

	LOG("Backend shut down");
	return 0;
}
