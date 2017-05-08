#include "Limelight-internal.h"
#include "RtpFecQueue.h"
#include "rs.h"

#define ushort(x) (unsigned short) ((x) % (UINT16_MAX+1))
#define isBefore(x, y) ushort((x) - (y)) > (UINT16_MAX/2)

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue) {
    reed_solomon_init();
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

static void repairPackets(PRTP_FEC_QUEUE queue) {
    int totalPackets = ushort(queue->bufferHighestSequenceNumber - queue->bufferLowestSequenceNumber) + 1;
    int totalParityPackets = (queue->bufferDataPackets * queue->fecPercentage + 99) / 100;
    int parityPackets = totalPackets - queue->bufferDataPackets;
    int missingPackets = totalPackets - queue->bufferSize;
    
    if (parityPackets < missingPackets || parityPackets <= 0) {
        return;
    }
    
    reed_solomon* rs = reed_solomon_new(queue->bufferDataPackets, totalParityPackets);

    int* missing = malloc(missingPackets * sizeof(int));
    unsigned char** packets = malloc(totalPackets * sizeof(unsigned char*));
    unsigned char* marks = malloc(totalPackets * sizeof(unsigned char));
    if (rs == NULL || missing == NULL || packets == NULL || marks == NULL)
        goto cleanup;

    rs->shards = queue->bufferDataPackets + missingPackets; //Don't let RS complain about missing parity packets

    memset(marks, 1, sizeof(char) * (totalPackets));
    
    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    int packetBufferSize = receiveSize + sizeof(int) + sizeof(RTPFEC_QUEUE_ENTRY);

    PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
    while (entry != NULL) {
        int index = ushort(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber);
        packets[index] = (unsigned char*) entry->packet;
        marks[index] = 0;

        int size;
        memcpy(&size, &packets[index][StreamConfig.packetSize + MAX_RTP_HEADER_SIZE], sizeof(int));
        
        //Set padding to zero
        if (size < receiveSize) {
            memset(&packets[index][size], 0, receiveSize - size);
        }

        entry = entry->next;
    }

    int i;
    int ret = -1;
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            packets[i] = malloc(packetBufferSize);
            if (packets[i] == NULL) {
                goto cleanup_packets;
            }
        }
    }
    
    ret = reed_solomon_reconstruct(rs, packets, marks, totalPackets, receiveSize);

cleanup_packets:
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            if (ret == 0) {
                PRTPFEC_QUEUE_ENTRY queueEntry = (PRTPFEC_QUEUE_ENTRY)&packets[i][receiveSize + sizeof(int)];
                PRTP_PACKET rtpPacket = (PRTP_PACKET) packets[i];
                rtpPacket->sequenceNumber = ushort(i + queue->bufferLowestSequenceNumber);
                rtpPacket->header = queue->bufferHead->packet->header;
                
                int dataOffset = sizeof(*rtpPacket);
                if (rtpPacket->header & FLAG_EXTENSION) {
                    dataOffset += 4; // 2 additional fields
                }

                PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)rtpPacket) + dataOffset);
                nvPacket->frameIndex = queue->currentFrameNumber;
                
                //Remove padding of generated packet
                int size = StreamConfig.packetSize + dataOffset;
                if (rtpPacket->sequenceNumber == ushort(queue->bufferLowestSequenceNumber + queue->bufferDataPackets - 1)) {
                    while (packets[i][size-1] == 0) {
                        size--;
                    }
                }

                memcpy(&packets[i][receiveSize], &size, sizeof(int));
                queuePacket(queue, queueEntry, 0, rtpPacket);
            } else if (packets[i] != NULL) {
                free(packets[i]);
            }
        }
    }

cleanup:
    reed_solomon_release(rs);

    if (missing != NULL)
        free(missing);

    if (packets != NULL)
        free(packets);

    if (marks != NULL)
        free(marks);
}

static void removeEntry(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY entry) {
    LC_ASSERT(entry != NULL);
    LC_ASSERT(queue->queueSize > 0);
    LC_ASSERT(queue->queueHead != NULL);
    LC_ASSERT(queue->queueTail != NULL);

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
        } else if (queue->bufferHead != NULL) {
            queue->queueTail->next = queue->bufferHead;
            queue->queueTail = queue->bufferTail;
        } else {
            LC_ASSERT(queue->bufferTail == NULL);
            LC_ASSERT(queue->bufferSize == 0);
        }
        
        queue->bufferHead = NULL;
        queue->bufferTail = NULL;

        queue->queueSize += queue->bufferSize;
        queue->nextRtpSequenceNumber = queue->bufferHighestSequenceNumber;
        
        int fecIndex = (nvPacket->fecInfo & 0xFF000) >> 12;
        queue->bufferLowestSequenceNumber = ushort(packet->sequenceNumber - fecIndex);
        queue->bufferSize = 0;
        queue->bufferHighestSequenceNumber = packet->sequenceNumber;
        queue->bufferDataPackets = ((nvPacket->fecInfo & 0xFF00000) >> 20) / 4;
        queue->fecPercentage = ((nvPacket->fecInfo & 0xFF0) >> 4);
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
