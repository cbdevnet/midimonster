#include <string.h>
#include "midimonster.h"
#include "config.h"
#include "backend.h"

//temporary prototypes
int artnet_init();
int midi_init();
int osc_init();

static size_t ninstances = 0;
static instance** instances = NULL;

instance* mm_instance(){
	instance** new_inst = realloc(instances, (ninstances + 1) * sizeof(instance*));
	if(!new_inst){
		//TODO free
		fprintf(stderr, "Failed to allocate memory\n");
		ninstances = 0;
		return NULL;
	}
	instances = new_inst;
	instances[ninstances] = calloc(1, sizeof(instance));
	if(!instances[ninstances]){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	return instances[ninstances++];
}

int mm_backend_instances(char* name, size_t* ninst, instance*** inst){
	backend* b = backend_match(name);
	size_t n = 0, u;
	//count number of affected instances
	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b){
			n++;
		}
	}

	*ninst = n;
	*inst = calloc(n, sizeof(instance*));
	if(!*inst){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	n = 0;
	for(u = 0; u < ninstances; u++){
		if(instances[u]->backend == b){
			(*inst)[n] = instances[u];
			n++;
		}
	}
	return 0;
}

static void instances_free(){
	size_t u;
	for(u = 0; u < ninstances; u++){
		free(instances[u]->name);
		instances[u]->name = NULL;
		instances[u]->backend = NULL;
		free(instances[u]);
		instances[u] = NULL;
	}
	free(instances);
	ninstances = 0;
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

	//wait for events
	
	rv = EXIT_SUCCESS;
bail:
	//free all data
	backends_stop();
	instances_free();

	return rv;
}
