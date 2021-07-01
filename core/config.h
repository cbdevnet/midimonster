/*
 * Channel glob type
 */
enum /*_mm_channel_glob_type */ {
	glob_range,
	glob_list
};

/*
 * Channel specification glob
 */
typedef struct /*_mm_channel_glob*/ {
	size_t offset[2];
	union {
		void* impl;
		uint64_t u64[2];
	} limits;
	uint8_t type;
	uint64_t values;
} channel_glob;

/*
 * (Multi-)Channel specification
 */
typedef struct /*_mm_channel_spec*/ {
	char* spec;
	uint8_t internal;
	size_t channels;
	size_t globs;
	channel_glob* glob;
} channel_spec;

/*
 * Command-line override types
 */
typedef enum {
	override_backend,
	override_instance
} override_type;

/*
 * Command-line override data
 */
typedef struct /*_mm_config_override*/ {
	override_type type;
	uint8_t handled;
	char* target;
	char* option;
	char* value;
} config_override;

/* Internal API */
void config_free();

/* Frontent API */
int config_read(char* file);
int config_add_override(override_type type, char* data);
