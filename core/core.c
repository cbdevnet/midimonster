#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#ifndef _WIN32
	#include <sys/select.h>
	#define MM_API __attribute__((visibility ("default")))
#else
	#include <fcntl.h>
	#define MM_API __attribute__((dllexport))
#endif

#define BACKEND_NAME "core"
#include "midimonster.h"
#include "core.h"
#include "backend.h"
#include "routing.h"
#include "plugin.h"
#include "config.h"

static struct {
	//static size_t fds = 0;
	size_t n;
	int max;
	managed_fd* fd;
	managed_fd* signaled;
	fd_set read;
} fds = {
	.max = -1
};

static volatile sig_atomic_t fd_set_dirty = 1;
static uint64_t global_timestamp = 0;

MM_API uint64_t mm_timestamp(){
	return global_timestamp;
}

static void core_timestamp(){
	#ifdef _WIN32
	global_timestamp = GetTickCount();
	#else
	struct timespec current;
	if(clock_gettime(CLOCK_MONOTONIC_COARSE, &current)){
		LOGPF("Failed to update global timestamp, time-based processing for some backends may be impaired: %s", strerror(errno));
		return;
	}

	global_timestamp = current.tv_sec * 1000 + current.tv_nsec / 1000000;
	#endif
}

static fd_set core_collect(int* max_fd){
	size_t u = 0;
	fd_set rv_fds;

	if(max_fd){
		*max_fd = -1;
	}

	DBGPF("Building selector set from %" PRIsize_t " FDs registered to core", fds);
	FD_ZERO(&rv_fds);
	for(u = 0; u < fds.n; u++){
		if(fds.fd[u].fd >= 0){
			FD_SET(fds.fd[u].fd, &rv_fds);
			if(max_fd){
				*max_fd = max(*max_fd, fds.fd[u].fd);
			}
		}
	}

	return rv_fds;
}

MM_API int mm_manage_fd(int new_fd, char* back, int manage, void* impl){
	backend* b = backend_match(back);
	size_t u;

	if(!b){
		LOGPF("Unknown backend %s registered for managed fd", back);
		return 1;
	}

	//find exact match
	for(u = 0; u < fds.n; u++){
		if(fds.fd[u].fd == new_fd && fds.fd[u].backend == b){
			fds.fd[u].impl = impl;
			if(!manage){
				fds.fd[u].fd = -1;
				fds.fd[u].backend = NULL;
				fds.fd[u].impl = NULL;
				fd_set_dirty = 1;
			}
			return 0;
		}
	}

	if(!manage){
		return 0;
	}

	//find free slot
	for(u = 0; u < fds.n; u++){
		if(fds.fd[u].fd < 0){
			break;
		}
	}
	//if necessary expand
	if(u == fds.n){
		fds.fd = realloc(fds.fd, (fds.n + 1) * sizeof(managed_fd));
		if(!fds.fd){
			LOG("Failed to allocate memory");
			return 1;
		}

		fds.signaled = realloc(fds.signaled, (fds.n + 1) * sizeof(managed_fd));
		if(!fds.signaled){
			LOG("Failed to allocate memory");
			return 1;
		}
		fds.n++;
	}

	//store new fd
	fds.fd[u].fd = new_fd;
	fds.fd[u].backend = b;
	fds.fd[u].impl = impl;
	fd_set_dirty = 1;
	return 0;
}

int core_initialize(){
	FD_ZERO(&(fds.read));

	//load initial timestamp
	core_timestamp();

	#ifdef _WIN32
	WSADATA wsa;
	WORD version = MAKEWORD(2, 2);
	if(WSAStartup(version, &wsa)){
		return 1;
	}
	_fmode = _O_BINARY;
	#endif

	//attach plugins
	if(plugins_load(PLUGINS)){
		LOG("Failed to initialize a backend");
		return 1;
	}

	return 0;
}

int core_start(){
	if(backends_start()){
		return 1;
	}

	routing_stats();

	if(!fds.n){
		LOG("No descriptors registered for multiplexing");
	}

	return 0;
}

int core_iteration(){
	fd_set read_fds;
	struct timeval tv;
	int error;
	size_t n, u;
	#ifdef _WIN32
	char* error_message = NULL;
	#else
	struct timespec ts;
	#endif

	//rebuild fd set if necessary
	if(fd_set_dirty || !fds.signaled){
		fds.read = core_collect(&(fds.max));
		fd_set_dirty = 0;
	}

	//wait for & translate events
	read_fds = fds.read;
	tv = backend_timeout();

	//check whether there are any fds active, windows does not like select() without descriptors
	if(fds.max >= 0){
		error = select(fds.max + 1, &read_fds, NULL, NULL, &tv);
		if(error < 0){
			#ifndef _WIN32
			LOGPF("select failed: %s", strerror(errno));
			#else
			FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error_message, 0, NULL);
			LOGPF("select failed: %s", error_message);
			LocalFree(error_message);
			error_message = NULL;
			#endif
			return 1;
		}
	}
	else{
		DBGPF("No descriptors, sleeping for %zu msec", tv.tv_sec * 1000 + tv.tv_usec / 1000);
		#ifdef _WIN32
		Sleep(tv.tv_sec * 1000 + tv.tv_usec / 1000);
		#else
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
		nanosleep(&ts, NULL);
		#endif
	}

	//update this iteration's timestamp
	core_timestamp();

	//find all signaled fds
	n = 0;
	for(u = 0; u < fds.n; u++){
		if(fds.fd[u].fd >= 0 && FD_ISSET(fds.fd[u].fd, &read_fds)){
			fds.signaled[n] = fds.fd[u];
			n++;
		}
	}

	//run backend processing to collect events
	DBGPF("%" PRIsize_t " backend FDs signaled", n);
	if(backends_handle(n, fds.signaled)){
		return 1;
	}

	//route generated events
	return routing_iteration();
}

static void fds_free(){
	size_t u;
	for(u = 0; u < fds.n; u++){
		if(fds.fd[u].fd >= 0){
			close(fds.fd[u].fd);
			fds.fd[u].fd = -1;
		}
	}

	fds.max = -1;
	free(fds.signaled);
	fds.signaled = NULL;
	free(fds.fd);
	fds.fd = NULL;
	fds.n = 0;
}

void core_shutdown(){
	backends_stop();
	routing_cleanup();
	fds_free();
	plugins_close();
	config_free();
	fd_set_dirty = 1;
}
