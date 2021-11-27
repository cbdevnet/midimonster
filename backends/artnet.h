#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "midimonster.h"

MM_PLUGIN_API int init();
static uint32_t artnet_interval();
static int artnet_configure(char* option, char* value);
static int artnet_configure_instance(instance* instance, char* option, char* value);
static int artnet_instance(instance* inst);
static channel* artnet_channel(instance* instance, char* spec, uint8_t flags);
static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v);
static int artnet_handle(size_t num, managed_fd* fds);
static int artnet_start(size_t n, instance** inst);
static int artnet_shutdown(size_t n, instance** inst);

#define ARTNET_PORT "6454"
#define ARTNET_VERSION 14
#define ARTNET_ESTA_MANUFACTURER 0x4653 //"FS" as registered with ESTA
#define ARTNET_OEM 0x2B93 //as registered with artistic license
#define ARTNET_RECV_BUF 4096

#define ARTNET_KEEPALIVE_INTERVAL 1000
//limit transmit rate to at most 44 packets per second (1000/44 ~= 22)
#define ARTNET_FRAME_TIMEOUT 20
#define ARTNET_SYNTHESIZE_MARGIN 10

#define MAP_COARSE 0x0200
#define MAP_FINE 0x0400
#define MAP_SINGLE 0x0800
#define MAP_MARK 0x1000
#define MAPPED_CHANNEL(a) ((a) & 0x01FF)
#define IS_ACTIVE(a) ((a) & 0xFE00)
#define IS_WIDE(a) ((a) & (MAP_FINE | MAP_COARSE))
#define IS_SINGLE(a) ((a) & MAP_SINGLE)

typedef struct /*_artnet_universe_model*/ {
	uint8_t seq;
	uint8_t in[512];
	uint8_t out[512];
	uint16_t map[512];
	channel channel[512];
} artnet_universe;

typedef struct /*_artnet_instance_model*/ {
	uint8_t net;
	uint8_t uni;
	struct sockaddr_storage dest_addr;
	socklen_t dest_len;
	artnet_universe data;
	size_t fd_index;
	uint64_t last_input;
	uint8_t realtime;
} artnet_instance_data;

typedef union /*_artnet_instance_id*/ {
	struct {
		uint8_t fd_index;
		uint8_t net;
		uint8_t uni;
	} fields;
	uint64_t label;
} artnet_instance_id;

typedef struct /*_artnet_fd_universe*/ {
	uint64_t label;
	uint64_t last_frame;
	uint8_t mark;
} artnet_output_universe;

typedef struct /*_artnet_fd*/ {
	int fd;
	size_t output_instances;
	artnet_output_universe* output_instance;
	struct sockaddr_storage announce_addr; //used for pollreplies if ss_family == AF_INET, port is always valid
} artnet_descriptor;

#pragma pack(push, 1)
typedef struct /*_artnet_hdr*/ {
	uint8_t magic[8];
	uint16_t opcode;
	uint16_t version;
} artnet_hdr;

typedef struct /*_artnet_dmx*/ {
	uint8_t magic[8];
	uint16_t opcode;
	uint16_t version;
	uint8_t sequence;
	uint8_t port;
	uint8_t universe;
	uint8_t net;
	uint16_t length;
	uint8_t data[512];
} artnet_dmx;

typedef struct /*_artnet_poll*/ {
	uint8_t magic[8];
	uint16_t opcode;
	uint16_t version;
	uint8_t flags;
	uint8_t priority;
} artnet_poll;

typedef struct /*_artnet_poll_reply*/ {
	uint8_t magic[8];
	uint16_t opcode; //little-endian
	uint8_t ip4[4]; //stop including l2/3 addresses in the payload, just use the sender address ffs
	uint16_t port; //little-endian, who does that?
	uint16_t firmware; //big-endian
	uint16_t port_address; //big-endian
	uint16_t oem; //big-endian
	uint8_t bios_version;
	uint8_t status;
	uint16_t manufacturer; //little-endian
	uint8_t shortname[18];
	uint8_t longname[64];
	uint8_t report[64];
	uint16_t ports; //big-endian
	uint8_t port_types[4]; //only use the first member, we report every universe in it's own reply
	uint8_t port_in[4];
	uint8_t port_out[4];
	uint8_t subaddr_in[4];
	uint8_t subaddr_out[4];
	uint8_t video; //deprecated
	uint8_t macro; //deprecatd
	uint8_t remote; //deprecated
	uint8_t spare[3];
	uint8_t style;
	uint8_t mac[6]; //come on
	uint8_t parent_ip[4]; //COME ON
	uint8_t parent_index; //i don't even know
	uint8_t status2;
	uint8_t port_out_b[4];
	uint8_t status3;
	uint8_t spare2[21];
} artnet_poll_reply;
#pragma pack(pop)

enum artnet_pkt_opcode {
	OpPoll = 0x0020,
	OpPollReply = 0x0021,
	OpDmx = 0x0050
};
