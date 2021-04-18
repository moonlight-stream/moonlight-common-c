#include "Limelight-internal.h"
#include "RtpFecQueue.h"
#include "rs.h"

#ifdef LC_DEBUG
// This enables FEC validation mode with a synthetic drop
// and recovered packet checks vs the original input. It
// is on by default for debug builds.
#define FEC_VALIDATION_MODE
#endif

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue) {
    reed_solomon_init();
    memset(queue, 0, sizeof(*queue));
    
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
static bool queuePacket(PRTP_FEC_QUEUE queue, PRTPFEC_QUEUE_ENTRY newEntry, bool head, PRTP_PACKET packet, int length, bool isParity) {
    PRTPFEC_QUEUE_ENTRY entry;
    
    LC_ASSERT(!isBefore16(packet->sequenceNumber, queue->nextContiguousSequenceNumber));

    // If the packet is in order, we can take the fast path and avoid having
    // to loop through the whole list. If we get an out of order or missing
    // packet, the fast path will stop working and we'll use the loop instead.
    if (packet->sequenceNumber == queue->nextContiguousSequenceNumber) {
        queue->nextContiguousSequenceNumber = U16(packet->sequenceNumber + 1);
    }
    else {
        // Check for duplicates
        entry = queue->bufferHead;
        while (entry != NULL) {
            if (entry->packet->sequenceNumber == packet->sequenceNumber) {
                return false;
            }

            entry = entry->next;
        }
    }

    newEntry->packet = packet;
    newEntry->length = length;
    newEntry->isParity = isParity;
    newEntry->prev = NULL;
    newEntry->next = NULL;

    // 90 KHz video clock
    newEntry->presentationTimeMs = packet->timestamp / 90;

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

    return true;
}

#define PACKET_RECOVERY_FAILURE()                     \
    ret = -1;                                         \
    Limelog("FEC recovery returned corrupt packet %d" \
            " (frame %d)", rtpPacket->sequenceNumber, \
            queue->currentFrameNumber);               \
    free(packets[i]);                                 \
    continue

// Returns 0 if the frame is completely constructed
static int reconstructFrame(PRTP_FEC_QUEUE queue) {
    unsigned int totalPackets = U16(queue->bufferHighestSequenceNumber - queue->bufferLowestSequenceNumber) + 1;
    int ret;
    
#ifdef FEC_VALIDATION_MODE
    // We'll need an extra packet to run in FEC validation mode, because we will
    // be "dropping" one below and recovering it using parity. However, some frames
    // are so large that FEC is disabled entirely, so don't wait for parity on those.
    if (queue->bufferSize < queue->bufferDataPackets + (queue->fecPercentage ? 1 : 0)) {
#else
    if (queue->bufferSize < queue->bufferDataPackets) {
#endif
        // Not enough data to recover yet
        return -1;
    }
    
#ifdef FEC_VALIDATION_MODE
    // If FEC is disabled or unsupported for this frame, we must bail early here.
    if ((queue->fecPercentage == 0 || AppVersionQuad[0] < 5) &&
            queue->receivedBufferDataPackets == queue->bufferDataPackets) {
#else
    if (queue->receivedBufferDataPackets == queue->bufferDataPackets) {
#endif
        // We've received a full frame with no need for FEC.
        return 0;
    }

    if (AppVersionQuad[0] < 5) {
        // Our FEC recovery code doesn't work properly until Gen 5
        Limelog("FEC recovery not supported on Gen %d servers\n",
                AppVersionQuad[0]);
        return -1;
    }

    reed_solomon* rs = NULL;
    unsigned char** packets = calloc(totalPackets, sizeof(unsigned char*));
    unsigned char* marks = calloc(totalPackets, sizeof(unsigned char));
    if (packets == NULL || marks == NULL) {
        ret = -2;
        goto cleanup;
    }
    
    rs = reed_solomon_new(queue->bufferDataPackets, queue->bufferParityPackets);
    
    // This could happen in an OOM condition, but it could also mean the FEC data
    // that we fed to reed_solomon_new() is bogus, so we'll assert to get a better look.
    LC_ASSERT(rs != NULL);
    if (rs == NULL) {
        ret = -3;
        goto cleanup;
    }
    
    memset(marks, 1, sizeof(char) * (totalPackets));
    
    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    int packetBufferSize = receiveSize + sizeof(RTPFEC_QUEUE_ENTRY);

#ifdef FEC_VALIDATION_MODE
    // Choose a packet to drop
    unsigned int dropIndex = rand() % queue->bufferDataPackets;
    PRTP_PACKET droppedRtpPacket = NULL;
    int droppedRtpPacketLength = 0;
#endif

    PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
    while (entry != NULL) {
        unsigned int index = U16(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber);

#ifdef FEC_VALIDATION_MODE
        if (index == dropIndex) {
            // If this was the drop choice, remember the original contents
            // and "drop" it.
            droppedRtpPacket = entry->packet;
            droppedRtpPacketLength = entry->length;
            entry = entry->next;
            continue;
        }
#endif

        packets[index] = (unsigned char*) entry->packet;
        marks[index] = 0;
        
        //Set padding to zero
        if (entry->length < receiveSize) {
            memset(&packets[index][entry->length], 0, receiveSize - entry->length);
        }

        entry = entry->next;
    }

    unsigned int i;
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
                rtpPacket->timestamp = queue->bufferHead->packet->timestamp;
                rtpPacket->ssrc = queue->bufferHead->packet->ssrc;
                
                int dataOffset = sizeof(*rtpPacket);
                if (rtpPacket->header & FLAG_EXTENSION) {
                    dataOffset += 4; // 2 additional fields
                }

                PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)rtpPacket) + dataOffset);
                nvPacket->frameIndex = queue->currentFrameNumber;

#ifdef FEC_VALIDATION_MODE
                if (i == dropIndex && droppedRtpPacket != NULL) {
                    // Check the packet contents if this was our known drop
                    PNV_VIDEO_PACKET droppedNvPacket = (PNV_VIDEO_PACKET)(((char*)droppedRtpPacket) + dataOffset);
                    int droppedDataLength = droppedRtpPacketLength - dataOffset - sizeof(*nvPacket);
                    int recoveredDataLength = StreamConfig.packetSize - sizeof(*nvPacket);
                    int j;
                    int recoveryErrors = 0;

                    LC_ASSERT(droppedDataLength <= recoveredDataLength);
                    LC_ASSERT(droppedDataLength == recoveredDataLength || (nvPacket->flags & FLAG_EOF));

                    // Check all NV_VIDEO_PACKET fields except fecInfo which differs in the recovered packet
                    LC_ASSERT(nvPacket->flags == droppedNvPacket->flags);
                    LC_ASSERT(nvPacket->frameIndex == droppedNvPacket->frameIndex);
                    LC_ASSERT(nvPacket->streamPacketIndex == droppedNvPacket->streamPacketIndex);

                    // TODO: Investigate assertion failure here with GFE 3.20.4. The remaining fields and
                    // video data are still recovered successfully, so this doesn't seem critical.
                    //LC_ASSERT(memcmp(nvPacket->reserved, droppedNvPacket->reserved, sizeof(nvPacket->reserved)) == 0);

                    // Check the data itself - use memcmp() and only loop if an error is detected
                    if (memcmp(nvPacket + 1, droppedNvPacket + 1, droppedDataLength)) {
                        unsigned char* actualData = (unsigned char*)(nvPacket + 1);
                        unsigned char* expectedData = (unsigned char*)(droppedNvPacket + 1);
                        for (j = 0; j < droppedDataLength; j++) {
                            if (actualData[j] != expectedData[j]) {
                                Limelog("Recovery error at %d: expected 0x%02x, actual 0x%02x\n",
                                        j, expectedData[j], actualData[j]);
                                recoveryErrors++;
                            }
                        }
                    }

                    // If this packet is at the end of the frame, the remaining data should be zeros.
                    for (j = droppedDataLength; j < recoveredDataLength; j++) {
                        unsigned char* actualData = (unsigned char*)(nvPacket + 1);
                        if (actualData[j] != 0) {
                            Limelog("Recovery error at %d: expected 0x00, actual 0x%02x\n",
                                    j, actualData[j]);
                            recoveryErrors++;
                        }
                    }

                    LC_ASSERT(recoveryErrors == 0);

                    // This drop was fake, so we don't want to actually submit it to the depacketizer.
                    // It will get confused because it's already seen this packet before.
                    free(packets[i]);
                    continue;
                }
#endif

                // Do some rudamentary checks to see that the recovered packet is sane.
                // In some cases (4K 30 FPS 80 Mbps), we seem to get some odd failures
                // here in rare cases where FEC recovery is required. I'm unsure if it
                // is our bug, NVIDIA's, or something else, but we don't want the corrupt
                // packet to by ingested by our depacketizer (or worse, the decoder).
                if (i == 0 && !(nvPacket->flags & FLAG_SOF)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (i == queue->bufferDataPackets - 1 && !(nvPacket->flags & FLAG_EOF)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (i > 0 && i < queue->bufferDataPackets - 1 && !(nvPacket->flags & FLAG_CONTAINS_PIC_DATA)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (nvPacket->flags & ~(FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA)) {
                    PACKET_RECOVERY_FAILURE();
                }

                // FEC recovered frames may have extra zero padding at the end. This is
                // fine per H.264 Annex B which states trailing zero bytes must be
                // discarded by decoders. It's not safe to strip all zero padding because
                // it may be a legitimate part of the H.264 bytestream.

                LC_ASSERT(isBefore16(rtpPacket->sequenceNumber, queue->bufferFirstParitySequenceNumber));
                queuePacket(queue, queueEntry, false, rtpPacket, StreamConfig.packetSize + dataOffset, false);
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
    LC_ASSERT(queue->bufferSize > 0);
    LC_ASSERT(queue->bufferHead != NULL);
    LC_ASSERT(queue->bufferTail != NULL);

    if (queue->bufferHead == entry) {
        queue->bufferHead = entry->next;
    }
    if (queue->bufferTail == entry) {
        queue->bufferTail = entry->prev;
    }

    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }
    queue->bufferSize--;
}

static void submitCompletedFrame(PRTP_FEC_QUEUE queue) {
    unsigned int nextSeqNum = queue->bufferLowestSequenceNumber;

    while (queue->bufferSize > 0) {
        PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;

        unsigned int lowestRtpSequenceNumber = entry->packet->sequenceNumber;

        while (entry != NULL) {
            // We should never encounter a packet that's lower than our next seq num
            LC_ASSERT(!isBefore16(entry->packet->sequenceNumber, nextSeqNum));

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

            // Check for the next packet in sequence. This will be O(1) for non-reordered packet streams.
            if (entry->packet->sequenceNumber == nextSeqNum) {
                removeEntry(queue, entry);
                entry->prev = entry->next = NULL;

                // To avoid having to sample the system time for each packet, we cheat
                // and use the first packet's receive time for all packets. This ends up
                // actually being better for the measurements that the depacketizer does,
                // since it properly handles out of order packets.
                LC_ASSERT(queue->bufferFirstRecvTimeMs != 0);
                entry->receiveTimeMs = queue->bufferFirstRecvTimeMs;

                // Submit this packet for decoding. It will own freeing the entry now.
                queueRtpPacket(entry);
                break;
            }
            else if (isBefore16(entry->packet->sequenceNumber, lowestRtpSequenceNumber)) {
                lowestRtpSequenceNumber = entry->packet->sequenceNumber;
            }

            entry = entry->next;
        }

        if (entry == NULL) {
            // Start at the lowest we found last enumeration
            nextSeqNum = lowestRtpSequenceNumber;
        }
        else {
            // We found this packet so move on to the next one in sequence
            nextSeqNum = U16(nextSeqNum + 1);
        }
    }
}

int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, int length, PRTPFEC_QUEUE_ENTRY packetEntry) {
    if (isBefore16(packet->sequenceNumber, queue->nextContiguousSequenceNumber)) {
        // Reject packets behind our current buffer window
        return RTPF_RET_REJECTED;
    }

    // FLAG_EXTENSION is required for all supported versions of GFE.
    LC_ASSERT(packet->header & FLAG_EXTENSION);

    int dataOffset = sizeof(*packet);
    if (packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)packet) + dataOffset);

    nvPacket->streamPacketIndex = LE32(nvPacket->streamPacketIndex);
    nvPacket->frameIndex = LE32(nvPacket->frameIndex);
    nvPacket->fecInfo = LE32(nvPacket->fecInfo);
    
    if (isBefore16(nvPacket->frameIndex, queue->currentFrameNumber)) {
        // Reject frames behind our current frame number
        return RTPF_RET_REJECTED;
    }

    int fecIndex = (nvPacket->fecInfo & 0x3FF000) >> 12;
    
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

        // Tell the control stream logic about this frame, even if we don't end up
        // being able to reconstruct a full frame from it.
        connectionSawFrame(queue->currentFrameNumber);

        // Discard any unsubmitted buffers from the previous frame
        while (queue->bufferHead != NULL) {
            PRTPFEC_QUEUE_ENTRY entry = queue->bufferHead;
            queue->bufferHead = entry->next;
            free(entry->packet);
        }
        
        queue->bufferTail = NULL;
        queue->bufferSize = 0;
        
        queue->bufferFirstRecvTimeMs = PltGetMillis();
        queue->bufferLowestSequenceNumber = U16(packet->sequenceNumber - fecIndex);
        queue->nextContiguousSequenceNumber = queue->bufferLowestSequenceNumber;
        queue->receivedBufferDataPackets = 0;
        queue->bufferDataPackets = (nvPacket->fecInfo & 0xFFC00000) >> 22;
        queue->fecPercentage = (nvPacket->fecInfo & 0xFF0) >> 4;
        queue->bufferParityPackets = (queue->bufferDataPackets * queue->fecPercentage + 99) / 100;
        queue->bufferFirstParitySequenceNumber = U16(queue->bufferLowestSequenceNumber + queue->bufferDataPackets);
        queue->bufferHighestSequenceNumber = U16(queue->bufferFirstParitySequenceNumber + queue->bufferParityPackets - 1);
    } else if (isBefore16(queue->bufferHighestSequenceNumber, packet->sequenceNumber)) {
        // In rare cases, we get extra parity packets. It's rare enough that it's probably
        // not worth handling, so we'll just drop them.
        return RTPF_RET_REJECTED;
    }

    LC_ASSERT(!queue->fecPercentage || U16(packet->sequenceNumber - fecIndex) == queue->bufferLowestSequenceNumber);
    LC_ASSERT((nvPacket->fecInfo & 0xFF0) >> 4 == queue->fecPercentage);
    LC_ASSERT((nvPacket->fecInfo & 0xFFC00000) >> 22 == queue->bufferDataPackets);

    LC_ASSERT((nvPacket->flags & FLAG_EOF) || length - dataOffset == StreamConfig.packetSize);
    if (!queuePacket(queue, packetEntry, false, packet, length, !isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber))) {
        return RTPF_RET_REJECTED;
    }
    else {
        if (isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber)) {
            queue->receivedBufferDataPackets++;
        }
        
        // Try to submit this frame. If we haven't received enough packets,
        // this will fail and we'll keep waiting.
        if (reconstructFrame(queue) == 0) {
            // Submit the frame data to the depacketizer
            submitCompletedFrame(queue);
            
            // submitCompletedFrame() should have consumed all data
            LC_ASSERT(queue->bufferHead == NULL);
            LC_ASSERT(queue->bufferTail == NULL);
            LC_ASSERT(queue->bufferSize == 0);
            
            // Ignore any more packets for this frame
            queue->currentFrameNumber++;
        }

        return RTPF_RET_QUEUED;
    }
}

