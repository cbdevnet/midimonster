#include <sys/socket.h>
#include "midimonster.h"

int init();
static int rtpmidi_configure(char* option, char* value);
static int rtpmidi_configure_instance(instance* instance, char* option, char* value);
static instance* rtpmidi_instance();
static channel* rtpmidi_channel(instance* instance, char* spec, uint8_t flags);
static int rtpmidi_set(instance* inst, size_t num, channel** c, channel_value* v);
static int rtpmidi_handle(size_t num, managed_fd* fds);
static int rtpmidi_start();
static int rtpmidi_shutdown();

#define RTPMIDI_DEFAULT_PORTBASE "9001"
#define RTPMIDI_RECV_BUF 4096
#define RTPMIDI_MDNS_PORT "5353"

typedef enum /*_rtpmidi_peer_mode*/ {
	peer_learned,
	peer_invited,
	peer_invited_by,
	peer_sync,
	peer_connect
} rtpmidi_peer_mode;

typedef struct /*_rtpmidi_peer*/ {
	rtpmidi_peer_mode mode;
	struct sockaddr_storage dest;
	socklen_t dest_len;
	uint32_t ssrc;
} rtpmidi_peer;

typedef struct /*_rtpmidi_fd*/ {
	int data;
	int control;
} rtpmidi_fd;

typedef struct /*_rtmidi_instance_data*/ {
	int fd;
	size_t npeers;
	rtpmidi_peer* peers;
	uint32_t ssrc;

	//apple-midi config
	char* session_name;
	char* invite_peers;
	char* invite_accept;

	//generic mode config
	uint8_t learn_peers;
} rtpmidi_instance_data;

#pragma pack(push, 1)
typedef struct /*_apple_session_command*/ {
	uint16_t res1;
	uint8_t command[2];
	uint32_t version;
	uint32_t token;
	uint32_t ssrc;
	//char* name
} apple_command;

typedef struct /*_apple_session_sync*/ {
	uint16_t res1;
	uint8_t command[2];
	uint32_t ssrc;
	uint8_t count;
	uint8_t res2[3];
	uint64_t timestamp[3];
} apple_sync;

typedef struct /*_apple_session_feedback*/ {
	uint16_t res1;
	uint8_t command[2];
	uint32_t ssrc;
	uint32_t sequence;
} apple_feedback;

typedef struct /*_rtp_midi_header*/ {
	uint16_t vpxccmpt; //this is really just an amalgamated constant value
	uint16_t sequence;
	uint32_t timestamp;
	uint32_t ssrc;
} rtpmidi_header;

typedef struct /*_rtp_midi_command*/ {
	uint8_t flags;
	uint8_t additional_length;
} rtpmidi_command;
#pragma pack(pop)
