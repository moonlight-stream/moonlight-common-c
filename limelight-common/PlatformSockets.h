#pragma once

#include "Limelight.h"
#include "Platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define SetLastSocketError(x) WSASetLastError(x)
#define LastSocketError() WSAGetLastError()

#ifdef LC_WINDOWS_PHONE
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#endif

#include <WinSock2.h>

#ifdef LC_WINDOWS_PHONE
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_PHONE_APP
#endif

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <errno.h>
#define SOCKET int
#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(x) close(x)
#endif

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port);
SOCKET bindUdpSocket(unsigned short port);
int enableNoDelay(SOCKET s);
int initializePlatformSockets(void);
void cleanupPlatformSockets(void);