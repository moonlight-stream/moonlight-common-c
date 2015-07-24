#pragma once

#include "Limelight.h"
#include "Platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

typedef int SOCK_RET;
typedef int SOCKADDR_LEN;

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(x) close(x)

typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef socklen_t SOCKADDR_LEN;
#endif

#define LastSocketFail() ((LastSocketError() != 0) ? LastSocketError() : -1)

// IPv6 addresses have 2 extra characters for URL escaping
#define URLSAFESTRING_LEN INET6_ADDRSTRLEN+2
void addrToUrlSafeString(struct sockaddr_storage *addr, char* string);

SOCKET connectTcpSocket(struct sockaddr_storage *dstaddr, SOCKADDR_LEN addrlen, unsigned short port);
SOCKET bindUdpSocket(int addrfamily, int bufferSize);
int enableNoDelay(SOCKET s);