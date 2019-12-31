#include "midimonster.h"
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif

#define OSC_RECV_BUF 8192
#define OSC_XMIT_BUF 8192

MM_PLUGIN_API int init();
static int osc_configure(char* option, char* value);
static int osc_configure_instance(instance* inst, char* option, char* value);
static instance* osc_instance();
static channel* osc_map_channel(instance* inst, char* spec, uint8_t flags);
static int osc_set(instance* inst, size_t num, channel** c, channel_value* v);
static int osc_handle(size_t num, managed_fd* fds);
static int osc_start(size_t n, instance** inst);
static int osc_shutdown(size_t n, instance** inst);

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
	uint8_t mark;

	osc_parameter_type* type;
	osc_parameter_value* max;
	osc_parameter_value* min;
	osc_parameter_value* in;
	osc_parameter_value* out;
} osc_channel;

typedef struct /*_osc_instance_data*/ {
	//pre-configured channel patterns
	size_t patterns;
	osc_channel* pattern;

	//actual channel registry
	size_t channels;
	osc_channel* channel;

	//instance config
	char* root;
	uint8_t learn;

	//peer addressing
	socklen_t dest_len;
	struct sockaddr_storage dest;
	uint16_t forced_rport;

	//peer fd
	int fd;
} osc_instance_data;

typedef union {
	struct {
		uint32_t channel;
		uint32_t parameter;
	} fields;
	uint64_t label;
} osc_channel_ident;

