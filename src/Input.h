#pragma once

#include <stdint.h>

#pragma pack(push, 1)

// netfloat is just a little-endian float in byte form
// for network transmission.
typedef uint8_t netfloat[4];

typedef struct _NV_INPUT_HEADER {
    uint32_t size; // Size of packet (excluding this field) - Big Endian
    uint32_t magic; // Packet type - Little Endian
} NV_INPUT_HEADER, *PNV_INPUT_HEADER;

#define ENABLE_HAPTICS_MAGIC 0x0000000D
typedef struct _NV_HAPTICS_PACKET {
    NV_INPUT_HEADER header;
    uint16_t enable;
} NV_HAPTICS_PACKET, *PNV_HAPTICS_PACKET;

#define KEY_DOWN_EVENT_MAGIC 0x00000003
#define KEY_UP_EVENT_MAGIC 0x00000004
typedef struct _NV_KEYBOARD_PACKET {
    NV_INPUT_HEADER header;
    char flags; // Sunshine extension (always 0 for GFE)
    short keyCode;
    char modifiers;
    short zero2;
} NV_KEYBOARD_PACKET, *PNV_KEYBOARD_PACKET;

#define UTF8_TEXT_EVENT_MAGIC 0x00000017
#define UTF8_TEXT_EVENT_MAX_COUNT 32
typedef struct _NV_UNICODE_PACKET {
    NV_INPUT_HEADER header;
    char text[UTF8_TEXT_EVENT_MAX_COUNT];
} NV_UNICODE_PACKET, *PNV_UNICODE_PACKET;

#define MOUSE_MOVE_REL_MAGIC 0x00000006
#define MOUSE_MOVE_REL_MAGIC_GEN5 0x00000007
typedef struct _NV_REL_MOUSE_MOVE_PACKET {
    NV_INPUT_HEADER header;
    short deltaX;
    short deltaY;
} NV_REL_MOUSE_MOVE_PACKET, *PNV_REL_MOUSE_MOVE_PACKET;

#define MOUSE_MOVE_ABS_MAGIC 0x00000005
typedef struct _NV_ABS_MOUSE_MOVE_PACKET {
    NV_INPUT_HEADER header;

    short x;
    short y;

    short unused;

    // Used on the server-side as a reference to scale x and y
    // to screen coordinates.
    short width;
    short height;
} NV_ABS_MOUSE_MOVE_PACKET, *PNV_ABS_MOUSE_MOVE_PACKET;

#define MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5 0x00000008
#define MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5 0x00000009
typedef struct _NV_MOUSE_BUTTON_PACKET {
    NV_INPUT_HEADER header;
    uint8_t button;
} NV_MOUSE_BUTTON_PACKET, *PNV_MOUSE_BUTTON_PACKET;

#define CONTROLLER_MAGIC 0x0000000A
#define C_HEADER_B 0x1400
#define C_TAIL_A 0x0000009C
#define C_TAIL_B 0x0055
typedef struct _NV_CONTROLLER_PACKET {
    NV_INPUT_HEADER header;
    short headerB;
    short buttonFlags;
    unsigned char leftTrigger;
    unsigned char rightTrigger;
    short leftStickX;
    short leftStickY;
    short rightStickX;
    short rightStickY;
    int tailA;
    short tailB;
} NV_CONTROLLER_PACKET, *PNV_CONTROLLER_PACKET;

#define MULTI_CONTROLLER_MAGIC 0x0000000D
#define MULTI_CONTROLLER_MAGIC_GEN5 0x0000000C
#define MC_HEADER_B 0x001A
#define MC_MID_B 0x0014
#define MC_TAIL_A 0x009C
#define MC_TAIL_B 0x0055
typedef struct _NV_MULTI_CONTROLLER_PACKET {
    NV_INPUT_HEADER header;
    short headerB;
    short controllerNumber;
    short activeGamepadMask;
    short midB;
    short buttonFlags;
    unsigned char leftTrigger;
    unsigned char rightTrigger;
    short leftStickX;
    short leftStickY;
    short rightStickX;
    short rightStickY;
    short tailA;
    short buttonFlags2; // Sunshine protocol extension (always 0 for GFE)
    short tailB;
} NV_MULTI_CONTROLLER_PACKET, *PNV_MULTI_CONTROLLER_PACKET;

#define SCROLL_MAGIC 0x00000009
#define SCROLL_MAGIC_GEN5 0x0000000A
typedef struct _NV_SCROLL_PACKET {
    NV_INPUT_HEADER header;
    short scrollAmt1;
    short scrollAmt2;
    short zero3;
} NV_SCROLL_PACKET, *PNV_SCROLL_PACKET;

#define SS_HSCROLL_MAGIC 0x55000001
typedef struct _SS_HSCROLL_PACKET {
    NV_INPUT_HEADER header;
    short scrollAmount;
} SS_HSCROLL_PACKET, *PSS_HSCROLL_PACKET;

#define SS_TOUCH_MAGIC 0x55000002
typedef struct _SS_TOUCH_PACKET {
    NV_INPUT_HEADER header;
    uint8_t eventType;
    uint8_t zero[1]; // Alignment/reserved
    uint16_t rotation;
    uint32_t pointerId;
    netfloat x;
    netfloat y;
    netfloat pressureOrDistance;
    netfloat contactAreaMajor;
    netfloat contactAreaMinor;
} SS_TOUCH_PACKET, *PSS_TOUCH_PACKET;

#define SS_PEN_MAGIC 0x55000003
typedef struct _SS_PEN_PACKET {
    NV_INPUT_HEADER header;
    uint8_t eventType;
    uint8_t toolType;
    uint8_t penButtons;
    uint8_t zero[1]; // Alignment/reserved
    netfloat x;
    netfloat y;
    netfloat pressureOrDistance;
    uint16_t rotation;
    uint8_t tilt;
    uint8_t zero2[1];
    netfloat contactAreaMajor;
    netfloat contactAreaMinor;
} SS_PEN_PACKET, *PSS_PEN_PACKET;

#define SS_CONTROLLER_ARRIVAL_MAGIC 0x55000004
typedef struct _SS_CONTROLLER_ARRIVAL_PACKET {
    NV_INPUT_HEADER header;
    uint8_t controllerNumber;
    uint8_t type;
    uint16_t capabilities;
    uint32_t supportedButtonFlags;
} SS_CONTROLLER_ARRIVAL_PACKET, *PSS_CONTROLLER_ARRIVAL_PACKET;

#define SS_CONTROLLER_TOUCH_MAGIC 0x55000005
typedef struct _SS_CONTROLLER_TOUCH_PACKET {
    NV_INPUT_HEADER header;
    uint8_t controllerNumber;
    uint8_t eventType;
    uint8_t zero[2]; // Alignment/reserved
    uint32_t pointerId;
    netfloat x;
    netfloat y;
    netfloat pressure;
} SS_CONTROLLER_TOUCH_PACKET, *PSS_CONTROLLER_TOUCH_PACKET;

#define SS_CONTROLLER_MOTION_MAGIC 0x55000006
typedef struct _SS_CONTROLLER_MOTION_PACKET {
    NV_INPUT_HEADER header;
    uint8_t controllerNumber;
    uint8_t motionType;
    uint8_t zero[2]; // Alignment/reserved
    netfloat x;
    netfloat y;
    netfloat z;
} SS_CONTROLLER_MOTION_PACKET, *PSS_CONTROLLER_MOTION_PACKET;

#define SS_CONTROLLER_BATTERY_MAGIC 0x55000007
typedef struct _SS_CONTROLLER_BATTERY_PACKET {
    NV_INPUT_HEADER header;
    uint8_t controllerNumber;
    uint8_t batteryState;
    uint8_t batteryPercentage;
    uint8_t zero[1]; // Alignment/reserved
} SS_CONTROLLER_BATTERY_PACKET, *PSS_CONTROLLER_BATTERY_PACKET;

#define SS_TRACKPAD_MAGIC 0x55000008
typedef struct _SS_TRACKPAD_PACKET {
    NV_INPUT_HEADER header;
    uint8_t eventType;
    uint8_t zero[1]; // Alignment/reserved
    uint16_t rotation;
    uint32_t pointerId;
    netfloat x;
    netfloat y;
    netfloat pressureOrDistance;
    netfloat contactAreaMajor;
    netfloat contactAreaMinor;
} SS_TRACKPAD_PACKET, *PSS_TRACKPAD_PACKET;


#pragma pack(pop)
