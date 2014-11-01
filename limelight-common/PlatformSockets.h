#pragma once

#include "Limelight.h"
#include "Platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

typedef int SOCK_RET;

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(x) close(x)

typedef int SOCKET;
typedef ssize_t SOCK_RET;
#endif

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port);
SOCKET bindUdpSocket(void);
int enableNoDelay(SOCKET s);
int initializePlatformSockets(PPLATFORM_CALLBACKS plCallbacks);
void cleanupPlatformSockets(void);