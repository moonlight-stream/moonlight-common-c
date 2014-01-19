#pragma once

#ifdef _WIN32
#include <Windows.h>
#define LastSocketError() WSAGetLastError()
#else
#define SOCKET int
#define LastSocketError() errno
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(x) close(x)
#endif

#define IP_ADDRESS unsigned int

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port);
int enableNoDelay(SOCKET s);