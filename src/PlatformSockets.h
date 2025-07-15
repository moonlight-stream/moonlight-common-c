#pragma once

#include "Limelight.h"
#include "Platform.h"
#ifdef __3DS__
#include <netinet/in.h>

#ifdef AF_INET6
#undef AF_INET6
#endif

extern in_port_t n3ds_udp_port;
#endif

#ifdef __vita__
#ifdef AF_INET6
#undef AF_INET6
#endif
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wlanapi.h>
#ifndef __MINGW32__
#include <timeapi.h>
#else
#include <mmsystem.h>
#endif
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

#define SHUT_RDWR SD_BOTH

#ifdef EAGAIN
#undef EAGAIN
#endif
#define EAGAIN WSAEWOULDBLOCK

#ifdef EINTR
#undef EINTR
#endif
#define EINTR WSAEINTR

#ifdef __MINGW32__
#undef EWOULDBLOCK
#undef EINPROGRESS
#undef ETIMEDOUT
#undef ECONNREFUSED
#endif

#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINPROGRESS WSAEINPROGRESS
#define ETIMEDOUT WSAETIMEDOUT
#define ECONNREFUSED WSAECONNREFUSED

typedef int SOCK_RET;
typedef int SOCKADDR_LEN;

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <stdarg.h>

#define ioctlsocket ioctl
#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef socklen_t SOCKADDR_LEN;
#endif

#ifdef AF_INET6
typedef struct sockaddr_in6 LC_SOCKADDR;
#define SET_FAMILY(addr, family) ((addr)->sin6_family = (family))
#define SET_PORT(addr, port) ((addr)->sin6_port = htons(port))
#else
typedef struct sockaddr_in LC_SOCKADDR;
#define SET_FAMILY(addr, family) ((addr)->sin_family = (family))
#define SET_PORT(addr, port) ((addr)->sin_port = htons(port))
#endif

#define LastSocketFail() ((LastSocketError() != 0) ? LastSocketError() : -1)

#ifdef AF_INET6
// IPv6 addresses have 2 extra characters for URL escaping
#define URLSAFESTRING_LEN (INET6_ADDRSTRLEN+2)
#else
#define URLSAFESTRING_LEN INET_ADDRSTRLEN
#endif
void addrToUrlSafeString(struct sockaddr_storage* addr, char* string, size_t stringLength);

#define SOCK_QOS_TYPE_BEST_EFFORT 0
#define SOCK_QOS_TYPE_AUDIO 1
#define SOCK_QOS_TYPE_VIDEO 2

SOCKET createSocket(int addressFamily, int socketType, int protocol, bool nonBlocking);
SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec);
int sendMtuSafe(SOCKET s, char* buffer, int size);
SOCKET bindUdpSocket(int addressFamily, struct sockaddr_storage* localAddr, SOCKADDR_LEN addrLen, int bufferSize, int socketQosType);
int enableNoDelay(SOCKET s);
int setSocketNonBlocking(SOCKET s, bool enabled);
int recvUdpSocket(SOCKET s, char* buffer, int size, bool useSelect);
void shutdownTcpSocket(SOCKET s);
int setNonFatalRecvTimeoutMs(SOCKET s, int timeoutMs);
void closeSocket(SOCKET s);
bool isPrivateNetworkAddress(struct sockaddr_storage* address);
bool isNat64SynthesizedAddress(struct sockaddr_storage* address);
bool is464XLATSynthesizedAddress(struct sockaddr_storage* address, const char *ipv4Str);
bool isIPv4Address(const char *str);
int pollSockets(struct pollfd* pollFds, int pollFdsCount, int timeoutMs);
bool isSocketReadable(SOCKET s);

#define TCP_PORT_MASK 0xFFFF
#define TCP_PORT_FLAG_ALWAYS_TEST 0x10000
int resolveHostName(const char* host, int family, int tcpTestPort, struct sockaddr_storage* addr, SOCKADDR_LEN* addrLen);

void enterLowLatencyMode(void);
void exitLowLatencyMode(void);

int initializePlatformSockets(void);
void cleanupPlatformSockets(void);

void logWithSockaddrStorage(struct sockaddr_storage* addr, const char *format);
