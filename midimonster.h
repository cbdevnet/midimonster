#ifndef MIDIMONSTER_HEADER
#define MIDIMONSTER_HEADER
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define max(a,b) (((a) > (b)) ? (a) : (b))
#ifdef DEBUG
	#define DBGPF(format, ...) fprintf(stderr, (format), __VA_ARGS__)
	#define DBG(message) fprintf(stderr, "%s", (message))
#else
	#define DBGPF(format, ...)
	#define DBG(message)
#endif

#define DEFAULT_CFG "monster.cfg"

struct _channel_value;
struct _backend_channel;
struct _backend_instance;
struct _managed_fd;

/*
 * Backend module callback defines
 *
 * The lifecycle of a backend module is as follows
 * 	* int init()
 * 		The only function that should be exported by the shared object.
 * 		Called when the shared object is attached. Should register
 * 		a backend structure with the core.
 * 		Returning anything other than zero causes midimonster to fail the
 * 		startup checks.
 * 	* mmbackend_configure
 * 		Parse backend-global configuration options from the user-supplied
 * 		configuration file. Returning a non-zero value fails config parsing.
 * 	* mmbackend_instance
 * 		Allocate space for a backend instance. Returning NULL signals an out-of-memory
 * 		condition and terminates the program.
 * 	* mmbackend_configure_instance
 * 		Parse instance configuration from the user-supplied configuration
 * 		file. Returning a non-zero value fails config parsing.
 * 	* mmbackend_channel
 * 		Parse a channel-spec to be mapped to/from. Returning NULL signals an
 * 		out-of-memory condition and terminates the program.
 * 	* mmbackend_start
 * 		Called after all instances have been created and all mappings
 * 		have been set up. May be used to connect to backing hardware
 * 		or to update runtime-specific data in the various data structures.
 * 		Returning a non-zero value signals an error starting the backend
 * 		and stops further progress.
 * 	* Normal processing loop starts here
 * 		* mmbackend_process_fd
 * 			Handle data from signaled fds registered via mm_manage_fd.
 * 			Push generated events to the core with mm_channel_event.
 * 			All registered fds that are ready to read are pushed at once.
 * 			Backends that have not registered any fds are still called with
 * 			nfds set to 0 in order to support polling backends.
 * 			Returning a non-zero value signals an error and gracefully terminates
 * 			the program.
 *		* mmbackend_handle_event
 *			An event resulted in a channel for an instance being set.
 *			Called once per changed instance with all updated channels for that
 *			specific instance.
 *			Returning a non-zero value terminates the program.
 *		* (optional) mmbackend_interval
 *			Return the maximum sleep interval for this backend in milliseconds.
 *			If not implemented, a maximum interval of one second is used.
 *	* mmbackend_shutdown
 *		Clean up all allocations, finalize all hardware connections.
 *		Return value is currently ignored.
 */
typedef int (*mmbackend_handle_event)(struct _backend_instance* inst, size_t channels, struct _backend_channel** c, struct _channel_value* v);
typedef struct _backend_instance* (*mmbackend_create_instance)();
typedef struct _backend_channel* (*mmbackend_parse_channel)(struct _backend_instance* instance, char* spec);
typedef void (*mmbackend_free_channel)(struct _backend_channel* c);
typedef int (*mmbackend_configure)(char* option, char* value);
typedef int (*mmbackend_configure_instance)(struct _backend_instance* instance, char* option, char* value);
typedef int (*mmbackend_process_fd)(size_t nfds, struct _managed_fd* fds);
typedef int (*mmbackend_start)();
typedef uint32_t (*mmbackend_interval)();
typedef int (*mmbackend_shutdown)();

typedef struct _channel_value {
	union {
		double dbl;
		uint64_t u64;
	} raw;
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
	mmbackend_interval interval;
} backend;

typedef struct _backend_instance {
	backend* backend;
	uint64_t ident;
	void* impl;
	char* name;
} instance;

typedef struct _backend_channel {
	instance* instance;
	uint64_t ident;
	void* impl;
} channel;

//FIXME might be replaced by struct pollfd
//FIXME who frees impl
typedef struct _managed_fd {
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
 * Finds an instance matching the specified backend and identifier.
 * Since setting an identifier for an instance is optional,
 * this may not work depending on the backend.
 * Instance identifiers may for example be set in the backends
 * mmbackend_start call.
 */
instance* mm_instance_find(char* backend, uint64_t ident);
/*
 * Provides a pointer to a channel structure, pre-filled with
 * the provided instance reference and identifier.
 * The `create` parameter is a boolean flag indicating whether
 * a channel matching the `ident` parameter should be created if
 * none exists. If the instance already registered a channel
 * matching `ident`, a pointer to it is returned.
 * This API is just a convenience function. The array of channels is
 * only used for mapping internally, creating and managing your own
 * channel store is possible.
 * For each channel with a non-NULL `impl` field registered using
 * this function, the backend will receive a call to its channel_free
 * function.
 */
channel* mm_channel(instance* i, uint64_t ident, uint8_t create);
//TODO channel* mm_channel_find()
/*
 * Register a file descriptor to be selected on. The backend
 * will be notified via the mmbackend_process_fd call.
 * This function may only be called from within the mmbackend_start
 * procedure.
 */
int mm_manage_fd(int fd, char* backend, int manage, void* impl);
/*
 * Notifies the core of a channel event. Used by backends to
 * inject events gathered from their backing implementation.
 */
int mm_channel_event(channel* c, channel_value v);
/*
 * Query all active instances for a given backend.
 * *i will need to be freed by the caller.
 */
int mm_backend_instances(char* backend, size_t* n, instance*** i);
int mm_map_channel(channel* from, channel* to);
#endif
