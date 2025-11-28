/**
 * @file src/ByteBuffer.h
 * @brief Byte buffer utilities for reading and writing binary data.
 */

#pragma once

#include "Platform.h"

/**
 * @def BYTE_ORDER_LITTLE
 * @brief Little-endian byte order.
 */
#define BYTE_ORDER_LITTLE 1

/**
 * @def BYTE_ORDER_BIG
 * @brief Big-endian byte order.
 */
#define BYTE_ORDER_BIG 2

/**
 * @brief Byte buffer structure for reading/writing binary data.
 */
typedef struct _BYTE_BUFFER {
    char* buffer;  ///< Pointer to the buffer data
    unsigned int length;  ///< Total length of the buffer
    unsigned int position;  ///< Current read/write position
    unsigned int byteOrder;  ///< Byte order (BYTE_ORDER_LITTLE or BYTE_ORDER_BIG)
} BYTE_BUFFER, *PBYTE_BUFFER;

/**
 * @brief Initialize a byte buffer with existing data.
 * @param buff Pointer to the byte buffer structure.
 * @param data Pointer to the data buffer.
 * @param offset Starting offset in the data buffer.
 * @param length Length of data to use.
 * @param byteOrder Byte order (BYTE_ORDER_LITTLE or BYTE_ORDER_BIG).
 */
void BbInitializeWrappedBuffer(PBYTE_BUFFER buff, char* data, int offset, int length, int byteOrder);

/**
 * @brief Advance the buffer position.
 * @param buff Pointer to the byte buffer.
 * @param offset Number of bytes to advance (can be negative).
 * @return `true` on success, `false` if position would go out of bounds.
 */
bool BbAdvanceBuffer(PBYTE_BUFFER buff, int offset);

/**
 * @brief Rewind the buffer to the beginning.
 * @param buff Pointer to the byte buffer.
 */
void BbRewindBuffer(PBYTE_BUFFER buff);

/**
 * @brief Read bytes from the buffer.
 * @param buff Pointer to the byte buffer.
 * @param data Output buffer for the read data.
 * @param length Number of bytes to read.
 * @return `true` on success, `false` on failure.
 */
bool BbGetBytes(PBYTE_BUFFER buff, uint8_t* data, int length);

/**
 * @brief Read an 8-bit unsigned integer from the buffer.
 * @param buff Pointer to the byte buffer.
 * @param c Output parameter for the read value.
 * @return `true` on success, `false` on failure.
 */
bool BbGet8(PBYTE_BUFFER buff, uint8_t* c);

/**
 * @brief Read a 16-bit unsigned integer from the buffer.
 * @param buff Pointer to the byte buffer.
 * @param s Output parameter for the read value.
 * @return `true` on success, `false` on failure.
 */
bool BbGet16(PBYTE_BUFFER buff, uint16_t* s);

/**
 * @brief Read a 32-bit unsigned integer from the buffer.
 * @param buff Pointer to the byte buffer.
 * @param i Output parameter for the read value.
 * @return `true` on success, `false` on failure.
 */
bool BbGet32(PBYTE_BUFFER buff, uint32_t* i);

/**
 * @brief Read a 64-bit unsigned integer from the buffer.
 * @param buff Pointer to the byte buffer.
 * @param l Output parameter for the read value.
 * @return `true` on success, `false` on failure.
 */
bool BbGet64(PBYTE_BUFFER buff, uint64_t* l);

/**
 * @brief Write bytes to the buffer.
 * @param buff Pointer to the byte buffer.
 * @param data Pointer to the data to write.
 * @param length Number of bytes to write.
 * @return `true` on success, `false` on failure.
 */
bool BbPutBytes(PBYTE_BUFFER buff, const uint8_t* data, int length);

/**
 * @brief Write an 8-bit unsigned integer to the buffer.
 * @param buff Pointer to the byte buffer.
 * @param c Value to write.
 * @return `true` on success, `false` on failure.
 */
bool BbPut8(PBYTE_BUFFER buff, uint8_t c);

/**
 * @brief Write a 16-bit unsigned integer to the buffer.
 * @param buff Pointer to the byte buffer.
 * @param s Value to write.
 * @return `true` on success, `false` on failure.
 */
bool BbPut16(PBYTE_BUFFER buff, uint16_t s);

/**
 * @brief Write a 32-bit unsigned integer to the buffer.
 * @param buff Pointer to the byte buffer.
 * @param i Value to write.
 * @return `true` on success, `false` on failure.
 */
bool BbPut32(PBYTE_BUFFER buff, uint32_t i);

/**
 * @brief Write a 64-bit unsigned integer to the buffer.
 * @param buff Pointer to the byte buffer.
 * @param l Value to write.
 * @return `true` on success, `false` on failure.
 */
bool BbPut64(PBYTE_BUFFER buff, uint64_t l);
