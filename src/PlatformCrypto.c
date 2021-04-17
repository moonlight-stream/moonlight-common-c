#include "Limelight-internal.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

static int addPkcs7PaddingInPlace(unsigned char* plaintext, int plaintextLen) {
    int paddedLength = ROUND_TO_PKCS7_PADDED_LEN(plaintextLen);
    unsigned char paddingByte = (unsigned char)(16 - (plaintextLen % 16));

    memset(&plaintext[plaintextLen], paddingByte, paddedLength - plaintextLen);

    return paddedLength;
}

// For CBC modes, inputData buffer must be allocated with length rounded up to next multiple of 16 and inputData buffer may be modified!
bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength) {
    bool ret = false;
    const EVP_CIPHER* cipher;

    switch (algorithm) {
    case ALGORITHM_AES_CBC:
        LC_ASSERT(keyLength == 16);
        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);
        cipher = EVP_aes_128_cbc();
        break;
    case ALGORITHM_AES_GCM:
        LC_ASSERT(keyLength == 16);
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);
        cipher = EVP_aes_128_gcm();
        break;
    default:
        LC_ASSERT(false);
        return false;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        if (EVP_EncryptInit_ex(ctx->ctx, cipher, NULL, NULL, NULL) != 1) {
            goto cleanup;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, NULL) != 1) {
            goto cleanup;
        }

        if (EVP_EncryptInit_ex(ctx->ctx, NULL, NULL, key, iv) != 1) {
            goto cleanup;
        }
    }
    else {
        if (!ctx->initialized) {
            if (EVP_EncryptInit_ex(ctx->ctx, cipher, NULL, key, iv) != 1) {
                goto cleanup;
            }

            ctx->initialized = true;
        }

        inputDataLength = addPkcs7PaddingInPlace(inputData, inputDataLength);
    }

    if (EVP_EncryptUpdate(ctx->ctx, outputData, outputDataLength, inputData, inputDataLength) != 1) {
        goto cleanup;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        int len;

        // GCM encryption won't ever fill ciphertext here but we have to call it anyway
        if (EVP_EncryptFinal_ex(ctx->ctx, outputData, &len) != 1) {
            goto cleanup;
        }
        LC_ASSERT(len == 0);

        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_GET_TAG, tagLength, tag) != 1) {
            goto cleanup;
        }
    }

    ret = true;

cleanup:
    if (algorithm == ALGORITHM_AES_GCM) {
        EVP_CIPHER_CTX_reset(ctx->ctx);
    }
    return ret;
}

bool PltDecryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength) {
    bool ret = false;
    const EVP_CIPHER* cipher;

    switch (algorithm) {
    case ALGORITHM_AES_CBC:
        LC_ASSERT(keyLength == 16);
        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);
        cipher = EVP_aes_128_cbc();
        break;
    case ALGORITHM_AES_GCM:
        LC_ASSERT(keyLength == 16);
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);
        cipher = EVP_aes_128_gcm();
        break;
    default:
        LC_ASSERT(false);
        return false;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        if (EVP_DecryptInit_ex(ctx->ctx, cipher, NULL, NULL, NULL) != 1) {
            goto cleanup;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, NULL) != 1) {
            goto cleanup;
        }

        if (EVP_DecryptInit_ex(ctx->ctx, NULL, NULL, key, iv) != 1) {
            goto cleanup;
        }
    }
    else {
        if (!ctx->initialized) {
            if (EVP_DecryptInit_ex(ctx->ctx, cipher, NULL, key, iv) != 1) {
                goto cleanup;
            }

            ctx->initialized = true;
        }
    }

    if (EVP_DecryptUpdate(ctx->ctx, outputData, outputDataLength, inputData, inputDataLength) != 1) {
        goto cleanup;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        int len;

        // Set the GCM tag before calling EVP_DecryptFinal_ex()
        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_TAG, tagLength, tag) != 1) {
            goto cleanup;
        }

        // GCM will never have additional plaintext here, but we need to call it to
        // ensure that the GCM authentication tag is correct for this data.
        if (EVP_DecryptFinal_ex(ctx->ctx, outputData, &len) != 1) {
            goto cleanup;
        }
        LC_ASSERT(len == 0);
    }

    ret = true;

cleanup:
    if (algorithm == ALGORITHM_AES_GCM) {
        EVP_CIPHER_CTX_reset(ctx->ctx);
    }
    return ret;
}

PPLT_CRYPTO_CONTEXT PltCreateCryptoContext(void) {
    PPLT_CRYPTO_CONTEXT ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    ctx->initialized = false;
    ctx->ctx = EVP_CIPHER_CTX_new();
    if (!ctx->ctx) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx) {
    EVP_CIPHER_CTX_free(ctx->ctx);
    free(ctx);
}

void PltGenerateRandomData(unsigned char* data, int length) {
    RAND_bytes(data, length);
}
