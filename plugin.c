#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "plugin.h"

size_t plugins = 0;
void** plugin_handle = NULL;

static int plugin_attach(char* path, char* file){
	plugin_init init = NULL;
	void* handle = NULL;

	char* lib = calloc(strlen(path) + strlen(file) + 1, sizeof(char));
	if(!lib){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	snprintf(lib, strlen(path) + strlen(file) + 1, "%s%s", path, file);

	handle = dlopen(lib, RTLD_NOW);
	if(!handle){
		fprintf(stderr, "Failed to load plugin %s: %s\n", lib, dlerror());
		free(lib);
		return 0;
	}

	init = (plugin_init) dlsym(handle, "init");
	if(init){
		if(init()){
			fprintf(stderr, "Plugin %s failed to initialize\n", lib);
			dlclose(handle);
			free(lib);
			return 1;
		}
	}
	else{
		free(lib);
		return 0;
	}
	free(lib);

	plugin_handle = realloc(plugin_handle, (plugins + 1) * sizeof(void*));
	if(!plugin_handle){
		fprintf(stderr, "Failed to allocate memory\n");
		dlclose(handle);
		return 1;
	}

	plugin_handle[plugins] = handle;
	plugins++;

	return 0;
}

int plugins_load(char* path){
	int rv = -1;

	struct dirent* entry;
	struct stat file_stat;
	DIR* directory = opendir(path);
	if(!directory){
		fprintf(stderr, "Failed to open plugin search path %s: %s\n", path, strerror(errno));
		return 1;
	}

	for(entry = readdir(directory); entry; entry = readdir(directory)){
		if(strlen(entry->d_name) < 4 || strncmp(".so", entry->d_name + (strlen(entry->d_name) - 3), 3)){
			continue;
		}

		if(fstatat(dirfd(directory), entry->d_name, &file_stat, 0) < 0){
			fprintf(stderr, "Failed to stat %s: %s\n", entry->d_name, strerror(errno));
			continue;
		}

		if(!S_ISREG(file_stat.st_mode)){
			continue;
		}

		if(!(file_stat.st_mode & S_IXUSR)){
			continue;
		}

		if(plugin_attach(path, entry->d_name)){
			goto load_done;
		}
	}
	rv = 0;

load_done:
	if(closedir(directory) < 0){
		fprintf(stderr, "Failed to close plugin directory %s: %s\n", path, strerror(errno));
		return -1;
	}
	return rv;
}

int plugins_close(){
	size_t u;
	for(u = 0; u < plugins; u++){
		if(dlclose(plugin_handle[u])){
			fprintf(stderr, "Failed to unload plugin: %s\n", dlerror());
		}
	}

	free(plugin_handle);
	plugins = 0;
	return 0;
}
