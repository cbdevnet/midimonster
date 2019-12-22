#include "midimonster.h"

MM_PLUGIN_API int init();
static int sacn_configure(char* option, char* value);
static int sacn_configure_instance(instance* instance, char* option, char* value);
static instance* sacn_instance();
static channel* sacn_channel(instance* instance, char* spec, uint8_t flags);
static int sacn_set(instance* inst, size_t num, channel** c, channel_value* v);
static int sacn_handle(size_t num, managed_fd* fds);
static int sacn_start(size_t n, instance** inst);
static int sacn_shutdown(size_t n, instance** inst);

#define SACN_PORT "5568"
#define SACN_RECV_BUF 8192
#define SACN_KEEPALIVE_INTERVAL 2000
#define SACN_DISCOVERY_TIMEOUT 9000
#define SACN_PDU_MAGIC "ASC-E1.17\0\0\0"

#define MAP_COARSE 0x0200
#define MAP_FINE 0x0400
#define MAP_SINGLE 0x0800
#define MAP_MARK 0x1000
#define MAPPED_CHANNEL(a) ((a) & 0x01FF)
#define IS_ACTIVE(a) ((a) & 0xFE00)
#define IS_WIDE(a) ((a) & (MAP_FINE | MAP_COARSE))
#define IS_SINGLE(a) ((a) & MAP_SINGLE)

typedef struct /*_sacn_universe_model*/ {
	uint8_t last_priority;
	uint8_t last_seq;
	uint8_t in[512];
	uint8_t out[512];
	uint16_t map[512];
} sacn_universe;

typedef struct /*_sacn_instance_model*/ {
	uint16_t uni;
	uint8_t xmit_prio;
	uint8_t cid_filter[16];
	uint8_t filter_enabled;
	uint8_t unicast_input;
	struct sockaddr_storage dest_addr;
	socklen_t dest_len;
	sacn_universe data;
	size_t fd_index;
} sacn_instance_data;

typedef union /*_sacn_instance_id*/ {
	struct {
		uint16_t fd_index;
		uint16_t uni;
		uint8_t pad[4];
	} fields;
	uint64_t label;
} sacn_instance_id;

typedef struct /*_sacn_socket*/ {
	int fd;
	size_t universes;
	uint16_t* universe;
	uint64_t* last_frame;
} sacn_fd;

#pragma pack(push, 1)
typedef struct /*_sacn_frame_root*/ {
	uint16_t preamble_size;
	uint16_t postamble_size;
	uint8_t magic[12];
	uint16_t flags;
	uint32_t vector;
	uint8_t sender_cid[16];
	//framing
	uint16_t frame_flags;
	uint32_t frame_vector;
} sacn_frame_root;

typedef struct /*_sacn_frame_data*/ {
	//framing
	uint8_t source_name[64];
	uint8_t priority;
	uint16_t sync_addr;
	uint8_t sequence;
	uint8_t options;
	uint16_t universe;
	//dmp
	uint16_t flags;
	uint8_t vector;
	uint8_t format;
	uint16_t startcode_offset;
	uint16_t address_increment;
	uint16_t channels;
	uint8_t data[513];
} sacn_frame_data;

typedef struct /*_sacn_frame_discovery*/ {
	//framing
	uint8_t source_name[64];
	uint32_t reserved;
	//universe discovery
	uint16_t flags;
	uint32_t vector;
	uint8_t page;
	uint8_t max_page;
	uint16_t data[512];
} sacn_frame_discovery;

typedef struct /*_sacn_xmit_data*/ {
	sacn_frame_root root;
	sacn_frame_data data;
} sacn_data_pdu;

typedef struct /*_sacn_xmit_discovery*/ {
	sacn_frame_root root;
	sacn_frame_discovery data;
} sacn_discovery_pdu;
#pragma pack(pop)

#define ROOT_E131_DATA 0x4
#define FRAME_E131_DATA 0x2
#define DMP_SET_PROPERTY 0x2

#define ROOT_E131_EXTENDED 0x8
#define FRAME_E131_DISCOVERY 0x2
#define DISCOVERY_UNIVERSE_LIST 0x1
