#pragma once

#include <stdbool.h>
#include <stdint.h>

// Retain backwards compatibility with older USE_MBEDTLS
#if defined(USE_MBEDTLS) && !defined(USE_PSA_CRYPTO)
#define USE_PSA_CRYPTO
#endif

#ifdef USE_PSA_CRYPTO
#include <psa/crypto.h>
#else
// Hide the real OpenSSL definition from other code
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
#endif

#define REPLAY_WINDOW_SIZE 512
#define REPLAY_WINDOW_WORDS (REPLAY_WINDOW_SIZE / 64)

typedef struct _REPLAY_WINDOW {
    bool decryptedAuthedMessage;
    unsigned int lastDecryptedAuthedSeqNum;
    uint64_t replayWindow[REPLAY_WINDOW_WORDS];
} REPLAY_WINDOW, *PREPLAY_WINDOW;

typedef struct _PLT_CRYPTO_CONTEXT {
#ifdef USE_PSA_CRYPTO
    psa_key_id_t key;
    psa_cipher_operation_t cipherOp;
    bool initialized;
    bool cipherOpActive;
#else
    EVP_CIPHER_CTX* ctx;
    bool initialized;
#endif
    unsigned char replayWindowCount;
    REPLAY_WINDOW replayWindows[];
} PLT_CRYPTO_CONTEXT, *PPLT_CRYPTO_CONTEXT;

#define ROUND_TO_PKCS7_PADDED_LEN(x) ((((x) + 15) / 16) * 16)

PPLT_CRYPTO_CONTEXT PltCreateCryptoContext(unsigned char replayWindowCount);
void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx);

#define ALGORITHM_AES_CBC 1
#define ALGORITHM_AES_GCM 2

#define CIPHER_FLAG_RESET_IV          0x01
#define CIPHER_FLAG_FINISH            0x02
#define CIPHER_FLAG_PAD_TO_BLOCK_SIZE 0x04
#define CIPHER_FLAG_NO_REPLAY_CHECK   0x08

bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

bool PltDecryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char replayWindowIndex,
                       unsigned int sequenceNumber,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

void PltGenerateRandomData(unsigned char* data, int length);
