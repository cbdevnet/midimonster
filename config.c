#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include "midimonster.h"
#include "config.h"
#include "backend.h"

static enum {
	none,
	backend_cfg,
	instance_cfg,
	map
} parser_state = none;

typedef enum {
	map_ltr,
	map_rtl,
	map_bidir
} map_type;

static backend* current_backend = NULL;
static instance* current_instance = NULL;

#ifdef _WIN32
#define GETLINE_BUFFER 4096

static ssize_t getline(char** line, size_t* alloc, FILE* stream){
	size_t bytes_read = 0;
	char c;
	//sanity checks
	if(!line || !alloc || !stream){
		return -1;
	}

	//allocate buffer if none provided
	if(!*line || !*alloc){
		*alloc = GETLINE_BUFFER;
		*line = calloc(GETLINE_BUFFER, sizeof(char));
		if(!*line){
			fprintf(stderr, "Failed to allocate memory\n");
			return -1;
		}
	}

	if(feof(stream)){
		return -1;
	}

	for(c = fgetc(stream); 1; c = fgetc(stream)){
		//end of buffer, resize
		if(bytes_read == (*alloc) - 1){
			*alloc += GETLINE_BUFFER;
			*line = realloc(*line, (*alloc) * sizeof(char));
			if(!*line){
				fprintf(stderr, "Failed to allocate memory\n");
				return -1;
			}
		}

		//store character
		(*line)[bytes_read] = c;

		//end of line
		if(feof(stream) || c == '\n'){
			//terminate string
			(*line)[bytes_read + 1] = 0;
			return bytes_read;
		}

		//input broken
		if(ferror(stream) || c < 0){
			return -1;
		}

		bytes_read++;
	}
}
#endif

static char* config_trim_line(char* in){
	ssize_t n;
	//trim front
	for(; *in && !isgraph(*in); in++){
	}

	//trim back
	for(n = strlen(in); n >= 0 && !isgraph(in[n]); n--){
		in[n] = 0;
	}

	return in;
}

static int config_glob_parse(channel_glob* glob, char* spec, size_t length){
	char* parse_offset = NULL;
	//FIXME might want to allow negative delimiters at some point

	//first interval member
	glob->limits.u64[0] = strtoul(spec, &parse_offset, 10);
	if(!parse_offset || parse_offset - spec >= length || strncmp(parse_offset, "..", 2)){
		return 1;
	}

	parse_offset += 2;
	//second interval member
	glob->limits.u64[1] = strtoul(parse_offset, &parse_offset, 10);
	if(!parse_offset || parse_offset - spec != length || *parse_offset != '}'){
		return 1;
	}

	//calculate number of channels within interval
	if(glob->limits.u64[0] < glob->limits.u64[1]){
		glob->values = glob->limits.u64[1] - glob->limits.u64[0] + 1;
	}
	else if(glob->limits.u64[0] > glob->limits.u64[1]){
		glob->values = glob->limits.u64[0] - glob->limits.u64[1] + 1;
	}
	else{
		glob->values = 1;
	}

	return 0;
}

static int config_glob_scan(instance* inst, channel_spec* spec){
	char* glob_start = spec->spec, *glob_end = NULL;
	size_t u;

	//assume a spec is one channel as default
	spec->channels = 1;

	//scan and mark globs
	for(glob_start = strchr(glob_start, '{'); glob_start; glob_start = strchr(glob_start, '{')){
		glob_end = strchr(glob_start, '}');
		if(!glob_end){
			fprintf(stderr, "Failed to parse channel spec, unterminated glob: %s\n", spec->spec);
			return 1;
		}

		spec->glob = realloc(spec->glob, (spec->globs + 1) * sizeof(channel_glob));
		if(!spec->glob){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}

		spec->glob[spec->globs].offset[0] = glob_start - spec->spec;
		spec->glob[spec->globs].offset[1] = glob_end - spec->spec;
		spec->globs++;

		//skip this opening brace
		glob_start++;
	}

	//try to parse globs internally
	spec->internal = 1;
	for(u = 0; u < spec->globs; u++){
		if(config_glob_parse(spec->glob + u,
					spec->spec + spec->glob[u].offset[0] + 1,
					spec->glob[u].offset[1] - spec->glob[u].offset[0] - 1)){
			spec->internal = 0;
			break;
		}
	}
	if(!spec->internal){
		//TODO try to parse globs externally
		fprintf(stderr, "Failed to parse glob %" PRIsize_t " in %s internally\n", u + 1, spec->spec);
		return 1;
	}

	//calculate channel total
	for(u = 0; u < spec->globs; u++){
		spec->channels *= spec->glob[u].values;
	}
	return 0;
}

static channel* config_glob_resolve(instance* inst, channel_spec* spec, uint64_t n, uint8_t map_direction){
	size_t glob = 0, glob_length;
	ssize_t bytes = 0;
	uint64_t current_value = 0;
	channel* result = NULL;
	char* resolved_spec = strdup(spec->spec);

	if(!resolved_spec){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	//TODO if not internal, try to resolve externally

	//iterate and resolve globs
	for(glob = spec->globs; glob > 0; glob--){
		current_value = spec->glob[glob - 1].limits.u64[0] + (n % spec->glob[glob - 1].values);
		if(spec->glob[glob - 1].limits.u64[0] > spec->glob[glob - 1].limits.u64[1]){
			current_value = spec->glob[glob - 1].limits.u64[0] - (n % spec->glob[glob - 1].values);
		}
		glob_length = spec->glob[glob - 1].offset[1] - spec->glob[glob - 1].offset[0];
		n /= spec->glob[glob - 1].values;

		//write out value
		bytes = snprintf(resolved_spec + spec->glob[glob - 1].offset[0],
				glob_length,
				"%" PRIu64,
				current_value);
		if(bytes > glob_length){
			fprintf(stderr, "Internal error resolving glob %s\n", spec->spec);
			goto bail;
		}

		//move trailing data
		if(bytes < glob_length){
			memmove(resolved_spec + spec->glob[glob - 1].offset[0] + bytes,
					resolved_spec + spec->glob[glob - 1].offset[1] + 1,
					strlen(spec->spec) - spec->glob[glob - 1].offset[1]);
		}
	}

	result = inst->backend->channel(inst, resolved_spec, map_direction);
	if(spec->globs && !result){
		fprintf(stderr, "Failed to match multichannel evaluation %s to a channel\n", resolved_spec);
	}

bail:
	free(resolved_spec);
	return result;
}

static int config_map(char* to_raw, char* from_raw){
	//create a copy because the original pointer may be used multiple times
	char* to = strdup(to_raw), *from = strdup(from_raw);
	channel_spec spec_to = {
		.spec = to
	}, spec_from = {
		.spec = from
	};
	instance* instance_to = NULL, *instance_from = NULL;
	channel* channel_from = NULL, *channel_to = NULL;
	uint64_t n = 0;
	int rv = 1;

	if(!from || !to){
		free(from);
		free(to);
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//separate channel spec from instance
	for(; *(spec_to.spec) && *(spec_to.spec) != '.'; spec_to.spec++){
	}

	for(; *(spec_from.spec) && *(spec_from.spec) != '.'; spec_from.spec++){
	}

	if(!spec_from.spec[0] || !spec_to.spec[0]){
		fprintf(stderr, "Mapping does not contain a proper instance specification\n");
		goto done;
	}

	//terminate
	spec_from.spec[0] = spec_to.spec[0] = 0;
	spec_from.spec++;
	spec_to.spec++;

	//find matching instances
	instance_to = instance_match(to);
	instance_from = instance_match(from);

	if(!instance_to || !instance_from){
		fprintf(stderr, "No such instance %s\n", instance_from ? to : from);
		goto done;
	}

	//scan for globs
	if(config_glob_scan(instance_to, &spec_to)
			|| config_glob_scan(instance_from, &spec_from)){
		goto done;
	}
	
	if((spec_to.channels != spec_from.channels && spec_from.channels != 1 && spec_to.channels != 1)
			|| spec_to.channels == 0
			|| spec_from.channels == 0){
		fprintf(stderr, "Multi-channel specification size mismatch: %s.%s (%" PRIsize_t " channels) - %s.%s (%" PRIsize_t " channels)\n",
				instance_from->name,
				spec_from.spec,
				spec_from.channels,
				instance_to->name,
				spec_to.spec,
				spec_to.channels);
		goto done;
	}

	//iterate, resolve globs and map
	rv = 0;
	for(n = 0; !rv && n < max(spec_from.channels, spec_to.channels); n++){
		channel_from = config_glob_resolve(instance_from, &spec_from, min(n, spec_from.channels), mmchannel_input);
		channel_to = config_glob_resolve(instance_to, &spec_to, min(n, spec_to.channels), mmchannel_output);
		
		if(!channel_from || !channel_to){
			rv = 1;
			goto done;
		}
		rv |= mm_map_channel(channel_from, channel_to);
	}

done:
	free(spec_from.glob);
	free(spec_to.glob);
	free(from);
	free(to);
	return rv;
}

int config_read(char* cfg_filepath){
	int rv = 1;
	size_t line_alloc = 0;
	ssize_t status;
	map_type mapping_type = map_rtl;
	FILE* source = NULL;
	char* line_raw = NULL, *line, *separator;

	//create heap copy of file name because original might be in readonly memory
	char* source_dir = strdup(cfg_filepath), *source_file = NULL;
	#ifdef _WIN32
	char path_separator = '\\';
	#else
	char path_separator = '/';
	#endif

	if(!source_dir){
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	//change working directory to the one containing the configuration file so relative paths work as expected
	source_file = strrchr(source_dir, path_separator);
	if(source_file){
		*source_file = 0;
		source_file++;
		if(chdir(source_dir)){
			fprintf(stderr, "Failed to change to configuration file directory %s: %s\n", source_dir, strerror(errno));
			goto bail;
		}
	}
	else{
		source_file = source_dir;
	}

	source = fopen(source_file, "r");

	if(!source){
		fprintf(stderr, "Failed to open configuration file for reading\n");
		goto bail;
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

				//validate instance name
				if(strchr(separator, ' ') || strchr(separator, '.')){
					fprintf(stderr, "Invalid instance name %s\n", separator);
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
		else if(parser_state == map){
			mapping_type = map_rtl;
			//find separator
			for(separator = line; *separator && *separator != '<' && *separator != '>'; separator++){
			}

			switch(*separator){
				case '>':
					mapping_type = map_ltr;
					//fall through
				case '<': //default
					*separator = 0;
					separator++;
					break;
				case 0:
				default:
					fprintf(stderr, "Not a channel mapping: %s\n", line);
					goto bail;
			}

			if((mapping_type == map_ltr && *separator == '<')
					|| (mapping_type == map_rtl && *separator == '>')){
				mapping_type = map_bidir;
				separator++;
			}

			line = config_trim_line(line);
			separator = config_trim_line(separator);

			if(mapping_type == map_ltr || mapping_type == map_bidir){
				if(config_map(separator, line)){
					fprintf(stderr, "Failed to map channel %s to %s\n", line, separator);
					goto bail;
				}
			}
			if(mapping_type == map_rtl || mapping_type == map_bidir){
				if(config_map(line, separator)){
					fprintf(stderr, "Failed to map channel %s to %s\n", separator, line);
					goto bail;
				}
			}
		}
		else{
			//pass to parser
			//find separator
			separator = strchr(line, '=');
			if(!separator){
				fprintf(stderr, "Not an assignment: %s\n", line);
				goto bail;
			}

			*separator = 0;
			separator++;
			line = config_trim_line(line);
			separator = config_trim_line(separator);

			if(parser_state == backend_cfg && current_backend->conf(line, separator)){
				fprintf(stderr, "Failed to configure backend %s\n", current_backend->name);
				goto bail;
			}
			else if(parser_state == instance_cfg && current_backend->conf_instance(current_instance, line, separator)){
				fprintf(stderr, "Failed to configure instance %s\n", current_instance->name);
				goto bail;
			}
		}
	}

	rv = 0;
bail:
	free(source_dir);
	if(source){
		fclose(source);
	}
	free(line_raw);
	return rv;
}
