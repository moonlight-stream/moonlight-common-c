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

typedef struct _STUN_MESSAGE {
    unsigned short messageType;
    unsigned short messageLength;
    unsigned int magicCookie;
    unsigned char transactionId[12];
} STUN_MESSAGE, *PSTUN_MESSAGE;

#pragma pack(pop)

static bool validateStunResponse(int bytesRead, PSTUN_MESSAGE request, PSTUN_MESSAGE response,
                                 struct addrinfo* stunAddrs, struct sockaddr_storage* remoteAddr)
{
    bool remoteAddrMatch;
    struct addrinfo *current;

    if (bytesRead <= 0) {
        // No response or an error
        return false;
    }

    // Check that the respone came from one of the addresses we tried
    remoteAddrMatch = false;
    for (current = stunAddrs; current != NULL && !remoteAddrMatch; current = current->ai_next) {
        if (current->ai_family != remoteAddr->ss_family) {
            continue;
        }

        if (current->ai_family == AF_INET) {
            struct sockaddr_in* sin1 = (struct sockaddr_in*)current->ai_addr;
            struct sockaddr_in* sin2 = (struct sockaddr_in*)remoteAddr;

            remoteAddrMatch = (sin1->sin_port == sin2->sin_port &&
                               memcmp(&sin1->sin_addr, &sin2->sin_addr, sizeof(sin1->sin_addr)) == 0);
        }
#ifdef AF_INET6
        else if (current->ai_family == AF_INET6) {
            struct sockaddr_in6* sin1 = (struct sockaddr_in6*)current->ai_addr;
            struct sockaddr_in6* sin2 = (struct sockaddr_in6*)remoteAddr;

            // We don't check scope for link-local addresses, but a STUN server will never be link-local
            remoteAddrMatch = (sin1->sin6_port == sin2->sin6_port &&
                               memcmp(&sin1->sin6_addr, &sin2->sin6_addr, sizeof(sin1->sin6_addr)) == 0);
        }
#endif
    }

    if (!remoteAddrMatch) {
        Limelog("STUN message received from unexpected address\n");
        return false;
    }

    // Verify that we at least got a full STUN header
    if (bytesRead < (int)sizeof(*response)) {
        Limelog("STUN message truncated: %d\n", bytesRead);
        return false;
    }

    // Verify that the message length is valid per RFC 5389
    unsigned short stunBodyLen = htons(response->messageLength);
    if ((stunBodyLen & 0x3) != 0) {
        Limelog("STUN message length is not a multiple of 4\n");
        return false;
    }
    else if (bytesRead < (int)(sizeof(STUN_MESSAGE) + stunBodyLen)) {
        Limelog("STUN message truncated: expected %d bytes, got %d\n",
                (int)sizeof(STUN_MESSAGE) + stunBodyLen, bytesRead);
        return false;
    }

    // Validate that the response is valid and successful
    if (htonl(response->magicCookie) != STUN_MESSAGE_COOKIE) {
        Limelog("Bad STUN cookie value: %x\n", htonl(response->magicCookie));
        return false;
    }
    else if (memcmp(request->transactionId, response->transactionId, sizeof(request->transactionId))) {
        Limelog("STUN transaction ID mismatch\n");
        return false;
    }
    else if (htons(response->messageType) != STUN_MESSAGE_BINDING_SUCCESS) {
        Limelog("STUN message type mismatch: %x\n", htons(response->messageType));
        return false;
    }

    return true;
}

// This is extremely rudamentary STUN code simply for deriving the WAN IPv4 address when behind a NAT.
int LiFindExternalAddressIP4(const char* stunServer, unsigned short stunPort, unsigned int* wanAddr)
{
    SOCKET sock;
    struct addrinfo* stunAddrs;
    struct addrinfo hints;
    char stunPortStr[6];
    int err;
    STUN_MESSAGE reqMsg;
    int i;
    int bytesRead;
    bool validResponse;
    struct sockaddr_storage remoteAddr;
    SOCKADDR_LEN addrLen;
    PSTUN_ATTRIBUTE_HEADER attribute;
    PSTUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE ipv4Attrib;
    union {
        STUN_MESSAGE hdr;
        char buf[1024];
    } resp;

    sock = INVALID_SOCKET;

    err = initializePlatformSockets();
    if (err != 0) {
        Limelog("Failed to initialize sockets: %d\n", err);
        return err;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_ADDRCONFIG;

    snprintf(stunPortStr, sizeof(stunPortStr), "%u", stunPort);
    err = getaddrinfo(stunServer, stunPortStr, &hints, &stunAddrs);
    if (err != 0 || stunAddrs == NULL) {
        Limelog("Failed to resolve STUN server: %d\n", err);
        stunAddrs = NULL;
        goto Exit;
    }

    sock = bindUdpSocket(hints.ai_family, NULL, 0, 0, SOCK_QOS_TYPE_BEST_EFFORT);
    if (sock == INVALID_SOCKET) {
        err = LastSocketFail();
        Limelog("Failed to connect to STUN server: %d\n", err);
        goto Exit;
    }

    reqMsg.messageType = htons(STUN_MESSAGE_BINDING_REQUEST);
    reqMsg.messageLength = 0;
    reqMsg.magicCookie = htonl(STUN_MESSAGE_COOKIE);
    PltGenerateRandomData(reqMsg.transactionId, sizeof(reqMsg.transactionId));

    bytesRead = 0;
    validResponse = false;
    for (i = 0; i < STUN_RECV_TIMEOUT_SEC * 1000 / UDP_RECV_POLL_TIMEOUT_MS && !validResponse; i++) {
        // Retransmit the request every second until the timeout elapses or we get a valid response
        if (i % (1000 / UDP_RECV_POLL_TIMEOUT_MS) == 0) {
            struct addrinfo *current;

            // Send a request to each resolved address but stop if we get a valid response
            for (current = stunAddrs; current != NULL && !validResponse; current = current->ai_next) {
                err = (int)sendto(sock, (char *)&reqMsg, sizeof(reqMsg), 0, current->ai_addr, (SOCKADDR_LEN)current->ai_addrlen);
                if (err == SOCKET_ERROR) {
                    err = LastSocketFail();
                    Limelog("Failed to send STUN binding request: %d\n", err);
                    continue;
                }

                // Wait UDP_RECV_POLL_TIMEOUT_MS before moving on to the next server to
                // avoid having to spam the other STUN servers if we find a working one.
                addrLen = sizeof(remoteAddr);
                bytesRead = recvUdpSocket(sock, resp.buf, sizeof(resp.buf), true, &remoteAddr, &addrLen);
                validResponse = validateStunResponse(bytesRead, &reqMsg, &resp.hdr, stunAddrs, &remoteAddr);
            }
        }
        else {
            // This waits in UDP_RECV_POLL_TIMEOUT_MS increments
            addrLen = sizeof(remoteAddr);
            bytesRead = recvUdpSocket(sock, resp.buf, sizeof(resp.buf), true, &remoteAddr, &addrLen);
            validResponse = validateStunResponse(bytesRead, &reqMsg, &resp.hdr, stunAddrs, &remoteAddr);
        }
    }

    if (bytesRead == 0) {
        Limelog("No response from STUN server\n");
        err = -2;
        goto Exit;
    }
    else if (bytesRead == SOCKET_ERROR) {
        err = LastSocketFail();
        Limelog("Failed to read STUN binding response: %d\n", err);
        goto Exit;
    }
    else if (!validResponse) {
        // Already logged in validateStunResponse()
        err = -3;
        goto Exit;
    }

    attribute = (PSTUN_ATTRIBUTE_HEADER)(&resp.hdr + 1);
    bytesRead -= sizeof(resp.hdr);
    while (bytesRead >= (int)sizeof(*attribute)) {
        int attrLen = htons(attribute->length);

        if (bytesRead < (int)sizeof(*attribute) + attrLen) {
            Limelog("STUN attribute out of bounds: %d\n", attrLen);
            err = -5;
            goto Exit;
        }
        // Mask off the comprehension bit
        else if ((htons(attribute->type) & 0x7FFF) != STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS) {
            // Attributes are padded to 4 byte alignment
            int attrPaddedLen = (attrLen + 3) & ~3;

            // Continue searching if this wasn't our address
            bytesRead -= sizeof(*attribute) + attrPaddedLen;
            attribute = (PSTUN_ATTRIBUTE_HEADER)(((char*)attribute) + sizeof(*attribute) + attrPaddedLen);
            continue;
        }

        ipv4Attrib = (PSTUN_MAPPED_IPV4_ADDRESS_ATTRIBUTE)attribute;
        if (attrLen != 8) {
            Limelog("STUN address length mismatch: %d\n", attrLen);
            err = -5;
            goto Exit;
        }
        else if (ipv4Attrib->addressFamily != 1) {
            Limelog("STUN address family mismatch: %x\n", ipv4Attrib->addressFamily);
            err = -5;
            goto Exit;
        }

        // The address is XORed with the cookie
        *wanAddr = ipv4Attrib->address ^ resp.hdr.magicCookie;

        err = 0;
        goto Exit;
    }

    Limelog("No XOR mapped address found in STUN response!\n");
    err = -6;

Exit:
    if (sock != INVALID_SOCKET) {
        closeSocket(sock);
    }

    if (stunAddrs != NULL) {
        freeaddrinfo(stunAddrs);
    }

    cleanupPlatformSockets();
    return err;
}
