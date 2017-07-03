#include "midimonster.h"
#include <sys/types.h>
#include <sys/socket.h>

#define OSC_RECV_BUF 4096

int init();
static int backend_configure(char* option, char* value);
static int backend_configure_instance(instance* instance, char* option, char* value);
static instance* backend_instance();
static channel* backend_channel(instance* instance, char* spec);
static int backend_set(instance* inst, size_t num, channel** c, channel_value* v);
static int backend_handle(size_t num, managed_fd* fds);
static int backend_start();
static int backend_shutdown();

typedef enum {
	not_set = 0,
	int32 = 'i',
	float32 = 'f',
	/*s, b*/ //ignored
	int64 = 'h',
	double64 = 'd',
} osc_parameter_type;

typedef union {
	int32_t i32;
	float f;
	int64_t i64;
	double d;
} osc_parameter_value;

typedef struct /*_osc_channel*/ {
	char* path;
	size_t params;
	size_t param_index;
	size_t* param;
	uint8_t mark;

	osc_parameter_type type;
	osc_parameter_value max;
	osc_parameter_value min;
	osc_parameter_value current;
} osc_channel;

typedef struct /*_osc_instance_data*/ {
	size_t channels;
	osc_channel* channel;
	uint8_t output;
	char* root;
	socklen_t dest_len;
	struct sockaddr_storage dest;
	int fd;
} osc_instance;
