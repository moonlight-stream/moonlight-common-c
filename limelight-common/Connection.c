#include "Limelight-internal.h"
#include "Platform.h"

static int stage = STAGE_NONE;
static CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;

static const char* stageNames[STAGE_MAX] = {
	"none",
	"platform initialization",
	"handshake",
	"control stream initialization",
	"video stream initialization",
	"audio stream initialization",
	"input stream initialization",
	"control stream establishment",
	"video stream establishment",
	"audio stream establishment",
	"input stream establishment"
};

const char* LiGetStageName(int stage) {
	return stageNames[stage];
}

void LiStopConnection(void) {
	if (stage == STAGE_INPUT_STREAM_START) {
		Limelog("Stopping input stream...");
		stopInputStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_AUDIO_STREAM_START) {
		Limelog("Stopping audio stream...");
		stopAudioStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_VIDEO_STREAM_START) {
		Limelog("Stopping video stream...");
		stopVideoStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_CONTROL_STREAM_START) {
		Limelog("Stopping control stream...");
		stopControlStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_INPUT_STREAM_INIT) {
		Limelog("Cleaning up input stream...");
		destroyInputStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_AUDIO_STREAM_INIT) {
		Limelog("Cleaning up audio stream...");
		destroyAudioStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_VIDEO_STREAM_INIT) {
		Limelog("Cleaning up video stream...");
		destroyVideoStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_CONTROL_STREAM_INIT) {
		Limelog("Cleaning up control stream...");
		destroyControlStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_HANDSHAKE) {
		Limelog("Terminating handshake...");
		terminateHandshake();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_PLATFORM_INIT) {
		Limelog("Cleaning up platform...");
		cleanupPlatformSockets();
		cleanupPlatformThreads();
		stage--;
		Limelog("done\n");
	}
	LC_ASSERT(stage == STAGE_NONE);
}

int LiStartConnection(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PCONNECTION_LISTENER_CALLBACKS clCallbacks,
	PDECODER_RENDERER_CALLBACKS drCallbacks, PAUDIO_RENDERER_CALLBACKS arCallbacks, void* renderContext, int drFlags) {
	int err;

	memcpy(&ListenerCallbacks, clCallbacks, sizeof(ListenerCallbacks));

	Limelog("Initializing platform...");
	ListenerCallbacks.stageStarting(STAGE_PLATFORM_INIT);
	err = initializePlatformSockets();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_PLATFORM_INIT, err);
		goto Cleanup;
	}
	err = initializePlatformThreads();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_PLATFORM_INIT, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_PLATFORM_INIT);
	ListenerCallbacks.stageComplete(STAGE_PLATFORM_INIT);
	Limelog("done\n");

	Limelog("Starting handshake...");
	ListenerCallbacks.stageStarting(STAGE_HANDSHAKE);
	err = performHandshake(host);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_HANDSHAKE, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_HANDSHAKE);
	ListenerCallbacks.stageComplete(STAGE_HANDSHAKE);
	Limelog("done\n");

	Limelog("Initializing control stream...");
	ListenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_INIT);
	err = initializeControlStream(host, streamConfig, &ListenerCallbacks);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_INIT, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_INIT);
	ListenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing video stream...");
	ListenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_INIT);
	initializeVideoStream(host, streamConfig, drCallbacks, &ListenerCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_INIT);
	ListenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing audio stream...");
	ListenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_INIT);
	initializeAudioStream(host, arCallbacks, &ListenerCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_INIT);
	ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing input stream...");
	ListenerCallbacks.stageStarting(STAGE_INPUT_STREAM_INIT);
	initializeInputStream(host, &ListenerCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_INPUT_STREAM_INIT);
	ListenerCallbacks.stageComplete(STAGE_INPUT_STREAM_INIT);
	Limelog("done\n");

	Limelog("Starting control stream...");
	ListenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_START);
	err = startControlStream();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_START);
	ListenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_START);
	Limelog("done\n");

	Limelog("Starting video stream...");
	ListenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_START);
	err = startVideoStream(renderContext, drFlags);
	if (err != 0) {
		Limelog("Video stream start failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_VIDEO_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_START);
	ListenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_START);
	Limelog("done\n");

	Limelog("Starting audio stream...");
	ListenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_START);
	err = startAudioStream();
	if (err != 0) {
		Limelog("Audio stream start failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_AUDIO_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_START);
	ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_START);
	Limelog("done\n");

	Limelog("Starting input stream...");
	ListenerCallbacks.stageStarting(STAGE_INPUT_STREAM_START);
	err = startInputStream();
	if (err != 0) {
		Limelog("Input stream start failed: %d\n", err);
		ListenerCallbacks.stageFailed(STAGE_INPUT_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_INPUT_STREAM_START);
	ListenerCallbacks.stageComplete(STAGE_INPUT_STREAM_START);
	Limelog("done\n");

	ListenerCallbacks.connectionStarted();

Cleanup:
	return err;
}