#include <string.h>
#include "midimonster.h"
#include "config.h"
#include "backend.h"

//temporary prototypes
int artnet_init();
int midi_init();
int osc_init();

static size_t mappings = 0;
static channel_mapping* map = NULL;

int mm_map_channel(channel* from, channel* to){
	size_t u, m;
	//find existing source mapping
	for(u = 0; u < mappings; u++){
		if(map[u].from == from){
			break;
		}
	}

	//create new entry
	if(u == mappings){
		map = realloc(map, (mappings + 1) * sizeof(channel_mapping));
		if(!map){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		memset(map + mappings, 0, sizeof(channel_mapping));
		mappings++;
	}

	//check whether the target is already mapped
	for(m = 0; m < map[u].destinations; m++){
		if(map[u].to[m] == to){
			return 0;
		}
	}

	map[u].to = realloc(map[u].to, (map[u].destinations + 1) * sizeof(channel*));
	if(!map[u].to){
		fprintf(stderr, "Failed to allocate memory\n");
		map[u].destinations = 0;
		return 1;
	}

	map[u].to[map[u].destinations] = to;
	map[u].destinations++;
	return 0;
}

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
	if(artnet_init() || midi_init() /* || osc_init()*/){
		fprintf(stderr, "Failed to initialize a backend\n");
		goto bail;
	}

	//read config
	if(config_read(cfg_file)){
		fprintf(stderr, "Failed to read configuration file %s\n", cfg_file);
		backends_stop();
		instances_free();
		return usage(argv[0]);
	}

	//start backends
	if(backends_start()){
		fprintf(stderr, "Failed to start backends\n");
		goto bail;
	}

	//TODO wait for & translate events
	
	rv = EXIT_SUCCESS;
bail:
	//free all data
	backends_stop();
	channels_free();
	instances_free();

	return rv;
}
