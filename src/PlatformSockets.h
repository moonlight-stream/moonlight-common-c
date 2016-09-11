#pragma once

#include "Limelight.h"
#include "Platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

#define SHUT_RDWR SD_BOTH

typedef int SOCK_RET;
typedef int SOCKADDR_LEN;

#else
#include <sys/types.h>
#include <errno.h>

#define ioctlsocket ioctl
#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef unsigned SOCKADDR_LEN;

#endif

#if defined(__vita__)

#define TCP_NODELAY SCE_NET_TCP_NODELAY
#define INADDR_ANY SCE_NET_INADDR_ANY

#define sockaddr_in6 sockaddr_in
#define sin6_port sin_port

#endif

#define LastSocketFail() ((LastSocketError() != 0) ? LastSocketError() : -1)

// IPv6 addresses have 2 extra characters for URL escaping
#define URLSAFESTRING_LEN (128+2)
void addrToUrlSafeString(void* addr, char* string);

SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec);
SOCKET bindUdpSocket(int addrfamily, int bufferSize);
int enableNoDelay(SOCKET s);
int recvUdpSocket(SOCKET s, char* buffer, int size);
void shutdownTcpSocket(SOCKET s);
void setRecvTimeout(SOCKET s, int timeoutSec);
void closeSocket(SOCKET s);
