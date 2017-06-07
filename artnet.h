#include "midimonster.h"

/*
 * TODO
 * 	bind per instance
 * 	destination per instance
 */

int artnet_init();
static int artnet_configure(char* option, char* value);
static int artnet_configure_instance(instance* instance, char* option, char* value);
static instance* artnet_instance();
static channel* artnet_channel(instance* instance, char* spec);
static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v);
static int artnet_handle(size_t num, managed_fd* fds);
static int artnet_start();
static int artnet_shutdown();

typedef struct /*_artnet_universe_model*/ {
	uint8_t last_frame;
	uint8_t data[512];
} artnet_universe;

typedef struct /*_artnet_instance_model*/ {
	uint8_t net;
	uint8_t uni;
	uint8_t mode;
	char* dest;
	artnet_universe data;
} artnet_instance_data;

typedef union /*_artnet_instance_id*/ {
	struct {
		uint8_t net;
		uint8_t uni;
	} fields;
	uint64_t label;
} artnet_instance_id;

enum {
	output = 1,
	mark = 2
};
