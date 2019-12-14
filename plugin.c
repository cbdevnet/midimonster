#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "portability.h"
#ifdef _WIN32
#define dlclose FreeLibrary
#define dlsym GetProcAddress
#define dlerror() "Failed"
#define dlopen(lib,ig) LoadLibrary(lib)
#else
#include <dlfcn.h>
#endif

#include "plugin.h"

static size_t plugins = 0;
static void** plugin_handle = NULL;

static int plugin_attach(char* path, char* file){
	plugin_init init = NULL;
	void* handle = NULL;
	char* lib = NULL;
	#ifdef _WIN32
	char* path_separator = "\\";
	#else
	char* path_separator = "/";
	#endif

	if(!path || !file || !strlen(path)){
		fprintf(stderr, "Invalid plugin loader path\n");
		return 1;
	}

	lib = calloc(strlen(path) + strlen(file) + 2, sizeof(char));
	if(!lib){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}
	snprintf(lib, strlen(path) + strlen(file) + 2, "%s%s%s",
			path,
			(path[strlen(path) - 1] == path_separator[0]) ? "" : path_separator,
			file);

	handle = dlopen(lib, RTLD_NOW);
	if(!handle){
		#ifdef _WIN32
		char* error = NULL;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error, 0, NULL);
		fprintf(stderr, "Failed to load plugin %s: %s\n", lib, error);
		LocalFree(error);
		#else
		fprintf(stderr, "Failed to load plugin %s: %s\n", lib, dlerror());
		#endif
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
		dlclose(handle);
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

#ifdef _WIN32
	char* search_expression = calloc(strlen(path) + strlen("*.dll") + 1, sizeof(char));
	if(!search_expression){
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}
	snprintf(search_expression, strlen(path) + strlen("*.dll"), "%s*.dll", path);

	WIN32_FIND_DATA result;
	HANDLE hSearch = FindFirstFile(search_expression, &result);

	if(hSearch == INVALID_HANDLE_VALUE){
		LPVOID lpMsgBuf = NULL;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL);
		fprintf(stderr, "Failed to search for backend plugin files in %s: %s\n", path, lpMsgBuf);
		LocalFree(lpMsgBuf);
		return -1;
	}

	do {
		if(plugin_attach(path, result.cFileName)){
			goto load_done;
		}
	} while(FindNextFile(hSearch, &result));

	rv = 0;
load_done:
	free(search_expression);
	FindClose(hSearch);
	return rv;
#else
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
#endif
}

int plugins_close(){
	size_t u;

	for(u = 0; u < plugins; u++){
#ifdef _WIN32
		char* error = NULL;
		//FreeLibrary returns the inverse of dlclose
		if(!FreeLibrary(plugin_handle[u])){
			FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error, 0, NULL);
			fprintf(stderr, "Failed to unload plugin: %s\n", error);
			LocalFree(error);
		}
#else
		if(dlclose(plugin_handle[u])){
			fprintf(stderr, "Failed to unload plugin: %s\n", dlerror());
		}
#endif
	}

	free(plugin_handle);
	plugins = 0;
	return 0;
}
