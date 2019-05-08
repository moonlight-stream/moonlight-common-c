#include "LinkedBlockingQueue.h"

// Destroy the linked blocking queue and associated mutex and event
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead) {
    LC_ASSERT(queueHead->shutdown || queueHead->lifetimeSize == 0);
    
    PltDeleteMutex(&queueHead->mutex);
    PltCloseEvent(&queueHead->containsDataEvent);

    return queueHead->head;
}

// Flush the queue
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead) {
    PLINKED_BLOCKING_QUEUE_ENTRY head;

    PltLockMutex(&queueHead->mutex);

    // Save the old head
    head = queueHead->head;

    // Reinitialize the queue to empty
    queueHead->head = NULL;
    queueHead->tail = NULL;
    queueHead->currentSize = 0;
    PltClearEvent(&queueHead->containsDataEvent);

    PltUnlockMutex(&queueHead->mutex);

    return head;
}

// Linked blocking queue init
int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound) {
    int err;

    memset(queueHead, 0, sizeof(*queueHead));

    err = PltCreateEvent(&queueHead->containsDataEvent);
    if (err != 0) {
        return err;
    }

    err = PltCreateMutex(&queueHead->mutex);
    if (err != 0) {
        return err;
    }

    queueHead->sizeBound = sizeBound;

    return 0;
}

void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead) {
    queueHead->shutdown = 1;
    PltSetEvent(&queueHead->containsDataEvent);
}

int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead) {
    return queueHead->currentSize;
}

int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    if (queueHead->shutdown) {
        return LBQ_INTERRUPTED;
    }
    
    entry->flink = NULL;
    entry->data = data;

    PltLockMutex(&queueHead->mutex);

    if (queueHead->currentSize == queueHead->sizeBound) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_BOUND_EXCEEDED;
    }

    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        LC_ASSERT(queueHead->tail == NULL);
        queueHead->head = entry;
        queueHead->tail = entry;
        entry->blink = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize >= 1);
        LC_ASSERT(queueHead->head != NULL);
        queueHead->tail->flink = entry;
        entry->blink = queueHead->tail;
        queueHead->tail = entry;
    }

    queueHead->currentSize++;
    queueHead->lifetimeSize++;

    PltUnlockMutex(&queueHead->mutex);

    PltSetEvent(&queueHead->containsDataEvent);

    return LBQ_SUCCESS;
}

// This must be synchronized with LbqFlushQueueItems by the caller
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    if (queueHead->shutdown) {
        return LBQ_INTERRUPTED;
    }
    
    if (queueHead->head == NULL) {
        return LBQ_NO_ELEMENT;
    }

    PltLockMutex(&queueHead->mutex);

    if (queueHead->head == NULL) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_NO_ELEMENT;
    }

    *data = queueHead->head->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;
    
    if (queueHead->shutdown) {
        return LBQ_INTERRUPTED;
    }

    if (queueHead->head == NULL) {
        return LBQ_NO_ELEMENT;
    }

    PltLockMutex(&queueHead->mutex);

    if (queueHead->head == NULL) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_NO_ELEMENT;
    }

    entry = queueHead->head;
    queueHead->head = entry->flink;
    queueHead->currentSize--;
    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        queueHead->tail = NULL;
        PltClearEvent(&queueHead->containsDataEvent);
    }
    else {
        LC_ASSERT(queueHead->currentSize != 0);
        queueHead->head->blink = NULL;
    }

    *data = entry->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;
    int err;
    
    if (queueHead->shutdown) {
        return LBQ_INTERRUPTED;
    }

    for (;;) {
        err = PltWaitForEvent(&queueHead->containsDataEvent);
        if (err != PLT_WAIT_SUCCESS) {
            return LBQ_INTERRUPTED;
        }

        if (queueHead->shutdown) {
            return LBQ_INTERRUPTED;
        }

        PltLockMutex(&queueHead->mutex);

        if (queueHead->head == NULL) {
            PltClearEvent(&queueHead->containsDataEvent);
            PltUnlockMutex(&queueHead->mutex);
            continue;
        }

        entry = queueHead->head;
        queueHead->head = entry->flink;
        queueHead->currentSize--;
        if (queueHead->head == NULL) {
            LC_ASSERT(queueHead->currentSize == 0);
            queueHead->tail = NULL;
            PltClearEvent(&queueHead->containsDataEvent);
        }
        else {
            LC_ASSERT(queueHead->currentSize != 0);
            queueHead->head->blink = NULL;
        }

        *data = entry->data;

        PltUnlockMutex(&queueHead->mutex);

        break;
    }

    return LBQ_SUCCESS;
}
