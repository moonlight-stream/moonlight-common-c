#include "PlatformSockets.h"

SOCKET bindUdpSocket(unsigned short port) {
	SOCKET s;
	struct sockaddr_in addr;

	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		return INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (bind(s, (struct sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR) {
		closesocket(s);
		return INVALID_SOCKET;
	}

	return s;
}

SOCKET connectTcpSocket(IP_ADDRESS dstaddr, unsigned short port) {
	SOCKET s;
	struct sockaddr_in addr;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		return INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, &dstaddr, sizeof(dstaddr));
	if (connect(s, (struct sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR) {
		closesocket(s);
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