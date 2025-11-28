/**
 * @file src/RtpVideoQueue.h
 * @brief RTP video queue with FEC (Forward Error Correction) support.
 */

#pragma once

#include "Video.h"

/**
 * @brief RTP video queue entry structure.
 */
typedef struct _RTPV_QUEUE_ENTRY {
    struct _RTPV_QUEUE_ENTRY* next;  ///< Next entry in list
    struct _RTPV_QUEUE_ENTRY* prev;  ///< Previous entry in list
    PRTP_PACKET packet;  ///< RTP packet pointer
    uint64_t receiveTimeMs;  ///< Receive time in milliseconds
    uint32_t presentationTimeMs;  ///< Presentation time in milliseconds
    int length;  ///< Packet length in bytes
    bool isParity;  ///< Whether this is a parity (FEC) packet
} RTPV_QUEUE_ENTRY, *PRTPV_QUEUE_ENTRY;

/**
 * @brief RTP video queue list structure.
 */
typedef struct _RTPV_QUEUE_LIST {
    PRTPV_QUEUE_ENTRY head;  ///< Head of entry list
    PRTPV_QUEUE_ENTRY tail;  ///< Tail of entry list
    uint32_t count;  ///< Number of entries in list
} RTPV_QUEUE_LIST, *PRTPV_QUEUE_LIST;

/**
 * @brief RTP video queue structure.
 */
typedef struct _RTP_VIDEO_QUEUE {
    RTPV_QUEUE_LIST pendingFecBlockList;  ///< List of pending FEC blocks
    RTPV_QUEUE_LIST completedFecBlockList;  ///< List of completed FEC blocks
    uint64_t bufferFirstRecvTimeMs;  ///< First packet receive time
    uint32_t bufferLowestSequenceNumber;  ///< Lowest sequence number in buffer
    uint32_t bufferHighestSequenceNumber;  ///< Highest sequence number in buffer
    uint32_t bufferFirstParitySequenceNumber;  ///< First parity packet sequence number
    uint32_t bufferDataPackets;  ///< Total data packets in buffer
    uint32_t bufferParityPackets;  ///< Total parity packets in buffer
    uint32_t receivedDataPackets;  ///< Number of data packets received
    uint32_t receivedParityPackets;  ///< Number of parity packets received
    uint32_t receivedHighestSequenceNumber;  ///< Highest sequence number received
    uint32_t fecPercentage;  ///< FEC percentage
    uint32_t nextContiguousSequenceNumber;  ///< Next contiguous sequence number expected
    uint32_t missingPackets;  ///< Number of missing packets (holes) behind receivedHighestSequenceNumber
    bool useFastQueuePath;  ///< Whether to use fast queue path
    bool reportedLostFrame;  ///< Whether lost frame was reported
    uint32_t currentFrameNumber;  ///< Current frame number
    bool multiFecCapable;  ///< Whether multi-FEC is supported
    uint8_t multiFecCurrentBlockNumber;  ///< Current multi-FEC block number
    uint8_t multiFecLastBlockNumber;  ///< Last multi-FEC block number
    uint32_t lastOosFramePresentationTimestamp;  ///< Last out-of-sequence frame presentation timestamp
    bool receivedOosData;  ///< Whether out-of-sequence data was received
} RTP_VIDEO_QUEUE, *PRTP_VIDEO_QUEUE;

/**
 * @def RTPF_RET_QUEUED
 * @brief Return value: packet was queued.
 */
#define RTPF_RET_QUEUED    0

/**
 * @def RTPF_RET_REJECTED
 * @brief Return value: packet was rejected.
 */
#define RTPF_RET_REJECTED  1

/**
 * @brief Initialize RTP video queue.
 * @param queue Pointer to video queue structure.
 */
void RtpvInitializeQueue(PRTP_VIDEO_QUEUE queue);

/**
 * @brief Cleanup RTP video queue.
 * @param queue Pointer to video queue structure.
 */
void RtpvCleanupQueue(PRTP_VIDEO_QUEUE queue);

/**
 * @brief Add a packet to the video queue.
 * @param queue Pointer to video queue structure.
 * @param packet RTP packet to add.
 * @param length Packet length in bytes.
 * @param packetEntry Queue entry structure to use.
 * @return RTPF_RET_QUEUED on success, RTPF_RET_REJECTED on failure.
 */
int RtpvAddPacket(PRTP_VIDEO_QUEUE queue, PRTP_PACKET packet, int length, PRTPV_QUEUE_ENTRY packetEntry);

/**
 * @brief Get the current frame number.
 * @param queue Pointer to video queue structure.
 * @return Current frame number.
 */
uint32_t RtpvGetCurrentFrameNumber(PRTP_VIDEO_QUEUE queue);

/**
 * @brief Submit queued packets for processing.
 * @param queue Pointer to video queue structure.
 */
void RtpvSubmitQueuedPackets(PRTP_VIDEO_QUEUE queue);
