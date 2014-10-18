#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"
#include "Input.h"

#include "OpenAES/oaes_lib.h"
#include "OpenAES/oaes_common.h"

static IP_ADDRESS host;
static SOCKET inputSock = INVALID_SOCKET;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;
static int initialized;

static LINKED_BLOCKING_QUEUE packetQueue;
static PLT_THREAD inputSendThread;
static OAES_CTX* oaesContext;

#define MAX_INPUT_PACKET_SIZE 128

typedef struct _PACKET_HOLDER {
	int packetLength;
	union {
		NV_KEYBOARD_PACKET keyboard;
		NV_MOUSE_MOVE_PACKET mouseMove;
		NV_MOUSE_BUTTON_PACKET mouseButton;
		NV_CONTROLLER_PACKET controller;
		NV_SCROLL_PACKET scroll;
	} packet;
} PACKET_HOLDER, *PPACKET_HOLDER;

int initializeInputStream(IP_ADDRESS addr, PCONNECTION_LISTENER_CALLBACKS clCallbacks,
	char* aesKeyData, int aesKeyDataLength, char* aesIv, int aesIvLength) {
	host = addr;
	listenerCallbacks = clCallbacks;

	if (aesIvLength != OAES_BLOCK_SIZE)
	{
		Limelog("AES IV is incorrect length. Should be %d\n", aesIvLength);
		return -1;
	}

	oaesContext = oaes_alloc();
	if (oaesContext == NULL)
	{
		Limelog("Failed to allocate OpenAES context\n");
		return -1;
	}

	if (oaes_set_option(oaesContext, OAES_OPTION_CBC, aesIv) != OAES_RET_SUCCESS)
	{
		Limelog("Failed to set CBC and IV on OAES context\n");
		return -1;
	}

	if (oaes_key_import_data(oaesContext, (const unsigned char*)aesKeyData, aesKeyDataLength) != OAES_RET_SUCCESS)
	{
		Limelog("Failed to import AES key data\n");
		return -1;
	}

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);

	initialized = 1;
	return 0;
}

void destroyInputStream(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;

	if (oaesContext != NULL)
	{
		oaes_free(oaesContext);
		oaesContext = NULL;
	}

	entry = LbqDestroyLinkedBlockingQueue(&packetQueue);

	while (entry != NULL) {
		nextEntry = entry->flink;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}

	initialized = 0;
}

static void inputSendThreadProc(void* context) {
	SOCK_RET err;
	PPACKET_HOLDER holder;
	char encryptedBuffer[MAX_INPUT_PACKET_SIZE];
	size_t encryptedSize;

	while (!PltIsThreadInterrupted(&inputSendThread)) {
		int encryptedLengthPrefix;

		err = LbqWaitForQueueElement(&packetQueue, (void**) &holder);
		if (err != LBQ_SUCCESS) {
			Limelog("Input thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		encryptedSize = sizeof(encryptedBuffer);
		err = oaes_encrypt(oaesContext, (const unsigned char*) &holder->packet, holder->packetLength,
			(unsigned char*) encryptedBuffer, &encryptedSize);
		free(holder);
		if (err != OAES_RET_SUCCESS) {
			Limelog("Input thread terminating #2\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		// The first 32-bytes of the output are internal OAES stuff that we want to ignore
		encryptedSize -= 32;

		// Send the encrypted length first
		encryptedLengthPrefix = htonl((unsigned long) encryptedSize);
		err = send(inputSock, (const char*) &encryptedLengthPrefix, sizeof(encryptedLengthPrefix), 0);
		if (err <= 0) {
			Limelog("Input thread terminating #3\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		// Send the encrypted payload
		err = send(inputSock, (const char*) &encryptedBuffer[32], encryptedSize, 0);
		if (err <= 0) {
			Limelog("Input thread terminating #4\n");
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

	if (!initialized) {
		return -2;
	}

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

	if (!initialized) {
		return -2;
	}

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

	if (!initialized) {
		return -2;
	}

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_KEYBOARD_PACKET);
	holder->packet.keyboard.header.packetType = htonl(PACKET_TYPE_KEYBOARD);
	holder->packet.keyboard.keyAction = keyAction;
	holder->packet.keyboard.zero1 = 0;
	holder->packet.keyboard.keyCode = keyCode;
	holder->packet.keyboard.modifiers = modifiers;
	holder->packet.keyboard.zero2 = 0;

	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}

int LiSendControllerEvent(short buttonFlags, char leftTrigger, char rightTrigger,
	short leftStickX, short leftStickY, short rightStickX, short rightStickY)
{
	PPACKET_HOLDER holder;
	int err;

	if (!initialized) {
		return -2;
	}

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_CONTROLLER_PACKET);
	holder->packet.controller.header.packetType = htonl(PACKET_TYPE_CONTROLLER);
	holder->packet.controller.headerA = HEADER_A;
	holder->packet.controller.headerB = HEADER_B;
	holder->packet.controller.buttonFlags = buttonFlags;
	holder->packet.controller.leftTrigger = leftTrigger;
	holder->packet.controller.rightTrigger = rightTrigger;
	holder->packet.controller.leftStickX = leftStickX;
	holder->packet.controller.leftStickY = leftStickY;
	holder->packet.controller.rightStickX = rightStickX;
	holder->packet.controller.rightStickY = rightStickY;
	holder->packet.controller.tailA = TAIL_A;
	holder->packet.controller.tailB = TAIL_B;
	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}

int LiSendScrollEvent(char scrollClicks) {
	PPACKET_HOLDER holder;
	int err;

	if (!initialized) {
		return -2;
	}

	holder = malloc(sizeof(*holder));
	if (holder == NULL) {
		return -1;
	}

	holder->packetLength = sizeof(NV_SCROLL_PACKET);
	holder->packet.scroll.header.packetType = htonl(PACKET_TYPE_SCROLL);
	holder->packet.scroll.magicA = MAGIC_A;
	holder->packet.scroll.zero1 = 0;
	holder->packet.scroll.zero2 = 0;
	holder->packet.scroll.scrollAmt1 = htons(scrollClicks * 120);
	holder->packet.scroll.scrollAmt2 = holder->packet.scroll.scrollAmt1;
	holder->packet.scroll.zero3 = 0;

	err = LbqOfferQueueItem(&packetQueue, holder);
	if (err != LBQ_SUCCESS) {
		free(holder);
	}

	return err;
}