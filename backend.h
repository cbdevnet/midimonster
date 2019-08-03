#include <sys/types.h>

/* Internal API */
int backends_handle(size_t nfds, managed_fd* fds);
int backends_notify(size_t nev, channel** c, channel_value* v);
backend* backend_match(char* name);
instance* instance_match(char* name);
struct timeval backend_timeout();
int backends_start();
int backends_stop();
void instances_free();
void channels_free();

/* Backend API */
channel* MM_API mm_channel(instance* inst, uint64_t ident, uint8_t create);
instance* MM_API mm_instance();
instance* MM_API mm_instance_find(char* name, uint64_t ident);
int MM_API mm_backend_instances(char* name, size_t* ninst, instance*** inst);
int MM_API mm_backend_register(backend b);
