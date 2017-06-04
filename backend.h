backend* backend_match(char* name);
instance* instance_match(char* name);
int backends_start();
int backends_stop();
void instances_free();
void channels_free();
