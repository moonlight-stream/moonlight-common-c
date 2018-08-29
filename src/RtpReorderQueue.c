#include "Limelight-internal.h"
#include "RtpReorderQueue.h"

void RtpqInitializeQueue(PRTP_REORDER_QUEUE queue, int maxSize, int maxQueueTimeMs) {
    memset(queue, 0, sizeof(*queue));
    queue->maxSize = maxSize;
    queue->maxQueueTimeMs = maxQueueTimeMs;
    queue->nextRtpSequenceNumber = UINT16_MAX;
    queue->oldestQueuedTimeMs = UINT64_MAX;
}

void RtpqCleanupQueue(PRTP_REORDER_QUEUE queue) {
    while (queue->queueHead != NULL) {
        PRTP_QUEUE_ENTRY entry = queue->queueHead;
        queue->queueHead = entry->next;
        free(entry->packet);
    }
}

// newEntry is contained within the packet buffer so we free the whole entry by freeing entry->packet
static int queuePacket(PRTP_REORDER_QUEUE queue, PRTP_QUEUE_ENTRY newEntry, int head, PRTP_PACKET packet) {
    PRTP_QUEUE_ENTRY entry;

    LC_ASSERT(!isBefore16(packet->sequenceNumber, queue->nextRtpSequenceNumber));

    // Don't queue duplicates
    entry = queue->queueHead;
    while (entry != NULL) {
        if (entry->packet->sequenceNumber == packet->sequenceNumber) {
            return 0;
        }

        entry = entry->next;
    }

    newEntry->packet = packet;
    newEntry->queueTimeMs = PltGetMillis();
    newEntry->prev = NULL;
    newEntry->next = NULL;

    if (queue->oldestQueuedTimeMs == UINT64_MAX) {
        queue->oldestQueuedTimeMs = newEntry->queueTimeMs;
    }

    if (queue->queueHead == NULL) {
        LC_ASSERT(queue->queueSize == 0);
        queue->queueHead = queue->queueTail = newEntry;
    }
    else if (head) {
        LC_ASSERT(queue->queueSize > 0);
        PRTP_QUEUE_ENTRY oldHead = queue->queueHead;
        newEntry->next = oldHead;
        LC_ASSERT(oldHead->prev == NULL);
        oldHead->prev = newEntry;
        queue->queueHead = newEntry;
    }
    else {
        LC_ASSERT(queue->queueSize > 0);
        PRTP_QUEUE_ENTRY oldTail = queue->queueTail;
        newEntry->prev = oldTail;
        LC_ASSERT(oldTail->next == NULL);
        oldTail->next = newEntry;
        queue->queueTail = newEntry;
    }
    queue->queueSize++;

    return 1;
}

static void updateOldestQueued(PRTP_REORDER_QUEUE queue) {
    PRTP_QUEUE_ENTRY entry;

    queue->oldestQueuedTimeMs = UINT64_MAX;

    entry = queue->queueHead;
    while (entry != NULL) {
        if (entry->queueTimeMs < queue->oldestQueuedTimeMs) {
            queue->oldestQueuedTimeMs = entry->queueTimeMs;
        }

        entry = entry->next;
    }
}

static PRTP_QUEUE_ENTRY getEntryByLowestSeq(PRTP_REORDER_QUEUE queue) {
    PRTP_QUEUE_ENTRY lowestSeqEntry, entry;

    lowestSeqEntry = queue->queueHead;
    entry = queue->queueHead;
    while (entry != NULL) {
        if (isBefore16(entry->packet->sequenceNumber, lowestSeqEntry->packet->sequenceNumber)) {
            lowestSeqEntry = entry;
        }

        entry = entry->next;
    }

    // Remember the updated lowest sequence number
    if (lowestSeqEntry != NULL) {
        queue->nextRtpSequenceNumber = lowestSeqEntry->packet->sequenceNumber;
    }

    return lowestSeqEntry;
}

static void removeEntry(PRTP_REORDER_QUEUE queue, PRTP_QUEUE_ENTRY entry) {
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

static PRTP_QUEUE_ENTRY enforceQueueConstraints(PRTP_REORDER_QUEUE queue) {
    int dequeuePacket = 0;

    // Empty queue is fine
    if (queue->queueHead == NULL) {
        return NULL;
    }

    // Check that the queue's time constraint is satisfied
    if (PltGetMillis() - queue->oldestQueuedTimeMs > queue->maxQueueTimeMs) {
        Limelog("Returning RTP packet queued for too long\n");
        dequeuePacket = 1;
    }

    // Check that the queue's size constraint is satisfied. We subtract one
    // because this is validating that the queue will meet constraints _after_
    // the current packet is enqueued.
    if (!dequeuePacket && queue->queueSize == queue->maxSize - 1) {
        Limelog("Returning RTP packet after queue overgrowth\n");
        dequeuePacket = 1;
    }

    if (dequeuePacket) {
        // Return the lowest seq queued
        return getEntryByLowestSeq(queue);
    }
    else {
        return NULL;
    }
}

int RtpqAddPacket(PRTP_REORDER_QUEUE queue, PRTP_PACKET packet, PRTP_QUEUE_ENTRY packetEntry) {
    if (queue->nextRtpSequenceNumber != UINT16_MAX &&
        isBefore16(packet->sequenceNumber, queue->nextRtpSequenceNumber)) {
        // Reject packets behind our current sequence number
        return 0;
    }

    if (queue->queueHead == NULL) {
        // Return immediately for an exact match with an empty queue
        if (queue->nextRtpSequenceNumber == UINT16_MAX ||
            packet->sequenceNumber == queue->nextRtpSequenceNumber) {
            queue->nextRtpSequenceNumber = packet->sequenceNumber + 1;
            return RTPQ_RET_HANDLE_NOW;
        }
        else {
            // Queue is empty currently so we'll put this packet on there
            if (!queuePacket(queue, packetEntry, 0, packet)) {
                return 0;
            }
            else {
                return RTPQ_RET_PACKET_CONSUMED;
            }
        }
    }
    else {
        PRTP_QUEUE_ENTRY lowestEntry;

        // Validate that the queue remains within our contraints
        // and get the lowest element
        lowestEntry = enforceQueueConstraints(queue);

        // If the queue is now empty after validating queue constraints,
        // this packet can be returned immediately
        if (lowestEntry == NULL && queue->queueHead == NULL) {
            queue->nextRtpSequenceNumber = packet->sequenceNumber + 1;
            return RTPQ_RET_HANDLE_NOW;
        }
        else if (lowestEntry != NULL && queue->nextRtpSequenceNumber != UINT16_MAX &&
                 isBefore16(packet->sequenceNumber, queue->nextRtpSequenceNumber)) {
            // The queue constraints were enforced and a new lowest entry was
            // made available for retrieval. This packet was behind the new lowest
            // so it will not be consumed by the queue.
            return RTPQ_RET_PACKET_READY;
        }

        // Queue has data inside, so we need to see where this packet fits
        if (packet->sequenceNumber == queue->nextRtpSequenceNumber) {
            // It fits in a hole where we need a packet, now we have some ready
            if (!queuePacket(queue, packetEntry, 0, packet)) {
                return 0;
            }
            else {
                return RTPQ_RET_PACKET_READY | RTPQ_RET_PACKET_CONSUMED;
            }
        }
        else {
            if (!queuePacket(queue, packetEntry, 0, packet)) {
                return 0;
            }
            else {
                // Constraint validation may have changed the oldest packet to one that
                // matches the next sequence number
                return RTPQ_RET_PACKET_CONSUMED | ((lowestEntry != NULL) ? RTPQ_RET_PACKET_READY : 0);
            }
        }
    }
}

PRTP_PACKET RtpqGetQueuedPacket(PRTP_REORDER_QUEUE queue) {
    PRTP_QUEUE_ENTRY queuedEntry, entry;

    // Find the next queued packet
    queuedEntry = NULL;
    entry = queue->queueHead;
    while (entry != NULL) {
        if (entry->packet->sequenceNumber == queue->nextRtpSequenceNumber) {
            queue->nextRtpSequenceNumber++;
            queuedEntry = entry;
            removeEntry(queue, entry);
            break;
        }

        entry = entry->next;
    }

    // Bail if we found nothing
    if (queuedEntry == NULL) {
        // Update the oldest queued packet time
        updateOldestQueued(queue);

        return NULL;
    }

    // We don't update the oldest queued entry here, because we know
    // the caller will call again until it receives null

    return queuedEntry->packet;
}