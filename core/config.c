#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#ifndef _WIN32
#include <limits.h>
#endif

#define BACKEND_NAME "core/cfg"
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
static size_t noverrides = 0;
static config_override* overrides = NULL;

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
			LOG("Failed to allocate memory");
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
				LOG("Failed to allocate memory");
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
		if(ferror(stream)){
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

static int config_glob_parse_range(channel_glob* glob, char* spec, size_t length){
	//FIXME might want to allow negative delimiters at some point
	char* parse_offset = NULL;
	glob->type = glob_range;

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

static int config_glob_parse_list(channel_glob* glob, char* spec, size_t length){
	size_t u = 0;
	glob->type = glob_list;
	glob->values = 1;

	//count number of values in list
	for(u = 0; u < length; u++){
		if(spec[u] == ','){
			glob->values++;
		}
	}
	return 0;
}

static int config_glob_parse(channel_glob* glob, char* spec, size_t length){
	size_t u = 0;

	//detect glob type
	for(u = 0; u < length; u++){
		if(length - u > 2 && !strncmp(spec + u, "..", 2)){
			DBGPF("Detected glob %.*s as range type", (int) length, spec);
			return config_glob_parse_range(glob, spec, length);
		}
		else if(spec[u] == ','){
			DBGPF("Detected glob %.*s as list type", (int) length, spec);
			return config_glob_parse_list(glob, spec, length);
		}
	}

	LOGPF("Failed to detect glob type for spec %.*s", (int) length, spec);
	return 1;
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
			LOGPF("Failed to parse channel spec, unterminated glob: %s", spec->spec);
			return 1;
		}

		spec->glob = realloc(spec->glob, (spec->globs + 1) * sizeof(channel_glob));
		if(!spec->glob){
			LOG("Failed to allocate memory");
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
		LOGPF("Failed to parse glob %" PRIsize_t " in %s internally", u + 1, spec->spec);
		return 1;
	}

	//calculate channel total
	for(u = 0; u < spec->globs; u++){
		spec->channels *= spec->glob[u].values;
	}
	return 0;
}

static ssize_t config_glob_resolve_range(char* spec, size_t length, channel_glob* glob, uint64_t n){
	uint64_t current_value = glob->limits.u64[0] + (n % glob->values);
	//if counting down
	if(glob->limits.u64[0] > glob->limits.u64[1]){
		current_value = glob->limits.u64[0] - (n % glob->values);
	}

	//write out value
	return snprintf(spec, length, "%" PRIu64, current_value);
}

static ssize_t config_glob_resolve_list(char* spec, size_t length, channel_glob* glob, uint64_t n){
	uint64_t current_replacement = 0;
	size_t replacement_length = 0;
	char* source = spec + 1;
	n %= glob->values;

	//find start of replacement value
	DBGPF("Searching instance %" PRIu64 " of spec %.*s", n, (int) length, spec);
	for(current_replacement = 0; current_replacement < n; current_replacement++){
		for(; source[0] != ','; source++){
		}
		source++;
	}

	//calculate replacement length
	for(; source[replacement_length] != ',' && source[replacement_length] != '}'; replacement_length++){
	}

	//write out new value
	memmove(spec, source, replacement_length);
	return replacement_length;
}

static channel* config_glob_resolve(instance* inst, channel_spec* spec, uint64_t n, uint8_t map_direction){
	size_t glob = 0, glob_length;
	ssize_t bytes = 0;
	channel* result = NULL;
	char* resolved_spec = strdup(spec->spec);

	if(!resolved_spec){
		LOG("Failed to allocate memory");
		return NULL;
	}

	//TODO if not internal, try to resolve externally
	//iterate and resolve globs
	for(glob = spec->globs; glob > 0; glob--){
		glob_length = spec->glob[glob - 1].offset[1] - spec->glob[glob - 1].offset[0];

		switch(spec->glob[glob - 1].type){
			case glob_range:
				bytes = config_glob_resolve_range(resolved_spec + spec->glob[glob - 1].offset[0],
						glob_length,
						spec->glob + (glob - 1),
						n);
				break;
			case glob_list:
				bytes = config_glob_resolve_list(resolved_spec + spec->glob[glob - 1].offset[0],
						glob_length,
						spec->glob + (glob - 1),
						n);
				break;
		}

		n /= spec->glob[glob - 1].values;

		//move trailing data
		if(bytes > 0 && bytes < glob_length){
			memmove(resolved_spec + spec->glob[glob - 1].offset[0] + bytes,
					resolved_spec + spec->glob[glob - 1].offset[1] + 1,
					strlen(spec->spec) - spec->glob[glob - 1].offset[1]);
		}
		else{
			LOGPF("Failure parsing glob spec %s", resolved_spec);
			goto bail;
		}
	}

	DBGPF("Resolved spec %s to %s", spec->spec, resolved_spec);
	result = inst->backend->channel(inst, resolved_spec, map_direction);
	if(spec->globs && !result){
		LOGPF("Failed to match multichannel evaluation %s to a channel", resolved_spec);
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
		LOG("Failed to allocate memory");
		return 1;
	}

	//separate channel spec from instance
	for(; *(spec_to.spec) && *(spec_to.spec) != '.'; spec_to.spec++){
	}

	for(; *(spec_from.spec) && *(spec_from.spec) != '.'; spec_from.spec++){
	}

	if(!spec_from.spec[0] || !spec_to.spec[0]){
		LOG("Mapping does not contain a proper instance specification");
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
		LOGPF("No such instance %s", instance_from ? to : from);
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
		LOGPF("Multi-channel specification size mismatch: %s.%s (%" PRIsize_t " channels) - %s.%s (%" PRIsize_t " channels)",
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

static int config_line(char* line){
	map_type mapping_type = map_rtl;
	char* separator = NULL;
	size_t u;

	line = config_trim_line(line);
	if(*line == ';' || strlen(line) == 0){
		//skip comments
		return 0;
	}
	if(*line == '[' && line[strlen(line) - 1] == ']'){
		if(!strncmp(line, "[backend ", 9)){
			//backend configuration
			parser_state = backend_cfg;
			line[strlen(line) - 1] = 0;
			current_backend = backend_match(line + 9);

			if(!current_backend){
				LOGPF("Cannot configure unknown backend %s", line + 9);
				return 1;
			}

			//apply overrides
			for(u = 0; u < noverrides; u++){
				if(!overrides[u].handled && overrides[u].type == override_backend
					       && !strcmp(overrides[u].target, current_backend->name)){
					if(current_backend->conf(overrides[u].option, overrides[u].value)){
						LOGPF("Configuration override for %s failed for backend %s",
								overrides[u].option, current_backend->name);
						return 1;
					}
					overrides[u].handled = 1;
				}
			}
		}
		else if(!strncmp(line, "[include ", 9)){
			line[strlen(line) - 1] = 0;
			return config_read(line + 9);
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
				LOGPF("No instance name specified for backend %s", line);
				return 1;
			}
			*separator = 0;
			separator++;

			current_backend = backend_match(line);
			if(!current_backend){
				LOGPF("No such backend %s", line);
				return 1;
			}

			if(instance_match(separator)){
				LOGPF("Duplicate instance name %s", separator);
				return 1;
			}

			//validate instance name
			if(strchr(separator, ' ') || strchr(separator, '.')){
				LOGPF("Invalid instance name %s", separator);
				return 1;
			}

			current_instance = mm_instance(current_backend);
			if(!current_instance){
				return 1;
			}

			if(current_backend->create(current_instance)){
				LOGPF("Failed to create %s instance %s", line, separator);
				return 1;
			}

			current_instance->name = strdup(separator);
			current_instance->backend = current_backend;
			LOGPF("Created %s instance %s", line, separator);

			//apply overrides
			for(u = 0; u < noverrides; u++){
				if(!overrides[u].handled && overrides[u].type == override_instance
					       && !strcmp(overrides[u].target, current_instance->name)){
					if(current_backend->conf_instance(current_instance, overrides[u].option, overrides[u].value)){
						LOGPF("Configuration override for %s failed for instance %s",
								overrides[u].option, current_instance->name);
						return 1;
					}
					overrides[u].handled = 1;
				}
			}
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
				LOGPF("Not a channel mapping: %s", line);
				return 1;
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
				LOGPF("Failed to map channel %s to %s", line, separator);
				return 1;
			}
		}
		if(mapping_type == map_rtl || mapping_type == map_bidir){
			if(config_map(line, separator)){
				LOGPF("Failed to map channel %s to %s", separator, line);
				return 1;
			}
		}
	}
	else{
		//pass to parser
		//find separator
		separator = strchr(line, '=');
		if(!separator){
			LOGPF("Not an assignment (currently expecting %s configuration): %s", line, (parser_state == backend_cfg) ? "backend" : "instance");
			return 1;
		}

		*separator = 0;
		separator++;
		line = config_trim_line(line);
		separator = config_trim_line(separator);

		if(parser_state == backend_cfg && current_backend->conf(line, separator)){
			LOGPF("Failed to configure backend %s", current_backend->name);
			return 1;
		}
		else if(parser_state == instance_cfg && current_backend->conf_instance(current_instance, line, separator)){
			LOGPF("Failed to configure instance %s", current_instance->name);
			return 1;
		}
	}

	return 0;
}

int config_read(char* cfg_filepath){
	int rv = 1;
	size_t line_alloc = 0;
	ssize_t status;
	FILE* source = NULL;
	char* line_raw = NULL;

	//create heap copy of file name because original might be in readonly memory
	char* source_dir = strdup(cfg_filepath), *source_file = NULL, original_dir[PATH_MAX * 2] = "";
	#ifdef _WIN32
	char path_separator = '\\';
	#else
	char path_separator = '/';
	#endif

	if(!source_dir){
		LOG("Failed to allocate memory");
		return 1;
	}

	//change working directory to the one containing the configuration file so relative paths work as expected
	source_file = strrchr(source_dir, path_separator);
	if(source_file){
		*source_file = 0;
		source_file++;

		if(!getcwd(original_dir, sizeof(original_dir))){
			LOGPF("Failed to read current working directory: %s", strerror(errno));
			goto bail;
		}

		if(chdir(source_dir)){
			LOGPF("Failed to change to configuration file directory %s: %s", source_dir, strerror(errno));
			goto bail;
		}
	}
	else{
		source_file = source_dir;
	}

	LOGPF("Reading configuration file %s", cfg_filepath);
	source = fopen(source_file, "r");

	if(!source){
		LOGPF("Failed to open %s for reading", cfg_filepath);
		goto bail;
	}

	for(status = getline(&line_raw, &line_alloc, source); status >= 0; status = getline(&line_raw, &line_alloc, source)){
		if(config_line(line_raw)){
			goto bail;
		}
	}

	//TODO check whether all overrides have been applied

	rv = 0;
bail:
	//change back to previous directory to allow recursive configuration file parsing
	if(source_file && source_dir != source_file){
		chdir(original_dir);
	}

	free(source_dir);
	if(source){
		fclose(source);
	}
	free(line_raw);
	return rv;
}

int config_add_override(override_type type, char* data_raw){
	int rv = 1;
	//heap a copy because the original data is probably not writable
	char* data = strdup(data_raw);

	if(!data){
		LOG("Failed to allocate memory");
		goto bail;
	}

	char* option = strchr(data, '.');
	char* value = strchr(data, '=');

	if(!option || !value){
		LOGPF("Override %s is not a valid assignment", data_raw);
		goto bail;
	}

	//terminate strings
	*option = 0;
	option++;

	*value = 0;
	value++;

	config_override new = {
		.type = type,
		.handled = 0,
		.target = strdup(config_trim_line(data)),
		.option = strdup(config_trim_line(option)),
		.value = strdup(config_trim_line(value))
	};

	if(!new.target || !new.option || !new.value){
		LOG("Failed to allocate memory");
		goto bail;
	}

	overrides = realloc(overrides, (noverrides + 1) * sizeof(config_override));
	if(!overrides){
		noverrides = 0;
		LOG("Failed to allocate memory");
		goto bail;
	}
	overrides[noverrides] = new;
	noverrides++;

	rv = 0;
bail:
	free(data);
	return rv;
}

void config_free(){
	size_t u;

	for(u = 0; u < noverrides; u++){
		free(overrides[u].target);
		free(overrides[u].option);
		free(overrides[u].value);
	}

	noverrides = 0;
	free(overrides);
	overrides = NULL;

	parser_state = none;
}
