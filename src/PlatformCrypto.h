#pragma once

#include <stdbool.h>

// Hide the real OpenSSL definition from other code
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

typedef struct _PLT_CRYPTO_CONTEXT {
    bool initialized; // Used for CBC only
    EVP_CIPHER_CTX* ctx;
} PLT_CRYPTO_CONTEXT, *PPLT_CRYPTO_CONTEXT;

#define ROUND_TO_PKCS7_PADDED_LEN(x) ((((x) + 15) / 16) * 16)

PPLT_CRYPTO_CONTEXT PltCreateCryptoContext();
void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx);

#define ALGORITHM_AES_CBC 1
#define ALGORITHM_AES_GCM 2

bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

bool PltDecryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength);

void PltGenerateRandomData(unsigned char* data, int length);
