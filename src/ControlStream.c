#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

#include "ByteBuffer.h"

#include <enet/enet.h>

// NV control stream packet header for TCP
typedef struct _NVCTL_TCP_PACKET_HEADER {
    unsigned short type;
    unsigned short payloadLength;
} NVCTL_TCP_PACKET_HEADER, *PNVCTL_TCP_PACKET_HEADER;

typedef struct _NVCTL_ENET_PACKET_HEADER {
    unsigned short type;
} NVCTL_ENET_PACKET_HEADER, *PNVCTL_ENET_PACKET_HEADER;

typedef struct _QUEUED_FRAME_INVALIDATION_TUPLE {
    int startFrame;
    int endFrame;
    LINKED_BLOCKING_QUEUE_ENTRY entry;
} QUEUED_FRAME_INVALIDATION_TUPLE, *PQUEUED_FRAME_INVALIDATION_TUPLE;

static SOCKET ctlSock = INVALID_SOCKET;
static ENetHost* client;
static ENetPeer* peer;
static PLT_MUTEX enetMutex;

static PLT_THREAD lossStatsThread;
static PLT_THREAD invalidateRefFramesThread;
static PLT_THREAD controlReceiveThread;
static PLT_EVENT invalidateRefFramesEvent;
static int lossCountSinceLastReport;
static long lastGoodFrame;
static long lastSeenFrame;
static int stopping;
static int disconnectPending;

static int idrFrameRequired;
static LINKED_BLOCKING_QUEUE invalidReferenceFrameTuples;

#define IDX_START_A 0
#define IDX_REQUEST_IDR_FRAME 0
#define IDX_START_B 1
#define IDX_INVALIDATE_REF_FRAMES 2
#define IDX_LOSS_STATS 3
#define IDX_INPUT_DATA 5
#define IDX_RUMBLE_DATA 6
#define IDX_TERMINATION 7

#define CONTROL_STREAM_TIMEOUT_SEC 10

static const short packetTypesGen3[] = {
    0x1407, // Request IDR frame
    0x1410, // Start B
    0x1404, // Invalidate reference frames
    0x140c, // Loss Stats
    0x1417, // Frame Stats (unused)
    -1,     // Input data (unused)
    -1,     // Rumble data (unused)
    -1,     // Termination (unused)
};
static const short packetTypesGen4[] = {
    0x0606, // Request IDR frame
    0x0609, // Start B
    0x0604, // Invalidate reference frames
    0x060a, // Loss Stats
    0x0611, // Frame Stats (unused)
    -1,     // Input data (unused)
    -1,     // Rumble data (unused)
    -1,     // Termination (unused)
};
static const short packetTypesGen5[] = {
    0x0305, // Start A
    0x0307, // Start B
    0x0301, // Invalidate reference frames
    0x0201, // Loss Stats
    0x0204, // Frame Stats (unused)
    0x0207, // Input data
    -1,     // Rumble data (unused)
    -1,     // Termination (unused)
};
static const short packetTypesGen7[] = {
    0x0305, // Start A
    0x0307, // Start B
    0x0301, // Invalidate reference frames
    0x0201, // Loss Stats
    0x0204, // Frame Stats (unused)
    0x0206, // Input data
    0x010b, // Rumble data
    0x0100, // Termination
};

static const char requestIdrFrameGen3[] = { 0, 0 };
static const int startBGen3[] = { 0, 0, 0, 0xa };

static const char requestIdrFrameGen4[] = { 0, 0 };
static const char startBGen4[] = { 0 };

static const char startAGen5[] = { 0, 0 };
static const char startBGen5[] = { 0 };

static const short payloadLengthsGen3[] = {
    sizeof(requestIdrFrameGen3), // Request IDR frame
    sizeof(startBGen3), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    64, // Frame Stats
    -1, // Input data
};
static const short payloadLengthsGen4[] = {
    sizeof(requestIdrFrameGen4), // Request IDR frame
    sizeof(startBGen4), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    64, // Frame Stats
    -1, // Input data
};
static const short payloadLengthsGen5[] = {
    sizeof(startAGen5), // Start A
    sizeof(startBGen5), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    80, // Frame Stats
    -1, // Input data
};
static const short payloadLengthsGen7[] = {
    sizeof(startAGen5), // Start A
    sizeof(startBGen5), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    80, // Frame Stats
    -1, // Input data
};

static const char* preconstructedPayloadsGen3[] = {
    requestIdrFrameGen3,
    (char*)startBGen3
};
static const char* preconstructedPayloadsGen4[] = {
    requestIdrFrameGen4,
    startBGen4
};
static const char* preconstructedPayloadsGen5[] = {
    startAGen5,
    startBGen5
};
static const char* preconstructedPayloadsGen7[] = {
    startAGen5,
    startBGen5
};

static short* packetTypes;
static short* payloadLengths;
static char**preconstructedPayloads;

#define LOSS_REPORT_INTERVAL_MS 50

// Initializes the control stream
int initializeControlStream(void) {
    stopping = 0;
    PltCreateEvent(&invalidateRefFramesEvent);
    LbqInitializeLinkedBlockingQueue(&invalidReferenceFrameTuples, 20);
    PltCreateMutex(&enetMutex);

    if (AppVersionQuad[0] == 3) {
        packetTypes = (short*)packetTypesGen3;
        payloadLengths = (short*)payloadLengthsGen3;
        preconstructedPayloads = (char**)preconstructedPayloadsGen3;
    }
    else if (AppVersionQuad[0] == 4) {
        packetTypes = (short*)packetTypesGen4;
        payloadLengths = (short*)payloadLengthsGen4;
        preconstructedPayloads = (char**)preconstructedPayloadsGen4;
    }
    else if (AppVersionQuad[0] == 5) {
        packetTypes = (short*)packetTypesGen5;
        payloadLengths = (short*)payloadLengthsGen5;
        preconstructedPayloads = (char**)preconstructedPayloadsGen5;
    }
    else {
        packetTypes = (short*)packetTypesGen7;
        payloadLengths = (short*)payloadLengthsGen7;
        preconstructedPayloads = (char**)preconstructedPayloadsGen7;
    }

    idrFrameRequired = 0;
    lastGoodFrame = 0;
    lastSeenFrame = 0;
    lossCountSinceLastReport = 0;
    disconnectPending = 0;

    return 0;
}

void freeFrameInvalidationList(PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    PLINKED_BLOCKING_QUEUE_ENTRY nextEntry;

    while (entry != NULL) {
        nextEntry = entry->flink;
        free(entry->data);
        entry = nextEntry;
    }
}

// Cleans up control stream
void destroyControlStream(void) {
    LC_ASSERT(stopping);
    PltCloseEvent(&invalidateRefFramesEvent);
    freeFrameInvalidationList(LbqDestroyLinkedBlockingQueue(&invalidReferenceFrameTuples));
    PltDeleteMutex(&enetMutex);
}

int getNextFrameInvalidationTuple(PQUEUED_FRAME_INVALIDATION_TUPLE* qfit) {
    int err = LbqPollQueueElement(&invalidReferenceFrameTuples, (void**)qfit);
    return (err == LBQ_SUCCESS);
}

void queueFrameInvalidationTuple(int startFrame, int endFrame) {
    LC_ASSERT(startFrame <= endFrame);
    
    if (isReferenceFrameInvalidationEnabled()) {
        PQUEUED_FRAME_INVALIDATION_TUPLE qfit;
        qfit = malloc(sizeof(*qfit));
        if (qfit != NULL) {
            qfit->startFrame = startFrame;
            qfit->endFrame = endFrame;
            if (LbqOfferQueueItem(&invalidReferenceFrameTuples, qfit, &qfit->entry) == LBQ_BOUND_EXCEEDED) {
                // Too many invalidation tuples, so we need an IDR frame now
                free(qfit);
                idrFrameRequired = 1;
            }
        }
        else {
            idrFrameRequired = 1;
        }
    }
    else {
        idrFrameRequired = 1;
    }

    PltSetEvent(&invalidateRefFramesEvent);
}

// Request an IDR frame on demand by the decoder
void requestIdrOnDemand(void) {
    idrFrameRequired = 1;
    PltSetEvent(&invalidateRefFramesEvent);
}

// Invalidate reference frames lost by the network
void connectionDetectedFrameLoss(int startFrame, int endFrame) {
    queueFrameInvalidationTuple(startFrame, endFrame);
}

// When we receive a frame, update the number of our current frame
void connectionReceivedCompleteFrame(int frameIndex) {
    lastGoodFrame = frameIndex;
}

void connectionSawFrame(int frameIndex) {
    lastSeenFrame = frameIndex;
}

// When we lose packets, update our packet loss count
void connectionLostPackets(int lastReceivedPacket, int nextReceivedPacket) {
    lossCountSinceLastReport += (nextReceivedPacket - lastReceivedPacket) - 1;
}

// Reads an NV control stream packet from the TCP connection
static PNVCTL_TCP_PACKET_HEADER readNvctlPacketTcp(void) {
    NVCTL_TCP_PACKET_HEADER staticHeader;
    PNVCTL_TCP_PACKET_HEADER fullPacket;
    SOCK_RET err;

    err = recv(ctlSock, (char*)&staticHeader, sizeof(staticHeader), 0);
    if (err != sizeof(staticHeader)) {
        return NULL;
    }

    fullPacket = (PNVCTL_TCP_PACKET_HEADER)malloc(staticHeader.payloadLength + sizeof(staticHeader));
    if (fullPacket == NULL) {
        return NULL;
    }

    memcpy(fullPacket, &staticHeader, sizeof(staticHeader));
    if (staticHeader.payloadLength != 0) {
        err = recv(ctlSock, (char*)(fullPacket + 1), staticHeader.payloadLength, 0);
        if (err != staticHeader.payloadLength) {
            free(fullPacket);
            return NULL;
        }
    }

    return fullPacket;
}

static int sendMessageEnet(short ptype, short paylen, const void* payload) {
    PNVCTL_ENET_PACKET_HEADER packet;
    ENetPacket* enetPacket;
    int err;

    LC_ASSERT(AppVersionQuad[0] >= 5);

    enetPacket = enet_packet_create(NULL, sizeof(*packet) + paylen, ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket == NULL) {
        return 0;
    }

    packet = (PNVCTL_ENET_PACKET_HEADER)enetPacket->data;
    packet->type = ptype;
    memcpy(&packet[1], payload, paylen);

    PltLockMutex(&enetMutex);
    err = enet_peer_send(peer, 0, enetPacket);
    PltUnlockMutex(&enetMutex);
    if (err < 0) {
        Limelog("Failed to send ENet control packet\n");
        enet_packet_destroy(enetPacket);
        return 0;
    }
    
    PltLockMutex(&enetMutex);
    enet_host_flush(client);
    PltUnlockMutex(&enetMutex);

    return 1;
}

static int sendMessageTcp(short ptype, short paylen, const void* payload) {
    PNVCTL_TCP_PACKET_HEADER packet;
    SOCK_RET err;

    LC_ASSERT(AppVersionQuad[0] < 5);

    packet = malloc(sizeof(*packet) + paylen);
    if (packet == NULL) {
        return 0;
    }

    packet->type = ptype;
    packet->payloadLength = paylen;
    memcpy(&packet[1], payload, paylen);

    err = send(ctlSock, (char*) packet, sizeof(*packet) + paylen, 0);
    free(packet);

    if (err != sizeof(*packet) + paylen) {
        return 0;
    }

    return 1;
}

static int sendMessageAndForget(short ptype, short paylen, const void* payload) {
    int ret;

    // Unlike regular sockets, ENet sockets aren't safe to invoke from multiple
    // threads at once. We have to synchronize them with a lock.
    if (AppVersionQuad[0] >= 5) {
        ret = sendMessageEnet(ptype, paylen, payload);
    }
    else {
        ret = sendMessageTcp(ptype, paylen, payload);
    }

    return ret;
}

static int sendMessageAndDiscardReply(short ptype, short paylen, const void* payload) {
    if (AppVersionQuad[0] >= 5) {
        if (!sendMessageEnet(ptype, paylen, payload)) {
            return 0;
        }
    }
    else {
        PNVCTL_TCP_PACKET_HEADER reply;

        if (!sendMessageTcp(ptype, paylen, payload)) {
            return 0;
        }

        // Discard the response
        reply = readNvctlPacketTcp();
        if (reply == NULL) {
            return 0;
        }

        free(reply);
    }

    return 1;
}

// This intercept function drops disconnect events to allow us to process
// pending receives first. It works around what appears to be a bug in ENet
// where pending disconnects can cause loss of unprocessed received data.
static int ignoreDisconnectIntercept(ENetHost* host, ENetEvent* event) {
    if (host->receivedDataLength == sizeof(ENetProtocolHeader) + sizeof(ENetProtocolDisconnect)) {
        ENetProtocolHeader* protoHeader = (ENetProtocolHeader*)host->receivedData;
        ENetProtocolDisconnect* disconnect = (ENetProtocolDisconnect*)(protoHeader + 1);

        if ((disconnect->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_DISCONNECT) {
            Limelog("ENet disconnect event pending\n");
            disconnectPending = 1;
            if (event) {
                event->type = ENET_EVENT_TYPE_NONE;
            }
            return 1;
        }
    }

    return 0;
}

static void controlReceiveThreadFunc(void* context) {
    int err;

    // This is only used for ENet
    if (AppVersionQuad[0] < 5) {
        return;
    }

    long terminationErrorCode = -1;

    while (!PltIsThreadInterrupted(&controlReceiveThread)) {
        ENetEvent event;

        // Poll every 100 ms for new packets
        PltLockMutex(&enetMutex);
        err = serviceEnetHost(client, &event, 0);
        PltUnlockMutex(&enetMutex);
        if (err == 0) {
            // Handle a pending disconnect after unsuccessfully polling
            // for new events to handle.
            if (disconnectPending) {
                PltLockMutex(&enetMutex);
                // Wait 100 ms for pending receives after a disconnect and
                // 1 second for the pending disconnect to be processed after
                // removing the intercept callback.
                err = serviceEnetHost(client, &event, client->intercept ? 100 : 1000);
                if (err == 0) {
                    if (client->intercept) {
                        // Now that no pending receive events remain, we can
                        // remove our intercept hook and allow the server's
                        // disconnect to be processed as expected. We will wait
                        // 1 second for this disconnect to be processed before
                        // we tear down the connection anyway.
                        client->intercept = NULL;
                        PltUnlockMutex(&enetMutex);
                        continue;
                    }
                    else {
                        // The 1 second timeout has expired with no disconnect event
                        // retransmission after the first notification. We can only
                        // assume the server died tragically, so go ahead and tear down.
                        PltUnlockMutex(&enetMutex);
                        Limelog("Disconnect event timeout expired\n");
                        ListenerCallbacks.connectionTerminated(-1);
                        return;
                    }
                }
                else {
                    PltUnlockMutex(&enetMutex);
                }
            }
            else {
                // No events ready
                PltSleepMs(100);
                continue;
            }
        }

        if (err < 0) {
            Limelog("Control stream connection failed: %d\n", err);
            ListenerCallbacks.connectionTerminated(err);
            return;
        }

        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            PNVCTL_ENET_PACKET_HEADER ctlHdr = (PNVCTL_ENET_PACKET_HEADER)event.packet->data;

            if (event.packet->dataLength < sizeof(*ctlHdr)) {
                Limelog("Discarding runt control packet: %d < %d\n", event.packet->dataLength, (int)sizeof(*ctlHdr));
                enet_packet_destroy(event.packet);
                continue;
            }

            if (ctlHdr->type == packetTypes[IDX_RUMBLE_DATA]) {
                BYTE_BUFFER bb;

                BbInitializeWrappedBuffer(&bb, event.packet->data, sizeof(*ctlHdr), event.packet->dataLength - sizeof(*ctlHdr), BYTE_ORDER_LITTLE);
                BbAdvanceBuffer(&bb, 4);

                unsigned short controllerNumber;
                unsigned short lowFreqRumble;
                unsigned short highFreqRumble;

                BbGetShort(&bb, &controllerNumber);
                BbGetShort(&bb, &lowFreqRumble);
                BbGetShort(&bb, &highFreqRumble);

                ListenerCallbacks.rumble(controllerNumber, lowFreqRumble, highFreqRumble);
            }
            else if (ctlHdr->type == packetTypes[IDX_TERMINATION]) {
                BYTE_BUFFER bb;

                BbInitializeWrappedBuffer(&bb, event.packet->data, sizeof(*ctlHdr), event.packet->dataLength - sizeof(*ctlHdr), BYTE_ORDER_LITTLE);

                unsigned short terminationReason;

                BbGetShort(&bb, &terminationReason);

                Limelog("Server notified termination reason: 0x%04x\n", terminationReason);

                // SERVER_TERMINATED_INTENDED
                if (terminationReason == 0x0100) {
                    // Pass error code 0 to notify the client that this was not an error
                    terminationErrorCode = 0;
                }
                else {
                    // Otherwise pass the reason unmodified
                    terminationErrorCode = terminationReason;
                }

                // We don't actually notify the connection listener until we receive
                // the disconnect event from the server that confirms the termination.
            }

            enet_packet_destroy(event.packet);
        }
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            Limelog("Control stream received disconnect event\n");
            ListenerCallbacks.connectionTerminated(terminationErrorCode);
            return;
        }
    }
}

static void lossStatsThreadFunc(void* context) {
    char*lossStatsPayload;
    BYTE_BUFFER byteBuffer;

    lossStatsPayload = malloc(payloadLengths[IDX_LOSS_STATS]);
    if (lossStatsPayload == NULL) {
        Limelog("Loss Stats: malloc() failed\n");
        ListenerCallbacks.connectionTerminated(-1);
        return;
    }

    while (!PltIsThreadInterrupted(&lossStatsThread)) {
        // Construct the payload
        BbInitializeWrappedBuffer(&byteBuffer, lossStatsPayload, 0, payloadLengths[IDX_LOSS_STATS], BYTE_ORDER_LITTLE);
        BbPutInt(&byteBuffer, lossCountSinceLastReport);
        BbPutInt(&byteBuffer, LOSS_REPORT_INTERVAL_MS);
        BbPutInt(&byteBuffer, 1000);
        BbPutLong(&byteBuffer, lastGoodFrame);
        BbPutInt(&byteBuffer, 0);
        BbPutInt(&byteBuffer, 0);
        BbPutInt(&byteBuffer, 0x14);

        // Send the message (and don't expect a response)
        if (!sendMessageAndForget(packetTypes[IDX_LOSS_STATS],
            payloadLengths[IDX_LOSS_STATS], lossStatsPayload)) {
            free(lossStatsPayload);
            Limelog("Loss Stats: Transaction failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketFail());
            return;
        }

        // Clear the transient state
        lossCountSinceLastReport = 0;

        // Wait a bit
        PltSleepMs(LOSS_REPORT_INTERVAL_MS);
    }

    free(lossStatsPayload);
}

static void requestIdrFrame(void) {
    long long payload[3];

    if (AppVersionQuad[0] >= 5) {
        // Form the payload
        if (lastSeenFrame < 0x20) {
            payload[0] = 0;
            payload[1] = lastSeenFrame;
        }
        else {
            payload[0] = lastSeenFrame - 0x20;
            payload[1] = lastSeenFrame;
        }

        payload[2] = 0;

        // Send the reference frame invalidation request and read the response
        if (!sendMessageAndDiscardReply(packetTypes[IDX_INVALIDATE_REF_FRAMES],
            payloadLengths[IDX_INVALIDATE_REF_FRAMES], payload)) {
            Limelog("Request IDR Frame: Transaction failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketFail());
            return;
        }
    }
    else {
        // Send IDR frame request and read the response
        if (!sendMessageAndDiscardReply(packetTypes[IDX_REQUEST_IDR_FRAME],
            payloadLengths[IDX_REQUEST_IDR_FRAME], preconstructedPayloads[IDX_REQUEST_IDR_FRAME])) {
            Limelog("Request IDR Frame: Transaction failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketFail());
            return;
        }
    }

    Limelog("IDR frame request sent\n");
}

static void requestInvalidateReferenceFrames(void) {
    long long payload[3];
    PQUEUED_FRAME_INVALIDATION_TUPLE qfit;

    LC_ASSERT(isReferenceFrameInvalidationEnabled());

    if (!getNextFrameInvalidationTuple(&qfit)) {
        return;
    }

    LC_ASSERT(qfit->startFrame <= qfit->endFrame);

    payload[0] = qfit->startFrame;
    payload[1] = qfit->endFrame;
    payload[2] = 0;

    // Aggregate all lost frames into one range
    do {
        LC_ASSERT(qfit->endFrame >= payload[1]);
        payload[1] = qfit->endFrame;
        free(qfit);
    } while (getNextFrameInvalidationTuple(&qfit));

    // Send the reference frame invalidation request and read the response
    if (!sendMessageAndDiscardReply(packetTypes[IDX_INVALIDATE_REF_FRAMES],
        payloadLengths[IDX_INVALIDATE_REF_FRAMES], payload)) {
        Limelog("Request Invaldiate Reference Frames: Transaction failed: %d\n", (int)LastSocketError());
        ListenerCallbacks.connectionTerminated(LastSocketFail());
        return;
    }

    Limelog("Invalidate reference frame request sent (%d to %d)\n", (int)payload[0], (int)payload[1]);
}

static void invalidateRefFramesFunc(void* context) {
    while (!PltIsThreadInterrupted(&invalidateRefFramesThread)) {
        // Wait for a request to invalidate reference frames
        PltWaitForEvent(&invalidateRefFramesEvent);
        PltClearEvent(&invalidateRefFramesEvent);
        
        // Bail if we've been shutdown
        if (stopping) {
            break;
        }

        // Sometimes we absolutely need an IDR frame
        if (idrFrameRequired) {
            // Empty invalidate reference frames tuples
            PQUEUED_FRAME_INVALIDATION_TUPLE qfit;
            while (getNextFrameInvalidationTuple(&qfit)) {
                free(qfit);
            }

            // Send an IDR frame request
            idrFrameRequired = 0;
            requestIdrFrame();
        }
        else {
            // Otherwise invalidate reference frames
            requestInvalidateReferenceFrames();
        }
    }
}

// Stops the control stream
int stopControlStream(void) {
    stopping = 1;
    LbqSignalQueueShutdown(&invalidReferenceFrameTuples);
    PltSetEvent(&invalidateRefFramesEvent);

    // This must be set to stop in a timely manner
    LC_ASSERT(ConnectionInterrupted);

    if (ctlSock != INVALID_SOCKET) {
        shutdownTcpSocket(ctlSock);
    }
    
    PltInterruptThread(&lossStatsThread);
    PltInterruptThread(&invalidateRefFramesThread);
    PltInterruptThread(&controlReceiveThread);

    PltJoinThread(&lossStatsThread);
    PltJoinThread(&invalidateRefFramesThread);
    PltJoinThread(&controlReceiveThread);

    PltCloseThread(&lossStatsThread);
    PltCloseThread(&invalidateRefFramesThread);
    PltCloseThread(&controlReceiveThread);

    if (peer != NULL) {
        // We use enet_peer_disconnect_now() so the host knows immediately
        // of our termination and can cleanup properly for reconnection.
        enet_peer_disconnect_now(peer, 0);
        peer = NULL;
    }
    if (client != NULL) {
        enet_host_destroy(client);
        client = NULL;
    }
    
    if (ctlSock != INVALID_SOCKET) {
        closeSocket(ctlSock);
        ctlSock = INVALID_SOCKET;
    }

    return 0;
}

// Called by the input stream to send a packet for Gen 5+ servers
int sendInputPacketOnControlStream(unsigned char* data, int length) {
    LC_ASSERT(AppVersionQuad[0] >= 5);

    // Send the input data (no reply expected)
    if (sendMessageAndForget(packetTypes[IDX_INPUT_DATA], length, data) == 0) {
        return -1;
    }

    return 0;
}

// Starts the control stream
int startControlStream(void) {
    int err;

    if (AppVersionQuad[0] >= 5) {
        ENetAddress address;
        ENetEvent event;
        
        enet_address_set_address(&address, (struct sockaddr *)&RemoteAddr, RemoteAddrLen);
        enet_address_set_port(&address, 47999);

        // Create a client that can use 1 outgoing connection and 1 channel
        client = enet_host_create(address.address.ss_family, NULL, 1, 1, 0, 0);
        if (client == NULL) {
            return -1;
        }

        client->intercept = ignoreDisconnectIntercept;

        // Connect to the host
        peer = enet_host_connect(client, &address, 1, 0);
        if (peer == NULL) {
            enet_host_destroy(client);
            client = NULL;
            return -1;
        }

        // Wait for the connect to complete
        if (serviceEnetHost(client, &event, CONTROL_STREAM_TIMEOUT_SEC * 1000) <= 0 ||
            event.type != ENET_EVENT_TYPE_CONNECT) {
            Limelog("RTSP: Failed to connect to UDP port 47999\n");
            enet_peer_reset(peer);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
            return -1;
        }

        // Ensure the connect verify ACK is sent immediately
        enet_host_flush(client);
        
        // Set the max peer timeout to 10 seconds
        enet_peer_timeout(peer, ENET_PEER_TIMEOUT_LIMIT, ENET_PEER_TIMEOUT_MINIMUM, 10000);
    }
    else {
        ctlSock = connectTcpSocket(&RemoteAddr, RemoteAddrLen,
            47995, CONTROL_STREAM_TIMEOUT_SEC);
        if (ctlSock == INVALID_SOCKET) {
            return LastSocketFail();
        }

        enableNoDelay(ctlSock);
    }

    err = PltCreateThread(controlReceiveThreadFunc, NULL, &controlReceiveThread);
    if (err != 0) {
        stopping = 1;
        if (ctlSock != INVALID_SOCKET) {
            closeSocket(ctlSock);
            ctlSock = INVALID_SOCKET;
        }
        else {
            enet_peer_disconnect_now(peer, 0);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
        }
        return err;
    }

    // Send START A
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_A],
        payloadLengths[IDX_START_A],
        preconstructedPayloads[IDX_START_A])) {
        Limelog("Start A failed: %d\n", (int)LastSocketError());
        err = LastSocketFail();
        stopping = 1;

        if (ctlSock != INVALID_SOCKET) {
            shutdownTcpSocket(ctlSock);
        }
        else {
            ConnectionInterrupted = 1;
        }

        PltInterruptThread(&controlReceiveThread);
        PltJoinThread(&controlReceiveThread);
        PltCloseThread(&controlReceiveThread);

        if (ctlSock != INVALID_SOCKET) {
            closeSocket(ctlSock);
            ctlSock = INVALID_SOCKET;
        }
        else {
            enet_peer_disconnect_now(peer, 0);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
        }
        return err;
    }

    // Send START B
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_B],
        payloadLengths[IDX_START_B],
        preconstructedPayloads[IDX_START_B])) {
        Limelog("Start B failed: %d\n", (int)LastSocketError());
        err = LastSocketFail();
        stopping = 1;

        if (ctlSock != INVALID_SOCKET) {
            shutdownTcpSocket(ctlSock);
        }
        else {
            ConnectionInterrupted = 1;
        }

        PltInterruptThread(&controlReceiveThread);
        PltJoinThread(&controlReceiveThread);
        PltCloseThread(&controlReceiveThread);

        if (ctlSock != INVALID_SOCKET) {
            closeSocket(ctlSock);
            ctlSock = INVALID_SOCKET;
        }
        else {
            enet_peer_disconnect_now(peer, 0);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
        }
        return err;
    }

    err = PltCreateThread(lossStatsThreadFunc, NULL, &lossStatsThread);
    if (err != 0) {
        stopping = 1;

        if (ctlSock != INVALID_SOCKET) {
            shutdownTcpSocket(ctlSock);
        }
        else {
            ConnectionInterrupted = 1;
        }

        PltInterruptThread(&controlReceiveThread);
        PltJoinThread(&controlReceiveThread);
        PltCloseThread(&controlReceiveThread);

        if (ctlSock != INVALID_SOCKET) {
            closeSocket(ctlSock);
            ctlSock = INVALID_SOCKET;
        }
        else {
            enet_peer_disconnect_now(peer, 0);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
        }
        return err;
    }

    err = PltCreateThread(invalidateRefFramesFunc, NULL, &invalidateRefFramesThread);
    if (err != 0) {
        stopping = 1;

        if (ctlSock != INVALID_SOCKET) {
            shutdownTcpSocket(ctlSock);
        }
        else {
            ConnectionInterrupted = 1;
        }

        PltInterruptThread(&lossStatsThread);
        PltJoinThread(&lossStatsThread);
        PltCloseThread(&lossStatsThread);

        PltInterruptThread(&controlReceiveThread);
        PltJoinThread(&controlReceiveThread);
        PltCloseThread(&controlReceiveThread);

        if (ctlSock != INVALID_SOCKET) {
            closeSocket(ctlSock);
            ctlSock = INVALID_SOCKET;
        }
        else {
            enet_peer_disconnect_now(peer, 0);
            peer = NULL;
            enet_host_destroy(client);
            client = NULL;
        }

        return err;
    }

    return 0;
}
