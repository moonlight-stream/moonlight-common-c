#include "Limelight-internal.h"

#define MAX_OPTION_NAME_LEN 128

#define MAX_SDP_HEADER_LEN 128
#define MAX_SDP_TAIL_LEN 128

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

static const char ATTRIB_X_NV_CALLBACKS [] = {
	0x50, 0x51, 0x49, 0x4a, 0x0d,
	0xad, 0x30, 0x4a, 0xf1, 0xbd, 0x30, 0x4a, 0xd5,
	0xac, 0x30, 0x4a, 0x21, 0xbc, 0x30, 0x4a, 0xc1,
	0xbb, 0x30, 0x4a, 0x7d, 0xbb, 0x30, 0x4a, 0x19,
	0xbb, 0x30, 0x4a, 0x00, 0x00, 0x00, 0x00
};

static const char ATTRIB_X_NV_VIDEO_DECODER [] = {
	0x50, 0x51, 0x49, 0x4a, 0x65, 0xad, 0x30, 0x4a, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd1, 0xac, 0x30,
	0x4a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x4d, 0xad, 0x30, 0x4a
};

static const char ATTRIB_X_NV_AUDIO_RENDERER [] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

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
	void* payloadBinary;
	int err;

	optionHead = NULL;

	err |= addAttributeBinary(&optionHead, "x-nv-callbacks",
		ATTRIB_X_NV_CALLBACKS, sizeof(ATTRIB_X_NV_CALLBACKS));
	err |= addAttributeBinary(&optionHead, "x-nv-videoDecoder",
		ATTRIB_X_NV_VIDEO_DECODER, sizeof(ATTRIB_X_NV_VIDEO_DECODER));
	err |= addAttributeBinary(&optionHead, "x-nv-audioRenderer",
		ATTRIB_X_NV_AUDIO_RENDERER, sizeof(ATTRIB_X_NV_AUDIO_RENDERER));
	err |= addAttributeString(&optionHead, "x-nv-general.serverAddress",
		inet_ntoa(targetAddress));
	err |= addAttributeBinary(&optionHead, "x-nv-general.serverPorts",
		ATTRIB_X_NV_GENERAL_SERVERPORTS, sizeof(ATTRIB_X_NV_GENERAL_SERVERPORTS));

	err |= addAttributeString(&optionHead, "x-nv-general.videoSyncAudioDelayAdjust", "10000");
	err |= addAttributeString(&optionHead, "x-nv-general.startTime", "0");
	payloadInt = 0xFFFFFFFF;
	err |= addAttributeBinary(&optionHead,
		"x-nv-general.featureFlags", &payloadInt, sizeof(payloadInt));
	err |= addAttributeString(&optionHead, "x-nv-general.userIdleWarningTimeout", "0");
	err |= addAttributeString(&optionHead, "x-nv-general.userIdleSessionTimeout", "0");
	err |= addAttributeString(&optionHead, "x-nv-general.serverCapture", "0");
	err |= addAttributeString(&optionHead, "x-nv-general.clientCapture", "0");
	err |= addAttributeString(&optionHead, "x-nv-general.rtpQueueMaxPackets", "16");
	err |= addAttributeString(&optionHead, "x-nv-general.rtpQueueMaxDurationMs", "40");
	err |= addAttributeString(&optionHead, "x-nv-general.useRtspClient", "257");

	sprintf(payloadStr, "%d", streamConfig->width);
	err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportWd", payloadStr);
	sprintf(payloadStr, "%d", streamConfig->height);
	err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportHt", payloadStr);

	err |= addAttributeString(&optionHead, "x-nv-video[0].adapterNumber", "0");

	sprintf(payloadStr, "%d", streamConfig->fps);
	err |= addAttributeString(&optionHead, "x-nv-video[0].maxFPS", payloadStr);

	err |= addAttributeString(&optionHead, "x-nv-video[0].iFrameOnDemand", "1");

	payloadInt = 1;
	err |= addAttributeBinary(&optionHead,
		"x-nv-video[0].transferProtocol", &payloadInt, sizeof(payloadInt));
	if (streamConfig->height >= 1080 && streamConfig->fps >= 60) {
		payloadInt = 4;
		err |= addAttributeBinary(&optionHead,
			"x-nv-video[0].rateControlMode", &payloadInt, sizeof(payloadInt));
		err |= addAttributeString(&optionHead, "x-nv-video[0].averageBitrate", "30");
		err |= addAttributeString(&optionHead, "x-nv-video[0].peakBitrate", "30");
	}
	else {
		payloadInt = 5;
		err |= addAttributeBinary(&optionHead,
			"x-nv-video[0].rateControlMode", &payloadInt, sizeof(payloadInt));
		err |= addAttributeString(&optionHead, "x-nv-video[0].averageBitrate", "7");
		err |= addAttributeString(&optionHead, "x-nv-video[0].peakBitrate", "7");
	}

	err |= addAttributeString(&optionHead, "x-nv-video[0].gopLength", "60");
	err |= addAttributeString(&optionHead, "x-nv-video[0].vbvMultiplier", "100");
	err |= addAttributeString(&optionHead, "x-nv-video[0].slicesPerFrame", "4");
	err |= addAttributeString(&optionHead, "x-nv-video[0].numTemporalLayers", "0");
	sprintf(payloadStr, "%d", streamConfig->packetSize);
	err |= addAttributeString(&optionHead, "x-nv-video[0].packetSize", payloadStr);
	err |= addAttributeString(&optionHead, "x-nv-video[0].enableSubframeEncoding", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].refPicInvalidation", "1");
	err |= addAttributeString(&optionHead, "x-nv-video[0].pingBackIntervalMs", "3000");
	err |= addAttributeString(&optionHead, "x-nv-video[0].pingBackTimeoutMs", "10000");
	err |= addAttributeString(&optionHead, "x-nv-video[0].timeoutLengthMs", "7000");
	err |= addAttributeString(&optionHead, "x-nv-video[0].fullFrameAssembly", "1");
	err |= addAttributeString(&optionHead, "x-nv-video[0].decodeIncompleteFrames", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].enableIntraRefresh", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].enableLongTermReferences", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].enableFrameRateCtrl", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].rtpDynamicPort", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].framesWithInvalidRefThreshold", "0");
	err |= addAttributeString(&optionHead, "x-nv-video[0].consecutiveFrameLostThreshold", "0");

	err |= addAttributeString(&optionHead, "x-nv-vqos[0].ts.enable", "0");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].ts.averageBitrate", "8");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].ts.maximumBitrate", "10");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.flags", "819"); // Bit 2 being set causes picture problems (should be 823)

	// We clamp to min = max so manual bitrate settings take effect without time to scale up
	sprintf(payloadStr, "%d", streamConfig->bitrate);
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.maximumBitrate", payloadStr);
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.minimumBitrate", payloadStr);

	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.statsTime", "50");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.zeroLossCount", "3000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.lossThreshold", "2");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.owdThreshold", "5000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.owdReference", "500");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.lossWaitTime", "75");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.rateDropMultiplier", "25");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.rateGainMultiplier", "10");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.maxFps", "60");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.minFps", "30");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.fpsThreshold", "3");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.jitterThreshold", "1000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.jitterWaitTime", "5000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.noJitterWaitTime", "5000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionEnableBitRatePercentThreshold", "110");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionEnableL1Threshold", "10");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionEnableL0Threshold", "6");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionDisableThreshold", "4");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionDisableWaitTime", "20000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionDisableWaitPercent", "100");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionLowerBoundRate", "1000");

	if (streamConfig->height >= 1080) {
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionLowerBoundWidth", "1280");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionLowerBoundHeight", "720");
	}
	else {
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionLowerBoundWidth", "720");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.earlyDetectionLowerBoundHeight", "480");
	}

	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.enableFlags", "3");

	if (streamConfig->height >= 1080 && streamConfig->fps >= 60) {
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.lowBitrate30FpsThreshold", "5000");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.lowBitrate60FpsThreshold", "5000");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.highBitrateThreshold", "7000");
	}
	else {
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.lowBitrate30FpsThreshold", "4000");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.lowBitrate60FpsThreshold", "5000");
		err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.highBitrateThreshold", "6000");
	}
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.pf.bitrateStepSize", "1000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bn.notifyUpBoundThreshold", "40");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bn.notifyLowBoundThreshold", "25");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].bn.notifyWaitTime", "3000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.enable", "1");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.numSrcPackets", "50");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.numOutPackets", "60");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.repairPercent", "20");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].pictureRefreshIntervalMs", "0");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].videoQualityScoreUpdateTime", "5000");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].invalidateThreshold", "3");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].invalidateSkipPercentage", "10");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].qosTrafficType", "7");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].videoQoSMaxRoundTripLatencyFrames", "12");
	err |= addAttributeString(&optionHead, "x-nv-vqos[0].videoQoSMaxConsecutiveDrops", "3");
	payloadInt = 1;
	err |= addAttributeBinary(&optionHead, "x-nv-vqos[0].profile", &payloadInt, sizeof(payloadInt));

	payloadInt = 1;
	err |= addAttributeBinary(&optionHead, "x-nv-aqos.mode", &payloadInt, sizeof(payloadInt));
	err |= addAttributeString(&optionHead, "x-nv-aqos.enableAudioStats", "1");
	err |= addAttributeString(&optionHead, "x-nv-aqos.audioStatsUpdateIntervalMs", "70");
	err |= addAttributeString(&optionHead, "x-nv-aqos.enablePacketLossPercentage", "1");
	err |= addAttributeString(&optionHead, "x-nv-aqos.bitRate", "96000");
	err |= addAttributeString(&optionHead, "x-nv-aqos.packetDuration", "5");
	err |= addAttributeString(&optionHead, "x-nv-aqos.packetLossPercentageUpdateIntervalMs", "100");
	err |= addAttributeString(&optionHead, "x-nv-aqos.qosTrafficType", "4");

	err |= addAttributeString(&optionHead, "x-nv-runtime.recordClientStats", "8");
	err |= addAttributeString(&optionHead, "x-nv-runtime.recordServerStats", "0");
	err |= addAttributeString(&optionHead, "x-nv-runtime.clientNetworkCapture", "0");
	err |= addAttributeString(&optionHead, "x-nv-runtime.clientTraceCapture", "0");
	err |= addAttributeString(&optionHead, "x-nv-runtime.serverNetworkCapture", "0");
	err |= addAttributeString(&optionHead, "x-nv-runtime.serverTraceCapture", "0");

	payloadInt = 0;
	err |= addAttributeBinary(&optionHead, "x-nv-ri.protocol", &payloadInt, sizeof(payloadInt));
	err |= addAttributeString(&optionHead, "x-nv-ri.sendStatus", "0");
	payloadInt = 0;
	err |= addAttributeBinary(&optionHead, "x-nv-ri.securityProtocol", &payloadInt, sizeof(payloadInt));
	payloadBinary = calloc(1, 0x20a);
	if (payloadBinary == NULL) {
		goto ExitFailure;
	}
	err |= addAttributeBinary(&optionHead, "x-nv-ri.secInfo", payloadBinary, 0x20a);
	free(payloadBinary);
	err |= addAttributeString(&optionHead, "x-nv-videoFrameDropIntervalNumber", "0");
	err |= addAttributeString(&optionHead, "x-nv-videoFrameDropContinualNumber", "0");

	if (err == 0) {
		return optionHead;
	}

ExitFailure:
	freeAttributeList(optionHead);
	return NULL;
}

int fillSdpHeader(char* buffer, struct in_addr targetAddress) {
	return sprintf(buffer,
		"v=0\r\n"
		"o=android 0 9 IN IPv4 %s\r\n"
		"s=NVIDIA Streaming Client\r\n", inet_ntoa(targetAddress));
}

int fillSdpTail(char* buffer) {
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

	*length = offset;
	return payload;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif