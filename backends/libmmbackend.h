#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef _WIN32
#include <ws2tcpip.h>
//#define close closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "../portability.h"

/*** BACKEND IMPLEMENTATION LIBRARY ***/

/** Networking functions **/

/* 
 * Parse spec as host specification in the form
 *	host port [options]
 * into its constituent parts.
 * Returns offsets into the original string and modifies it.
 * Returns NULL in *port if none given.
 * Returns NULL in both *port and *host if spec was an empty string.
 * Returns a pointer after the port in *options if options is non-NULL
 * and the port was not followed by \0
 */
void mmbackend_parse_hostspec(char* spec, char** host, char** port, char** options);

/* 
 * Parse a given host / port combination into a sockaddr_storage
 * suitable for usage with connect / sendto
 * Returns 0 on success
 */
int mmbackend_parse_sockaddr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len);

/* 
 * Create a socket of given type and mode for a bind / connect host.
 * Returns -1 on failure, a valid file descriptor for the socket on success.
 */
int mmbackend_socket(char* host, char* port, int socktype, uint8_t listener, uint8_t mcast);

/*
 * Send arbitrary data over multiple writes if necessary
 * Returns 1 on failure, 0 on success.
 */
int mmbackend_send(int fd, uint8_t* data, size_t length);

/*
 * Wraps mmbackend_send for cstrings
 */
int mmbackend_send_str(int fd, char* data);


/** JSON parsing **/

typedef enum /*_json_types*/ {
	JSON_INVALID = 0,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
	JSON_NUMBER,
	JSON_BOOL,
	JSON_NULL
} json_type;

/* 
 * Try to identify the type of JSON data next in the buffer
 * Will access at most the next `length` bytes
 */
json_type json_identify(char* json, size_t length);

/* 
 * Validate that a buffer contains a valid JSON document/data within `length` bytes
 * Returns the length of a detected JSON document, 0 otherwise (ie. parse failures)
 */
size_t json_validate(char* json, size_t length);
size_t json_validate_string(char* json, size_t length);
size_t json_validate_array(char* json, size_t length);
size_t json_validate_object(char* json, size_t length);
size_t json_validate_value(char* json, size_t length);

/* 
 * Calculate offset for value of `key`
 * Assumes a zero-terminated, validated JSON object / array as input
 * Returns offset on success, 0 on failure
 */
size_t json_obj_offset(char* json, char* key);
size_t json_array_offset(char* json, uint64_t key);

/*
 * Check for for a key within a JSON object / index within an array
 * Assumes a zero-terminated, validated JSON object / array as input
 * Returns type of value
 */
json_type json_obj(char* json, char* key);
json_type json_array(char* json, uint64_t key);

/*
 * Fetch boolean value for an object / array key
 * Assumes a zero-terminated, validated JSON object / array as input
 */
uint8_t json_obj_bool(char* json, char* key, uint8_t fallback);
uint8_t json_array_bool(char* json, uint64_t key, uint8_t fallback);

/*
 * Fetch integer/double value for an object / array key
 * Assumes a zero-terminated validated JSON object / array as input
 */
int64_t json_obj_int(char* json, char* key, int64_t fallback);
double json_obj_double(char* json, char* key, double fallback);
int64_t json_array_int(char* json, uint64_t key, int64_t fallback);
double json_array_double(char* json, uint64_t key, double fallback);

/* 
 * Fetch a string value for an object / array key
 * Assumes a zero-terminated validated JSON object / array as input
 * json_*_strdup returns a newly-allocated buffer containing
 * only the requested value
 */
char* json_obj_str(char* json, char* key, size_t* length);
char* json_obj_strdup(char* json, char* key);
char* json_array_str(char* json, uint64_t key, size_t* length);
char* json_array_strdup(char* json, uint64_t key);
