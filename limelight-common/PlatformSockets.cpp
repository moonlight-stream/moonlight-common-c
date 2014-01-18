#include "PlatformSockets.h"

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