/**
 * @file src/RtpAudioQueue.h
 * @brief RTP audio queue with FEC (Forward Error Correction) support.
 */

#pragma once

#include "Video.h"

#include "rs.h"

/**
 * @def RTPQ_OOS_WAIT_TIME_MS
 * @brief Maximum time to wait for an out-of-sequence data/FEC shard after the entire FEC block should have been received (in milliseconds).
 */
#define RTPQ_OOS_WAIT_TIME_MS 10

/**
 * @def RTPA_DATA_SHARDS
 * @brief Number of data shards per FEC block.
 */
#define RTPA_DATA_SHARDS 4

/**
 * @def RTPA_FEC_SHARDS
 * @brief Number of FEC parity shards per FEC block.
 */
#define RTPA_FEC_SHARDS 2

/**
 * @def RTPA_TOTAL_SHARDS
 * @brief Total number of shards per FEC block.
 */
#define RTPA_TOTAL_SHARDS (RTPA_DATA_SHARDS + RTPA_FEC_SHARDS)

/**
 * @def RTPA_CACHED_FEC_BLOCK_LIMIT
 * @brief Maximum number of FEC block entries to cache.
 */
#define RTPA_CACHED_FEC_BLOCK_LIMIT 4

/**
 * @brief Audio FEC header structure.
 */
typedef struct _AUDIO_FEC_HEADER {
    uint8_t fecShardIndex;  ///< FEC shard index
    uint8_t payloadType;  ///< RTP payload type
    uint16_t baseSequenceNumber;  ///< Base sequence number for this FEC block
    uint32_t baseTimestamp;  ///< Base timestamp for this FEC block
    uint32_t ssrc;  ///< Synchronization source identifier
} AUDIO_FEC_HEADER, *PAUDIO_FEC_HEADER;

/**
 * @brief RTP audio FEC block structure.
 */
typedef struct _RTPA_FEC_BLOCK {
    struct _RTPA_FEC_BLOCK* prev;  ///< Previous block in list
    struct _RTPA_FEC_BLOCK* next;  ///< Next block in list
    PRTP_PACKET dataPackets[RTPA_DATA_SHARDS];  ///< Data packet pointers
    uint8_t* fecPackets[RTPA_FEC_SHARDS];  ///< FEC packet data
    uint8_t marks[RTPA_TOTAL_SHARDS];  ///< Mark flags for each shard
    AUDIO_FEC_HEADER fecHeader;  ///< FEC header information
    uint64_t queueTimeMs;  ///< Time when block was queued
    uint8_t dataShardsReceived;  ///< Number of data shards received
    uint8_t fecShardsReceived;  ///< Number of FEC shards received
    bool fullyReassembled;  ///< Whether block is fully reassembled
    uint8_t nextDataPacketIndex;  ///< Next data packet index for dequeuing
    bool allowDiscontinuity;  ///< Whether to allow sequence discontinuities
    uint16_t blockSize;  ///< Block size in bytes
} RTPA_FEC_BLOCK, *PRTPA_FEC_BLOCK;

/**
 * @brief RTP audio queue structure.
 */
typedef struct _RTP_AUDIO_QUEUE {
    PRTPA_FEC_BLOCK blockHead;  ///< Head of FEC block list
    PRTPA_FEC_BLOCK blockTail;  ///< Tail of FEC block list
    reed_solomon* rs;  ///< Reed-Solomon FEC decoder
    PRTPA_FEC_BLOCK freeBlockHead;  ///< Head of free block pool
    uint16_t freeBlockCount;  ///< Number of free blocks
    uint16_t nextRtpSequenceNumber;  ///< Expected next RTP sequence number
    uint16_t oldestRtpBaseSequenceNumber;  ///< Oldest base sequence number in queue
    uint16_t lastOosSequenceNumber;  ///< Last out-of-sequence sequence number
    bool receivedOosData;  ///< Whether out-of-sequence data was received
    bool synchronizing;  ///< Whether queue is synchronizing
    bool incompatibleServer;  ///< Whether server is incompatible
} RTP_AUDIO_QUEUE, *PRTP_AUDIO_QUEUE;

/**
 * @def RTPQ_RET_PACKET_CONSUMED
 * @brief Return value: packet was consumed.
 */
#define RTPQ_RET_PACKET_CONSUMED 0x1

/**
 * @def RTPQ_RET_PACKET_READY
 * @brief Return value: packet is ready for processing.
 */
#define RTPQ_RET_PACKET_READY    0x2

/**
 * @def RTPQ_RET_HANDLE_NOW
 * @brief Return value: handle immediately.
 */
#define RTPQ_RET_HANDLE_NOW      0x4

/**
 * @def RTPQ_PACKET_CONSUMED
 * @brief Check if return value indicates packet was consumed.
 */
#define RTPQ_PACKET_CONSUMED(x) ((x) & RTPQ_RET_PACKET_CONSUMED)

/**
 * @def RTPQ_PACKET_READY
 * @brief Check if return value indicates packet is ready.
 */
#define RTPQ_PACKET_READY(x)    ((x) & RTPQ_RET_PACKET_READY)

/**
 * @def RTPQ_HANDLE_NOW
 * @brief Check if return value indicates handle now.
 */
#define RTPQ_HANDLE_NOW(x)      ((x) == RTPQ_RET_HANDLE_NOW)

/**
 * @brief Initialize RTP audio queue.
 * @param queue Pointer to audio queue structure.
 */
void RtpaInitializeQueue(PRTP_AUDIO_QUEUE queue);

/**
 * @brief Cleanup RTP audio queue.
 * @param queue Pointer to audio queue structure.
 */
void RtpaCleanupQueue(PRTP_AUDIO_QUEUE queue);

/**
 * @brief Add a packet to the audio queue.
 * @param queue Pointer to audio queue structure.
 * @param packet RTP packet to add.
 * @param length Packet length in bytes.
 * @return Return value flags (RTPQ_RET_*).
 */
int RtpaAddPacket(PRTP_AUDIO_QUEUE queue, PRTP_PACKET packet, uint16_t length);

/**
 * @brief Get the next queued packet from the audio queue.
 * @param queue Pointer to audio queue structure.
 * @param customHeaderLength Length of custom header to skip.
 * @param length Output parameter for packet length.
 * @return Pointer to RTP packet, or NULL if no packet available.
 */
PRTP_PACKET RtpaGetQueuedPacket(PRTP_AUDIO_QUEUE queue, uint16_t customHeaderLength, uint16_t* length);
