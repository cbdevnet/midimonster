#ifndef MIDIMONSTER_HEADER
#define MIDIMONSTER_HEADER
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define DEFAULT_CFG "monster.cfg"

struct _channel_value;
struct _backend_channel;
struct _backend_instance;

typedef int (*mmbackend_handle_event)(size_t channels, struct _backend_channel* c, struct _channel_value* v);
typedef struct _backend_instance* (*mmbackend_create_instance)();
typedef struct _backend_channel* (*mmbackend_parse_channel)(struct _backend_instance* instance, char* spec);
typedef void (*mmbackend_free_channel)(struct _backend_channel* c);
typedef int (*mmbackend_configure)(char* option, char* value);
typedef int (*mmbackend_configure_instance)(struct _backend_instance* instance, char* option, char* value);
typedef int (*mmbackend_process_fd)(size_t fds, int* fd, void** impl);
typedef int (*mmbackend_start)();
typedef uint32_t (*mmbackend_max_interval)();
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
	mmbackend_start start;
	mmbackend_shutdown shutdown;
	mmbackend_free_channel channel_free;
} backend;

typedef struct _backend_instance {
	backend* backend;
	uint64_t ident; /*FIXME needed? identification provided by name*/
	void* impl;
	char* name;
} instance;

typedef struct _backend_channel {
	instance* instance;
	uint64_t ident;
	void* impl;
} channel;

//FIXME might be replaced by struct pollfd
typedef struct /*_mm_managed_fd*/ {
	int fd;
	backend* backend;
	void* impl;
} managed_fd;

typedef struct /*_mm_channel_mapping*/ {
	channel* from;
	size_t destinations;
	channel** to;
} channel_mapping;

/*
 * Register a new backend.
 */
int mm_backend_register(backend b);

/*
 * Provides a pointer to a newly (zero-)allocated instance.
 * All instance pointers need to be allocated via this API
 * in order to be assignable from the configuration parser.
 * This API should be called from the mmbackend_create_instance
 * call of your backend.
 *
 * Instances returned from this call are freed by midimonster.
 * The contents of the impl members should be freed in the
 * mmbackend_shutdown procedure of the backend, eg. by querying
 * all instances for the backend.
 */
instance* mm_instance();
/*
 * Provides a pointer to a channel structure, pre-filled with
 * the provided instance reference and identifier.
 * Will return previous allocations if the provided fields
 * match.
 * This API is just a convenience function. The array
 * of channels is only used for mapping internally,
 * creating and managing your own channel store is
 * possible.
 * For each channel with a non-NULL impl field, the backend
 * will receive a call to its channel_free function.
 */
channel* mm_channel(instance* i, uint64_t ident);
int mm_manage_fd(int fd, char* backend, int manage, void* impl);
int mm_channel_event(channel* c, channel_value v);
/*
 * Query all active instances for a given backend.
 */
int mm_backend_instances(char* backend, size_t* n, instance*** i);
int mm_map_channel(channel* from, channel* to);
#endif
