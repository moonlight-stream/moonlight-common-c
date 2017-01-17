#include "Limelight-internal.h"
#include "RtpFecQueue.h"

#define ushort(x) (unsigned short) ((x) % (UINT16_MAX+1))
#define isBefore(x, y) ushort((x) - (y)) > (UINT16_MAX/2)

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue) {
    memset(queue, 0, sizeof(*queue));
    queue->nextRtpSequenceNumber = UINT16_MAX;
    
    queue->currentFrameNumber = UINT16_MAX;
}

void RtpfCleanupQueue(PRTP_FEC_QUEUE queue) {
    while (queue->bufferHead != NULL) {
        PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
        queue->bufferHead = entry->next;
        free(entry->packet);
    }
}

// newEntry is contained within the packet buffer so we free the whole entry by freeing entry->packet
static int queuePacket(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY newEntry, int head, PRTP_PACKET packet) {
    PRTPFEC_QUEUE_ENTRY entry;

    // Don't queue packets we're already ahead of
    if (isBefore(packet->sequenceNumber, queue->nextRtpSequenceNumber)) {
        return 0;
    }

    // Don't queue duplicates either
    entry = queue->bufferHead;
    while (entry != NULL) {
        if (entry->packet->sequenceNumber == packet->sequenceNumber) {
            return 0;
        }

        entry = entry->next;
    }
    
    newEntry->packet = packet;
    newEntry->prev = NULL;
    newEntry->next = NULL;

    if (queue->bufferHead == NULL) {
        LC_ASSERT(queue->bufferSize == 0);
        queue->bufferHead = queue->bufferTail = newEntry;
    }
    else if (head) {
        LC_ASSERT(queue->bufferSize > 0);
        PRTPFEC_QUEUE_ENTRY oldHead = queue->bufferHead;
        newEntry->next = oldHead;
        LC_ASSERT(oldHead->prev == NULL);
        oldHead->prev = newEntry;
        queue->bufferHead = newEntry;
    }
    else {
        LC_ASSERT(queue->bufferSize > 0);
        PRTPFEC_QUEUE_ENTRY oldTail = queue->bufferTail;
        newEntry->prev = oldTail;
        LC_ASSERT(oldTail->next == NULL);
        oldTail->next = newEntry;
        queue->bufferTail = newEntry;
    }
    queue->bufferSize++;

    return 1;
}

static void queueRecoveredPacket(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY entry, PRTP_PACKET packet, int sequenceNumber) {
    //Correct RTP header as it isn't in the FEC data
    packet->header = queue->queueHead->packet->header;
    packet->sequenceNumber = sequenceNumber;
    
    int dataOffset = sizeof(*packet);
    if (packet->header & FLAG_EXTENSION) {
       dataOffset += 4; // 2 additional fields
    }

    PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)packet) + dataOffset);
    nvPacket->frameIndex = queue->currentFrameNumber;
    
    //Set size of generated packet
    int size = StreamConfig.packetSize + dataOffset;

    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    memcpy(&((unsigned char*) packet)[receiveSize], &size, sizeof(int));
    
    queuePacket(queue, entry, 0, packet);
}

static void repairPackets(PRTP_FEC_QUEUE queue) {
    //TODO repair packets and add through queueRecoveredPacket()
}

static void removeEntry(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY entry) {
    LC_ASSERT(entry != NULL);
    LC_ASSERT(queue->queueSize > 0);
    LC_ASSERT(queue->readyQueueHead != NULL);
    LC_ASSERT(queue->readyQueueTail != NULL);

    if (queue->queueHead == entry) {
        queue->queueHead = entry->next;
    }
    if (queue->queueTail == entry) {
        queue->queueTail = entry->prev;
    }

    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }
    queue->queueSize--;
}

int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, PRTPFEC_QUEUE_ENTRY packetEntry) {
    if (isBefore(packet->sequenceNumber, queue->nextRtpSequenceNumber)) {
        // Reject packets behind our current sequence number
        return RTPF_RET_REJECTED;
    }

    int dataOffset = sizeof(*packet);
    if (packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)packet) + dataOffset);

    if (nvPacket->frameIndex != queue->currentFrameNumber) {
        //Make current frame available to depacketizer
        queue->currentFrameNumber = nvPacket->frameIndex;
        if (queue->queueTail == NULL) {
            queue->queueHead = queue->bufferHead;
            queue->queueTail = queue->bufferTail;
        } else {
            queue->queueTail->next = queue->bufferHead;
            queue->queueTail = queue->bufferTail;
        }
        queue->bufferHead = NULL;
        queue->bufferTail = NULL;

        queue->queueSize = queue->bufferSize;
        queue->nextRtpSequenceNumber = queue->bufferHighestSequenceNumber;
        
        int fecIndex = (nvPacket->fecInfo & 0xFF000) >> 12;
        queue->bufferLowestSequenceNumber = ushort(packet->sequenceNumber - fecIndex);
        queue->bufferSize = 0;
        queue->bufferHighestSequenceNumber = packet->sequenceNumber;
        queue->bufferDataPackets = ((nvPacket->fecInfo & 0xFF00000) >> 20) / 4;
    } else if (isBefore(queue->bufferHighestSequenceNumber, packet->sequenceNumber)) {
        queue->bufferHighestSequenceNumber = packet->sequenceNumber;
    }
    
    if (!queuePacket(queue, packetEntry, 0, packet)) {
        return RTPF_RET_REJECTED;
    }
    else {
        if (queue->bufferSize < (ushort(packet->sequenceNumber - queue->bufferLowestSequenceNumber) + 1)) {
            repairPackets(queue);
        }

        return (queue->queueHead != NULL) ? RTPF_RET_QUEUED_PACKETS_READY : RTPF_RET_QUEUED_NOTHING_READY;
    }
}

PRTP_PACKET RtpfGetQueuedPacket(PRTP_FEC_QUEUE queue) {
    PRTPFEC_QUEUE_ENTRY queuedEntry, entry;

    // Find the next queued packet
    queuedEntry = NULL;
    entry = queue->queueHead;
    unsigned int lowestRtpSequenceNumber = UINT16_MAX;
    
    while (entry != NULL) {
        if (queuedEntry == NULL || isBefore(entry->packet->sequenceNumber, lowestRtpSequenceNumber)) {
            lowestRtpSequenceNumber = entry->packet->sequenceNumber;
            queuedEntry = entry;
        }

        entry = entry->next;
    }

    if (queuedEntry != NULL) {
        removeEntry(queue, queuedEntry);
        return queuedEntry->packet;
    } else {
        return NULL;
    }
}
