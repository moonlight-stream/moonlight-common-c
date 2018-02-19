#include "Limelight-internal.h"
#include "RtpFecQueue.h"
#include "rs.h"

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
    
    while (queue->queueHead != NULL) {
        PRTPFEC_QUEUE_ENTRY entry = queue->queueHead;
        queue->queueHead = entry->next;
        free(entry->packet);
    }
}

// newEntry is contained within the packet buffer so we free the whole entry by freeing entry->packet
static int queuePacket(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY newEntry, int head, PRTP_PACKET packet, int length, int isParity) {
    PRTPFEC_QUEUE_ENTRY entry;
    
    LC_ASSERT(!isBefore16(packet->sequenceNumber, queue->nextRtpSequenceNumber));

    // Don't queue duplicates either
    entry = queue->bufferHead;
    while (entry != NULL) {
        if (entry->packet->sequenceNumber == packet->sequenceNumber) {
            return 0;
        }

        entry = entry->next;
    }

    newEntry->packet = packet;
    newEntry->length = length;
    newEntry->isParity = isParity;
    newEntry->receiveTimeMs = PltGetMillis();
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

// Returns 0 if the frame is completely constructed
static int reconstructFrame(PRTP_FEC_QUEUE queue) {
    int totalPackets = U16(queue->bufferHighestSequenceNumber - queue->bufferLowestSequenceNumber) + 1;
    int totalParityPackets = (queue->bufferDataPackets * queue->fecPercentage + 99) / 100;
    int parityPackets = totalPackets - queue->bufferDataPackets;
    int missingPackets = totalPackets - queue->bufferSize;
    int ret;
    
    if (parityPackets < missingPackets) {
        // Not enough parity data to recover yet
        return -1;
    }
    
    if (queue->receivedBufferDataPackets == queue->bufferDataPackets) {
        // We've received a full frame with no need for FEC.
        return 0;
    }

    reed_solomon* rs = NULL;
    unsigned char** packets = malloc(totalPackets * sizeof(unsigned char*));
    unsigned char* marks = malloc(totalPackets * sizeof(unsigned char));
    if (packets == NULL || marks == NULL) {
        ret = -2;
        goto cleanup;
    }
    
    rs = reed_solomon_new(queue->bufferDataPackets, totalParityPackets);
    
    // This could happen in an OOM condition, but it could also mean the FEC data
    // that we fed to reed_solomon_new() is bogus, so we'll assert to get a better look.
    LC_ASSERT(rs != NULL);
    if (rs == NULL) {
        ret = -3;
        goto cleanup;
    }
    
    rs->shards = queue->bufferDataPackets + missingPackets; //Don't let RS complain about missing parity packets

    memset(marks, 1, sizeof(char) * (totalPackets));
    
    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    int packetBufferSize = receiveSize + sizeof(RTPFEC_QUEUE_ENTRY);

    PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
    while (entry != NULL) {
        int index = U16(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber);
        packets[index] = (unsigned char*) entry->packet;
        marks[index] = 0;
        
        //Set padding to zero
        if (entry->length < receiveSize) {
            memset(&packets[index][entry->length], 0, receiveSize - entry->length);
        }

        entry = entry->next;
    }

    int i;
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            packets[i] = malloc(packetBufferSize);
            if (packets[i] == NULL) {
                ret = -4;
                goto cleanup_packets;
            }
        }
    }
    
    ret = reed_solomon_reconstruct(rs, packets, marks, totalPackets, receiveSize);
    
    // We should always provide enough parity to recover the missing data successfully.
    // If this fails, something is probably wrong with our FEC state.
    LC_ASSERT(ret == 0);

cleanup_packets:
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            // Only submit frame data, not FEC packets
            if (ret == 0 && i < queue->bufferDataPackets) {
                PRTPFEC_QUEUE_ENTRY queueEntry = (PRTPFEC_QUEUE_ENTRY)&packets[i][receiveSize];
                PRTP_PACKET rtpPacket = (PRTP_PACKET) packets[i];
                rtpPacket->sequenceNumber = U16(i + queue->bufferLowestSequenceNumber);
                rtpPacket->header = queue->bufferHead->packet->header;
                
                int dataOffset = sizeof(*rtpPacket);
                if (rtpPacket->header & FLAG_EXTENSION) {
                    dataOffset += 4; // 2 additional fields
                }

                PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)rtpPacket) + dataOffset);
                nvPacket->frameIndex = queue->currentFrameNumber;

                // FEC recovered frames may have extra zero padding at the end. This is
                // fine per H.264 Annex B which states trailing zero bytes must be
                // discarded by decoders. It's not safe to strip all zero padding because
                // it may be a legitimate part of the H.264 bytestream.

                LC_ASSERT(isBefore16(rtpPacket->sequenceNumber, queue->bufferFirstParitySequenceNumber));
                queuePacket(queue, queueEntry, 0, rtpPacket, StreamConfig.packetSize + dataOffset, 0);
            } else if (packets[i] != NULL) {
                free(packets[i]);
            }
        }
    }

cleanup:
    reed_solomon_release(rs);

    if (packets != NULL)
        free(packets);

    if (marks != NULL)
        free(marks);
    
    return ret;
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

int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, int length, PRTPFEC_QUEUE_ENTRY packetEntry) {
    if (isBefore16(packet->sequenceNumber, queue->nextRtpSequenceNumber)) {
        // Reject packets behind our current sequence number
        return RTPF_RET_REJECTED;
    }

    int dataOffset = sizeof(*packet);
    if (packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)packet) + dataOffset);
    
    if (isBefore16(nvPacket->frameIndex, queue->currentFrameNumber)) {
        // Reject frames behind our current frame number
        return RTPF_RET_REJECTED;
    }
    
    // Reinitialize the queue if it's empty after a frame delivery or
    // if we can't finish a frame before receiving the next one.
    if (queue->bufferSize == 0 || queue->currentFrameNumber != nvPacket->frameIndex) {
        if (queue->currentFrameNumber != nvPacket->frameIndex && queue->bufferSize != 0) {
            Limelog("Unrecoverable frame %d: %d+%d=%d received < %d needed\n",
                    queue->currentFrameNumber, queue->receivedBufferDataPackets,
                    queue->bufferSize - queue->receivedBufferDataPackets,
                    queue->bufferSize,
                    queue->bufferDataPackets);
        }
        
        queue->currentFrameNumber = nvPacket->frameIndex;
        queue->nextRtpSequenceNumber = queue->bufferHighestSequenceNumber;
        
        // Discard any unsubmitted buffers from the previous frame
        while (queue->bufferHead != NULL) {
            PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
            queue->bufferHead = entry->next;
            free(entry->packet);
        }
        
        queue->bufferTail = NULL;
        queue->bufferSize = 0;
        
        int fecIndex = (nvPacket->fecInfo & 0xFF000) >> 12;
        queue->bufferLowestSequenceNumber = U16(packet->sequenceNumber - fecIndex);
        queue->receivedBufferDataPackets = 0;
        queue->bufferHighestSequenceNumber = packet->sequenceNumber;
        queue->bufferDataPackets = ((nvPacket->fecInfo & 0xFFF00000) >> 20) / 4;
        queue->fecPercentage = ((nvPacket->fecInfo & 0xFF0) >> 4);
        queue->bufferFirstParitySequenceNumber = U16(queue->bufferLowestSequenceNumber + queue->bufferDataPackets);
    } else if (isBefore16(queue->bufferHighestSequenceNumber, packet->sequenceNumber)) {
        queue->bufferHighestSequenceNumber = packet->sequenceNumber;
    }
    
    if (!queuePacket(queue, packetEntry, 0, packet, length, !isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber))) {
        return RTPF_RET_REJECTED;
    }
    else {
        if (isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber)) {
            queue->receivedBufferDataPackets++;
        }
        
        // Try to submit this frame. If we haven't received enough packets,
        // this will fail and we'll keep waiting.
        if (reconstructFrame(queue) == 0) {
            // Queue the pending frame data
            if (queue->queueTail == NULL) {
                queue->queueHead = queue->bufferHead;
                queue->queueTail = queue->bufferTail;
            } else {
                queue->queueTail->next = queue->bufferHead;
                queue->queueTail = queue->bufferTail;
            }
            queue->queueSize += queue->bufferSize;
            
            // Clear the buffer list
            queue->bufferHead = NULL;
            queue->bufferTail = NULL;
            queue->bufferSize = 0;
            
            // Ignore any more packets for this frame
            queue->currentFrameNumber++;
        }

        return (queue->queueHead != NULL) ? RTPF_RET_QUEUED_PACKETS_READY : RTPF_RET_QUEUED_NOTHING_READY;
    }
}

PRTPFEC_QUEUE_ENTRY RtpfGetQueuedPacket(PRTP_FEC_QUEUE queue) {
    PRTPFEC_QUEUE_ENTRY queuedEntry, entry;

    // Find the next queued packet
    queuedEntry = NULL;
    entry = queue->queueHead;
    unsigned int lowestRtpSequenceNumber = UINT16_MAX;
    
    while (entry != NULL) {
        // Never return parity packets
        if (entry->isParity) {
            PRTPFEC_QUEUE_ENTRY parityEntry = entry;
            
            // Skip this entry
            entry = parityEntry->next;
            
            // Remove this entry
            removeEntry(queue, parityEntry);
            
            // Free the entry and packet
            free(parityEntry->packet);
            
            continue;
        }
        
        if (queuedEntry == NULL || isBefore16(entry->packet->sequenceNumber, lowestRtpSequenceNumber)) {
            lowestRtpSequenceNumber = entry->packet->sequenceNumber;
            queuedEntry = entry;
        }

        entry = entry->next;
    }

    if (queuedEntry != NULL) {
        removeEntry(queue, queuedEntry);
        queuedEntry->prev = queuedEntry->next = NULL;
        return queuedEntry;
    } else {
        return NULL;
    }
}
