#pragma once

#pragma pack(push, 1)

typedef struct _NV_INPUT_HEADER {
	int packetType;
} NV_INPUT_HEADER, PNV_INPUT_HEADER;

#define PACKET_TYPE_KEYBOARD 0x0A
typedef struct _NV_KEYBOARD_PACKET {
	NV_INPUT_HEADER header;
	char keyAction;
	int zero1;
	short keyCode;
	char modifiers;
	short zero2;
} NV_KEYBOARD_PACKET, *PNV_KEYBOARD_PACKET;

#define PACKET_TYPE_MOUSE_MOVE 0x08
#define MOUSE_MOVE_MAGIC 0x06000000
typedef struct _NV_MOUSE_MOVE_PACKET {
	NV_INPUT_HEADER header;
	int magic;
	short deltaX;
	short deltaY;
} NV_MOUSE_MOVE_PACKET, *PNV_MOUSE_MOVE_PACKET;

#define PACKET_TYPE_MOUSE_BUTTON 0x05
typedef struct _NV_MOUSE_BUTTON_PACKET {
	NV_INPUT_HEADER header;
	char action;
	int button;
} NV_MOUSE_BUTTON_PACKET, *PNV_MOUSE_BUTTON_PACKET;

#pragma pack(pop)