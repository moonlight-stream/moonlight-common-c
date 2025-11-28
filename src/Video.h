/**
 * @file src/Video.h
 * @brief Video packet structures and definitions for streaming.
 */

#pragma once

#include "LinkedBlockingQueue.h"

/**
 * @brief Queued decode unit structure.
 */
typedef struct _QUEUED_DECODE_UNIT {
    DECODE_UNIT decodeUnit;  ///< Decode unit data
    LINKED_BLOCKING_QUEUE_ENTRY entry;  ///< Queue entry
} QUEUED_DECODE_UNIT, *PQUEUED_DECODE_UNIT;

#pragma pack(push, 1)

/**
 * @brief Encrypted video header structure.
 * @details Must be a multiple of 16 bytes in size to ensure the block size
 *          for FEC stays a multiple of 16 too.
 */
typedef struct _ENC_VIDEO_HEADER {
    uint8_t iv[12];  ///< Initialization vector
    uint32_t frameNumber;  ///< Frame number
    uint8_t tag[16];  ///< Authentication tag
} ENC_VIDEO_HEADER, *PENC_VIDEO_HEADER;

/**
 * @def FLAG_CONTAINS_PIC_DATA
 * @brief Flag: packet contains picture data.
 */
#define FLAG_CONTAINS_PIC_DATA 0x1

/**
 * @def FLAG_EOF
 * @brief Flag: end of frame.
 */
#define FLAG_EOF 0x2

/**
 * @def FLAG_SOF
 * @brief Flag: start of frame.
 */
#define FLAG_SOF 0x4

/**
 * @brief NVIDIA video packet structure.
 */
typedef struct _NV_VIDEO_PACKET {
    uint32_t streamPacketIndex;  ///< Stream packet index
    uint32_t frameIndex;  ///< Frame index
    uint8_t flags;  ///< Packet flags
    uint8_t reserved;  ///< Reserved field
    uint8_t multiFecFlags;  ///< Multi-FEC flags
    uint8_t multiFecBlocks;  ///< Multi-FEC block count
    uint32_t fecInfo;  ///< FEC information
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

/**
 * @def FLAG_EXTENSION
 * @brief Flag: extension header present.
 */
#define FLAG_EXTENSION 0x10

/**
 * @def FIXED_RTP_HEADER_SIZE
 * @brief Fixed RTP header size in bytes.
 */
#define FIXED_RTP_HEADER_SIZE 12

/**
 * @def MAX_RTP_HEADER_SIZE
 * @brief Maximum RTP header size in bytes.
 */
#define MAX_RTP_HEADER_SIZE 16

/**
 * @brief RTP packet structure.
 */
typedef struct _RTP_PACKET {
    uint8_t header;  ///< RTP header byte
    uint8_t packetType;  ///< Packet type
    uint16_t sequenceNumber;  ///< Sequence number
    uint32_t timestamp;  ///< RTP timestamp
    uint32_t ssrc;  ///< Synchronization source identifier
} RTP_PACKET, *PRTP_PACKET;

/**
 * @brief SS-Ping structure (fields are big-endian).
 */
typedef struct _SS_PING {
    char payload[16];  ///< Ping payload
    uint32_t sequenceNumber;  ///< Sequence number (big-endian)
} SS_PING, *PSS_PING;

/**
 * @def SS_FRAME_FEC_PTYPE
 * @brief SS frame FEC payload type.
 */
#define SS_FRAME_FEC_PTYPE 0x5502

/**
 * @brief SS frame FEC status structure (fields are big-endian).
 */
typedef struct _SS_FRAME_FEC_STATUS {
    uint32_t frameIndex;  ///< Frame index
    uint16_t highestReceivedSequenceNumber;  ///< Highest received sequence number
    uint16_t nextContiguousSequenceNumber;  ///< Next contiguous sequence number
    uint16_t missingPacketsBeforeHighestReceived;  ///< Missing packets before highest received
    uint16_t totalDataPackets;  ///< Total data packets
    uint16_t totalParityPackets;  ///< Total parity packets
    uint16_t receivedDataPackets;  ///< Received data packets
    uint16_t receivedParityPackets;  ///< Received parity packets
    uint8_t fecPercentage;  ///< FEC percentage
    uint8_t multiFecBlockIndex;  ///< Multi-FEC block index
    uint8_t multiFecBlockCount;  ///< Multi-FEC block count
} SS_FRAME_FEC_STATUS, *PSS_FRAME_FEC_STATUS;

#pragma pack(pop)
