#include "midimonster.h"

MM_PLUGIN_API int init();
static int atem_configure(char* option, char* value);
static int atem_configure_instance(instance* inst, char* option, char* value);
static int atem_instance(instance* inst);
static channel* atem_channel(instance* inst, char* spec, uint8_t flags);
static int atem_set(instance* inst, size_t num, channel** c, channel_value* v);
static int atem_handle(size_t num, managed_fd* fds);
static int atem_start(size_t n, instance** inst);
static int atem_shutdown(size_t n, instance** inst);
static uint32_t atem_interval();

#define ATEM_DEFAULT_PORT "9910"
#define ATEM_PAYLOAD_MAX 2048 //packet length is 11 bit

#define ATEM_HELLO 0x1000
#define ATEM_ACK_VALID 0x8000
#define ATEM_SEQUENCE_VALID 0x0800

#define ATEM_LENGTH(a) ((a) & 0x07FF)

#pragma pack(push, 1)
typedef struct /*_atem_proto_hdr*/ {
	uint16_t length;
	uint16_t session;
	uint16_t ack;
	uint8_t reserved[4];
	uint16_t seqno;
} atem_hdr;

typedef struct /*_atem_proto_command*/ {
	uint16_t length;
	uint16_t reserved1;
	uint8_t command[4];
	uint8_t me;
	uint8_t reserved2;
} atem_command_hdr;
#pragma pack(pop)

typedef union {
	struct {
		uint8_t me;
		uint8_t extra;
		uint16_t system;
		uint16_t control;
		uint16_t subcontrol;
	} fields;
	uint64_t label;
} atem_channel_ident;

enum /*_atem_system*/ {
	atem_unknown = 0,
	atem_input,
	atem_mediaplayer,
	atem_dsk,
	atem_usk,
	atem_colorgen,
	atem_playout,
	atem_transition,
	atem_audio,
	atem_sentinel
};

enum /*_atem_control*/ {
	/*colorgen controls*/
	color_hue,
	color_saturation,
	color_luminance,

	/*input controls*/
	input_preview,
	input_program,
	input_usk_fill,
	input_usk_key,
	input_dsk_fill,
	input_dsk_key,
	input_aux,

	/*transition controls*/
	transition_cut,
	transition_auto,
	transition_tbar,
	transition_ftb,
	transition_transition_mix, //TODO
	transition_transition_dip, //TODO
	transition_transition_wipe, //TODO
	transition_transition_dve, //TODO

	/*playout controls*/
	playout_stream
};

typedef int (*atem_command_handler)(instance*, size_t, uint8_t*);
typedef int (*atem_channel_parser)(instance*, atem_channel_ident*, char*, uint8_t);
typedef int (*atem_channel_control)(instance*, atem_channel_ident*, channel* c, channel_value* v);

static int atem_handle_version(instance* inst, size_t n, uint8_t* data);
static int atem_handle_strings(instance* inst, size_t n, uint8_t* data);
static int atem_handle_topology(instance* inst, size_t n, uint8_t* data);
static int atem_handle_input_props(instance* inst, size_t n, uint8_t* data);
static int atem_handle_time(instance* inst, size_t n, uint8_t* data);
static int atem_handle_tally_index(instance* inst, size_t n, uint8_t* data);
static int atem_handle_tally_source(instance* inst, size_t n, uint8_t* data);
static int atem_handle_preview(instance* inst, size_t n, uint8_t* data);
static int atem_handle_program(instance* inst, size_t n, uint8_t* data);
static int atem_handle_tbar(instance* inst, size_t n, uint8_t* data);
static int atem_handle_ignore(instance* inst, size_t n, uint8_t* data);
static int atem_handle_color(instance* inst, size_t n, uint8_t* data);
static int atem_handle_stream(instance* inst, size_t n, uint8_t* data);
static int atem_handle_aux(instance* inst, size_t n, uint8_t* data);

static struct {
	char command[4];
	atem_command_handler handler;
} atem_command_map[] = {
	{"_ver", atem_handle_version}, //version
	{"_pin", atem_handle_strings}, //product id
	{"Warn", atem_handle_strings}, //warnings
	{"_top", atem_handle_topology}, //topology
	{"_MeC", atem_handle_topology}, //M/E config
	{"_mpl", atem_handle_topology}, //mediaplayer config
	{"_MvC", atem_handle_topology}, //multiview config
	{"_FAC", atem_handle_topology}, //audio config?
	{"_FEC", atem_handle_topology}, //equalizer config?
	{"_MAC", atem_handle_topology}, //macro config
	{"Powr", atem_handle_ignore}, //power supply
	{"Time", atem_handle_time}, //timecode?
	{"InPr", atem_handle_input_props}, //input properties
	{"TlIn", atem_handle_tally_index},
	{"TlSr", atem_handle_tally_source},
	{"PrvI", atem_handle_preview},
	{"PrgI", atem_handle_program},
	{"TrPs", atem_handle_tbar}, //transition position
	{"CCdP", atem_handle_ignore}, //camera control?
	{"MPrp", atem_handle_ignore}, //macro
	{"ColV", atem_handle_color}, //color value
	{"SRST", atem_handle_time}, //stream duration
	{"StRS", atem_handle_stream}, //stream status
	{"AuxS", atem_handle_aux} //aux source switch
};

static int atem_channel_input(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_mediaplayer(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_dsk(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_usk(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_colorgen(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_playout(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_transition(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);
static int atem_channel_audio(instance* inst, atem_channel_ident* ident, char* spec, uint8_t flags);

static int atem_control_input(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v);
static int atem_control_colorgen(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v);
static int atem_control_transition(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v);
static int atem_control_playout(instance* inst, atem_channel_ident* ident, channel* c, channel_value* v);

static struct {
	char* id;
	atem_channel_parser parser;
	atem_channel_control handler;
} atem_systems[] = {
	[atem_unknown] = {"unknown", NULL},
	[atem_input] = {"input", atem_channel_input, atem_control_input},
	[atem_mediaplayer] = {"mediaplayer", atem_channel_mediaplayer},
	[atem_dsk] = {"dsk", atem_channel_dsk},
	[atem_usk] = {"usk", atem_channel_usk},
	[atem_colorgen] = {"colorgen", atem_channel_colorgen, atem_control_colorgen},
	[atem_playout] = {"playout", atem_channel_playout, atem_control_playout},
	[atem_transition] = {"transition", atem_channel_transition, atem_control_transition},
	[atem_audio] = {"audio", atem_channel_audio},
	[atem_sentinel] = {NULL, NULL}
};

#define ATEM_INTERNAL_SOURCE 1
#define ATEM_INTERNAL_SINK 2

typedef struct /*_atem_bus*/ {
	uint16_t id; //bus id
	uint8_t type; //bus type
	uint8_t flags; //internal flags
	uint16_t source; //for output buses: selected source
	uint8_t tally;
} atem_bus;

typedef struct /*_atem_instance_data*/ {
	int fd;

	uint8_t established;
	atem_hdr txhdr;

	uint8_t tbar_inversed;
	size_t buses;
	atem_bus* bus;
} atem_instance_data;
