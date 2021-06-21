#include "midimonster.h"

MM_PLUGIN_API int init();
static int midi_configure(char* option, char* value);
static int midi_configure_instance(instance* instance, char* option, char* value);
static int midi_instance(instance* inst);
static channel* midi_channel(instance* instance, char* spec, uint8_t flags);
static int midi_set(instance* inst, size_t num, channel** c, channel_value* v);
static int midi_handle(size_t num, managed_fd* fds);
static int midi_start(size_t n, instance** inst);
static int midi_shutdown(size_t n, instance** inst);

#define EPN_NRPN 8
#define EPN_PARAMETER_HI 4
#define EPN_PARAMETER_LO 2
#define EPN_VALUE_HI 1

typedef struct /*_midi_instance_data*/ {
	int port;
	char* read;
	char* write;

	uint8_t epn_tx_short;
	uint16_t epn_control[16];
	uint16_t epn_value[16];
	uint8_t epn_status[16];
} midi_instance_data;

typedef union {
	struct {
		uint8_t pad[4];
		uint8_t type;
		uint8_t channel;
		uint16_t control;
	} fields;
	uint64_t label;
} midi_channel_ident;

