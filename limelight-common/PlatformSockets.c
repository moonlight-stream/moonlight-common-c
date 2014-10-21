#include "PlatformSockets.h"
#include "Limelight-internal.h"

SOCKET bindUdpSocket(void) {
	SOCKET s;
	struct sockaddr_in addr;
	int val;
	int err;

	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		return INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (bind(s, (struct sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR) {
		err = LastSocketError();
		closesocket(s);
		SetLastSocketError(err);
		return INVALID_SOCKET;
	}

	val = 65536;
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*) &val, sizeof(val));

	return s;
}

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port) {
	SOCKET s;
	struct sockaddr_in addr;
	int err;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		return INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, &dstaddr, sizeof(dstaddr));
	if (connect(s, (struct sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR) {
		err = LastSocketError();
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
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	WSADATA data;
	return WSAStartup(MAKEWORD(2, 0), &data);
#elif defined(LC_POSIX)
    // Disable SIGPIPE signals to avoid us getting
    // killed when a socket gets an EPIPE error
    struct sigaction sa;
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
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	WSACleanup();
#else
#endif
}
