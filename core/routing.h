/* Internal API */
int mm_map_channel(channel* from, channel* to);
int routing_iteration();
void routing_stats();
void routing_cleanup();

/* Public backend API */
MM_API int mm_channel_event(channel* c, channel_value v);

