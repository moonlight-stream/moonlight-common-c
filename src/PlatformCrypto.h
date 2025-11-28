/**
 * @file src/PlatformCrypto.h
 * @brief Platform abstraction layer for cryptographic operations.
 * 
 * Provides cross-platform crypto functions supporting both OpenSSL and MbedTLS.
 */

#pragma once

#include <stdbool.h>

#ifdef USE_MBEDTLS
#include <mbedtls/cipher.h>
#else
// Hide the real OpenSSL definition from other code
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
#endif

/**
 * @brief Platform crypto context structure.
 */
typedef struct _PLT_CRYPTO_CONTEXT {
#ifdef USE_MBEDTLS
    mbedtls_cipher_context_t ctx;  ///< MbedTLS cipher context
    bool initialized;  ///< Whether context is initialized
#else
    EVP_CIPHER_CTX* ctx;  ///< OpenSSL cipher context
    bool initialized;  ///< Whether context is initialized
#endif
} PLT_CRYPTO_CONTEXT, *PPLT_CRYPTO_CONTEXT;

/**
 * @def ROUND_TO_PKCS7_PADDED_LEN
 * @brief Round length to PKCS#7 padding boundary (16 bytes).
 */
#define ROUND_TO_PKCS7_PADDED_LEN(x) ((((x) + 15) / 16) * 16)

/**
 * @brief Create a platform crypto context.
 * @return Pointer to crypto context, or NULL on error.
 */
PPLT_CRYPTO_CONTEXT PltCreateCryptoContext(void);

/**
 * @brief Destroy a platform crypto context.
 * @param ctx Pointer to crypto context to destroy.
 */
void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx);

/**
 * @def ALGORITHM_AES_CBC
 * @brief Algorithm: AES-CBC.
 */
#define ALGORITHM_AES_CBC 1

/**
 * @def ALGORITHM_AES_GCM
 * @brief Algorithm: AES-GCM.
 */
#define ALGORITHM_AES_GCM 2

/**
 * @def CIPHER_FLAG_RESET_IV
 * @brief Cipher flag: reset IV.
 */
#define CIPHER_FLAG_RESET_IV          0x01

/**
 * @def CIPHER_FLAG_FINISH
 * @brief Cipher flag: finish encryption/decryption.
 */
#define CIPHER_FLAG_FINISH            0x02

/**
 * @def CIPHER_FLAG_PAD_TO_BLOCK_SIZE
 * @brief Cipher flag: pad to block size.
 */
#define CIPHER_FLAG_PAD_TO_BLOCK_SIZE 0x04

/**
 * @brief Encrypt a message using platform crypto.
 * @param ctx Crypto context.
 * @param algorithm Algorithm to use (ALGORITHM_*).
 * @param flags Cipher flags (CIPHER_FLAG_*).
 * @param key Encryption key.
 * @param keyLength Key length in bytes.
 * @param iv Initialization vector.
 * @param ivLength IV length in bytes.
 * @param tag Authentication tag (for GCM, output parameter).
 * @param tagLength Tag length in bytes.
 * @param inputData Input data to encrypt.
 * @param inputDataLength Input data length in bytes.
 * @param outputData Output buffer for encrypted data.
 * @param outputDataLength Input/output: output buffer size / encrypted data length.
 * @return `true` on success, `false` on error.
 */
bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

/**
 * @brief Decrypt a message using platform crypto.
 * @param ctx Crypto context.
 * @param algorithm Algorithm to use (ALGORITHM_*).
 * @param flags Cipher flags (CIPHER_FLAG_*).
 * @param key Decryption key.
 * @param keyLength Key length in bytes.
 * @param iv Initialization vector.
 * @param ivLength IV length in bytes.
 * @param tag Authentication tag (for GCM, input parameter).
 * @param tagLength Tag length in bytes.
 * @param inputData Input data to decrypt.
 * @param inputDataLength Input data length in bytes.
 * @param outputData Output buffer for decrypted data.
 * @param outputDataLength Input/output: output buffer size / decrypted data length.
 * @return `true` on success, `false` on error.
 */
bool PltDecryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

/**
 * @brief Generate random data.
 * @param data Output buffer for random data.
 * @param length Number of bytes to generate.
 */
void PltGenerateRandomData(unsigned char* data, int length);
