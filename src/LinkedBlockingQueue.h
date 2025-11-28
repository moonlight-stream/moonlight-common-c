/**
 * @file src/LinkedBlockingQueue.h
 * @brief Thread-safe linked blocking queue implementation.
 */

#pragma once

#include "Platform.h"
#include "PlatformThreads.h"

/**
 * @def LBQ_SUCCESS
 * @brief Queue operation: success.
 */
#define LBQ_SUCCESS 0

/**
 * @def LBQ_INTERRUPTED
 * @brief Queue operation: interrupted.
 */
#define LBQ_INTERRUPTED 1

/**
 * @def LBQ_BOUND_EXCEEDED
 * @brief Queue operation: size bound exceeded.
 */
#define LBQ_BOUND_EXCEEDED 2

/**
 * @def LBQ_NO_ELEMENT
 * @brief Queue operation: no element available.
 */
#define LBQ_NO_ELEMENT 3

/**
 * @def LBQ_USER_WAKE
 * @brief Queue operation: user wake signal.
 */
#define LBQ_USER_WAKE 4

/**
 * @brief Linked blocking queue entry structure.
 */
typedef struct _LINKED_BLOCKING_QUEUE_ENTRY {
    struct _LINKED_BLOCKING_QUEUE_ENTRY* flink;  ///< Forward link
    struct _LINKED_BLOCKING_QUEUE_ENTRY* blink;  ///< Backward link
    void* data;  ///< Entry data
} LINKED_BLOCKING_QUEUE_ENTRY, *PLINKED_BLOCKING_QUEUE_ENTRY;

/**
 * @brief Linked blocking queue structure.
 */
typedef struct _LINKED_BLOCKING_QUEUE {
    PLT_MUTEX mutex;  ///< Queue mutex
    PLT_COND cond;  ///< Queue condition variable
    PLINKED_BLOCKING_QUEUE_ENTRY head;  ///< Queue head entry
    PLINKED_BLOCKING_QUEUE_ENTRY tail;  ///< Queue tail entry
    int sizeBound;  ///< Maximum queue size
    int currentSize;  ///< Current queue size
    int lifetimeSize;  ///< Total items processed in queue lifetime
    bool shutdown;  ///< Whether queue is shut down
    bool draining;  ///< Whether queue is draining
    bool pendingUserWake;  ///< Whether user wake is pending
} LINKED_BLOCKING_QUEUE, *PLINKED_BLOCKING_QUEUE;

/**
 * @brief Initialize a linked blocking queue.
 * @param queueHead Pointer to queue structure.
 * @param sizeBound Maximum queue size.
 * @return LBQ_SUCCESS on success, error code on failure.
 */
int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound);

/**
 * @brief Offer an item to the queue (non-blocking).
 * @param queueHead Pointer to queue structure.
 * @param data Data to add to queue.
 * @param entry Queue entry structure to use.
 * @return LBQ_SUCCESS on success, error code on failure.
 */
int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry);

/**
 * @brief Wait for and retrieve a queue element (blocking).
 * @param queueHead Pointer to queue structure.
 * @param data Output parameter for retrieved data.
 * @return LBQ_SUCCESS on success, error code on failure.
 */
int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);

/**
 * @brief Poll for a queue element (non-blocking).
 * @param queueHead Pointer to queue structure.
 * @param data Output parameter for retrieved data.
 * @return LBQ_SUCCESS on success, LBQ_NO_ELEMENT if empty, error code on failure.
 */
int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);

/**
 * @brief Peek at the head element without removing it.
 * @param queueHead Pointer to queue structure.
 * @param data Output parameter for peeked data.
 * @return LBQ_SUCCESS on success, LBQ_NO_ELEMENT if empty, error code on failure.
 */
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);

/**
 * @brief Destroy a linked blocking queue.
 * @param queueHead Pointer to queue structure.
 * @return Pointer to first remaining entry, or NULL if queue is empty.
 */
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead);

/**
 * @brief Flush all items from the queue.
 * @param queueHead Pointer to queue structure.
 * @return Pointer to first flushed entry, or NULL if queue is empty.
 */
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead);

/**
 * @brief Signal queue shutdown.
 * @param queueHead Pointer to queue structure.
 */
void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead);

/**
 * @brief Signal queue drain.
 * @param queueHead Pointer to queue structure.
 */
void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead);

/**
 * @brief Signal user wake.
 * @param queueHead Pointer to queue structure.
 */
void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead);

/**
 * @brief Get the current item count in the queue.
 * @param queueHead Pointer to queue structure.
 * @return Current number of items in queue.
 */
int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead);
