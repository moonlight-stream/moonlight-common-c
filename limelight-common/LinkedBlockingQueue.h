#include "platform.h"
#include "PlatformThreads.h"

typedef struct _LINKED_BLOCKING_QUEUE_ENTRY {
	struct _LINKED_BLOCKING_QUEUE_ENTRY *next;
	void* data;
} LINKED_BLOCKING_QUEUE_ENTRY, *PLINKED_BLOCKING_QUEUE_ENTRY;

typedef struct _LINKED_BLOCKING_QUEUE {
	PLT_MUTEX mutex;
	PLT_EVENT containsDataEvent;
	int sizeBound;
	PLINKED_BLOCKING_QUEUE_ENTRY head;
} LINKED_BLOCKING_QUEUE, *PLINKED_BLOCKING_QUEUE;

int initializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound);
int offerQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data);
void* waitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead);