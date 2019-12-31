extern "C" {
	#include "midimonster.h"
	//C++ has it's own implementation of these...
	#undef min
	#undef max

	MM_PLUGIN_API int init();
	static int ola_configure(char* option, char* value);
	static int ola_configure_instance(instance* instance, char* option, char* value);
	static instance* ola_instance();
	static channel* ola_channel(instance* instance, char* spec, uint8_t flags);
	static int ola_set(instance* inst, size_t num, channel** c, channel_value* v);
	static int ola_handle(size_t num, managed_fd* fds);
	static int ola_start(size_t n, instance** inst);
	static int ola_shutdown(size_t n, instance** inst);
}

#define MAP_COARSE 0x0200
#define MAP_FINE 0x0400
#define MAP_SINGLE 0x0800
#define MAP_MARK 0x1000
#define MAPPED_CHANNEL(a) ((a) & 0x01FF)
#define IS_ACTIVE(a) ((a) & 0xFE00)
#define IS_WIDE(a) ((a) & (MAP_FINE | MAP_COARSE))
#define IS_SINGLE(a) ((a) & MAP_SINGLE)

//since ola seems to immediately loop back any sent data as input, we only use one buffer
//to avoid excessive event feedback loops
typedef struct /*_ola_universe_model*/ {
	uint8_t data[512];
	uint16_t map[512];
} ola_universe;

typedef struct /*_ola_instance_model*/ {
	/*TODO does ola support remote connections?*/
	unsigned int universe_id;
	ola_universe data;
} ola_instance_data;
