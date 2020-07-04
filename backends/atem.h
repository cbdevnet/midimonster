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
#define ATEM_RESPONSE_EXPECTED 0x8000
#define ATEM_ACK 0x0800

#define ATEM_LENGTH(a) ((a) & 0x07FF)

#pragma pack(push, 1)
typedef struct /*_atem_proto_hdr*/ {
	uint16_t length;
	uint16_t session;
	uint16_t ack;
	uint8_t reserved[4];
	uint16_t seqno;
} atem_hdr;
#pragma pack(pop)

typedef struct /*_atem_instance_data*/ {
	int fd;

	uint8_t established;
	atem_hdr txhdr;
} atem_instance_data;
