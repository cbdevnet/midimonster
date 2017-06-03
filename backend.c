#include <string.h>
#include "midimonster.h"
#include "backend.h"

static size_t nbackends = 0;
static backend* backends = NULL;

backend* backend_match(char* name){
	size_t u;
	for(u = 0; u < nbackends; u++){
		if(!strcmp(backends[u].name, name)){
			return backends + u;
		}
	}
	return NULL;
}

backend* mm_backend_register(backend b){
	if(!backend_match(b.name)){
		backends = realloc(backends, (nbackends + 1) * sizeof(backend));
		if(!backends){
			fprintf(stderr, "Failed to allocate memory\n");
			nbackends = 0;
			return NULL;
		}
		backends[nbackends] = b;
		nbackends++;

		fprintf(stderr, "Registered backend %s\n", b.name);
		return backends + (nbackends - 1);
	}
	return NULL;
}

int backends_stop(){
	size_t u;
	for(u = 0; u < nbackends; u++){
		backends[u].shutdown();
	}
	free(backends);
	nbackends = 0;
	return 0;
}
