#include "Limelight-internal.h"

#define DR_CLEANUP -1000

#define CONSECUTIVE_DROP_LIMIT 120

//解包器 对象化处理
typedef struct _VIDEO_DEPACKETIZER {
    PLENTRY nalChainHead;
    PLENTRY nalChainTail;
    int nalChainDataLength;
    unsigned int nextFrameNumber;
    unsigned int startFrameNumber;
    bool waitingForNextSuccessfulFrame;
    bool waitingForIdrFrame;
    bool waitingForRefInvalFrame;
    unsigned int lastPacketInStream;
    bool decodingFrame;
    int frameType;
    uint16_t lastPacketPayloadLength;
    bool strictIdrFrameWait;
    uint64_t syntheticPtsBase;
    uint16_t frameHostProcessingLatency;
    uint64_t firstPacketReceiveTime;
    unsigned int firstPacketPresentationTime;
    bool dropStatePending;
    bool idrFrameProcessed;

    unsigned int consecutiveFrameDrops;
    LINKED_BLOCKING_QUEUE decodeUnitQueue;

    int trackIndex;
} VIDEO_DEPACKETIZER, *PVIDEO_DEPACKETIZER;

typedef struct _VIDEO_DEPACKETIZERS {
    PVIDEO_DEPACKETIZER data;
    int trackCount;
} VIDEO_DEPACKETIZERS, *PVIDEO_DEPACKETIZERS;


typedef struct _BUFFER_DESC {
    char* data;
    unsigned int offset;
    unsigned int length;
} BUFFER_DESC, *PBUFFER_DESC;

typedef struct _LENTRY_INTERNAL {
    LENTRY entry;
    void* allocPtr;
} LENTRY_INTERNAL, *PLENTRY_INTERNAL;

#define H264_NAL_TYPE(x) ((x) & 0x1F)
#define HEVC_NAL_TYPE(x) (((x) & 0x7E) >> 1)

#define H264_NAL_TYPE_SEI 6
#define H264_NAL_TYPE_SPS 7
#define H264_NAL_TYPE_PPS 8
#define H264_NAL_TYPE_AUD 9
#define H264_NAL_TYPE_FILLER 12
#define HEVC_NAL_TYPE_VPS 32
#define HEVC_NAL_TYPE_SPS 33
#define HEVC_NAL_TYPE_PPS 34
#define HEVC_NAL_TYPE_AUD 35
#define HEVC_NAL_TYPE_FILLER 38
#define HEVC_NAL_TYPE_SEI 39

static PVIDEO_DEPACKETIZERS depacketizers;
// Init
void initializeVideoDepacketizer(int pktSize,int trackCount) {
    if(depacketizers==NULL){
        depacketizers= malloc(sizeof(VIDEO_DEPACKETIZERS));
        depacketizers->data=NULL;
    }
    depacketizers->trackCount=0;
    if(depacketizers->data!=NULL){
        free(depacketizers->data);
    }
    depacketizers->data= malloc(sizeof(VIDEO_DEPACKETIZER)*trackCount);//多少个轨道，我们就需要多少个解包器

    for (int i = 0; i < trackCount; ++i) {
        VIDEO_DEPACKETIZER depacketizer;
        LbqInitializeLinkedBlockingQueue(&depacketizer.decodeUnitQueue, 15);
        depacketizer.trackIndex=i;
        depacketizer.startFrameNumber = 0;
        depacketizer.nextFrameNumber=1;//check every pkt
        depacketizer.waitingForNextSuccessfulFrame = false;
        depacketizer.waitingForIdrFrame = true;
        depacketizer.waitingForRefInvalFrame = false;
        depacketizer.lastPacketInStream = UINT32_MAX;
        depacketizer.decodingFrame = false;
        depacketizer.syntheticPtsBase = 0;
        depacketizer.frameHostProcessingLatency = 0;
        depacketizer.firstPacketReceiveTime = 0;
        depacketizer.firstPacketPresentationTime = 0;
        depacketizer.lastPacketPayloadLength = 0;
        depacketizer.dropStatePending = false;
        depacketizer.idrFrameProcessed = false;
        depacketizer.strictIdrFrameWait = !isReferenceFrameInvalidationEnabled();
        depacketizer.nalChainHead = NULL; //before first frame it must NULL
        depacketizer.nalChainTail = NULL; //before first frame it must NULL
        depacketizer.nalChainDataLength = 0;

        depacketizers->data[i]=depacketizer;
        depacketizers->trackCount++;
    }
}

// Free the NAL chain
static void cleanupFrameState(PVIDEO_DEPACKETIZER depacketizer) {
    PLENTRY_INTERNAL lastEntry;

    while (depacketizer->nalChainHead != NULL) {
        lastEntry = (PLENTRY_INTERNAL)depacketizer->nalChainHead;
        depacketizer->nalChainHead = lastEntry->entry.next;
        free(lastEntry->allocPtr);
    }
    depacketizer->nalChainTail = NULL;
    depacketizer->nalChainDataLength = 0;
}

// Cleanup frame state and set that we're waiting for an IDR Frame
static void dropFrameState(PVIDEO_DEPACKETIZER depacketizer) {
    // This may only be called at frame boundaries
    LC_ASSERT(!depacketizer->decodingFrame);
    // We're dropping frame state now
    depacketizer->dropStatePending = false;

    if (depacketizer->strictIdrFrameWait || !depacketizer->idrFrameProcessed || depacketizer->waitingForIdrFrame) {
        // We'll need an IDR frame now if we're in non-RFI mode, if we've never
        // received an IDR frame, or if we explicitly need an IDR frame.
        depacketizer->waitingForIdrFrame = true;
    }
    else {
        depacketizer->waitingForRefInvalFrame = true;
    }
    // Count the number of consecutive frames dropped
    depacketizer->consecutiveFrameDrops++;
    // If we reach our limit, immediately request an IDR frame and reset
    if (depacketizer->consecutiveFrameDrops == CONSECUTIVE_DROP_LIMIT) {
        Limelog("Reached consecutive drop limit\n");

        // Restart the count
        depacketizer->consecutiveFrameDrops = 0;

        // Request an IDR frame
        depacketizer->waitingForIdrFrame = true;
        LiRequestIdrFrame(depacketizer->trackIndex);
    }
    cleanupFrameState(depacketizer);
}

// Cleanup the list of decode units
static void freeDecodeUnitList(PLINKED_BLOCKING_QUEUE_ENTRY entry,int trackIndex) {
    PLINKED_BLOCKING_QUEUE_ENTRY nextEntry;

    while (entry != NULL) {
        nextEntry = entry->flink;

        // Complete this with a failure status
        LiCompleteVideoFrame(entry->data, DR_CLEANUP,trackIndex);

        entry = nextEntry;
    }
}

void stopVideoDepacketizer(void) {
    int size=depacketizers->trackCount;
    for (int i = 0; i < size; ++i) {
        LbqSignalQueueShutdown(&depacketizers->data[i].decodeUnitQueue);
    }
}

// Cleanup video depacketizer and free malloced memory
void destroyVideoDepacketizer(void) {
    int size=depacketizers->trackCount;
    for (int i = 0; i < size; ++i) {
        freeDecodeUnitList(LbqDestroyLinkedBlockingQueue(&depacketizers->data[i].decodeUnitQueue),i);
        cleanupFrameState(&depacketizers->data[i]);
    }
}

// NB: This function also ensures an additional byte for the NALU type exists after the start sequence
static bool getAnnexBStartSequence(PBUFFER_DESC current, PBUFFER_DESC startSeq) {
    // We must not get called for other codecs
    LC_ASSERT(NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265));

    if (current->length <= 3) {
        return false;
    }

    if (current->data[current->offset] == 0 &&
        current->data[current->offset + 1] == 0) {
        if (current->data[current->offset + 2] == 0) {
            if (current->length > 4 && current->data[current->offset + 3] == 1) {
                // Frame start
                if (startSeq != NULL) {
                    startSeq->data = current->data;
                    startSeq->offset = current->offset;
                    startSeq->length = 4;
                }
                return true;
            }
        }
        else if (current->data[current->offset + 2] == 1) {
            // NAL start
            if (startSeq != NULL) {
                startSeq->data = current->data;
                startSeq->offset = current->offset;
                startSeq->length = 3;
            }
            return true;
        }
    }

    return false;
}

void validateDecodeUnitForPlayback(VIDEO_DEPACKETIZER depacketizer,PDECODE_UNIT decodeUnit) {
    // Frames must always have at least one buffer
    LC_ASSERT(decodeUnit->bufferList != NULL);
    LC_ASSERT(decodeUnit->fullLength != 0);

    // Validate the buffers in the frame
    if (decodeUnit->frameType == FRAME_TYPE_IDR) {
        // IDR frames always start with codec configuration data
        if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
            // H.264 IDR frames should have an SPS, PPS, then picture data
            LC_ASSERT_VT(decodeUnit->bufferList->bufferType == BUFFER_TYPE_SPS);
            LC_ASSERT_VT(decodeUnit->bufferList->next != NULL);
            LC_ASSERT_VT(decodeUnit->bufferList->next->bufferType == BUFFER_TYPE_PPS);
            LC_ASSERT_VT(decodeUnit->bufferList->next->next != NULL);
        }
        else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
            // HEVC IDR frames should have an VPS, SPS, PPS, then picture data
            LC_ASSERT_VT(decodeUnit->bufferList->bufferType == BUFFER_TYPE_VPS);
            LC_ASSERT_VT(decodeUnit->bufferList->next != NULL);
            LC_ASSERT_VT(decodeUnit->bufferList->next->bufferType == BUFFER_TYPE_SPS);
            LC_ASSERT_VT(decodeUnit->bufferList->next->next != NULL);
            LC_ASSERT_VT(decodeUnit->bufferList->next->next->bufferType == BUFFER_TYPE_PPS);
            LC_ASSERT_VT(decodeUnit->bufferList->next->next->next != NULL);
        }
        else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_AV1) {
            // We don't parse the AV1 bitstream
            LC_ASSERT_VT(decodeUnit->bufferList->bufferType == BUFFER_TYPE_PICDATA);
        }
        else {
            LC_ASSERT(false);
        }
    }
    else {
        LC_ASSERT(decodeUnit->frameType == FRAME_TYPE_PFRAME);

        // P frames always start with picture data
        LC_ASSERT(decodeUnit->bufferList->bufferType == BUFFER_TYPE_PICDATA);

        // We must not dequeue a P frame before an IDR frame has been successfully processed
        LC_ASSERT(depacketizer.idrFrameProcessed);
    }
}

bool LiWaitForNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit,int trackIndex) {
    PQUEUED_DECODE_UNIT qdu;
    int err = LbqWaitForQueueElement(&depacketizers->data[trackIndex].decodeUnitQueue, (void**)&qdu);
    if (err != LBQ_SUCCESS) {
        return false;
    }
    validateDecodeUnitForPlayback(depacketizers->data[trackIndex],&qdu->decodeUnit);

    *frameHandle = qdu;
    *decodeUnit = &qdu->decodeUnit;
    return true;
}

bool LiPollNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit,int trackIndex) {
    PQUEUED_DECODE_UNIT qdu;

    int err = LbqPollQueueElement(&depacketizers->data[trackIndex].decodeUnitQueue, (void**)&qdu);
    if (err != LBQ_SUCCESS) {
        return false;
    }
    validateDecodeUnitForPlayback(depacketizers->data[trackIndex],&qdu->decodeUnit);
    *frameHandle = qdu;
    *decodeUnit = &qdu->decodeUnit;
    return true;
}

bool LiPeekNextVideoFrame(PDECODE_UNIT* decodeUnit,int trackIndex) {
    PQUEUED_DECODE_UNIT qdu;

    int err = LbqPeekQueueElement(&depacketizers->data[trackIndex].decodeUnitQueue, (void**)&qdu);
    if (err != LBQ_SUCCESS) {
        return false;
    }

    validateDecodeUnitForPlayback(depacketizers->data[trackIndex],&qdu->decodeUnit);

    *decodeUnit = &qdu->decodeUnit;
    return true;
}

void LiWakeWaitForVideoFrame(int trackIndex) {
    LbqSignalQueueUserWake(&depacketizers->data[trackIndex].decodeUnitQueue);
}

// Cleanup a decode unit by freeing the buffer chain and the holder
void LiCompleteVideoFrame(VIDEO_FRAME_HANDLE handle, int drStatus,int trackIndex) {
    PQUEUED_DECODE_UNIT qdu = handle;
    PLENTRY_INTERNAL lastEntry;

    if (drStatus == DR_NEED_IDR) {
        Limelog("Requesting IDR frame on behalf of DR\n");
        requestDecoderRefresh(trackIndex);
    }
    else if (drStatus == DR_OK && qdu->decodeUnit.frameType == FRAME_TYPE_IDR) {
        // Remember that the IDR frame was processed. We can now use
        // reference frame invalidation.
        depacketizers->data[trackIndex].idrFrameProcessed = true;
    }

    while (qdu->decodeUnit.bufferList != NULL) {
        lastEntry = (PLENTRY_INTERNAL)qdu->decodeUnit.bufferList;
        qdu->decodeUnit.bufferList = lastEntry->entry.next;
        free(lastEntry->allocPtr);
    }

    // We will have stack-allocated entries iff we have a direct-submit decoder
    if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        free(qdu);
    }
}

static bool isSeqReferenceFrameStart(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == 5;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        switch (HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length])) {
            case 16:
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
                return true;

            default:
                return false;
        }
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}

static bool isAccessUnitDelimiter(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == H264_NAL_TYPE_AUD;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        return HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == HEVC_NAL_TYPE_AUD;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}

static bool isSeiNal(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == H264_NAL_TYPE_SEI;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        return HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == HEVC_NAL_TYPE_SEI;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}

#ifdef LC_DEBUG
static bool isFillerDataNal(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == H264_NAL_TYPE_FILLER;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        return HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == HEVC_NAL_TYPE_FILLER;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}
#endif

static bool isPictureParameterSetNal(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == H264_NAL_TYPE_PPS;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        return HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == HEVC_NAL_TYPE_PPS;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}

// Advance the buffer descriptor to the start of the next NAL or end of buffer
static void skipToNextNalOrEnd(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    // If we're starting on a NAL boundary, skip to the next one
    if (getAnnexBStartSequence(buffer, &startSeq)) {
        buffer->offset += startSeq.length;
        buffer->length -= startSeq.length;
    }

    // Loop until we find an Annex B start sequence (3 or 4 byte)
    while (!getAnnexBStartSequence(buffer, NULL)) {
        if (buffer->length == 0) {
            // Reached the end of the buffer
            return;
        }

        buffer->offset++;
        buffer->length--;
    }
}

// Advance the buffer descriptor to the start of the next NAL
static void skipToNextNal(PBUFFER_DESC buffer) {
    skipToNextNalOrEnd(buffer);

    // If we skipped all the data, something has gone horribly wrong
    LC_ASSERT(buffer->length > 0);
}

static bool isIdrFrameStart(PBUFFER_DESC buffer) {
    BUFFER_DESC startSeq;

    if (!getAnnexBStartSequence(buffer, &startSeq)) {
        return false;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        return H264_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == H264_NAL_TYPE_SPS;
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        return HEVC_NAL_TYPE(startSeq.data[startSeq.offset + startSeq.length]) == HEVC_NAL_TYPE_VPS;
    }
    else {
        LC_ASSERT(false);
        return false;
    }
}

// Reassemble the frame with the given frame number
static void reassembleFrame(PVIDEO_DEPACKETIZER depacketizer,int frameNumber) {
    if (depacketizer->nalChainHead != NULL) {
        QUEUED_DECODE_UNIT qduDS;
        PQUEUED_DECODE_UNIT qdu;

        // Use a stack allocation if we won't be queuing this
        if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
            qdu = (PQUEUED_DECODE_UNIT)malloc(sizeof(*qdu));
        }
        else {
            qdu = &qduDS;
        }

        if (qdu != NULL) {
            qdu->decodeUnit.bufferList = depacketizer->nalChainHead;
            qdu->decodeUnit.fullLength = depacketizer->nalChainDataLength;
            qdu->decodeUnit.frameType = depacketizer->frameType;
            qdu->decodeUnit.frameNumber = frameNumber;
            qdu->decodeUnit.frameHostProcessingLatency = depacketizer->frameHostProcessingLatency;
            qdu->decodeUnit.receiveTimeMs = depacketizer->firstPacketReceiveTime;
            qdu->decodeUnit.presentationTimeMs = depacketizer->firstPacketPresentationTime;
            qdu->decodeUnit.enqueueTimeMs = LiGetMillis();

            // These might be wrong for a few frames during a transition between SDR and HDR,
            // but the effects shouldn't very noticable since that's an infrequent operation.
            //
            // If we start sending this state in the frame header, we can make it 100% accurate.
            qdu->decodeUnit.hdrActive = LiGetCurrentHostDisplayHdrMode();
            qdu->decodeUnit.colorspace = (uint8_t)(qdu->decodeUnit.hdrActive ? COLORSPACE_REC_2020 : StreamConfig.colorSpace);

            // Invoke the key frame callback if needed
            if (depacketizer->nalChainHead->bufferType != BUFFER_TYPE_PICDATA || qdu->decodeUnit.frameType == FRAME_TYPE_IDR) {
                qdu->decodeUnit.frameType = FRAME_TYPE_IDR;
                notifyKeyFrameReceived();
            }
            else {
                qdu->decodeUnit.frameType = FRAME_TYPE_PFRAME;
            }
            
            depacketizer->nalChainHead = depacketizer->nalChainTail = NULL;
            depacketizer->nalChainDataLength = 0;

            if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
                if (LbqOfferQueueItem(&depacketizer->decodeUnitQueue, qdu, &qdu->entry) == LBQ_BOUND_EXCEEDED) {
                    Limelog("Video decode unit queue overflow=====> %d\n",depacketizer->trackIndex);

                    // RFI recovery is not supported here
                    depacketizer->waitingForIdrFrame = true;

                    // Clear NAL state for the frame that we failed to enqueue
                    depacketizer->nalChainHead = qdu->decodeUnit.bufferList;
                    depacketizer->nalChainDataLength = qdu->decodeUnit.fullLength;
                    dropFrameState(depacketizer);

                    // Free the DU we were going to queue
                    free(qdu);

                    // Free all frames in the decode unit queue
                    freeDecodeUnitList(LbqFlushQueueItems(&depacketizer->decodeUnitQueue),depacketizer->trackIndex);

                    // Request an IDR frame to recover
                    LiRequestIdrFrame(depacketizer->trackIndex);
                    return;
                }
            }
            else {
                // Submit the frame to the decoder
                validateDecodeUnitForPlayback(*depacketizer,&qdu->decodeUnit);
                LiCompleteVideoFrame(qdu, VideoCallbacks.submitDecodeUnit(&qdu->decodeUnit),depacketizer->trackIndex);
            }

            // Notify the control connection
            connectionReceivedCompleteFrame(depacketizer->trackIndex, frameNumber);

            // Clear frame drops
            depacketizer->consecutiveFrameDrops = 0;

            // Move the start of our (potential) RFI window to the next frame
            depacketizer->startFrameNumber = depacketizer->nextFrameNumber;
        }
    }
}

static int getBufferFlags(char* data, int length) {
    BUFFER_DESC buffer;
    BUFFER_DESC candidate;

    // We only parse H.264 and HEVC bitstreams
    if (!(NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265))) {
        return BUFFER_TYPE_PICDATA;
    }

    buffer.data = data;
    buffer.length = (unsigned int)length;
    buffer.offset = 0;

    if (!getAnnexBStartSequence(&buffer, &candidate)) {
        return BUFFER_TYPE_PICDATA;
    }

    if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) {
        switch (H264_NAL_TYPE(candidate.data[candidate.offset + candidate.length])) {
        case H264_NAL_TYPE_SPS:
            return BUFFER_TYPE_SPS;

        case H264_NAL_TYPE_PPS:
            return BUFFER_TYPE_PPS;

        default:
            return BUFFER_TYPE_PICDATA;
        }
    }
    else if (NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) {
        switch (HEVC_NAL_TYPE(candidate.data[candidate.offset + candidate.length])) {
            case HEVC_NAL_TYPE_SPS:
                return BUFFER_TYPE_SPS;

            case HEVC_NAL_TYPE_PPS:
                return BUFFER_TYPE_PPS;

            case HEVC_NAL_TYPE_VPS:
                return BUFFER_TYPE_VPS;

            default:
                return BUFFER_TYPE_PICDATA;
        }
    }
    else {
        LC_ASSERT(false);
        return BUFFER_TYPE_PICDATA;
    }
}

// As an optimization, we can cast the existing packet buffer to a PLENTRY and avoid
// a malloc() and a memcpy() of the packet data.
static void queueFragment(PVIDEO_DEPACKETIZER depacketizer, PLENTRY_INTERNAL* existingEntry, char* data, int offset, int length) {
    PLENTRY_INTERNAL entry;

    if (existingEntry == NULL || *existingEntry == NULL) {
        entry = (PLENTRY_INTERNAL)malloc(sizeof(*entry) + length);
    }
    else {
        entry = *existingEntry;
    }

    if (entry != NULL) {
        entry->entry.next = NULL;
        entry->entry.length = length;

        // If we had to allocate a new entry, we must copy the data. If not,
        // the data already resides within the LENTRY allocation.
        if (existingEntry == NULL || *existingEntry == NULL) {
            entry->allocPtr = entry;

            entry->entry.data = (char*)(entry + 1);
            memcpy(entry->entry.data, &data[offset], entry->entry.length);
        }
        else {
            entry->entry.data = &data[offset];

            // The caller should have already set this up for us
            LC_ASSERT(entry->allocPtr != NULL);

            // We now own the packet buffer and will manage freeing it
            *existingEntry = NULL;
        }

        entry->entry.bufferType = getBufferFlags(entry->entry.data, entry->entry.length);

        depacketizer->nalChainDataLength += entry->entry.length;

        if (depacketizer->nalChainTail == NULL) {
            LC_ASSERT(depacketizer->nalChainHead == NULL);
            depacketizer->nalChainHead = depacketizer->nalChainTail = (PLENTRY)entry;
        }
        else {
            LC_ASSERT(depacketizer->nalChainHead != NULL);
            depacketizer->nalChainTail->next = (PLENTRY)entry;
            depacketizer->nalChainTail = depacketizer->nalChainTail->next;
        }
    }
}

// Process an RTP Payload using the slow path that handles multiple NALUs per packet
static void processAvcHevcRtpPayloadSlow(PVIDEO_DEPACKETIZER depacketizer,PBUFFER_DESC currentPos, PLENTRY_INTERNAL* existingEntry) {
    // We should not have any NALUs when processing the first packet in an IDR frame
    LC_ASSERT(depacketizer->nalChainHead == NULL);
    LC_ASSERT(depacketizer->nalChainTail == NULL);

    while (currentPos->length != 0) {
        // Skip through any padding bytes
        if (!getAnnexBStartSequence(currentPos, NULL)) {
            skipToNextNal(currentPos);
        }

        // Skip any prepended AUD or SEI NALUs. We may have padding between
        // these on IDR frames, so the check in processRtpPayload() is not
        // completely sufficient to handle that case.
        while (isAccessUnitDelimiter(currentPos) || isSeiNal(currentPos)) {
            skipToNextNal(currentPos);
        }

        int start = currentPos->offset;
        bool containsPicData = false;

#ifdef FORCE_3_BYTE_START_SEQUENCES
        start++;
#endif

        if (isSeqReferenceFrameStart(currentPos)) {
            // No longer waiting for an IDR frame
            depacketizer->waitingForIdrFrame = false;
            depacketizer->waitingForRefInvalFrame = false;

            // Cancel any pending IDR frame request
            depacketizer->waitingForNextSuccessfulFrame = false;

            // Use the cached LENTRY for this NALU since it will be
            // the bulk of the data in this packet.
            containsPicData = true;

            // This is an IDR frame
            depacketizer->frameType = FRAME_TYPE_IDR;
        }

        // Move to the next NALU
        skipToNextNalOrEnd(currentPos);

        // If this is the picture data, we expect it to extend to the end of the packet
        if (containsPicData) {
            while (currentPos->length != 0) {
                // Any NALUs we encounter on the way to the end of the packet must be
                // reference frame slices or filler data.
                LC_ASSERT_VT(isSeqReferenceFrameStart(currentPos) || isFillerDataNal(currentPos));
                skipToNextNalOrEnd(currentPos);
            }
        }

        // To minimize copies, we'll allocate for SPS, PPS, and VPS to allow
        // us to reuse the packet buffer for the picture data in the I-frame.
        queueFragment(depacketizer,containsPicData ? existingEntry : NULL,
                      currentPos->data, start, currentPos->offset - start);
    }
}

// Dumps the decode unit queue and ensures the next frame submitted to the decoder will be an IDR frame
void requestDecoderRefresh(int trackIndex) {
    // Wait for the next IDR frame
    depacketizers->data[trackIndex].waitingForIdrFrame = true;
    
    // Flush the decode unit queue
    freeDecodeUnitList(LbqFlushQueueItems(&depacketizers->data[trackIndex].decodeUnitQueue),trackIndex);
    
    // Request the receive thread drop its state
    // on the next call. We can't do it here because
    // it may be trying to queue DUs and we'll nuke
    // the state out from under it.
    depacketizers->data[trackIndex].dropStatePending = true;
    
    // Request the IDR frame
    LiRequestIdrFrame(trackIndex);//todo:如果需要的化，可以分屏幕送idr的请求
}

// Return 1 if packet is the first one in the frame
static bool isFirstPacket(uint8_t flags, uint8_t fecBlockNumber) {
    // Clear the picture data flag
    flags &= ~FLAG_CONTAINS_PIC_DATA;

    // Check if it's just the start or both start and end of a frame
    return (flags == (FLAG_SOF | FLAG_EOF) || flags == FLAG_SOF) && fecBlockNumber == 0;
}

// Process an RTP Payload
// The caller will free *existingEntry unless we NULL it
static void processRtpPayload(PVIDEO_DEPACKETIZER depacketizer,PNV_VIDEO_PACKET videoPacket, int length,
                       uint64_t receiveTimeMs, unsigned int presentationTimeMs,
                       PLENTRY_INTERNAL* existingEntry) {
    BUFFER_DESC currentPos;
    uint32_t frameIndex;
    uint8_t flags;
    bool firstPacket, lastPacket;
    uint32_t streamPacketIndex;
    uint8_t fecCurrentBlockNumber;
    uint8_t fecLastBlockNumber;

    // Mask the top 8 bits from the SPI
    videoPacket->streamPacketIndex >>= 8;
    videoPacket->streamPacketIndex &= 0xFFFFFF;

    currentPos.data = (char*)(videoPacket + 1);
    currentPos.offset = 0;
    currentPos.length = length - sizeof(*videoPacket);

    fecCurrentBlockNumber = (videoPacket->multiFecBlocks >> 4) & 0x3;
    fecLastBlockNumber = (videoPacket->multiFecBlocks >> 6) & 0x3;
    frameIndex = videoPacket->frameIndex;
    flags = videoPacket->flags;
    firstPacket = isFirstPacket(flags, fecCurrentBlockNumber);
    lastPacket = (flags & FLAG_EOF) && fecCurrentBlockNumber == fecLastBlockNumber;

    LC_ASSERT_VT((flags & ~(FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA)) == 0);

    streamPacketIndex = videoPacket->streamPacketIndex;

    // Drop packets from a previously corrupt frame 丢弃先前损坏帧中的数据包
    if (isBefore32(frameIndex, depacketizer->nextFrameNumber)) {
        return;
    }

    // The FEC queue can sometimes recover corrupt frames (see comments in RtpFecQueue).
    // It almost always detects them before they get to us, but in case it doesn't
    // the streamPacketIndex not matching correctly should find nearly all of the rest.
    if (isBefore24(streamPacketIndex, U24(depacketizer->lastPacketInStream + 1)) ||
            (!(flags & FLAG_SOF) && streamPacketIndex != U24(depacketizer->lastPacketInStream + 1))) {
        Limelog("Depacketizer detected corrupt frame: %d", frameIndex);
        depacketizer->decodingFrame = false;
        depacketizer->nextFrameNumber = frameIndex + 1;
        dropFrameState(depacketizer);
        if (depacketizer->waitingForIdrFrame) {
            LiRequestIdrFrame(depacketizer->trackIndex);
        }
        else {
            connectionDetectedFrameLoss(depacketizer->trackIndex, depacketizer->startFrameNumber, frameIndex);
        }
        return;
    }
    
    // Verify that we didn't receive an incomplete frame
    LC_ASSERT(firstPacket ^ depacketizer->decodingFrame);
    
    // Check sequencing of this frame to ensure we didn't
    // miss one in between
    if (firstPacket) {
        // Make sure this is the next consecutive frame
        if (isBefore32(depacketizer->nextFrameNumber, frameIndex)) {
            if (depacketizer->nextFrameNumber + 1 == frameIndex) {
                Limelog("Network dropped 1 frame (frame %d)\n", frameIndex - 1);
            }
            else {
                Limelog("Network dropped %d frames (frames %d to %d)\n",
                        frameIndex - depacketizer->nextFrameNumber,
                        depacketizer->nextFrameNumber,
                        frameIndex - 1);
            }

            depacketizer->nextFrameNumber = frameIndex;

            // Wait until next complete frame
            depacketizer->waitingForNextSuccessfulFrame = true;
            dropFrameState(depacketizer);
        }
        else {
            LC_ASSERT(depacketizer->nextFrameNumber == frameIndex);
        }

        // We're now decoding a frame
        depacketizer->decodingFrame = true;
        depacketizer->frameType = FRAME_TYPE_PFRAME;
        depacketizer->firstPacketReceiveTime = receiveTimeMs;
        
        // Some versions of Sunshine don't send a valid PTS, so we will
        // synthesize one using the receive time as the time base.
        if (!depacketizer->syntheticPtsBase) {
            depacketizer->syntheticPtsBase = receiveTimeMs;
        }
        
        if (!presentationTimeMs && frameIndex > 0) {
            depacketizer->firstPacketPresentationTime = (unsigned int)(receiveTimeMs - depacketizer->syntheticPtsBase);
        }
        else {
            depacketizer->firstPacketPresentationTime = presentationTimeMs;
        }
    }

    depacketizer->lastPacketInStream = streamPacketIndex;

    // If this is the first packet, skip the frame header (if one exists)
    uint32_t frameHeaderSize;
    LC_ASSERT_VT(currentPos.length > 0);
    if (firstPacket && currentPos.length > 0) {
        // Parse the frame type from the header
        LC_ASSERT_VT(currentPos.length >= 4);

        if (APP_VERSION_AT_LEAST(7, 1, 350) && currentPos.length >= 4) {
            switch (currentPos.data[currentPos.offset + 3]) {
            case 1: // Normal P-frame
                break;
            case 2: // IDR frame
                // For other codecs, we trust the frame header rather than parsing the bitstream
                // to determine if a given frame is an IDR frame.
                //对于其他编解码器，我们信任帧头而不是解析比特流来确定给定帧是否是IDR帧。
                if (!(NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265))) {
                    depacketizer->waitingForIdrFrame = false;
                    depacketizer->waitingForNextSuccessfulFrame = false;
                    depacketizer->frameType = FRAME_TYPE_IDR;
                }
                // Fall-through
            case 4: // Intra-refresh
            case 5: // P-frame with reference frames invalidated
                if (depacketizer->waitingForRefInvalFrame) {
                    Limelog("Next post-invalidation frame is: %d (%s-frame)\n",
                            frameIndex,
                            currentPos.data[currentPos.offset + 3] == 5 ? "P" : "I");
                    depacketizer->waitingForRefInvalFrame = false;
                    depacketizer->waitingForNextSuccessfulFrame = false;
                }
                break;
            case 104: // Sunshine hardcoded header
                break;
            default:
                Limelog("Unrecognized frame type: %d", currentPos.data[currentPos.offset + 3]);
                LC_ASSERT_VT(false);
                break;
            }
        }
        else {
            // Hope for the best with older servers
            if (depacketizer->waitingForRefInvalFrame) {
                connectionDetectedFrameLoss(depacketizer->trackIndex,depacketizer->startFrameNumber, frameIndex - 1);
                depacketizer->waitingForRefInvalFrame = false;
                depacketizer->waitingForNextSuccessfulFrame = false;
            }
        }

        // Sunshine can provide host processing latency of the frame
        LC_ASSERT_VT(currentPos.length >= 3);
        if (IS_SUNSHINE() && currentPos.length >= 3) {
            BYTE_BUFFER bb;
            BbInitializeWrappedBuffer(&bb, currentPos.data, currentPos.offset + 1, 2, BYTE_ORDER_LITTLE);
            BbGet16(&bb, &depacketizer->frameHostProcessingLatency);
        }

        // Codecs like H.264 and HEVC handle the FEC trailing zero padding just fine, but other
        // codecs need the exact length encoded separately.
        LC_ASSERT_VT(currentPos.length >= 6);
        if (!(NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265)) && currentPos.length >= 6) {
            BYTE_BUFFER bb;
            BbInitializeWrappedBuffer(&bb, currentPos.data, currentPos.offset + 4, 2, BYTE_ORDER_LITTLE);
            BbGet16(&bb, &depacketizer->lastPacketPayloadLength);
        }

        if (APP_VERSION_AT_LEAST(7, 1, 450)) {
            // >= 7.1.450 uses 2 different header lengths based on the first byte:
            // 0x01 indicates an 8 byte header
            // 0x81 indicates a 44 byte header
            if (currentPos.data[0] == 0x01) {
                frameHeaderSize = 8;
            }
            else {
                LC_ASSERT_VT(currentPos.data[0] == (char)0x81);
                frameHeaderSize = 44;
            }
        }
        else if (APP_VERSION_AT_LEAST(7, 1, 446)) {
            // [7.1.446, 7.1.450) uses 2 different header lengths based on the first byte:
            // 0x01 indicates an 8 byte header
            // 0x81 indicates a 41 byte header
            if (currentPos.data[0] == 0x01) {
                frameHeaderSize = 8;
            }
            else {
                LC_ASSERT_VT(currentPos.data[0] == (char)0x81);
                frameHeaderSize = 41;
            }
        }
        else if (APP_VERSION_AT_LEAST(7, 1, 415)) {
            // [7.1.415, 7.1.446) uses 2 different header lengths based on the first byte:
            // 0x01 indicates an 8 byte header
            // 0x81 indicates a 24 byte header
            if (currentPos.data[0] == 0x01) {
                frameHeaderSize = 8;
            }
            else {
                LC_ASSERT_VT(currentPos.data[0] == (char)0x81);
                frameHeaderSize = 24;
            }
        }
        else if (APP_VERSION_AT_LEAST(7, 1, 350)) {
            // [7.1.350, 7.1.415) should use the 8 byte header again
            frameHeaderSize = 8;
        }
        else if (APP_VERSION_AT_LEAST(7, 1, 320)) {
            // [7.1.320, 7.1.350) should use the 12 byte frame header
            frameHeaderSize = 12;
        }
        else if (APP_VERSION_AT_LEAST(5, 0, 0)) {
            // [5.x, 7.1.320) should use the 8 byte header
            frameHeaderSize = 8;
        }
        else {
            // Other versions don't have a frame header at all
            frameHeaderSize = 0;
        }

        LC_ASSERT_VT(currentPos.length >= frameHeaderSize);
        if (currentPos.length >= frameHeaderSize) {
            // Skip past the frame header
            currentPos.offset += frameHeaderSize;
            currentPos.length -= frameHeaderSize;
        }

        // We only parse H.264 and HEVC at the NALU level
        if (NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265)) {
            // The Annex B NALU start prefix must be next
            if (!getAnnexBStartSequence(&currentPos, NULL)) {
                // If we aren't starting on a start prefix, something went wrong.
                LC_ASSERT_VT(false);

                // For release builds, we will try to recover by searching for one.
                // This mimics the way most decoders handle this situation.
                skipToNextNal(&currentPos);
            }

            // If an AUD NAL is prepended to this frame data, remove it.
            // Other parts of this code are not prepared to deal with a
            // NAL of that type, so stripping it is the easiest option.
            if (isAccessUnitDelimiter(&currentPos)) {
                skipToNextNal(&currentPos);
            }

            // There may be one or more SEI NAL units prepended to the
            // frame data *after* the (optional) AUD.
            while (isSeiNal(&currentPos)) {
                skipToNextNal(&currentPos);
            }
        }
    }
    else {
        // There is no frame header on later packets
        frameHeaderSize = 0;
    }

    if (NegotiatedVideoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265)) {
        if (firstPacket && isIdrFrameStart(&currentPos)) {
            Limelog("video packet is idr===========================>%d\n",(*existingEntry)->entry.ssrc);

            // SPS and PPS prefix is padded between NALs, so we must decode it with the slow path
            processAvcHevcRtpPayloadSlow(depacketizer,&currentPos, existingEntry);
        }
        else {
            // Intel's H.264 Media Foundation encoder prepends a PPS to each P-frame.
            // Skip it to avoid confusing clients.
            if (firstPacket && isPictureParameterSetNal(&currentPos)) {
                skipToNextNal(&currentPos);
            }

#ifdef FORCE_3_BYTE_START_SEQUENCES
            if (firstPacket) {
                currentPos.offset++;
                currentPos.length--;
            }
#endif
            queueFragment(depacketizer,existingEntry, currentPos.data, currentPos.offset, currentPos.length);
        }
    }
    else {
        // We fixup the length of the last packet for other codecs since they may not be tolerant
        // of trailing zero padding like H.264/HEVC Annex B bitstream parsers are.
        if (lastPacket) {
            // The payload length includes the frame header, so it cannot be smaller than that
            LC_ASSERT_VT(depacketizer->lastPacketPayloadLength > frameHeaderSize);

            // The payload length cannot be smaller than the actual received payload
            // NB: currentPos.length is already adjusted to exclude the frameHeaderSize from above
            LC_ASSERT_VT(depacketizer->lastPacketPayloadLength - frameHeaderSize <= currentPos.length);

            // If the payload length is valid, truncate the packet. If not, discard this frame.
            if (depacketizer->lastPacketPayloadLength > frameHeaderSize && depacketizer->lastPacketPayloadLength - frameHeaderSize <= currentPos.length) {
                currentPos.length = depacketizer->lastPacketPayloadLength - frameHeaderSize;
            }
            else {
                if (depacketizer->lastPacketPayloadLength <= frameHeaderSize) {
                    Limelog("Invalid last payload length for header on frame %u: %u <= %u",
                            frameIndex, depacketizer->lastPacketPayloadLength, frameHeaderSize);
                }
                else {
                    Limelog("Invalid last payload length for packet size on frame %u: %u > %u",
                            frameIndex, depacketizer->lastPacketPayloadLength - frameHeaderSize, currentPos.length);
                }

                // Skip to the next frame and tell the host we lost this one
               depacketizer-> decodingFrame = false;
               depacketizer-> nextFrameNumber = frameIndex + 1;
                dropFrameState(depacketizer);
                if (depacketizer->waitingForIdrFrame) {
                    LiRequestIdrFrame(depacketizer->trackIndex);
                }
                else {
                    connectionDetectedFrameLoss(depacketizer->trackIndex,depacketizer->startFrameNumber, frameIndex);
                }

                return;
            }
        }

        // Other codecs are just passed through as is.
        queueFragment(depacketizer,existingEntry, currentPos.data, currentPos.offset, currentPos.length);
    }

    if (lastPacket) {
        // Move on to the next frame
        depacketizer->decodingFrame = false;
        depacketizer->nextFrameNumber = frameIndex + 1;

        // If we can't submit this frame due to a discontinuity in the bitstream,
        // inform the host (if needed) and drop the data.
        if (depacketizer->waitingForIdrFrame || depacketizer->waitingForRefInvalFrame) {
            // IDR wait takes priority over RFI wait (and an IDR frame will satisfy both)
            if (depacketizer->waitingForIdrFrame) {
                Limelog("Waiting for IDR frame=================>track index: %d\n",depacketizer->trackIndex);

                // We wait for the first fully received frame after a loss to approximate
                // detection of the recovery of the network. Requesting an IDR frame while
                // the network is unstable will just contribute to congestion collapse.
                if (depacketizer->waitingForNextSuccessfulFrame) {
                    LiRequestIdrFrame(depacketizer->trackIndex);
                }
            }
            else {
                // If we need an RFI frame first, then drop this frame
                // and update the reference frame invalidation window.
                Limelog("Waiting for RFI frame\n");
                connectionDetectedFrameLoss(depacketizer->trackIndex,depacketizer->startFrameNumber, frameIndex);
            }

            depacketizer->waitingForNextSuccessfulFrame = false;
            dropFrameState(depacketizer);
            return;
        }

        LC_ASSERT(!depacketizer->waitingForNextSuccessfulFrame);

        // Carry out any pending state drops. We can't just do this
        // arbitrarily in the middle of processing a frame because
        // may cause the depacketizer state to become corrupted. For
        // example, if we drop state after the first packet, the
        // depacketizer will next try to process a non-SOF packet,
        // and cause it to assert.
        if (depacketizer->dropStatePending) {
            if (depacketizer->nalChainHead && depacketizer->frameType == FRAME_TYPE_IDR) {
                // Don't drop the frame state if this frame is an IDR frame itself,
                // otherwise we'll lose this IDR frame without another in flight
                // and have to wait until we hit our consecutive drop limit to
                // request a new one (potentially several seconds).
                depacketizer->dropStatePending = false;
            }
            else {
                dropFrameState(depacketizer);
                return;
            }
        }

        reassembleFrame(depacketizer,frameIndex);
    }
}

// Called by the video RTP FEC queue to notify us of a lost frame
// if it determines the frame to be unrecoverable. This lets us
// avoid having to wait until the next received frame to determine
// that we lost a frame and submit an RFI request.
void notifyFrameLost(int trackIndex,unsigned int frameNumber, bool speculative) {
    // We may not invalidate frames that we've already received
    PVIDEO_DEPACKETIZER depacketizer=&depacketizers[trackIndex];
    LC_ASSERT(frameNumber >= depacketizer->startFrameNumber);

    // Drop state and determine if we need an IDR frame or if RFI is okay
    dropFrameState(depacketizer);

    // If dropFrameState() determined that RFI was usable, issue it now
    if (!depacketizer->waitingForIdrFrame) {
        LC_ASSERT(depacketizer->waitingForRefInvalFrame);

        if (speculative) {
            Limelog("Sending speculative RFI request for predicted loss of frame %d\n", frameNumber);
        }
        else {
            Limelog("Sending RFI request for unrecoverable frame %d\n", frameNumber);
        }

        // Advance the frame number since we won't be expecting this one anymore
        depacketizer->nextFrameNumber = frameNumber + 1;

        // Notify the host that we lost this one
        connectionDetectedFrameLoss(depacketizer->trackIndex,depacketizer->startFrameNumber, frameNumber);
    }
}

// Add an RTP Packet to the queue
// 从视频流缓存中接受完整的rpt包到待解码的队列中
void queueRtpPacket(int trackIndex,PRTPV_QUEUE_ENTRY queueEntryPtr) {
    int dataOffset;
    RTPV_QUEUE_ENTRY queueEntry = *queueEntryPtr;

    LC_ASSERT(!queueEntry.isParity);
    LC_ASSERT(queueEntry.receiveTimeMs != 0);

    dataOffset = sizeof(*queueEntry.packet);
    if (queueEntry.packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    // The packet length was validated by the RtpVideoQueue
    LC_ASSERT(queueEntry.length >= dataOffset + (int)sizeof(NV_VIDEO_PACKET));

    // Reuse the memory reserved for the RTPFEC_QUEUE_ENTRY to store the LENTRY_INTERNAL
    // now that we're in the depacketizer. We saved a copy of the real FEC queue entry
    // on the stack here so we can safely modify this memory in place.
    LC_ASSERT(sizeof(LENTRY_INTERNAL) <= sizeof(RTPV_QUEUE_ENTRY));
    PLENTRY_INTERNAL existingEntry = (PLENTRY_INTERNAL)queueEntryPtr;
    existingEntry->allocPtr = queueEntry.packet;
    existingEntry->entry.ssrc=queueEntry.packet->ssrc;//传递ssrc
    processRtpPayload(&depacketizers->data[trackIndex],(PNV_VIDEO_PACKET)(((char*)queueEntry.packet) + dataOffset),
                      queueEntry.length - dataOffset,
                      queueEntry.receiveTimeMs,
                      queueEntry.presentationTimeMs,
                      &existingEntry);

    if (existingEntry != NULL) {
        // processRtpPayload didn't want this packet, so just free it
        free(existingEntry->allocPtr);
    }
}

int LiGetPendingVideoFrames(int trackIndex) {
    return LbqGetItemCount(&depacketizers->data[trackIndex].decodeUnitQueue);
}
