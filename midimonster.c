#include <string.h>
#include <signal.h>
#include <stdarg.h>
#ifndef _WIN32
	#define MM_API __attribute__((visibility("default")))
#else
	#define MM_API __attribute__((dllexport))
#endif

#define BACKEND_NAME "cli"
#include "midimonster.h"
#include "core/core.h"
#include "core/config.h"

volatile static sig_atomic_t shutdown_requested = 0;

MM_API int log_printf(int level, char* module, char* fmt, ...){
	int rv = 0;
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "%s%s\t", level ? "debug/" : "", module);
	rv = vfprintf(stderr, fmt, args);
	va_end(args);
	return rv;
}

static void signal_handler(int signum){
	shutdown_requested = 1;
}

static void version(){
	printf("MIDIMonster %s\n", MIDIMONSTER_VERSION);
}

static int usage(char* fn){
	version();
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s <configfile>\n", fn);
	return EXIT_FAILURE;
}

static int platform_initialize(){
	#ifdef _WIN32
	unsigned error_mode = SetErrorMode(0);
	SetErrorMode(error_mode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	#endif
	return 0;
}

static int platform_shutdown(){
	#ifdef _WIN32
	DWORD processes;
	if(GetConsoleProcessList(&processes, 1) == 1){
		fprintf(stderr, "\nMIDIMonster is the last process in this console, please press any key to exit\n");
		HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
		SetConsoleMode(input, 0);
		FlushConsoleInputBuffer(input);
		WaitForSingleObject(input, INFINITE);
	}
	#endif
	return 0;
}

static int args_parse(int argc, char** argv, char** cfg_file){
	size_t u;
	for(u = 1; u < argc; u++){
		if(!strcmp(argv[u], "-v") || !strcmp(argv[u], "--version")){
			version();
			return 1;
		}
		else if(!strcmp(argv[u], "-i")){
			if(!argv[u + 1]){
				fprintf(stderr, "Missing instance override specification\n");
				return 1;
			}
			if(config_add_override(override_instance, argv[u + 1])){
				return 1;
			}
			u++;
		}
		else if(!strcmp(argv[u], "-b")){
			if(!argv[u + 1]){
				fprintf(stderr, "Missing backend override specification\n");
				return 1;
			}
			if(config_add_override(override_backend, argv[u + 1])){
				return 1;
			}
			u++;
		}
		else{
			//if nothing else matches, it's probably the configuration file
			*cfg_file = argv[u];
		}
	}

	return 0;
}

int main(int argc, char** argv){
	int rv = EXIT_FAILURE;
	char* cfg_file = DEFAULT_CFG;

	//parse commandline arguments
	if(args_parse(argc, argv, &cfg_file)){
		return EXIT_FAILURE;
	}

	version();
	if(platform_initialize()){
		fprintf(stderr, "Failed to perform platform-specific initialization\n");
		return EXIT_FAILURE;
	}

	//initialize backends
	if(core_initialize()){
		goto bail;
	}

	//read config
	if(config_read(cfg_file)){
		fprintf(stderr, "Failed to parse master configuration file %s\n", cfg_file);
		core_shutdown();
		return (usage(argv[0]) | platform_shutdown());
	}

	//start core
	if(core_start()){
		goto bail;
	}

	signal(SIGINT, signal_handler);

	//run the core loop
	while(!shutdown_requested){
		if(core_iteration()){
			goto bail;
		}
	}

	rv = EXIT_SUCCESS;
bail:
	core_shutdown();
	return rv;
}
