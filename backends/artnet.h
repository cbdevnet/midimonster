#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "midimonster.h"

MM_PLUGIN_API int init();
static int artnet_configure(char* option, char* value);
static int artnet_configure_instance(instance* instance, char* option, char* value);
static instance* artnet_instance();
static channel* artnet_channel(instance* instance, char* spec, uint8_t flags);
static int artnet_set(instance* inst, size_t num, channel** c, channel_value* v);
static int artnet_handle(size_t num, managed_fd* fds);
static int artnet_start(size_t n, instance** inst);
static int artnet_shutdown(size_t n, instance** inst);

#define ARTNET_PORT "6454"
#define ARTNET_VERSION 14
#define ARTNET_RECV_BUF 4096
#define ARTNET_KEEPALIVE_INTERVAL 2000

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
} artnet_universe;

typedef struct /*_artnet_instance_model*/ {
	uint8_t net;
	uint8_t uni;
	struct sockaddr_storage dest_addr;
	socklen_t dest_len;
	artnet_universe data;
	size_t fd_index;
} artnet_instance_data;

typedef union /*_artnet_instance_id*/ {
	struct {
		uint8_t fd_index;
		uint8_t net;
		uint8_t uni;
	} fields;
	uint64_t label;
} artnet_instance_id;

typedef struct /*_artnet_fd*/ {
	int fd;
	size_t output_instances;
	artnet_instance_id* output_instance;
	uint64_t* last_frame;
} artnet_descriptor;

#pragma pack(push, 1)
typedef struct /*_artnet_hdr*/ {
	uint8_t magic[8];
	uint16_t opcode;
	uint16_t version;
} artnet_hdr;

typedef struct /*_artnet_pkt*/ {
	uint8_t magic[8];
	uint16_t opcode;
	uint16_t version;
	uint8_t sequence;
	uint8_t port;
	uint8_t universe;
	uint8_t net;
	uint16_t length;
	uint8_t data[512];
} artnet_pkt;
#pragma pack(pop)

enum artnet_pkt_opcode {
	OpDmx = 0x0050
};
