#include "Limelight-internal.h"
#include <arpa/inet.h>

#define MAX_OPTION_NAME_LEN 128

#define MAX_SDP_HEADER_LEN 128
#define MAX_SDP_TAIL_LEN 128

#define RTSP_CLIENT_VERSION_S "10"

static const int ATTRIB_X_NV_GENERAL_SERVERPORTS [] = {
	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff,

	0x00000000, 0xffffffff, 0xffffffff, 0x00000000,
	0xffffffff, 0xffffffff, 0x00000000, 0xffffffff,
	0xffffffff, 0x00000000, 0xffffffff, 0xffffffff
};

typedef struct _SDP_OPTION {
	char name[MAX_OPTION_NAME_LEN+1];
	void* payload;
	int payloadLen;
	struct _SDP_OPTION *next;
} SDP_OPTION, *PSDP_OPTION;

static void freeAttributeList(PSDP_OPTION head) {
	PSDP_OPTION next;
	while (head != NULL) {
		next = head->next;
		free(head);
		head = next;
	}
}

static int getSerializedAttributeListSize(PSDP_OPTION head) {
	PSDP_OPTION currentEntry = head;
	int size = 0;
	while (currentEntry != NULL) {
		size += strlen("a=");
		size += strlen(currentEntry->name);
		size += strlen(":");
		size += currentEntry->payloadLen;
		size += strlen(" \r\n");

		currentEntry = currentEntry->next;
	}
	return size;
}

static int fillSerializedAttributeList(char* buffer, PSDP_OPTION head) {
	PSDP_OPTION currentEntry = head;
	int offset = 0;
	while (currentEntry != NULL) {
		offset += sprintf(&buffer[offset], "a=%s:", currentEntry->name);
		memcpy(&buffer[offset], currentEntry->payload, currentEntry->payloadLen);
		offset += currentEntry->payloadLen;
		offset += sprintf(&buffer[offset], " \r\n");

		currentEntry = currentEntry->next;
	}
	return offset;
}

static int addAttributeBinary(PSDP_OPTION *head, char* name, const void* payload, int payloadLen) {
	PSDP_OPTION option, currentOption;

	option = malloc(sizeof(*option) + payloadLen);
	if (option == NULL) {
		return -1;
	}

	option->next = NULL;
	option->payloadLen = payloadLen;
	strcpy(option->name, name);
	option->payload = (void*)(option + 1);
	memcpy(option->payload, payload, payloadLen);

	if (*head == NULL) {
		*head = option;
	}
	else {
		currentOption = *head;
		while (currentOption->next != NULL) {
			currentOption = currentOption->next;
		}
		currentOption->next = option;
	}

	return 0;
}

static int addAttributeString(PSDP_OPTION *head, char* name, const char* payload) {
	// We purposefully omit the null terminating character
	return addAttributeBinary(head, name, payload, strlen(payload));
}

static PSDP_OPTION getAttributesList(PSTREAM_CONFIGURATION streamConfig, struct in_addr targetAddress) {
	PSDP_OPTION optionHead;
	int payloadInt;
	char payloadStr[64];
	int err;

	optionHead = NULL;

	err = 0;
	err |= addAttributeString(&optionHead, "x-nv-general.serverAddress",
		inet_ntoa(targetAddress));
	err |= addAttributeBinary(&optionHead, "x-nv-general.serverPorts",
		ATTRIB_X_NV_GENERAL_SERVERPORTS, sizeof(ATTRIB_X_NV_GENERAL_SERVERPORTS));

	payloadInt = 0x42774141;
	err |= addAttributeBinary(&optionHead,
		"x-nv-general.featureFlags", &payloadInt, sizeof(payloadInt));

	sprintf(payloadStr, "%d", streamConfig->width);
	err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportWd", payloadStr);
	sprintf(payloadStr, "%d", streamConfig->height);
	err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportHt", payloadStr);

	sprintf(payloadStr, "%d", streamConfig->fps);
	err |= addAttributeString(&optionHead, "x-nv-video[0].maxFPS", payloadStr);

	payloadInt = 0x41514120;
	err |= addAttributeBinary(&optionHead,
		"x-nv-video[1].transferProtocol", &payloadInt, sizeof(payloadInt));
	err |= addAttributeBinary(&optionHead,
		"x-nv-video[2].transferProtocol", &payloadInt, sizeof(payloadInt));

	payloadInt = 0x42414141;
	err |= addAttributeBinary(&optionHead,
		"x-nv-video[0].rateControlMode", &payloadInt, sizeof(payloadInt));
	err |= addAttributeBinary(&optionHead,
		"x-nv-video[2].rateControlMode", &payloadInt, sizeof(payloadInt));

	err |= addAttributeString(&optionHead, "x-nv-video[0].timeoutLengthMs", "7000");
	err |= addAttributeString(&optionHead, "x-nv-video[0].framesWithInvalidRefThreshold", "0");
	

	// The low nibble of the high byte should be 0x9 but that causes video issues
	// The bit 0x80 enables video scaling on packet loss which we can't support (for now)
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.flags", "7011");

	sprintf(payloadStr, "%d", streamConfig->bitrate);
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.maximumBitrate", payloadStr);

	// Since we can only deal with FEC data on a 1 packet frame,
	// restrict FEC repair percentage to minimum so we get only 1
	// FEC packet per frame
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.enable", "1");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.repairPercent", "1");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.repairMaxPercent", "1");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.repairMinPercent", "1");

	err |= addAttributeString(&optionHead, "x-nv-vqos[0].videoQualityScoreUpdateTime", "5000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].qosTrafficType", "7");
	err |= addAttributeString(&optionHead, "x-nv-aqos.qosTrafficType", "8");

	if (err == 0) {
		return optionHead;
	}

	freeAttributeList(optionHead);
	return NULL;
}

static int fillSdpHeader(char* buffer, struct in_addr targetAddress) {
	return sprintf(buffer,
		"v=0\r\n"
		"o=android 0 "RTSP_CLIENT_VERSION_S" IN IPv4 %s\r\n"
		"s=NVIDIA Streaming Client\r\n", inet_ntoa(targetAddress));
}

static int fillSdpTail(char* buffer) {
	return sprintf(buffer,
		"t=0 0\r\n"
		"m=video 47996  \r\n");
}

char* getSdpPayloadForStreamConfig(PSTREAM_CONFIGURATION streamConfig, struct in_addr targetAddress, int *length) {
	PSDP_OPTION attributeList;
	int offset;
	char* payload;

	attributeList = getAttributesList(streamConfig, targetAddress);
	if (attributeList == NULL) {
		return NULL;
	}

	payload = malloc(MAX_SDP_HEADER_LEN + MAX_SDP_TAIL_LEN +
		getSerializedAttributeListSize(attributeList));
	if (payload == NULL) {
		freeAttributeList(attributeList);
		return NULL;
	}

	offset = fillSdpHeader(payload, targetAddress);
	offset += fillSerializedAttributeList(&payload[offset], attributeList);
	offset += fillSdpTail(&payload[offset]);

	freeAttributeList(attributeList);
	*length = offset;
	return payload;
}