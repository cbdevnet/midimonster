#include "midimonster.h"

int usage(char* fn){
	fprintf(stderr, "MIDIMonster v0.1\n");
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s <configfile>\n", fn);
	return EXIT_FAILURE;
}

int main(int argc, char** argv){
	char* cfg_file = DEFAULT_CFG;
	if(argc > 1){
		cfg_file = argv[1];
	}

	//initialize backends
	//read config
	//wait for events
	//free all data

	return EXIT_SUCCESS;
}
