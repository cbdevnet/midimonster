#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "midimonster.h"

MM_PLUGIN_API int init();
static int rtpmidi_configure(char* option, char* value);
static int rtpmidi_configure_instance(instance* instance, char* option, char* value);
static int rtpmidi_instance(instance* inst);
static channel* rtpmidi_channel(instance* instance, char* spec, uint8_t flags);
static uint32_t rtpmidi_interval();
static int rtpmidi_set(instance* inst, size_t num, channel** c, channel_value* v);
static int rtpmidi_handle(size_t num, managed_fd* fds);
static int rtpmidi_start(size_t n, instance** inst);
static int rtpmidi_shutdown(size_t n, instance** inst);

#define RTPMIDI_PACKET_BUFFER 8192
#define RTPMIDI_DEFAULT_HOST "::"
#define RTPMIDI_MDNS_PORT "5353"
#define RTPMIDI_HEADER_MAGIC 0x80
#define RTPMIDI_HEADER_TYPE 0x61
#define RTPMIDI_GET_TYPE(a) ((a) & 0x7F)
#define RTPMIDI_DEFAULT_NAME "MIDIMonster"
#define RTPMIDI_SERVICE_INTERVAL 1000
#define RTPMIDI_MDNS_DOMAIN "_apple-midi._udp.local."
#define RTPMIDI_DNSSD_DOMAIN "_services._dns-sd._udp.local."
#define RTPMIDI_ANNOUNCE_INTERVAL (60 * 1000)

#define DNS_POINTER(a) (((a) & 0xC0) == 0xC0)
#define DNS_LABEL_LENGTH(a) ((a) & 0x3F)
#define DNS_OPCODE(a) (((a) & 0x78) >> 3)
#define DNS_RESPONSE(a) ((a) & 0x80)

enum /*_rtpmidi_channel_type*/ {
	none = 0,
	note = 0x90,
	cc = 0xB0,
	pressure = 0xA0,
	aftertouch = 0xD0,
	pitchbend = 0xE0
};

typedef enum /*_rtpmidi_instance_mode*/ {
	unconfigured = 0,
	direct,
	apple
} rtpmidi_instance_mode;

typedef union {
	struct {
		uint8_t pad[5];
		uint8_t type;
		uint8_t channel;
		uint8_t control;
	} fields;
	uint64_t label;
} rtpmidi_channel_ident;

typedef struct /*_rtpmidi_peer*/ {
	struct sockaddr_storage dest;
	socklen_t dest_len;
	//uint32_t ssrc;
	uint8_t active; //marked for reuse
	uint8_t learned; //learned / configured peer (learned peers are marked inactive on session shutdown)
	uint8_t connected; //currently in active session
	ssize_t invite; //invite-list index for apple-mode learned peers
} rtpmidi_peer;

typedef struct /*_rtmidi_instance_data*/ {
	rtpmidi_instance_mode mode;

	int fd;
	int control_fd;
	uint16_t control_port; /*convenience member set by rtpmidi_bind_instance*/

	size_t peers;
	rtpmidi_peer* peer;
	uint32_t ssrc;
	uint16_t sequence;

	//apple-midi config
	char* accept;
	uint64_t last_announce;

	//direct mode config
	uint8_t learn_peers;
} rtpmidi_instance_data;

typedef struct /*rtpmidi_invited_peer*/ {
	instance* inst;
	size_t invites;
	char** name;
} rtpmidi_invite;

typedef struct /*_rtpmidi_addr*/ {
	int family;
	//this is actually a fair bit too big, but whatever
	uint8_t addr[sizeof(struct sockaddr_storage)];
} rtpmidi_addr;

typedef enum {
	apple_invite = 0x494E, //IN
	apple_accept = 0x4F4B, //OK
	apple_reject = 0x4E4F, //NO
	apple_leave = 0x4259, //BY
	apple_sync = 0x434B, //CK
	apple_feedback = 0x5253 //RS
} applemidi_command;

typedef struct /*_dns_name*/ {
	size_t alloc;
	char* name;
	size_t length;
} dns_name;

#pragma pack(push, 1)
typedef struct /*_apple_session_command*/ {
	uint16_t res1;
	uint16_t command;
	uint32_t version;
	uint32_t token;
	uint32_t ssrc;
	//char* name
} apple_command;

typedef struct /*_apple_session_sync*/ {
	uint16_t res1;
	uint16_t command;
	uint32_t ssrc;
	uint8_t count;
	uint8_t res2[3];
	uint64_t timestamp[3];
} apple_sync_frame;

typedef struct /*_apple_session_feedback*/ {
	uint16_t res1;
	uint8_t command[2];
	uint32_t ssrc;
	uint32_t sequence;
} apple_journal_feedback;

typedef struct /*_rtp_midi_header*/ {
	uint8_t vpxcc;
	uint8_t mpt;
	uint16_t sequence;
	uint32_t timestamp;
	uint32_t ssrc;
} rtpmidi_header;

typedef struct /*_rtp_midi_command*/ {
	uint8_t flags;
	uint8_t length;
} rtpmidi_command_header;

typedef struct /*_dns_header*/ {
	uint16_t id;
	uint8_t flags[2];
	uint16_t questions;
	uint16_t answers;
	uint16_t servers;
	uint16_t additional;
} dns_header;

typedef struct /*_dns_question*/ {
	uint16_t qtype;
	uint16_t qclass;
} dns_question;

typedef struct /*_dns_rr*/ {
	uint16_t rtype;
	uint16_t rclass;
	uint32_t ttl;
	uint16_t data;
} dns_rr;

typedef struct /*_dns_rr_srv*/ {
	uint16_t priority;
	uint16_t weight;
	uint16_t port;
} dns_rr_srv;
#pragma pack(pop)
