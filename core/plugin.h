typedef int (*plugin_init)();
int plugins_load(char* dir);
int plugins_close();
