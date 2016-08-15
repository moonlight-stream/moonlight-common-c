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

#elif defined(__vita__)
#include <sys/types.h>
#include <psp2/net/net.h>

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
typedef int SOCKET;
typedef ssize_t SOCK_RET;
typedef unsigned SOCKADDR_LEN;

#define sockaddr SceNetSockaddr
#define sockaddr_in SceNetSockaddrIn
#define msghdr SceNetMsghdr
#define iovec SceNetIovec
#define sockaddr SceNetSockaddr
#define in_addr SceNetInAddr
#define sockaddr_in6 SceNetSockaddrIn
#define sin6_port sin_port

#define AF_INET SCE_NET_AF_INET
#define PF_INET SCE_NET_AF_INET
#define SOL_SOCKET SCE_NET_SOL_SOCKET
#define SO_SNDTIMEO SCE_NET_SO_SNDTIMEO 
#define IPPROTO_IP SCE_NET_IPPROTO_IP
#define IPPROTO_TCP SCE_NET_IPPROTO_TCP
#define TCP_NODELAY SCE_NET_TCP_NODELAY
#define SO_RCVBUF SCE_NET_SO_RCVBUF
#define SO_SNDBUF SCE_NET_SO_SNDBUF
#define SO_BROADCAST SCE_NET_SO_BROADCAST
#define SO_REUSEADDR SCE_NET_SO_REUSEADDR
#define SO_RCVTIMEO SCE_NET_SO_RCVTIMEO
#define SOCK_STREAM SCE_NET_SOCK_STREAM
#define SOCK_DGRAM SCE_NET_SOCK_DGRAM
#define INADDR_ANY SCE_NET_INADDR_ANY
#define SO_ERROR SCE_NET_SO_ERROR
#define AF_UNSPEC 0
#define AF_UNIX 1
#define SO_KEEPALIVE SCE_NET_SO_KEEPALIVE
#define IPPROTO_UDP SCE_NET_IPPROTO_UDP
#define SHUT_RDWR SCE_NET_SHUT_RDWR

#define recv sceNetRecv
#define send sceNetSend
#define recvmsg sceNetRecvmsg
#define sendmsg sceNetSendmsg
#define connect sceNetConnect
#define accept sceNetAccept
#define shutdown sceNetShutdown
#define setsockopt sceNetSetsockopt
#define getsockopt sceNetGetsockopt
#define listen sceNetListen
#define bind sceNetBind
#define getsockname sceNetGetsockname
#define getpeername sceNetGetpeername
#define sendto sceNetSendto

int* sceNetErrnoLoc();

#define LastSocketError() (*(int*)sceNetErrnoLoc())
#define SetLastSocketError(x) (*(int*)sceNetErrnoLoc()) = x

#define socket(a, b, c) sceNetSocket("sock", a, b, c)

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