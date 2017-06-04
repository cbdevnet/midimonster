#include <string.h>
#include <ctype.h>
#include "midimonster.h"
#include "config.h"
#include "backend.h"

enum {
	none,
	backend_cfg,
	instance_cfg,
	map
} parser_state = none;

backend* current_backend = NULL;
instance* current_instance = NULL;

static char* config_trim_line(char* in){
	ssize_t u;
	//trim front
	for(; *in && !isgraph(*in); in++){
	}

	//trim back
	for(u = strlen(in); u >= 0 && !isgraph(in[u]); u--){
		in[u] = 0;
	}

	return in;
}

int config_read(char* cfg_file){
	int rv = 1;
	size_t line_alloc = 0;
	ssize_t status;
	char* line_raw = NULL, *line, *separator;
	FILE* source = fopen(cfg_file, "r");
	if(!source){
		fprintf(stderr, "Failed to open configuration file for reading\n");
		return 1;
	}

	for(status = getline(&line_raw, &line_alloc, source); status >= 0; status = getline(&line_raw, &line_alloc, source)){
		line = config_trim_line(line_raw);
		if(*line == ';' || strlen(line) == 0){
			//skip comments
			continue;
		}
		if(*line == '[' && line[strlen(line) - 1] == ']'){
			if(!strncmp(line, "[backend ", 9)){
				//backend configuration
				parser_state = backend_cfg;
				line[strlen(line) - 1] = 0;
				current_backend = backend_match(line + 9);

				if(!current_backend){
					fprintf(stderr, "Cannot configure unknown backend %s\n", line + 9);
					goto bail;
				}
			}
			else if(!strcmp(line, "[map]")){
				//mapping configuration
				parser_state = map;
			}
			else{
				//backend instance configuration
				parser_state = instance_cfg;
				
				//trim braces
				line[strlen(line) - 1] = 0;
				line++;

				//find separating space and terminate
				for(separator = line; *separator && *separator != ' '; separator++){
				}
				if(!*separator){
					fprintf(stderr, "No instance name specified for backend %s\n", line);
					goto bail;
				}
				*separator = 0;
				separator++;

				current_backend = backend_match(line);
				if(!current_backend){
					fprintf(stderr, "No such backend %s\n", line);
					goto bail;
				}

				if(instance_match(separator)){
					fprintf(stderr, "Duplicate instance name %s\n", separator);
					goto bail;
				}

				current_instance = current_backend->create();
				if(!current_instance){
					fprintf(stderr, "Failed to instantiate backend %s\n", line);
					goto bail;
				}

				current_instance->name = strdup(separator);
				current_instance->backend = current_backend;
				fprintf(stderr, "Created %s instance %s\n", line, separator);
			}
		}
		else{
			//pass to parser
			//find separator
			for(separator = line; *separator && *separator != '='; separator++){
			}

			if(!*separator){
				fprintf(stderr, "No assignment: %s\n", line);
				goto bail;
			}

			*separator = 0;
			separator++;

			if(parser_state == backend_cfg && current_backend->conf(config_trim_line(line), config_trim_line(separator))){
				fprintf(stderr, "Failed to configure backend %s\n", current_backend->name);
				goto bail;
			}
			else if(parser_state == instance_cfg && current_backend->conf_instance(current_instance, config_trim_line(line), config_trim_line(separator))){
				fprintf(stderr, "Failed to configure instance %s\n", current_instance->name);
				goto bail;
			}
			else if(parser_state == map){
				//TODO map two channels
			}
		}
	}

	rv = 0;
bail:
	fclose(source);
	free(line_raw);
	return rv;
}
