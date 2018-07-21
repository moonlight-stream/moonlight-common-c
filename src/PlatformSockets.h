#pragma once

#include "Limelight.h"
#include "Platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

#define SHUT_RDWR SD_BOTH
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EAGAIN WSAEWOULDBLOCK

typedef int SOCK_RET;
typedef int SOCKADDR_LEN;

#else
#if defined(__vita__)
#include <psp2/net/net.h>
#include <enet/enet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
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
#endif

#define ioctlsocket ioctl
#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef socklen_t SOCKADDR_LEN;
#endif

#if defined(__vita__)
#define TCP_NODELAY SCE_NET_TCP_NODELAY
#undef EAGAIN
#undef EWOULDBLOCK
#define EAGAIN SCE_NET_EAGAIN
#define EWOULDBLOCK SCE_NET_EWOULDBLOCK

#define sockaddr_in6 sockaddr_in
#define sin6_addr sin_addr
#define sin6_port sin_port
#define INET6_ADDRSTRLEN 128
#define inet_ntop sceNetInetNtop
#endif

#define LastSocketFail() ((LastSocketError() != 0) ? LastSocketError() : -1)

// IPv6 addresses have 2 extra characters for URL escaping
#define URLSAFESTRING_LEN (INET6_ADDRSTRLEN+2)
void addrToUrlSafeString(struct sockaddr_storage* addr, char* string);

SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec);
SOCKET bindUdpSocket(int addrfamily, int bufferSize);
int enableNoDelay(SOCKET s);
int recvUdpSocket(SOCKET s, char* buffer, int size, int useSelect);
void shutdownTcpSocket(SOCKET s);
int setNonFatalRecvTimeoutMs(SOCKET s, int timeoutMs);
void setRecvTimeout(SOCKET s, int timeoutSec);
void closeSocket(SOCKET s);
