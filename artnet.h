#include <sys/socket.h>
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

#define ARTNET_PORT "6454"
#define ARTNET_VERSION 14
#define ARTNET_RECV_BUF 4096
#define ARTNET_KEEPALIVE_INTERVAL 15e5

typedef struct /*_artnet_universe_model*/ {
	uint8_t last_frame;
	uint8_t seq;
	uint8_t in[512];
	uint8_t out[512];
} artnet_universe;

typedef struct /*_artnet_instance_model*/ {
	uint8_t net;
	uint8_t uni;
	uint8_t mode;
	struct sockaddr_storage dest_addr;
	socklen_t dest_len;
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

enum artnet_pkt_opcode {
	OpDmx = 0x0050
};

