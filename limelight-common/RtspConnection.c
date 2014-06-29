#include "Limelight-internal.h"

static SOCKET sock = INVALID_SOCKET;

#define RTSP_MAX_RESP_SIZE 1024

// FIXME defs
static void* transactRtspMessage(IP_ADDRESS addr, void* message) {
	int err;
	char responseBuffer[RTSP_MAX_RESP_SIZE];
	int offset;
	void* responseMsg = NULL;

	sock = connectTcpSocket(addr, 48010);
	if (sock == INVALID_SOCKET) {
		return NULL;
	}
	enableNoDelay(sock);

	char* serializedMessage = NULL; // FIXME
	int messageLen = 0; // FIXME

	// Send our message
	err = send(sock, serializedMessage, messageLen, 0);
	if (err == SOCKET_ERROR) {
		goto Exit;
	}

	// Read the response until the server closes the connection
	offset = 0;
	for (;;) {
		err = recv(sock, &responseBuffer[offset], RTSP_MAX_RESP_SIZE - offset, 0);
		if (err <= 0) {
			// Done reading
			break;
		}
		offset += err;

		// Warn if the RTSP message is too big
		if (offset == RTSP_MAX_RESP_SIZE) {
			Limelog("RTSP message too long");
			goto Exit;
		}
	}

	responseMsg = NULL; // FIXME

Exit:
	closesocket(sock);
	sock = INVALID_SOCKET;
	return responseMsg;
}

void terminateRtspHandshake(void) {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
}

int performRtspHandshake(IP_ADDRESS addr, PSTREAM_CONFIGURATION streamConfigPtr) {
	return 0;
}