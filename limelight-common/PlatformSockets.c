#include "PlatformSockets.h"
#include "Limelight-internal.h"

void addrToUrlSafeString(struct sockaddr_storage *addr, char* string)
{
	char addrstr[INET6_ADDRSTRLEN];

    if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
        inet_ntop(addr->ss_family, &sin6->sin6_addr, addrstr, sizeof(addrstr));
        
        // IPv6 addresses need to be enclosed in brackets for URLs
        sprintf(string, "[%s]", addrstr);
    }
    else {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
		inet_ntop(addr->ss_family, &sin->sin_addr, addrstr, sizeof(addrstr));
        
        // IPv4 addresses are returned without changes
        sprintf(string, "%s", addrstr);
    }
}

SOCKET bindUdpSocket(int addrfamily, int bufferSize) {
	SOCKET s;
    struct sockaddr_storage addr;
#ifdef LC_DARWIN
	int val;
#endif
	int err;

    LC_ASSERT(addrfamily == AF_INET || addrfamily == AF_INET6);
    
	s = socket(addrfamily, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
        Limelog("socket() failed: %d\n", (int)LastSocketError());
		return INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
    addr.ss_family = addrfamily;
	if (bind(s, (struct sockaddr*) &addr,
             addrfamily == AF_INET ?
                sizeof(struct sockaddr_in) :
                sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
		err = LastSocketError();
        Limelog("bind() failed: %d\n", err);
		closesocket(s);
		SetLastSocketError(err);
		return INVALID_SOCKET;
	}
    
#ifdef LC_DARWIN
    // Disable SIGPIPE on iOS
    val = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (char* )&val, sizeof(val));
#endif

	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*) &bufferSize, sizeof(bufferSize));

	return s;
}

SOCKET connectTcpSocket(struct sockaddr_storage *dstaddr, SOCKADDR_LEN addrlen, unsigned short port) {
	SOCKET s;
    struct sockaddr_in6 addr;
	int err;
#ifdef LC_DARWIN
    int val;
#endif

	s = socket(dstaddr->ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
        Limelog("socket() failed: %d\n", (int)LastSocketError());
		return INVALID_SOCKET;
	}
    
#ifdef LC_DARWIN
    // Disable SIGPIPE on iOS
    val = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (char* )&val, sizeof(val));
#endif

    memcpy(&addr, dstaddr, sizeof(addr));
	addr.sin6_port = htons(port);
	if (connect(s, (struct sockaddr*) &addr, addrlen) == SOCKET_ERROR) {
		err = LastSocketError();
        Limelog("connect() failed: %d\n", err);
		closesocket(s);
		SetLastSocketError(err);
		return INVALID_SOCKET;
	}

	return s;
}

int enableNoDelay(SOCKET s) {
	int err;
	int val;

	val = 1;
	err = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(val));
	if (err == SOCKET_ERROR) {
		return LastSocketError();
	}

	return 0;
}

int initializePlatformSockets(void) {
#if defined(LC_WINDOWS)
	WSADATA data;
	return WSAStartup(MAKEWORD(2, 0), &data);
#elif defined(LC_POSIX)
    // Disable SIGPIPE signals to avoid us getting
    // killed when a socket gets an EPIPE error
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
#else
	return 0;
#endif
}

void cleanupPlatformSockets(void) {
#if defined(LC_WINDOWS)
	WSACleanup();
#else
#endif
}
