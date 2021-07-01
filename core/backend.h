#include <sys/types.h>

/* Internal API */
int backends_handle(size_t nfds, managed_fd* fds);
int backends_notify(size_t nev, channel** c, channel_value* v);
backend* backend_match(char* name);
instance* instance_match(char* name);
struct timeval backend_timeout();
int backends_start();
int backends_stop();
instance* mm_instance(backend* b);

/* Public backend API */
MM_API channel* mm_channel(instance* inst, uint64_t ident, uint8_t create);
MM_API void mm_channel_update(channel* chan, uint64_t ident);
MM_API instance* mm_instance_find(char* name, uint64_t ident);
MM_API int mm_backend_instances(char* name, size_t* ninst, instance*** inst);
MM_API int mm_backend_register(backend b);
