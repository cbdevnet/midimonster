#include "midimonster.h"

int midi_init();
static int midi_configure(char* option, char* value);
static int midi_configure_instance(instance* instance, char* option, char* value);
static instance* midi_instance();
static channel* midi_channel(instance* instance, char* spec);
static int midi_set(size_t num, channel* c, channel_value* v);
static int midi_handle(size_t num, int* fd, void** data);
static int midi_start();
static int midi_shutdown();
