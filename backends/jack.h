#include "midimonster.h"
#include <jack/jack.h>
#include <pthread.h>

MM_PLUGIN_API int init();
static int mmjack_configure(char* option, char* value);
static int mmjack_configure_instance(instance* inst, char* option, char* value);
static instance* mmjack_instance();
static channel* mmjack_channel(instance* inst, char* spec, uint8_t flags);
static int mmjack_set(instance* inst, size_t num, channel** c, channel_value* v);
static int mmjack_handle(size_t num, managed_fd* fds);
static int mmjack_start(size_t n, instance** inst);
static int mmjack_shutdown(size_t n, instance** inst);

#define JACK_DEFAULT_CLIENT_NAME "MIDIMonster"
#define JACK_DEFAULT_SERVER_NAME "default"
#define JACK_MIDIQUEUE_CHUNK 10

enum /*mmjack_midi_channel_type*/ {
	midi_none = 0,
	midi_note = 0x90,
	midi_cc = 0xB0,
	midi_pressure = 0xA0,
	midi_aftertouch = 0xD0,
	midi_pitchbend = 0xE0
};

typedef union {
	struct {
		uint32_t port;
		uint8_t pad;
		uint8_t sub_type;
		uint8_t sub_channel;
		uint8_t sub_control;
	} fields;
	uint64_t label;
} mmjack_channel_ident;

typedef enum /*_mmjack_port_type*/ {
	port_none = 0,
	port_midi,
	port_osc,
	port_cv
} mmjack_port_type;

typedef struct /*_mmjack_midiqueue_entry*/ {
	mmjack_channel_ident ident;
	uint16_t raw;
} mmjack_midiqueue;

typedef struct /*_mmjack_port_data*/ {
	char* name;
	mmjack_port_type type;
	uint8_t input;
	jack_port_t* port;

	double max;
	double min;
	uint8_t mark;
	double last;
	size_t queue_len;
	size_t queue_alloc;
	mmjack_midiqueue* queue;

	pthread_mutex_t lock;
} mmjack_port;

typedef struct /*_jack_instance_data*/ {
	char* server_name;
	char* client_name;
	int fd;

	jack_client_t* client;
	size_t ports;
	mmjack_port* port;
} mmjack_instance_data;
