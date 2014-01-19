#include "LinkedBlockingQueue.h"

int initializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound) {
	int err;
	
	err = PltCreateEvent(&queueHead->containsDataEvent);
	if (err != 0) {
		return err;
	}

	err = PltCreateMutex(&queueHead->mutex);
	if (err != 0) {
		return err;
	}

	queueHead->head = NULL;
	queueHead->sizeBound = sizeBound;

	return 0;
}

int offerQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, lastEntry;

	entry = (PLINKED_BLOCKING_QUEUE_ENTRY) malloc(sizeof(*entry));
	if (entry == NULL) {
		return 0;
	}

	entry->next = NULL;
	entry->data = data;

	PltLockMutex(queueHead->mutex);

	if (queueHead->head == NULL) {
		queueHead->head = entry;
	}
	else {
		lastEntry = queueHead->head;
		while (lastEntry->next != NULL) {
			lastEntry = lastEntry->next;
		}
		lastEntry->next = entry;
	}

	PltSetEvent(queueHead->containsDataEvent);

	PltUnlockMutex(queueHead->mutex);

	return 1;
}

void* waitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry;
	void* data;

	for (;;) {
		PltWaitForEvent(queueHead->containsDataEvent);

		PltLockMutex(queueHead->mutex);

		if (queueHead->head == NULL) {
			PltUnlockMutex(queueHead->mutex);
			continue;
		}

		entry = queueHead->head;
		queueHead->head = entry->next;

		data = entry->data;

		free(entry);

		if (queueHead->head == NULL) {
			PltClearEvent(queueHead->containsDataEvent);
		}

		PltUnlockMutex(queueHead->mutex);

		break;
	}

	return data;
}
