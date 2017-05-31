#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define DEFAULT_CFG "monster.cfg"

struct _channel_value;
struct _backend_channel;
struct _backend_instance;

typedef int (*mmbackend_handle_event)(struct _backend_channel* c, struct _channel_value v);
typedef struct _backend_channel* (*mmbackend_parse_channel)(char* spec);
typedef int (*mmbackend_configure)(char* option, char* value);
typedef struct _backend_instance* (*mmbackend_create_instance)();
typedef int (*mmbackend_configure_instance)(char* option, char* value);
typedef int (*mmbackend_process_fd)(int fd, void** impl);
typedef int (*mmbackend_shutdown)();

typedef struct _channel_value {
	double raw_double;
	uint64_t raw_u64;
	double normalised;
} channel_value;

typedef struct /*_mm_backend*/ {
	char* name;
	mmbackend_configure conf;
	mmbackend_create_instance create;
	mmbackend_configure_instance conf_instance;
	mmbackend_parse_channel channel;
	mmbackend_handle_event handle;
	mmbackend_process_fd process;
	mmbackend_shutdown shutdown;
} backend;

typedef struct _backend_instance {
	backend* backend;
	size_t ident;
	void* impl;
	char* name;
} instance;

typedef struct _backend_channel {
	instance* instance;
	size_t ident;
	void* impl;
} channel;

typedef struct /*_mm_managed_fd*/ {
	int fd;
	backend* backend;
	void* impl;
} managed_fd;

backend* mm_backend_register(backend b);
int mm_manage_fd(int fd, backend* b, int manage, void* impl);
int mm_channel_event(channel* c, channel_value v);
