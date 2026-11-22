// Adapted from qiin2333/moonlight-common-c, commit 31a2a458.
#include "RemoteTextContextStream.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static const uint8_t uiaPacket[76] = {
    0x01, 0x4c, 0xa7, 0x00,
    0x12, 0x34, 0x56, 0x78,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
    0x02, 0x02, 0x00, 0x00,
    0xf6, 0xff, 0xff, 0xff, 0x14, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00,
    0xf4, 0x01, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00,
    0xea, 0x01, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00,
    0xeb, 0x01, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00,
    0x80, 0x07, 0x00, 0x00, 0x38, 0x04, 0x00, 0x00,
};

static void testUiaGoldenVector(void) {
    LI_REMOTE_TEXT_CONTEXT context;

    CHECK(decodeRemoteTextContextPacket(uiaPacket, sizeof(uiaPacket), &context));
    CHECK(context.flags == 0x00a7);
    CHECK(context.revision == 0x78563412);
    CHECK(context.activationId == UINT64_C(0x0102030405060708));
    CHECK(context.inputToken == UINT64_C(0x1112131415161718));
    CHECK(context.source == LI_TEXT_CONTEXT_SOURCE_UIA);
    CHECK(context.cause == LI_TEXT_CONTEXT_CAUSE_REMOTE_MOUSE);
    CHECK(context.anchorX == -10);
    CHECK(context.anchorY == 20);
    CHECK(context.elementLeft == 100);
    CHECK(context.elementTop == 200);
    CHECK(context.elementRight == 500);
    CHECK(context.elementBottom == 260);
    CHECK(context.caretLeft == 490);
    CHECK(context.caretTop == 250);
    CHECK(context.caretRight == 491);
    CHECK(context.caretBottom == 259);
    CHECK(context.captureWidth == 1920);
    CHECK(context.captureHeight == 1080);
}

static void expectRejectedAt(size_t offset, uint8_t value) {
    uint8_t packet[sizeof(uiaPacket)];
    LI_REMOTE_TEXT_CONTEXT context;

    memcpy(packet, uiaPacket, sizeof(packet));
    packet[offset] = value;
    CHECK(!decodeRemoteTextContextPacket(packet, sizeof(packet), &context));
}

static void testMalformedPackets(void) {
    uint8_t packet[sizeof(uiaPacket)];
    LI_REMOTE_TEXT_CONTEXT context;

    CHECK(!decodeRemoteTextContextPacket(NULL, sizeof(uiaPacket), &context));
    CHECK(!decodeRemoteTextContextPacket(uiaPacket, sizeof(uiaPacket) - 1, &context));
    CHECK(!decodeRemoteTextContextPacket(uiaPacket, sizeof(uiaPacket), NULL));
    expectRejectedAt(0, 2);
    expectRejectedAt(1, 75);
    expectRejectedAt(26, 1);
    memcpy(packet, uiaPacket, sizeof(packet));
    memset(packet + 68, 0, 4);
    CHECK(!decodeRemoteTextContextPacket(packet, sizeof(packet), &context));
}

static void testSemanticFieldsAreTransported(void) {
    uint8_t packet[sizeof(uiaPacket)];
    LI_REMOTE_TEXT_CONTEXT context;

    memcpy(packet, uiaPacket, sizeof(packet));
    packet[2] = LI_TEXT_CONTEXT_FLAG_INPUT_MATCHED;
    packet[3] = LI_TEXT_CONTEXT_FLAG_PANE_VISIBLE >> 8;
    packet[24] = LI_TEXT_CONTEXT_SOURCE_INPUT_PANE;
    packet[25] = LI_TEXT_CONTEXT_CAUSE_REMOTE_TOUCH;
    CHECK(decodeRemoteTextContextPacket(packet, sizeof(packet), &context));
    packet[3] |= LI_TEXT_CONTEXT_FLAG_AUTO_SHOW >> 8;
    CHECK(decodeRemoteTextContextPacket(packet, sizeof(packet), &context));

    memcpy(packet, uiaPacket, sizeof(packet));
    packet[2] &= (uint8_t)~LI_TEXT_CONTEXT_FLAG_ELEMENT_RECT;
    CHECK(decodeRemoteTextContextPacket(packet, sizeof(packet), &context));

    // UIA deactivation reuses the trusted activation ID but intentionally has
    // no editable/element flags. It must reach the client state machine.
    memcpy(packet, uiaPacket, sizeof(packet));
    packet[2] = LI_TEXT_CONTEXT_FLAG_INPUT_MATCHED | LI_TEXT_CONTEXT_FLAG_ANCHOR_POINT;
    packet[3] = 0;
    CHECK(decodeRemoteTextContextPacket(packet, sizeof(packet), &context));

    // New flags and source/cause values remain available to newer clients.
    // The transport layer must not reject structurally valid packets merely
    // because it does not understand their activation policy yet.
    memcpy(packet, uiaPacket, sizeof(packet));
    packet[3] |= 0x80;
    packet[24] = 99;
    packet[25] = 99;
    CHECK(decodeRemoteTextContextPacket(packet, sizeof(packet), &context));
    CHECK(context.source == 99);
    CHECK(context.cause == 99);
}

int main(void) {
    testUiaGoldenVector();
    testMalformedPackets();
    testSemanticFieldsAreTransported();

    if (failures != 0) {
        fprintf(stderr, "%d remote text context golden-vector checks failed\n", failures);
        return 1;
    }

    puts("remote text context golden vectors passed");
    return 0;
}
