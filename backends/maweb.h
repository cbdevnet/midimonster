#include "midimonster.h"

int init();
static int maweb_configure(char* option, char* value);
static int maweb_configure_instance(instance* inst, char* option, char* value);
static instance* maweb_instance();
static channel* maweb_channel(instance* inst, char* spec);
static int maweb_set(instance* inst, size_t num, channel** c, channel_value* v);
static int maweb_handle(size_t num, managed_fd* fds);
static int maweb_start();
static int maweb_shutdown();

//Default login password: MD5("midimonster")
#define MAWEB_DEFAULT_PASSWORD "2807623134739142b119aff358f8a219"
#define MAWEB_DEFAULT_PORT "80"
#define MAWEB_RECV_CHUNK 1024
#define MAWEB_XMIT_CHUNK 2048
#define MAWEB_FRAME_HEADER_LENGTH 16
#define MAWEB_CONNECTION_KEEPALIVE 10000

typedef enum /*_maweb_channel_type*/ {
	type_unset = 0,
	exec_fader = 1,
	exec_button = 2,
	exec_upper = 3,
	exec_lower = 4,
	exec_flash = 5,
	cmdline_button
} maweb_channel_type;

typedef enum /*_ws_conn_state*/ {
	ws_new,
	ws_http,
	ws_open,
	ws_closed
} maweb_state;

typedef enum /*_ws_frame_op*/ {
	ws_text = 1,
	ws_binary = 2,
	ws_ping = 9,
	ws_pong = 10
} maweb_operation;

typedef union {
	struct {
		uint8_t padding[3];
		uint8_t type;
		uint16_t page;
		uint16_t index;
	} fields;
	uint64_t label;
} maweb_channel_ident;

typedef struct /*_maweb_instance_data*/ {
	char* host;
	char* port;
	char* user;
	char* pass;
	
	uint8_t login;
	int64_t session;

	int fd;
	maweb_state state;
	size_t offset;
	size_t allocated;
	uint8_t* buffer;
} maweb_instance_data;
