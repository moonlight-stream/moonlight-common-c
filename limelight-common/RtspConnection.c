#include "Limelight-internal.h"
#include "Rtsp.h"

#define RTSP_MAX_RESP_SIZE 16384

static SOCKET sock = INVALID_SOCKET;
static IP_ADDRESS remoteAddr;
static int currentSeqNumber;
static char rtspTargetUrl[256];
static char sessionIdString[16];
static int hasSessionId;
static char responseBuffer[RTSP_MAX_RESP_SIZE];

/* GFE 2.1.1 */
#define RTSP_CLIENT_VERSION 10
#define RTSP_CLIENT_VERSION_S "10"

/* Create RTSP Option */
static POPTION_ITEM createOptionItem(char* option, char* content)
{
	POPTION_ITEM item = malloc(sizeof(*item));
	if (item == NULL) {
		return NULL;
	}

	item->option = malloc(strlen(option) + 1);
	if (item->option == NULL) {
		free(item);
		return NULL;
	}

	strcpy(item->option, option);

	item->content = malloc(strlen(content) + 1);
	if (item->content == NULL) {
		free(item->option);
		free(item);
		return NULL;
	}

	strcpy(item->content, content);

	item->next = NULL;
	item->flags = FLAG_ALLOCATED_OPTION_FIELDS;

	return item;
}

/* Add an option to the RTSP Message */
static int addOption(PRTSP_MESSAGE msg, char* option, char* content)
{
	POPTION_ITEM item = createOptionItem(option, content);
	if (item == NULL) {
		return 0;
	}

	insertOption(&msg->options, item);
	msg->flags |= FLAG_ALLOCATED_OPTION_ITEMS;

	return 1;
}

/* Create an RTSP Request */
static int initializeRtspRequest(PRTSP_MESSAGE msg, char* command, char* target)
{
	char sequenceNumberStr[16];

	// FIXME: Hacked CSeq attribute due to RTSP parser bug
	createRtspRequest(msg, NULL, 0, command, target, "RTSP/1.0",
		0, NULL, NULL, 0);
	
	sprintf(sequenceNumberStr, "%d", currentSeqNumber++);
	if (!addOption(msg, "CSeq", sequenceNumberStr) ||
		!addOption(msg, "X-GS-ClientVersion", RTSP_CLIENT_VERSION_S)) {
		freeMessage(msg);
		return 0;
	}

	return 1;
}

/* Returns 1 on success, 0 otherwise */
static int transactRtspMessage(PRTSP_MESSAGE request, PRTSP_MESSAGE response) {
    SOCK_RET err;
    int ret = 0;
	int offset;
	char* serializedMessage = NULL;
	int messageLen;

	sock = connectTcpSocket(remoteAddr, 48010);
	if (sock == INVALID_SOCKET) {
		return ret;
	}
	enableNoDelay(sock);

	serializedMessage = serializeRtspMessage(request, &messageLen);
	if (serializedMessage == NULL) {
		closesocket(sock);
		return ret;
	}

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
			Limelog("RTSP message too long\n");
			goto Exit;
		}
	}

	if (parseRtspMessage(response, responseBuffer, offset) == RTSP_ERROR_SUCCESS) {
		// Successfully parsed response
		ret = 1;
	}
	else {
		Limelog("Failed to parse RTSP response\n");
	}

Exit:
	if (serializedMessage != NULL) {
		free(serializedMessage);
	}

	closesocket(sock);
	sock = INVALID_SOCKET;
	return ret;
}

/* Terminate the RTSP Handshake process by closing the socket */
void terminateRtspHandshake(void) {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
}

/* Send RTSP OPTIONS request */
static int requestOptions(PRTSP_MESSAGE response) {
	RTSP_MESSAGE request;
	int ret;

	ret = initializeRtspRequest(&request, "OPTIONS", rtspTargetUrl);
	if (ret != 0) {
		ret = transactRtspMessage(&request, response);
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP DESCRIBE request */
static int requestDescribe(PRTSP_MESSAGE response) {
	RTSP_MESSAGE request;
	int ret;

	ret = initializeRtspRequest(&request, "DESCRIBE", rtspTargetUrl);
	if (ret != 0) {
		if (addOption(&request, "Accept",
				"application/sdp") &&
			addOption(&request, "If-Modified-Since",
				"Thu, 01 Jan 1970 00:00:00 GMT")) {
			ret = transactRtspMessage(&request, response);
		}
		else {
			ret = 0;
		}
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP SETUP request */
static int setupStream(PRTSP_MESSAGE response, char* target) {
	RTSP_MESSAGE request;
	int ret;

	ret = initializeRtspRequest(&request, "SETUP", target);
	if (ret != 0) {
		if (hasSessionId) {
			if (!addOption(&request, "Session", sessionIdString)) {
				ret = 0;
				goto FreeMessage;
			}
		}

		if (addOption(&request, "Transport", " ") &&
			addOption(&request, "If-Modified-Since",
				"Thu, 01 Jan 1970 00:00:00 GMT")) {
			ret = transactRtspMessage(&request, response);
		}
		else {
			ret = 0;
		}

	FreeMessage:
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP PLAY request*/
static int playStream(PRTSP_MESSAGE response, char* target) {
	RTSP_MESSAGE request;
	int ret;

	ret = initializeRtspRequest(&request, "PLAY", target);
	if (ret != 0) {
		if (addOption(&request, "Session", sessionIdString)) {
			ret = transactRtspMessage(&request, response);
		}
		else {
			ret = 0;
		}
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP ANNOUNCE message */
static int sendVideoAnnounce(PRTSP_MESSAGE response, PSTREAM_CONFIGURATION streamConfig) {
	RTSP_MESSAGE request;
	int ret;
	int payloadLength;
	char payloadLengthStr[16];
	struct in_addr sdpAddr;

	ret = initializeRtspRequest(&request, "ANNOUNCE", "streamid=video");
	if (ret != 0) {
		ret = 0;

		if (!addOption(&request, "Session", sessionIdString) ||
			!addOption(&request, "Content-type", "application/sdp")) {
			goto FreeMessage;
		}

        memcpy(&sdpAddr, &remoteAddr, sizeof(remoteAddr));
		request.payload = getSdpPayloadForStreamConfig(streamConfig, sdpAddr, &payloadLength);
		if (request.payload == NULL) {
			goto FreeMessage;
		}
		request.flags |= FLAG_ALLOCATED_PAYLOAD;
		request.payloadLength = payloadLength;

		sprintf(payloadLengthStr, "%d", payloadLength);
		if (!addOption(&request, "Content-length", payloadLengthStr)) {
			goto FreeMessage;
		}

		ret = transactRtspMessage(&request, response);

	FreeMessage:
		freeMessage(&request);
	}

	return ret;
}

/* Perform RTSP Handshake with the streaming server machine as part of the connection process */
int performRtspHandshake(IP_ADDRESS addr, PSTREAM_CONFIGURATION streamConfigPtr) {
	struct in_addr inaddr;

	// Initialize global state
	remoteAddr = addr;
    memcpy(&inaddr, &addr, sizeof(addr));
	sprintf(rtspTargetUrl, "rtsp://%s", inet_ntoa(inaddr));
	currentSeqNumber = 1;
	hasSessionId = 0;

	{
		RTSP_MESSAGE response;

		if (!requestOptions(&response)) {
			Limelog("RTSP OPTIONS request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP OPTIONS request failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;

		if (!requestDescribe(&response)) {
			Limelog("RTSP DESCRIBE request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP DESCRIBE request failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
		char* sessionId;

		if (!setupStream(&response, "streamid=audio")) {
			Limelog("RTSP SETUP streamid=audio request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP SETUP streamid=audio request failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		sessionId = getOptionContent(response.options, "Session");
		if (sessionId == NULL) {
			Limelog("RTSP SETUP streamid=audio is missing session attribute");
			return -1;
		}

		strcpy(sessionIdString, sessionId);
		hasSessionId = 1;

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;

		if (!setupStream(&response, "streamid=video")) {
			Limelog("RTSP SETUP streamid=video request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP SETUP streamid=video request failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;

		if (!sendVideoAnnounce(&response, streamConfigPtr)) {
			Limelog("RTSP ANNOUNCE request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP ANNOUNCE request failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;

		if (!playStream(&response, "streamid=video")) {
			Limelog("RTSP PLAY streamid=video request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP PLAY streamid=video failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;

		if (!playStream(&response, "streamid=audio")) {
			Limelog("RTSP PLAY streamid=audio request failed\n");
			return -1;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP PLAY streamid=audio failed: %d\n",
				response.message.response.statusCode);
			return -1;
		}

		freeMessage(&response);
	}

	return 0;
}