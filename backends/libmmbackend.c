#include "libmmbackend.h"

void mmbackend_parse_hostspec(char* spec, char** host, char** port){
	size_t u = 0;

	if(!spec || !host || !port){
		return;
	}

	*port = NULL;

	//skip leading spaces
	for(; spec[u] && isspace(spec[u]); u++){
	}

	if(!spec[u]){
		*host = NULL;
		return;
	}

	*host = spec + u;

	//scan until string end or space
	for(; spec[u] && !isspace(spec[u]); u++){
	}

	//if space, the rest should be the port
	if(spec[u]){
		spec[u] = 0;
		*port = spec + u + 1;
	}
}

int mmbackend_parse_sockaddr(char* host, char* port, struct sockaddr_storage* addr, socklen_t* len){
	struct addrinfo* head;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC
	};

	int error = getaddrinfo(host, port, &hints, &head);
	if(error || !head){
		fprintf(stderr, "Failed to parse address %s port %s: %s\n", host, port, gai_strerror(error));
		return 1;
	}

	memcpy(addr, head->ai_addr, head->ai_addrlen);
	if(len){
		*len = head->ai_addrlen;
	}

	freeaddrinfo(head);
	return 0;
}

int mmbackend_socket(char* host, char* port, int socktype, uint8_t listener){
	int fd = -1, status, yes = 1, flags;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = socktype,
		.ai_flags = (listener ? AI_PASSIVE : 0)
	};
	struct addrinfo *info, *addr_it;

	status = getaddrinfo(host, port, &hints, &info);
	if(status){
		fprintf(stderr, "Failed to parse address %s port %s: %s\n", host, port, gai_strerror(status));
		return -1;
	}

	//traverse the result list
	for(addr_it = info; addr_it; addr_it = addr_it->ai_next){
		fd = socket(addr_it->ai_family, addr_it->ai_socktype, addr_it->ai_protocol);
		if(fd < 0){
			continue;
		}

		//set required socket options
		yes = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to enable SO_REUSEADDR on socket\n");
		}

		yes = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to enable SO_BROADCAST on socket\n");
		}

		yes = 0;
		if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to disable IP_MULTICAST_LOOP on socket: %s\n", strerror(errno));
		}

		status = bind(fd, addr_it->ai_addr, addr_it->ai_addrlen);
		if(status < 0){
			close(fd);
			continue;
		}

		break;
	}
	freeaddrinfo(info);

	if(!addr_it){
		fprintf(stderr, "Failed to create socket for %s port %s\n", host, port);
		return -1;
	}

	//set nonblocking
	flags = fcntl(fd, F_GETFL, 0);
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){
		fprintf(stderr, "Failed to set socket nonblocking\n");
		close(fd);
		return -1;
	}

	return fd;
}
