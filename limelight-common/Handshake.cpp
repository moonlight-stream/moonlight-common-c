#include "Limelight.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

const char HELLO [] = {
	0x07, 0x00, 0x00, 0x00,
	0x61, 0x6e, 0x64, 0x72,
	0x6f, 0x69, 0x64, 0x03,
	0x01, 0x00, 0x00
};

const char PACKET2 [] = {
	0x01, 0x03, 0x02, 0x00,
	0x08, 0x00
};

const char PACKET3 [] = {
	0x04, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

const char PACKET4 [] = {
	0x01, 0x01, 0x00, 0x0
};

static int waitAndDiscardResponse(SOCKET sock) {
	char temp[256];
	return recv(sock, temp, sizeof(temp), 0);
}

int performHandshake(IP_ADDRESS host) {
	SOCKET s;
	int err;

	s = connectTcpSocket(host, 47991);
	if (s == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(s);

	err = send(s, HELLO, sizeof(HELLO), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(s);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(s, PACKET2, sizeof(PACKET2), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(s);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(s, PACKET3, sizeof(PACKET3), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(s);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(s, PACKET4, sizeof(PACKET4), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	closesocket(s);
	return 0;

CleanupError:
	closesocket(s);
	return LastSocketError();
}