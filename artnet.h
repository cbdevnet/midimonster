#include "midimonster.h"

int artnet_init();
static int artnet_configure(char* option, char* value);
static int artnet_configure_instance(instance* instance, char* option, char* value);
static instance* artnet_instance();
static channel* artnet_channel(instance* instance, char* spec);
static int artnet_set(size_t num, channel* c, channel_value* v);
static int artnet_handle(size_t num, int* fd, void** data);
static int artnet_shutdown();

typedef struct /*_artnet_instance_model*/ {
	uint8_t net;
	uint8_t uni;
} artnet_instance_data;
