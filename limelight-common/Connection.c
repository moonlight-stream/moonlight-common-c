#include "Limelight-internal.h"
#include "Platform.h"

#define STAGE_NONE 0
#define STAGE_PLATFORM_INIT 1
#define STAGE_HANDSHAKE 2
#define STAGE_CONTROL_STREAM_INIT 3
#define STAGE_VIDEO_STREAM_INIT 4
#define STAGE_AUDIO_STREAM_INIT 5
#define STAGE_CONTROL_STREAM_START 6
#define STAGE_VIDEO_STREAM_START 7
#define STAGE_AUDIO_STREAM_START 8

static int stage = STAGE_NONE;

void LiStopConnection(void) {
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
		stage--;
		Limelog("done\n");
	}
	LC_ASSERT(stage == STAGE_NONE);
}

int LiStartConnection(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks,
	PAUDIO_RENDERER_CALLBACKS arCallbacks, void* renderContext, int drFlags) {
	int err;

	Limelog("Initializing platform...");
	err = initializePlatformSockets();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_PLATFORM_INIT);
	Limelog("done\n");

	Limelog("Starting handshake...");
	err = performHandshake(host);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_HANDSHAKE);
	Limelog("done\n");

	Limelog("Initializing control stream...");
	err = initializeControlStream(host, streamConfig);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing video stream...");
	initializeVideoStream(host, streamConfig, drCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing audio stream...");
	initializeAudioStream(host, arCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Starting control stream...");
	err = startControlStream();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_START);
	Limelog("done\n");

	Limelog("Starting video stream...");
	err = startVideoStream(renderContext, drFlags);
	if (err != 0) {
		Limelog("Video stream start failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_START);
	Limelog("done\n");

	Limelog("Starting audio stream...");
	err = startAudioStream();
	if (err != 0) {
		Limelog("Audio stream start failed: %d\n", err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_START);
	Limelog("done\n");

Cleanup:
	return err;
}