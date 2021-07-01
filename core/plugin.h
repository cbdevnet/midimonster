typedef int (*plugin_init)();

/* Internal API */
int plugins_load(char* dir);
int plugins_close();
