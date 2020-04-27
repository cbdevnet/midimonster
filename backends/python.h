#include "midimonster.h"

MM_PLUGIN_API int init();
static uint32_t python_interval();
static int python_configure(char* option, char* value);
static int python_configure_instance(instance* inst, char* option, char* value);
static int python_instance(instance* inst);
static channel* python_channel(instance* inst, char* spec, uint8_t flags);
static int python_set(instance* inst, size_t num, channel** c, channel_value* v);
static int python_handle(size_t num, managed_fd* fds);
static int python_start(size_t n, instance** inst);
static int python_shutdown(size_t n, instance** inst);

typedef struct /*_python_channel_data*/ {
	char* name;
	PyObject* handler;
	double in;
	double out;
	uint8_t mark;
} mmpython_channel;

typedef struct /*_mmpy_registered_socket*/ {
	int fd;
	PyObject* handler;
	PyObject* socket;
} mmpy_socket;

typedef struct /*_mmpy_interval*/ {
	uint64_t interval;
	uint64_t delta;
	PyObject* reference;
	PyThreadState* interpreter;
} mmpy_timer;

typedef struct /*_python_instance_data*/ {
	PyThreadState* interpreter;
	PyObject* config; //TODO

	size_t sockets;
	mmpy_socket* socket;

	size_t channels;
	mmpython_channel* channel;
	mmpython_channel* current_channel;

	char* default_handler;
	PyObject* handler;
	PyObject* cleanup_handler;
} python_instance_data;
