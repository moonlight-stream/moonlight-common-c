#include "Limelight.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

static const char HELLO [] = {
	0x07, 0x00, 0x00, 0x00,
	0x61, 0x6e, 0x64, 0x72,
	0x6f, 0x69, 0x64, 0x03,
	0x01, 0x00, 0x00
};

static const char PACKET2 [] = {
	0x01, 0x03, 0x02, 0x00,
	0x08, 0x00
};

static const char PACKET3 [] = {
	0x04, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

static const char PACKET4 [] = {
	0x01, 0x01, 0x00, 0x0
};

static SOCKET sock = INVALID_SOCKET;

static int waitAndDiscardResponse(SOCKET sock) {
	char temp[256];
	return recv(sock, temp, sizeof(temp), 0);
}

void terminateHandshake(void) {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
}

#include <stdio.h>

int performHandshake(IP_ADDRESS host) {
	int err;

	sock = connectTcpSocket(host, 47991);
	if (sock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(sock);

	err = send(sock, HELLO, sizeof(HELLO), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(sock);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(sock, PACKET2, sizeof(PACKET2), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(sock);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(sock, PACKET3, sizeof(PACKET3), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = waitAndDiscardResponse(sock);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	err = send(sock, PACKET4, sizeof(PACKET4), 0);
	if (err == SOCKET_ERROR) {
		goto CleanupError;
	}

	closesocket(sock);
	sock = INVALID_SOCKET;

	return 0;

CleanupError:
	closesocket(sock);
	sock = INVALID_SOCKET;

	return LastSocketError();
}