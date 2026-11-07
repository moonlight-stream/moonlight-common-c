#include "Limelight-internal.h"

#define MIC_RTP_HEADER_SIZE 12
#define MIC_OPUS_FRAME_SIZE 20
#define MIC_CIPHER_SIZE 32
#define MIC_SOURCE_PORT 50002

struct _NATIVE_MIC_SENDER {
    SOCKET socket;
    PPLT_CRYPTO_CONTEXT crypto;
    struct sockaddr_in destination;
    unsigned char key[16];
    uint32_t keyId;
    uint16_t sequence;
    uint32_t timestamp;
    uint64_t nextPing;
    bool failed;
};

static bool isPrivateAddress(uint32_t address) {
    return (address >> 24) == 10 ||
           (address >> 24) == 127 ||
           (address >> 20) == 0xac1 ||
           (address >> 16) == 0xc0a8 ||
           (address >> 22) == 0x191; // Shared address space, including Tailscale.
}

void LiDestroyNativeMicSender(NATIVE_MIC_SENDER* sender) {
    if (sender == NULL) {
        return;
    }
    if (sender->socket != INVALID_SOCKET) {
        closeSocket(sender->socket);
    }
    if (sender->crypto != NULL) {
        PltDestroyCryptoContext(sender->crypto);
    }
    volatile unsigned char* key = sender->key;
    for (size_t i = 0; i < sizeof(sender->key); i++) {
        key[i] = 0;
    }
    free(sender);
}

NATIVE_MIC_SENDER* LiCreateNativeMicSender(void) {
    unsigned short port = LiGetNativeMicPort();
    if (!StreamConfig.requestNativeMic || !port || !LiIsNativeMicAudioEncrypted() ||
            RemoteAddr.ss_family != AF_INET) {
        return NULL;
    }

    const struct sockaddr_in* remote = (const struct sockaddr_in*)&RemoteAddr;
    if (!isPrivateAddress(ntohl(remote->sin_addr.s_addr))) {
        return NULL;
    }

    NATIVE_MIC_SENDER* sender = calloc(1, sizeof(*sender));
    if (sender == NULL) {
        return NULL;
    }
    sender->socket = createSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    sender->crypto = PltCreateCryptoContext(0);
    sender->destination = *remote;
    sender->destination.sin_port = htons(port);

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(MIC_SOURCE_PORT);
    if (sender->socket == INVALID_SOCKET || sender->crypto == NULL ||
            bind(sender->socket, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        LiDestroyNativeMicSender(sender);
        return NULL;
    }
    memcpy(sender->key, StreamConfig.remoteInputAesKey, sizeof(sender->key));
    memcpy(&sender->keyId, StreamConfig.remoteInputAesIv, sizeof(sender->keyId));
    sender->keyId = ntohl(sender->keyId);
    return sender;
}

int LiSendNativeMicFrame(NATIVE_MIC_SENDER* sender, const unsigned char* frame, int length) {
    if (sender == NULL || sender->failed || frame == NULL || length != MIC_OPUS_FRAME_SIZE) {
        return -1;
    }

    uint64_t now = PltGetMillis();
    const struct sockaddr* destination = (const struct sockaddr*)&sender->destination;
    if (now >= sender->nextPing) {
        if (sendto(sender->socket, "PING", 4, 0, destination, sizeof(sender->destination)) != 4) {
            sender->failed = true;
            return -1;
        }
        sender->nextPing = now + 500;
    }

    unsigned char packet[MIC_RTP_HEADER_SIZE + MIC_CIPHER_SIZE] = {0x80, 97};
    uint16_t sequence = htons(sender->sequence);
    uint32_t timestamp = htonl(sender->timestamp);
    uint32_t ssrc = htonl(1);
    memcpy(packet + 2, &sequence, sizeof(sequence));
    memcpy(packet + 4, &timestamp, sizeof(timestamp));
    memcpy(packet + 8, &ssrc, sizeof(ssrc));

    unsigned char iv[16] = {0};
    uint32_t prefix = htonl(sender->keyId + sender->sequence);
    memcpy(iv, &prefix, sizeof(prefix));
    unsigned char input[MIC_OPUS_FRAME_SIZE];
    memcpy(input, frame, sizeof(input));
    int encryptedLength;
    if (!PltEncryptMessage(sender->crypto, ALGORITHM_AES_CBC,
                           CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH,
                           sender->key, sizeof(sender->key), iv, sizeof(iv), NULL, 0,
                           input, sizeof(input), packet + MIC_RTP_HEADER_SIZE, &encryptedLength) ||
            encryptedLength != MIC_CIPHER_SIZE ||
            sendto(sender->socket, (const char*)packet, sizeof(packet), 0,
                   destination, sizeof(sender->destination)) != sizeof(packet)) {
        sender->failed = true;
        return -1;
    }

    sender->sequence++;
    sender->timestamp += 5; // Native microphone timestamps count milliseconds.
    return 0;
}
