#include "Limelight-internal.h"

#ifdef USE_PSA_CRYPTO
#include <psa/crypto.h>
#include <stdlib.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

static int addPkcs7PaddingInPlace(unsigned char* plaintext, int plaintextLen) {
    int paddedLength = ROUND_TO_PKCS7_PADDED_LEN(plaintextLen);
    unsigned char paddingByte = (unsigned char)(16 - (plaintextLen % 16));

    memset(&plaintext[plaintextLen], paddingByte, paddedLength - plaintextLen);

    return paddedLength;
}

// When CIPHER_FLAG_PAD_TO_BLOCK_SIZE is used, inputData buffer must be allocated such that
// the buffer length is at least ROUND_TO_PKCS7_PADDED_LEN(inputDataLength) and inputData
// buffer may be modified! If CIPHER_FLAG_PAD_TO_BLOCK_SIZE is used, it must be passed to
// all invocations of PltEncryptMessage() on the same crypto context (mixing padded and
// non-padded encryption is not allowed).
//
// CIPHER_FLAG_PAD_TO_BLOCK_SIZE and CIPHER_FLAG_FINISH may not be used on the same context.
//
// When CIPHER_FLAG_FINISH is used with CBC encryption, the output buffer size must be at
// least ROUND_TO_PKCS7_PADDED_LEN(inputDataLength).
//
// For GCM, the IV can change from message to message without CIPHER_FLAG_RESET_IV.
// CIPHER_FLAG_RESET_IV is only required for GCM when the IV length changes.
//
// Changing the key between encrypt/decrypt calls on a single context is not supported.
// Using the same crypto context for both encryption and decryption is not supported.
bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength) {
#ifdef USE_PSA_CRYPTO
    if (algorithm == ALGORITHM_AES_GCM) {
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);

        if (!ctx->initialized) {
            if (psa_crypto_init() != PSA_SUCCESS) {
                return false;
            }

            psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
            psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
            psa_set_key_bits(&attributes, keyLength * 8);
            psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
            psa_set_key_algorithm(&attributes, PSA_ALG_GCM);

            if (psa_import_key(&attributes, key, keyLength, &ctx->key) != PSA_SUCCESS) {
                psa_reset_key_attributes(&attributes);
                return false;
            }

            psa_reset_key_attributes(&attributes);
            ctx->initialized = true;
        }

        psa_aead_operation_t aeadOp = PSA_AEAD_OPERATION_INIT;
        if (psa_aead_encrypt_setup(&aeadOp, ctx->key, PSA_ALG_GCM) != PSA_SUCCESS) {
            return false;
        }

        if (psa_aead_set_lengths(&aeadOp, 0, inputDataLength) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        if (psa_aead_set_nonce(&aeadOp, iv, ivLength) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        size_t outLen = 0;
        if (psa_aead_update(&aeadOp, inputData, inputDataLength, outputData, inputDataLength, &outLen) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        size_t finishLen = 0;
        size_t tagOutLen = 0;
        if (psa_aead_finish(&aeadOp, outputData + outLen, inputDataLength - outLen, &finishLen, tag, tagLength, &tagOutLen) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        LC_ASSERT(tagOutLen == (size_t)tagLength);
        *outputDataLength = (int)(outLen + finishLen);
        return true;
    }
    else if (algorithm == ALGORITHM_AES_CBC) {
        psa_algorithm_t alg = (flags & CIPHER_FLAG_PAD_TO_BLOCK_SIZE) ? PSA_ALG_CBC_NO_PADDING : PSA_ALG_CBC_PKCS7;

        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);

        if (!ctx->initialized) {
            if (psa_crypto_init() != PSA_SUCCESS) {
                return false;
            }

            psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
            psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
            psa_set_key_bits(&attributes, keyLength * 8);
            psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
            psa_set_key_algorithm(&attributes, alg);

            if (psa_import_key(&attributes, key, keyLength, &ctx->key) != PSA_SUCCESS) {
                psa_reset_key_attributes(&attributes);
                return false;
            }

            psa_reset_key_attributes(&attributes);
            ctx->initialized = true;
        }

        if (!ctx->cipherOpActive || (flags & CIPHER_FLAG_RESET_IV)) {
            if (ctx->cipherOpActive) {
                psa_cipher_abort(&ctx->cipherOp);
                ctx->cipherOpActive = false;
            }

            if (psa_cipher_encrypt_setup(&ctx->cipherOp, ctx->key, alg) != PSA_SUCCESS) {
                return false;
            }

            if (psa_cipher_set_iv(&ctx->cipherOp, iv, ivLength) != PSA_SUCCESS) {
                psa_cipher_abort(&ctx->cipherOp);
                return false;
            }

            ctx->cipherOpActive = true;
        }

        if (flags & CIPHER_FLAG_PAD_TO_BLOCK_SIZE) {
            inputDataLength = addPkcs7PaddingInPlace(inputData, inputDataLength);
        }

        // NB: We do not use ROUND_TO_PKCS7_PADDED_LEN() here because outputData size is
        // only guaranteed to be padded to PKCS7 length when CIPHER_FLAG_FINISH is used.
        size_t outLen = 0;
        if (psa_cipher_update(&ctx->cipherOp, inputData, inputDataLength, outputData, inputDataLength, &outLen) != PSA_SUCCESS) {
            psa_cipher_abort(&ctx->cipherOp);
            ctx->cipherOpActive = false;
            return false;
        }

        if (flags & CIPHER_FLAG_FINISH) {
            size_t finishLength = 0;

            if (psa_cipher_finish(&ctx->cipherOp, outputData + outLen, ROUND_TO_PKCS7_PADDED_LEN(inputDataLength) - outLen, &finishLength) != PSA_SUCCESS) {
                ctx->cipherOpActive = false;
                return false;
            }

            ctx->cipherOpActive = false;
            outLen += finishLength;
        }

        *outputDataLength = (int)outLen;
        return true;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
#else
    LC_ASSERT(keyLength == 16);

    if (algorithm == ALGORITHM_AES_GCM) {
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);

        if (!ctx->initialized || (flags & CIPHER_FLAG_RESET_IV)) {
            // Perform a full initialization. This codepath also allows
            // us to change the IV length if required.
            if (EVP_EncryptInit_ex(ctx->ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) {
                return false;
            }

            if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, NULL) != 1) {
                return false;
            }

            if (EVP_EncryptInit_ex(ctx->ctx, NULL, NULL, key, iv) != 1) {
                return false;
            }

            ctx->initialized = true;
        }
        else {
            // Calling with cipher == NULL results in a parameter change
            // without requiring a reallocation of the internal cipher ctx.
            if (EVP_EncryptInit_ex(ctx->ctx, NULL, NULL, NULL, iv) != 1) {
                return false;
            }
        }
    }
    else if (algorithm == ALGORITHM_AES_CBC) {
        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);

        if (!ctx->initialized) {
            // Perform a full initialization
            if (EVP_EncryptInit_ex(ctx->ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
                return false;
            }

            ctx->initialized = true;
        }
        else if (flags & CIPHER_FLAG_RESET_IV) {
            // Calling with cipher == NULL results in a parameter change
            // without requiring a reallocation of the internal cipher ctx.
            if (EVP_EncryptInit_ex(ctx->ctx, NULL, NULL, NULL, iv) != 1) {
                return false;
            }
        }

        if (flags & CIPHER_FLAG_PAD_TO_BLOCK_SIZE) {
            inputDataLength = addPkcs7PaddingInPlace(inputData, inputDataLength);
        }
    }
    else {
        LC_ASSERT(false);
        return false;
    }

    if (EVP_EncryptUpdate(ctx->ctx, outputData, outputDataLength, inputData, inputDataLength) != 1) {
        return false;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        int len;

        // GCM encryption won't ever fill ciphertext here but we have to call it anyway
        if (EVP_EncryptFinal_ex(ctx->ctx, outputData, &len) != 1) {
            return false;
        }
        LC_ASSERT(len == 0);

        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_GET_TAG, tagLength, tag) != 1) {
            return false;
        }
    }
    else if (flags & CIPHER_FLAG_FINISH) {
        int len;

        if (EVP_EncryptFinal_ex(ctx->ctx, &outputData[*outputDataLength], &len) != 1) {
            return false;
        }

        *outputDataLength += len;
    }

    return true;
#endif
}

// When CBC is used, outputData buffer must be allocated such that the buffer length is
// at least ROUND_TO_PKCS7_PADDED_LEN(inputDataLength) to allow room for PKCS7 padding.
//
// For GCM, the IV can change from message to message without CIPHER_FLAG_RESET_IV.
// CIPHER_FLAG_RESET_IV is only required for GCM when the IV length changes.
//
// Changing the key between encrypt/decrypt calls on a single context is not supported.
// Using the same crypto context for both encryption and decryption is not supported.
bool PltDecryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength) {
#ifdef USE_PSA_CRYPTO
    if (algorithm == ALGORITHM_AES_GCM) {
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);

        if (!ctx->initialized) {
            if (psa_crypto_init() != PSA_SUCCESS) {
                return false;
            }

            psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
            psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
            psa_set_key_bits(&attributes, keyLength * 8);
            psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
            psa_set_key_algorithm(&attributes, PSA_ALG_GCM);

            if (psa_import_key(&attributes, key, keyLength, &ctx->key) != PSA_SUCCESS) {
                psa_reset_key_attributes(&attributes);
                return false;
            }

            psa_reset_key_attributes(&attributes);
            ctx->initialized = true;
        }

        psa_aead_operation_t aeadOp = PSA_AEAD_OPERATION_INIT;
        if (psa_aead_decrypt_setup(&aeadOp, ctx->key, PSA_ALG_GCM) != PSA_SUCCESS) {
            return false;
        }

        if (psa_aead_set_lengths(&aeadOp, 0, inputDataLength) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        if (psa_aead_set_nonce(&aeadOp, iv, ivLength) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        size_t outLen = 0;
        if (psa_aead_update(&aeadOp, inputData, inputDataLength, outputData, inputDataLength, &outLen) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        size_t verifyLen = 0;
        if (psa_aead_verify(&aeadOp, outputData + outLen, inputDataLength - outLen, &verifyLen, tag, tagLength) != PSA_SUCCESS) {
            psa_aead_abort(&aeadOp);
            return false;
        }

        *outputDataLength = (int)(outLen + verifyLen);
        return true;
    }
    else if (algorithm == ALGORITHM_AES_CBC) {
        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);

        if (!ctx->initialized) {
            if (psa_crypto_init() != PSA_SUCCESS) {
                return false;
            }

            psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
            psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
            psa_set_key_bits(&attributes, keyLength * 8);
            psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
            psa_set_key_algorithm(&attributes, PSA_ALG_CBC_PKCS7);

            if (psa_import_key(&attributes, key, keyLength, &ctx->key) != PSA_SUCCESS) {
                psa_reset_key_attributes(&attributes);
                return false;
            }

            psa_reset_key_attributes(&attributes);
            ctx->initialized = true;
        }

        if (!ctx->cipherOpActive || (flags & CIPHER_FLAG_RESET_IV)) {
            if (ctx->cipherOpActive) {
                psa_cipher_abort(&ctx->cipherOp);
                ctx->cipherOpActive = false;
            }

            if (psa_cipher_decrypt_setup(&ctx->cipherOp, ctx->key, PSA_ALG_CBC_PKCS7) != PSA_SUCCESS) {
                return false;
            }

            if (psa_cipher_set_iv(&ctx->cipherOp, iv, ivLength) != PSA_SUCCESS) {
                psa_cipher_abort(&ctx->cipherOp);
                return false;
            }

            ctx->cipherOpActive = true;
        }

        size_t outBufferSize = ROUND_TO_PKCS7_PADDED_LEN(inputDataLength);
        size_t outLen = 0;
        if (psa_cipher_update(&ctx->cipherOp, inputData, inputDataLength, outputData, outBufferSize, &outLen) != PSA_SUCCESS) {
            psa_cipher_abort(&ctx->cipherOp);
            ctx->cipherOpActive = false;
            return false;
        }

        if (flags & CIPHER_FLAG_FINISH) {
            size_t finishLength = 0;

            if (psa_cipher_finish(&ctx->cipherOp, outputData + outLen, outBufferSize - outLen, &finishLength) != PSA_SUCCESS) {
                ctx->cipherOpActive = false;
                return false;
            }

            ctx->cipherOpActive = false;
            outLen += finishLength;
        }

        *outputDataLength = (int)outLen;
        return true;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
#else
    LC_ASSERT(keyLength == 16);

    if (algorithm == ALGORITHM_AES_GCM) {
        LC_ASSERT(tag != NULL);
        LC_ASSERT(tagLength > 0);

        if (!ctx->initialized || (flags & CIPHER_FLAG_RESET_IV)) {
            // Perform a full initialization. This codepath also allows
            // us to change the IV length if required.
            if (EVP_DecryptInit_ex(ctx->ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) {
                return false;
            }

            if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, NULL) != 1) {
                return false;
            }

            if (EVP_DecryptInit_ex(ctx->ctx, NULL, NULL, key, iv) != 1) {
                return false;
            }

            ctx->initialized = true;
        }
        else {
            // Calling with cipher == NULL results in a parameter change
            // without requiring a reallocation of the internal cipher ctx.
            if (EVP_DecryptInit_ex(ctx->ctx, NULL, NULL, NULL, iv) != 1) {
                return false;
            }
        }
    }
    else if (algorithm == ALGORITHM_AES_CBC) {
        LC_ASSERT(tag == NULL);
        LC_ASSERT(tagLength == 0);

        if (!ctx->initialized) {
            // Perform a full initialization
            if (EVP_DecryptInit_ex(ctx->ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
                return false;
            }

            ctx->initialized = true;
        }
        else if (flags & CIPHER_FLAG_RESET_IV) {
            // Calling with cipher == NULL results in a parameter change
            // without requiring a reallocation of the internal cipher ctx.
            if (EVP_DecryptInit_ex(ctx->ctx, NULL, NULL, NULL, iv) != 1) {
                return false;
            }
        }
    }
    else {
        LC_ASSERT(false);
        return false;
    }

    if (EVP_DecryptUpdate(ctx->ctx, outputData, outputDataLength, inputData, inputDataLength) != 1) {
        return false;
    }

    if (algorithm == ALGORITHM_AES_GCM) {
        int len;

        // Set the GCM tag before calling EVP_DecryptFinal_ex()
        if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_TAG, tagLength, tag) != 1) {
            return false;
        }

        // GCM will never have additional plaintext here, but we need to call it to
        // ensure that the GCM authentication tag is correct for this data.
        if (EVP_DecryptFinal_ex(ctx->ctx, outputData, &len) != 1) {
            return false;
        }
        LC_ASSERT(len == 0);
    }
    else if (flags & CIPHER_FLAG_FINISH) {
        int len;

        if (EVP_DecryptFinal_ex(ctx->ctx, &outputData[*outputDataLength], &len) != 1) {
            return false;
        }

        *outputDataLength += len;
    }

    return true;
#endif
}

PPLT_CRYPTO_CONTEXT PltCreateCryptoContext(void) {
    PPLT_CRYPTO_CONTEXT ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    ctx->initialized = false;

#ifdef USE_PSA_CRYPTO
    ctx->key = PSA_KEY_ID_NULL;
    ctx->cipherOp = psa_cipher_operation_init();
    ctx->cipherOpActive = false;
#else
    ctx->ctx = EVP_CIPHER_CTX_new();
    if (!ctx->ctx) {
        free(ctx);
        return NULL;
    }
#endif

    return ctx;
}

void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx) {
    if (!ctx) {
        return;
    }

#ifdef USE_PSA_CRYPTO
    if (ctx->cipherOpActive) {
        psa_cipher_abort(&ctx->cipherOp);
    }

    if (ctx->initialized) {
        psa_destroy_key(ctx->key);
    }
#else
    EVP_CIPHER_CTX_free(ctx->ctx);
#endif
    free(ctx);
}

void PltGenerateRandomData(unsigned char* data, int length) {
#ifdef USE_PSA_CRYPTO
    if (psa_crypto_init() != PSA_SUCCESS) {
        Limelog("Initializing PSA crypto failed!\n");
        LC_ASSERT(false);
        return;
    }

    if (psa_generate_random(data, length) != PSA_SUCCESS) {
        Limelog("Generating random data failed!\n");
        LC_ASSERT(false);
    }
#else
    RAND_bytes(data, length);
#endif
}
