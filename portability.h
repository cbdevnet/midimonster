#ifdef __APPLE__
	#ifndef CLOCK_MONOTONIC_COARSE
		#define CLOCK_MONOTONIC_COARSE _CLOCK_MONOTONIC_RAW
	#endif

	#include <libkern/OSByteOrder.h>
	#define htobe16(x) OSSwapHostToBigInt16(x)
	#define htole16(x) OSSwapHostToLittleInt16(x)
	#define be16toh(x) OSSwapBigToHostInt16(x)
	#define le16toh(x) OSSwapLittleToHostInt16(x)

	#define htobe32(x) OSSwapHostToBigInt32(x)
	#define htole32(x) OSSwapHostToLittleInt32(x)
	#define be32toh(x) OSSwapBigToHostInt32(x)
	#define le32toh(x) OSSwapLittleToHostInt32(x)

	#define htobe64(x) OSSwapHostToBigInt64(x)
	#define htole64(x) OSSwapHostToLittleInt64(x)
	#define be64toh(x) OSSwapBigToHostInt64(x)
	#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <winsock2.h>

	#define htobe16(x) htons(x)
	#define be16toh(x) ntohs(x)

	#define htobe32(x) htonl(x)
	#define be32toh(x) ntohl(x)

	#define htobe64(x) _byteswap_uint64(x)
	#define htole64(x) (x)
	#define be64toh(x) _byteswap_uint64(x)
	#define le64toh(x) (x)

	#define PRIsize_t "Iu"
#else
	#define PRIsize_t "zu"
#endif
