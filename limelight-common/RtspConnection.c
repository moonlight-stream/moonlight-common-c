#include "Limelight-internal.h"
#include "Rtsp.h"

#define RTSP_MAX_RESP_SIZE 32768

static SOCKET sock = INVALID_SOCKET;
static int currentSeqNumber;
static char rtspTargetUrl[256];
static char sessionIdString[16];
static int hasSessionId;
static char responseBuffer[RTSP_MAX_RESP_SIZE];
static int rtspClientVersion;

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
    char clientVersionStr[16];

	// FIXME: Hacked CSeq attribute due to RTSP parser bug
	createRtspRequest(msg, NULL, 0, command, target, "RTSP/1.0",
		0, NULL, NULL, 0);
	
	sprintf(sequenceNumberStr, "%d", currentSeqNumber++);
    sprintf(clientVersionStr, "%d", rtspClientVersion);
	if (!addOption(msg, "CSeq", sequenceNumberStr) ||
		!addOption(msg, "X-GS-ClientVersion", clientVersionStr)) {
		freeMessage(msg);
		return 0;
	}

	return 1;
}

/* Send RTSP message and get response */
static int transactRtspMessage(PRTSP_MESSAGE request, PRTSP_MESSAGE response, int* error) {
    SOCK_RET err;
    int ret = 0;
	int offset;
	char* serializedMessage = NULL;
	int messageLen;
    
    *error = -1;

	sock = connectTcpSocket(&RemoteAddr, RemoteAddrLen, 48010);
	if (sock == INVALID_SOCKET) {
        *error = LastSocketError();
		return ret;
	}
	enableNoDelay(sock);

	serializedMessage = serializeRtspMessage(request, &messageLen);
	if (serializedMessage == NULL) {
		closesocket(sock);
        sock = INVALID_SOCKET;
		return ret;
	}

	// Send our message
	err = send(sock, serializedMessage, messageLen, 0);
	if (err == SOCKET_ERROR) {
        *error = LastSocketError();
        Limelog("Failed to send RTSP message: %d\n", *error);
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
static int requestOptions(PRTSP_MESSAGE response, int* error) {
	RTSP_MESSAGE request;
	int ret;
    
    *error = -1;

	ret = initializeRtspRequest(&request, "OPTIONS", rtspTargetUrl);
	if (ret != 0) {
		ret = transactRtspMessage(&request, response, error);
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP DESCRIBE request */
static int requestDescribe(PRTSP_MESSAGE response, int* error) {
	RTSP_MESSAGE request;
	int ret;
    
    *error = -1;

	ret = initializeRtspRequest(&request, "DESCRIBE", rtspTargetUrl);
	if (ret != 0) {
		if (addOption(&request, "Accept",
				"application/sdp") &&
			addOption(&request, "If-Modified-Since",
				"Thu, 01 Jan 1970 00:00:00 GMT")) {
			ret = transactRtspMessage(&request, response, error);
		}
		else {
			ret = 0;
		}
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP SETUP request */
static int setupStream(PRTSP_MESSAGE response, char* target, int* error) {
	RTSP_MESSAGE request;
	int ret;
    
    *error = -1;

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
			ret = transactRtspMessage(&request, response, error);
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
static int playStream(PRTSP_MESSAGE response, char* target, int* error) {
	RTSP_MESSAGE request;
	int ret;
    
    *error = -1;

	ret = initializeRtspRequest(&request, "PLAY", target);
	if (ret != 0) {
		if (addOption(&request, "Session", sessionIdString)) {
			ret = transactRtspMessage(&request, response, error);
		}
		else {
			ret = 0;
		}
		freeMessage(&request);
	}

	return ret;
}

/* Send RTSP ANNOUNCE message */
static int sendVideoAnnounce(PRTSP_MESSAGE response, int* error) {
	RTSP_MESSAGE request;
	int ret;
	int payloadLength;
	char payloadLengthStr[16];
    
    *error = -1;

	ret = initializeRtspRequest(&request, "ANNOUNCE", "streamid=video");
	if (ret != 0) {
		ret = 0;

		if (!addOption(&request, "Session", sessionIdString) ||
			!addOption(&request, "Content-type", "application/sdp")) {
			goto FreeMessage;
		}

		request.payload = getSdpPayloadForStreamConfig(rtspClientVersion, &payloadLength);
		if (request.payload == NULL) {
			goto FreeMessage;
		}
		request.flags |= FLAG_ALLOCATED_PAYLOAD;
		request.payloadLength = payloadLength;

		sprintf(payloadLengthStr, "%d", payloadLength);
		if (!addOption(&request, "Content-length", payloadLengthStr)) {
			goto FreeMessage;
		}

		ret = transactRtspMessage(&request, response, error);

	FreeMessage:
		freeMessage(&request);
	}

	return ret;
}

/* Perform RTSP Handshake with the streaming server machine as part of the connection process */
int performRtspHandshake(void) {
    char urlAddr[URLSAFESTRING_LEN];
    
	// Initialize global state
    addrToUrlSafeString(&RemoteAddr, urlAddr);
	sprintf(rtspTargetUrl, "rtsp://%s", urlAddr);
	currentSeqNumber = 1;
	hasSessionId = 0;
    
    if (ServerMajorVersion == 3) {
        rtspClientVersion = 10;
    }
    else {
        rtspClientVersion = 11;
    }

	{
		RTSP_MESSAGE response;
        int error = -1;

		if (!requestOptions(&response, &error)) {
			Limelog("RTSP OPTIONS request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP OPTIONS request failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
        int error = -1;

		if (!requestDescribe(&response, &error)) {
			Limelog("RTSP DESCRIBE request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP DESCRIBE request failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
		char* sessionId;
        int error = -1;

		if (!setupStream(&response, "streamid=audio", &error)) {
			Limelog("RTSP SETUP streamid=audio request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP SETUP streamid=audio request failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
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
        int error = -1;

		if (!setupStream(&response, "streamid=video", &error)) {
			Limelog("RTSP SETUP streamid=video request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP SETUP streamid=video request failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
        int error = -1;

		if (!sendVideoAnnounce(&response, &error)) {
			Limelog("RTSP ANNOUNCE request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP ANNOUNCE request failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
        int error = -1;

		if (!playStream(&response, "streamid=video", &error)) {
			Limelog("RTSP PLAY streamid=video request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP PLAY streamid=video failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	{
		RTSP_MESSAGE response;
        int error = -1;

		if (!playStream(&response, "streamid=audio", &error)) {
			Limelog("RTSP PLAY streamid=audio request failed: %d\n", error);
			return error;
		}

		if (response.message.response.statusCode != 200) {
			Limelog("RTSP PLAY streamid=audio failed: %d\n",
				response.message.response.statusCode);
			return response.message.response.statusCode;
		}

		freeMessage(&response);
	}

	return 0;
}