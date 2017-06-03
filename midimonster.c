#include "midimonster.h"
#include "config.h"
#include "backend.h"

//temporary prototypes
int artnet_init();
int midi_init();
int osc_init();

int usage(char* fn){
	fprintf(stderr, "MIDIMonster v0.1\n");
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s <configfile>\n", fn);
	return EXIT_FAILURE;
}

int main(int argc, char** argv){
	int rv = EXIT_FAILURE;
	char* cfg_file = DEFAULT_CFG;
	if(argc > 1){
		cfg_file = argv[1];
	}

	//initialize backends
	//TODO replace this with loading shared objects
	if(artnet_init() /*|| midi_init() || osc_init()*/){
		fprintf(stderr, "Failed to initialize a backend\n");
		goto bail;
	}

	//read config
	if(config_read(cfg_file)){
		fprintf(stderr, "Failed to read configuration file %s\n", cfg_file);
		backends_stop();
		return usage(argv[0]);
	}

	//wait for events
	
	rv = EXIT_SUCCESS;
bail:
	//free all data
	backends_stop();

	return rv;
}
