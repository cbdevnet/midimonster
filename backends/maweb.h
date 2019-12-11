#include "midimonster.h"

MM_PLUGIN_API int init();
static int maweb_configure(char* option, char* value);
static int maweb_configure_instance(instance* inst, char* option, char* value);
static instance* maweb_instance();
static channel* maweb_channel(instance* inst, char* spec, uint8_t flags);
static int maweb_set(instance* inst, size_t num, channel** c, channel_value* v);
static int maweb_handle(size_t num, managed_fd* fds);
static int maweb_start(size_t n, instance** inst);
static int maweb_shutdown(size_t n, instance** inst);
static uint32_t maweb_interval();

//Default login password: MD5("midimonster")
#define MAWEB_DEFAULT_PASSWORD "2807623134739142b119aff358f8a219"
#define MAWEB_DEFAULT_PORT "80"
#define MAWEB_RECV_CHUNK 1024
#define MAWEB_XMIT_CHUNK 4096
#define MAWEB_FRAME_HEADER_LENGTH 16
#define MAWEB_CONNECTION_KEEPALIVE 10000

typedef enum /*_maweb_channel_type*/ {
	type_unset = 0,
	exec_fader = 1,
	exec_button = 2, //gma: 0 dot: 0
	exec_lower = 3, //gma: 1 dot: 1
	exec_upper = 4, //gma: 2 dot: 0
	cmdline
} maweb_channel_type;

typedef enum /*_maweb_peer_type*/ {
	peer_unidentified = 0,
	peer_ma2,
	peer_ma3,
	peer_dot2
} maweb_peer_type;

typedef enum /*_ws_conn_state*/ {
	ws_new,
	ws_http,
	ws_open,
	ws_closed
} maweb_state;

typedef enum /*_maweb_cmdline_mode*/ {
	cmd_remote = 0,
	cmd_console,
	cmd_downgrade
} maweb_cmdline_mode;

typedef enum /*_ws_frame_op*/ {
	ws_text = 1,
	ws_binary = 2,
	ws_ping = 9,
	ws_pong = 10
} maweb_operation;

typedef struct {
	char* name;
	unsigned lua;
	uint8_t press;
	uint8_t release;
	uint8_t auto_submit;
} maweb_command_key;

typedef struct /*_maweb_channel*/ {
	maweb_channel_type type;
	uint16_t page;
	uint16_t index;

	uint8_t input_blocked;

	double in;
	double out;

	//reverse reference required because the identifiers are not stable
	//because we sort the backing store...
	channel* chan;
} maweb_channel_data;

typedef struct /*_maweb_instance_data*/ {
	char* host;
	char* port;
	char* user;
	char* pass;

	uint8_t login;
	int64_t session;
	maweb_peer_type peer_type;

	size_t channels;
	maweb_channel_data* channel;
	maweb_cmdline_mode cmdline;

	int fd;
	maweb_state state;
	size_t offset;
	size_t allocated;
	uint8_t* buffer;
} maweb_instance_data;
