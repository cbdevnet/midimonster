#include <string.h>
#include "lua.h"

#define BACKEND_NAME "lua"
#define LUA_REGISTRY_KEY "_midimonster_lua_instance"

//TODO instance identification for callvacks

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
	return 0;
}

static int lua_callback_output(lua_State* interpreter){
	int arguments = lua_gettop(interpreter);
	size_t n;
	channel_value val;
	const char* channel_name = NULL;
	channel* channel = NULL;
	instance* inst = NULL;
	lua_instance_data* data = NULL;

	if(arguments != 2){
		fprintf(stderr, "Lua output function called with %d arguments, expected 2\n", arguments);
		return 0;
	}

	channel_name = lua_tostring(interpreter, 1);
	val.normalised = clamp(lua_tonumber(interpreter, 2), 1.0, 0.0);

	lua_pushstring(interpreter, LUA_REGISTRY_KEY);
	lua_gettable(interpreter, LUA_REGISTRYINDEX);
	inst = (instance *) lua_touserdata(interpreter, -1);
	data = (lua_instance_data*) inst->impl;

	for(n = 0; n < data->channels; n++){
		if(!strcmp(channel_name, data->channel_name[n])){
			channel = mm_channel(inst, n, 0);
			if(!channel){
				return 0;
			}
			mm_channel_event(channel, val);
			return 0;
		}
	}

	fprintf(stderr, "Tried to set unknown channel %s.%s\n", inst->name, channel_name);
	return 0;
}

static int lua_configure(char* option, char* value){
	fprintf(stderr, "The lua backend does not take any global configuration\n");
	return 1;
}

static int lua_configure_instance(instance* inst, char* option, char* value){
	lua_instance_data* data = (lua_instance_data*) inst->impl;

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

	//register lua api functions
	lua_register(data->interpreter, "output", lua_callback_output);

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
		if(!data->channel_name){
			fprintf(stderr, "Failed to allocate memory\n");
			return NULL;
		}

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

	for(n = 0; n < num; n++){
		//call lua channel handlers
		lua_getglobal(data->interpreter, data->channel_name[c[n]->ident]);
		lua_pushnumber(data->interpreter, v[n].normalised);
		if(lua_pcall(data->interpreter, 1, 0, 0) != LUA_OK){
			fprintf(stderr, "Failed to call handler for %s.%s: %s\n", inst->name, data->channel_name[c[n]->ident], lua_tostring(data->interpreter, -1));
			lua_pop(data->interpreter, 1);
		}
	}
	return 0;
}

static int lua_handle(size_t num, managed_fd* fds){
	//TODO call timer callbacks
	return 0;
}

static int lua_start(){
	//TODO start timers / register fds
	return 0;
}

static int lua_shutdown(){
	size_t n, u, p;
	instance** inst = NULL;
	lua_instance_data* data = NULL;

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
		free(inst[u]->impl);
	}

	free(inst);

	fprintf(stderr, "Lua backend shut down\n");
	return 0;
}
