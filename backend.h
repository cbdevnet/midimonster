#include <sys/types.h>

int backends_handle(size_t nfds, managed_fd* fds);
int backends_notify(size_t nev, channel* c, channel_value* v);

backend* backend_match(char* name);
instance* instance_match(char* name);
struct timeval backend_timeout();
int backends_start();
int backends_stop();
void instances_free();
void channels_free();
