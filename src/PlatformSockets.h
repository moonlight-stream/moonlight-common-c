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
#define EINTR WSAEINTR
#define ETIMEDOUT WSAETIMEDOUT

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

#define ioctlsocket ioctl
#define LastSocketError() errno
#define SetLastSocketError(x) errno = x
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef socklen_t SOCKADDR_LEN;
#endif

#define LastSocketFail() ((LastSocketError() != 0) ? LastSocketError() : -1)

// IPv6 addresses have 2 extra characters for URL escaping
#define URLSAFESTRING_LEN (INET6_ADDRSTRLEN+2)
void addrToUrlSafeString(struct sockaddr_storage* addr, char* string);

int resolveHostName(const char* host, int family, int tcpTestPort, struct sockaddr_storage* addr, SOCKADDR_LEN* addrLen);
SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec);
int sendMtuSafe(SOCKET s, char* buffer, int size);
SOCKET bindUdpSocket(int addrfamily, int bufferSize);
int enableNoDelay(SOCKET s);
int setSocketNonBlocking(SOCKET s, int val);
int recvUdpSocket(SOCKET s, char* buffer, int size, int useSelect);
void shutdownTcpSocket(SOCKET s);
int setNonFatalRecvTimeoutMs(SOCKET s, int timeoutMs);
void setRecvTimeout(SOCKET s, int timeoutSec);
void closeSocket(SOCKET s);
int isPrivateNetworkAddress(struct sockaddr_storage* address);

int initializePlatformSockets(void);
void cleanupPlatformSockets(void);
