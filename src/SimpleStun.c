#include "Limelight-internal.h"

#define STUN_RECV_TIMEOUT_SEC 3

#define STUN_MESSAGE_BINDING_REQUEST 0x0001
#define STUN_MESSAGE_BINDING_SUCCESS 0x0101
#define STUN_MESSAGE_COOKIE 0x2112a442

#define STUN_ATTRIBUTE_MAPPED_ADDRESS 0x0001
#define STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS 0x0020

#pragma pack(push, 1)

typedef struct _STUN_ATTRIBUTE_HEADER {
    unsigned short type;
    unsigned short length;
} STUN_ATTRIBUTE_HEADER, *PSTUN_ATTRIBUTE_HEADER;

typedef struct _STUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE {
    STUN_ATTRIBUTE_HEADER hdr;
    unsigned char reserved;
    unsigned char addressFamily;
    unsigned short port;
    unsigned int address;
} STUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE, *PSTUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE;

#define TXID_DWORDS 3
typedef struct _STUN_MESSAGE {
    unsigned short messageType;
    unsigned short messageLength;
    unsigned int magicCookie;
    int transactionId[TXID_DWORDS];
} STUN_MESSAGE, *PSTUN_MESSAGE;

#pragma pack(pop)

// This is extremely rudamentary STUN code simply for deriving the WAN IPv4 address when behind a NAT.
int LiFindExternalAddressIP4(const char* stunServer, unsigned short stunPort, unsigned int* wanAddr)
{
    SOCKET sock;
    struct sockaddr_storage stunAddr;
    struct sockaddr_in* stunAddrIn;
    SOCKADDR_LEN stunAddrLen;
    int err;
    STUN_MESSAGE reqMsg;
    int i;
    int bytesRead;
    PSTUN_ATTRIBUTE_HEADER attribute;
    PSTUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE ipv4Attrib;
    union {
        STUN_MESSAGE hdr;
        char buf[1024];
    } resp;

    err = resolveHostName(stunServer, AF_INET, 0, &stunAddr, &stunAddrLen);
    if (err != 0) {
        Limelog("Failed to resolve STUN server: %d\n", err);
        return err;
    }

    sock = bindUdpSocket(AF_INET, 2048);
    if (sock == INVALID_SOCKET) {
        err = LastSocketFail();
        Limelog("Failed to connect to STUN server: %d\n", err);
        return err;
    }

    reqMsg.messageType = htons(STUN_MESSAGE_BINDING_REQUEST);
    reqMsg.messageLength = 0;
    reqMsg.magicCookie = htonl(STUN_MESSAGE_COOKIE);
    for (i = 0; i < TXID_DWORDS; i++) {
        reqMsg.transactionId[i] = rand();
    }

    bytesRead = SOCKET_ERROR;
    for (i = 0; i < STUN_RECV_TIMEOUT_SEC * 1000 / UDP_RECV_POLL_TIMEOUT_MS; i++) {
        // Retransmit the request every second until the timeout elapses
        if (i % (1000 / UDP_RECV_POLL_TIMEOUT_MS) == 0) {
            stunAddrIn = (struct sockaddr_in*)&stunAddr;
            stunAddrIn->sin_port = htons(stunPort);
            err = (int)sendto(sock, (char *)&reqMsg, sizeof(reqMsg), 0,
                               (struct sockaddr*)stunAddrIn, stunAddrLen);
            if (err == SOCKET_ERROR) {
                err = LastSocketFail();
                Limelog("Failed to send STUN binding request: %d\n", err);
                closeSocket(sock);
                return err;
            }
        }

        // This waits in UDP_RECV_POLL_TIMEOUT_MS increments
        bytesRead = recvUdpSocket(sock, resp.buf, sizeof(resp.buf), 1);
        if (bytesRead == 0) {
            // Timeout - continue looping
            continue;
        }

        break;
    }

    closeSocket(sock);

    if (bytesRead == 0) {
        Limelog("No response from STUN server\n");
        return -2;
    }
    else if (bytesRead == SOCKET_ERROR) {
        err = LastSocketFail();
        Limelog("Failed to read STUN binding response: %d\n", err);
        return err;
    }
    else if (bytesRead < sizeof(resp.hdr)) {
        Limelog("STUN message truncated: %d\n", bytesRead);
        return -3;
    }
    else if (htonl(resp.hdr.magicCookie) != STUN_MESSAGE_COOKIE) {
        Limelog("Bad STUN cookie value: %x\n", htonl(resp.hdr.magicCookie));
        return -3;
    }
    else if (memcmp(reqMsg.transactionId, resp.hdr.transactionId, sizeof(reqMsg.transactionId))) {
        Limelog("STUN transaction ID mismatch\n");
        return -3;
    }
    else if (htons(resp.hdr.messageType) != STUN_MESSAGE_BINDING_SUCCESS) {
        Limelog("STUN message type mismatch: %x\n", htons(resp.hdr.messageType));
        return -4;
    }

    attribute = (PSTUN_ATTRIBUTE_HEADER)(&resp.hdr + 1);
    bytesRead -= sizeof(resp.hdr);
    while (bytesRead > sizeof(*attribute)) {
        if (bytesRead < sizeof(*attribute) + htons(attribute->length)) {
            Limelog("STUN attribute out of bounds: %d\n", htons(attribute->length));
            return -5;
        }
        else if (htons(attribute->type) != STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS) {
            // Continue searching if this wasn't our address
            bytesRead -= sizeof(*attribute) + htons(attribute->length);
            attribute = (PSTUN_ATTRIBUTE_HEADER)(((char*)attribute) + sizeof(*attribute) + htons(attribute->length));
            continue;
        }

        ipv4Attrib = (PSTUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE)attribute;
        if (htons(ipv4Attrib->hdr.length) != 8) {
            Limelog("STUN address length mismatch: %d\n", htons(ipv4Attrib->hdr.length));
            return -5;
        }
        else if (ipv4Attrib->addressFamily != 1) {
            Limelog("STUN address family mismatch: %x\n", ipv4Attrib->addressFamily);
            return -5;
        }

        // The address is XORed with the cookie
        *wanAddr = ipv4Attrib->address ^ resp.hdr.magicCookie;

        return 0;
    }

    Limelog("No XOR mapped address found in STUN response!\n");
    return -6;
}
