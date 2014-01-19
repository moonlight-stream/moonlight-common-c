#pragma once

#ifdef _WIN32
#include <Windows.h>
#define LastSocketError() WSAGetLastError()
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <errno.h>
#define SOCKET int
#define LastSocketError() errno
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(x) close(x)
#endif

#define IP_ADDRESS unsigned int

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port);
SOCKET bindUdpSocket(unsigned short port);
int enableNoDelay(SOCKET s);