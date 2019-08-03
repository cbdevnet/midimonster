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

/* Parse spec as host specification in the form
 *	host port
 * into its constituent parts.
 * Returns offsets into the original string and modifies it.
 * Returns NULL in *port if none given.
 * Returns NULL in both *port and *host if spec was an empty string.
 */
void mmbackend_parse_hostspec(char* spec, char** host, char** port);

/* Parse a given host / port combination into a sockaddr_storage
 * suitable for usage with connect / sendto
 * Returns 0 on success
 */
int mmbackend_parse_sockaddr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len);

/* Create a socket of given type and mode for a bind / connect host.
 * Returns -1 on failure, a valid file descriptor for the socket on success.
 */
int mmbackend_socket(char* host, char* port, int socktype, uint8_t listener);
