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
static PLT_EVENT invalidateRefFramesEvent;
static int lossCountSinceLastReport;
static long lastGoodFrame;
static long lastSeenFrame;
static int stopping;

static int idrFrameRequired;
static LINKED_BLOCKING_QUEUE invalidReferenceFrameTuples;

#define IDX_START_A 0
#define IDX_REQUEST_IDR_FRAME 0
#define IDX_START_B 1
#define IDX_INVALIDATE_REF_FRAMES 2
#define IDX_LOSS_STATS 3
#define IDX_INPUT_DATA 5

#define CONTROL_STREAM_TIMEOUT_SEC 10

static const short packetTypesGen3[] = {
    0x1407, // Request IDR frame
    0x1410, // Start B
    0x1404, // Invalidate reference frames
    0x140c, // Loss Stats
    0x1417, // Frame Stats (unused)
    -1,     // Input data (unused)
};
static const short packetTypesGen4[] = {
    0x0606, // Request IDR frame
    0x0609, // Start B
    0x0604, // Invalidate reference frames
    0x060a, // Loss Stats
    0x0611, // Frame Stats (unused)
    -1,     // Input data (unused)
};
static const short packetTypesGen5[] = {
    0x0305, // Start A
    0x0307, // Start B
    0x0301, // Invalidate reference frames
    0x0201, // Loss Stats
    0x0204, // Frame Stats (unused)
    0x0207, // Input data
};
static const short packetTypesGen7[] = {
    0x0305, // Start A
    0x0307, // Start B
    0x0301, // Invalidate reference frames
    0x0201, // Loss Stats
    0x0204, // Frame Stats (unused)
    0x0206, // Input data
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
    ENetEvent event;
    int err;

    LC_ASSERT(AppVersionQuad[0] >= 5);

    // Gen 5+ servers do control protocol over ENet instead of TCP
    while ((err = serviceEnetHost(client, &event, 0)) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            Limelog("Control stream received disconnect event\n");
            return 0;
        }
    }
    
    if (err < 0) {
        Limelog("Control stream connection failed\n");
        return 0;
    }

    packet = malloc(sizeof(*packet) + paylen);
    if (packet == NULL) {
        return 0;
    }

    packet->type = ptype;
    memcpy(&packet[1], payload, paylen);

    enetPacket = enet_packet_create(packet, sizeof(*packet) + paylen, ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket == NULL) {
        free(packet);
        return 0;
    }

    if (enet_peer_send(peer, 0, enetPacket) < 0) {
        Limelog("Failed to send ENet control packet\n");
        enet_packet_destroy(enetPacket);
        free(packet);
        return 0;
    }
    
    enet_host_flush(client);

    free(packet);

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
        PltLockMutex(&enetMutex);
        ret = sendMessageEnet(ptype, paylen, payload);
        PltUnlockMutex(&enetMutex);
    }
    else {
        ret = sendMessageTcp(ptype, paylen, payload);
    }

    return ret;
}

static int sendMessageAndDiscardReply(short ptype, short paylen, const void* payload) {
    // Discard the response
    if (AppVersionQuad[0] >= 5) {
        ENetEvent event;

        PltLockMutex(&enetMutex);

        if (!sendMessageEnet(ptype, paylen, payload)) {
            PltUnlockMutex(&enetMutex);
            return 0;
        }

        if (serviceEnetHost(client, &event, CONTROL_STREAM_TIMEOUT_SEC * 1000) <= 0 ||
            event.type != ENET_EVENT_TYPE_RECEIVE) {
            PltUnlockMutex(&enetMutex);
            return 0;
        }

        enet_packet_destroy(event.packet);

        PltUnlockMutex(&enetMutex);
    }
    else {
        PNVCTL_TCP_PACKET_HEADER reply;

        if (!sendMessageTcp(ptype, paylen, payload)) {
            return 0;
        }

        reply = readNvctlPacketTcp();
        if (reply == NULL) {
            return 0;
        }

        free(reply);
    }

    return 1;
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
            ListenerCallbacks.connectionTerminated(LastSocketError());
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
            payload[1] = 0x20;
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
            ListenerCallbacks.connectionTerminated(LastSocketError());
            return;
        }
    }
    else {
        // Send IDR frame request and read the response
        if (!sendMessageAndDiscardReply(packetTypes[IDX_REQUEST_IDR_FRAME],
            payloadLengths[IDX_REQUEST_IDR_FRAME], preconstructedPayloads[IDX_REQUEST_IDR_FRAME])) {
            Limelog("Request IDR Frame: Transaction failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
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
        ListenerCallbacks.connectionTerminated(LastSocketError());
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

    PltJoinThread(&lossStatsThread);
    PltJoinThread(&invalidateRefFramesThread);

    PltCloseThread(&lossStatsThread);
    PltCloseThread(&invalidateRefFramesThread);

    if (peer != NULL) {
        enet_peer_reset(peer);
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

    // Send START A
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_A],
        payloadLengths[IDX_START_A],
        preconstructedPayloads[IDX_START_A])) {
        Limelog("Start A failed: %d\n", (int)LastSocketError());
        err = LastSocketFail();
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

    // Send START B
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_B],
        payloadLengths[IDX_START_B],
        preconstructedPayloads[IDX_START_B])) {
        Limelog("Start B failed: %d\n", (int)LastSocketError());
        err = LastSocketFail();
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

    err = PltCreateThread(lossStatsThreadFunc, NULL, &lossStatsThread);
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
