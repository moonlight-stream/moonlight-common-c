#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"
#include "Input.h"

static IP_ADDRESS host;
static SOCKET inputSock = INVALID_SOCKET;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;

static LINKED_BLOCKING_QUEUE packetQueue;
static PLT_THREAD inputSendThread;

typedef struct _PACKET_HOLDER {
	int packetLength;
	union {
		NV_KEYBOARD_PACKET keyboard;
		NV_MOUSE_MOVE_PACKET mouseMove;
		NV_MOUSE_BUTTON_PACKET mouseButton;
	} packet;
} PACKET_HOLDER, *PPACKET_HOLDER;

int initializeInputStream(IP_ADDRESS addr, PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	host = addr;
	listenerCallbacks = clCallbacks;

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);

	return 0;
}

void destroyInputStream(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;

	entry = LbqDestroyLinkedBlockingQueue(&packetQueue);

	while (entry != NULL) {
		nextEntry = entry->flink;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}
}

static void inputSendThreadProc(void* context) {
	int err;
	PPACKET_HOLDER holder;

	while (!PltIsThreadInterrupted(&inputSendThread)) {
		err = LbqWaitForQueueElement(&packetQueue, (void**) &holder);
		if (err != LBQ_SUCCESS) {
			Limelog("Input thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		err = send(inputSock, (const char*) &holder->packet, holder->packetLength, 0);
		free(holder);
		if (err <= 0) {
			Limelog("Input thread terminating #2\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}
	}
}

int startInputStream(void) {
	int err;

	inputSock = connectTcpSocket(host, 35043);
	if (inputSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(inputSock);

	err = PltCreateThread(inputSendThreadProc, NULL, &inputSendThread);
	if (err != 0) {
		return err;
	}

	return err;
}

int stopInputStream(void) {
	PltInterruptThread(&inputSendThread);

	if (inputSock != INVALID_SOCKET) {
		closesocket(inputSock);
		inputSock = INVALID_SOCKET;
	}

	PltJoinThread(&inputSendThread);
	PltCloseThread(&inputSendThread);

	return 0;
}

int LiSendMouseMoveEvent(short deltaX, short deltaY) {
	PPACKET_HOLDER holder;
	int err;

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_MOUSE_MOVE_PACKET);
	holder->packet.mouseMove.header.packetType = htonl(PACKET_TYPE_MOUSE_MOVE);
	holder->packet.mouseMove.magic = htonl(MOUSE_MOVE_MAGIC);
	holder->packet.mouseMove.deltaX = htons(deltaX);
	holder->packet.mouseMove.deltaY = htons(deltaY);

	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}

int LiSendMouseButtonEvent(char action, int button) {
	PPACKET_HOLDER holder;
	int err;

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_MOUSE_BUTTON_PACKET);
	holder->packet.mouseButton.header.packetType = htonl(PACKET_TYPE_MOUSE_BUTTON);
	holder->packet.mouseButton.action = action;
	holder->packet.mouseButton.button = htonl(button);

	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}

int LiSendKeyboardEvent(short keyCode, char keyAction, char modifiers) {
	PPACKET_HOLDER holder;
	int err;

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_KEYBOARD_PACKET);
	holder->packet.keyboard.header.packetType = htonl(PACKET_TYPE_KEYBOARD);
	holder->packet.keyboard.keyAction = keyAction;
	holder->packet.keyboard.zero1 = 0;
	holder->packet.keyboard.keyCode = htons(keyCode);
	holder->packet.keyboard.modifiers = modifiers;
	holder->packet.keyboard.zero2 = 0;

	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}

