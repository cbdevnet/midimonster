#include "midimonster.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

int init();
static int lua_configure(char* option, char* value);
static int lua_configure_instance(instance* inst, char* option, char* value);
static instance* lua_instance();
static channel* lua_channel(instance* inst, char* spec);
static int lua_set(instance* inst, size_t num, channel** c, channel_value* v);
static int lua_handle(size_t num, managed_fd* fds);
static int lua_start();
static int lua_shutdown();

typedef struct /*_lua_instance_data*/ {
	size_t channels;
	char** channel_name;
	lua_State* interpreter;
} lua_instance_data;
