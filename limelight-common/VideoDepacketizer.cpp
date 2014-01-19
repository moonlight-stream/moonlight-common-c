#include "Platform.h"
#include "Limelight.h"
#include "LinkedBlockingQueue.h"

LENTRY *nalChainHead;
int nalChainDataLength;
int decodingAvc;

LINKED_BLOCKING_QUEUE decodeUnitQueue;