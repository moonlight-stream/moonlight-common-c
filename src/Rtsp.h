/**
 * @file src/Rtsp.h
 * @brief RTSP message parsing and creation functions.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * @def TYPE_REQUEST
 * @brief RTSP message type: request.
 */
#define TYPE_REQUEST 0

/**
 * @def TYPE_RESPONSE
 * @brief RTSP message type: response.
 */
#define TYPE_RESPONSE 1

/**
 * @def TOKEN_OPTION
 * @brief Token option identifier.
 */
#define TOKEN_OPTION 0

/**
 * @def RTSP_ERROR_SUCCESS
 * @brief RTSP error code: success.
 */
#define RTSP_ERROR_SUCCESS 0

/**
 * @def RTSP_ERROR_NO_MEMORY
 * @brief RTSP error code: no memory.
 */
#define RTSP_ERROR_NO_MEMORY -1

/**
 * @def RTSP_ERROR_MALFORMED
 * @brief RTSP error code: malformed message.
 */
#define RTSP_ERROR_MALFORMED -2

/**
 * @def SEQ_INVALID
 * @brief Invalid sequence number.
 */
#define SEQ_INVALID -1

/**
 * @def FLAG_ALLOCATED_OPTION_FIELDS
 * @brief Flag: option fields are allocated.
 */
#define FLAG_ALLOCATED_OPTION_FIELDS 0x1

/**
 * @def FLAG_ALLOCATED_MESSAGE_BUFFER
 * @brief Flag: message buffer is allocated.
 */
#define FLAG_ALLOCATED_MESSAGE_BUFFER 0x2

/**
 * @def FLAG_ALLOCATED_OPTION_ITEMS
 * @brief Flag: option items are allocated.
 */
#define FLAG_ALLOCATED_OPTION_ITEMS 0x4

/**
 * @def FLAG_ALLOCATED_PAYLOAD
 * @brief Flag: payload is allocated.
 */
#define FLAG_ALLOCATED_PAYLOAD 0x8

/**
 * @def CRLF_LENGTH
 * @brief Length of CRLF sequence in bytes.
 */
#define CRLF_LENGTH 2

/**
 * @def MESSAGE_END_LENGTH
 * @brief Length of message end marker in bytes.
 */
#define MESSAGE_END_LENGTH (2 + CRLF_LENGTH)

/**
 * @brief RTSP option item structure.
 */
typedef struct _OPTION_ITEM {
    char flags;  ///< Allocation flags
    char* option;  ///< Option name
    char* content;  ///< Option content
    struct _OPTION_ITEM* next;  ///< Next option item in list
} OPTION_ITEM, *POPTION_ITEM;

/**
 * @brief RTSP message structure.
 * @details In this implementation, a flag indicates the message type:
 *          TYPE_REQUEST = 0, TYPE_RESPONSE = 1
 */
typedef struct _RTSP_MESSAGE {
    char type;  ///< Message type (TYPE_REQUEST or TYPE_RESPONSE)
    char flags;  ///< Allocation flags
    int sequenceNumber;  ///< Sequence number
    char* protocol;  ///< Protocol string
    POPTION_ITEM options;  ///< Option items list
    char* payload;  ///< Message payload
    int payloadLength;  ///< Payload length
    char* messageBuffer;  ///< Message buffer
    union {
        struct {
            char* command;  ///< Request command
            char* target;  ///< Request target
        } request;  ///< Request fields
        struct {
            char* statusString;  ///< Response status string
            int statusCode;  ///< Response status code
        } response;  ///< Response fields
    } message;  ///< Message-specific fields
} RTSP_MESSAGE, *PRTSP_MESSAGE;

/**
 * @brief Parse an RTSP message from a buffer.
 * @param msg Pointer to RTSP message structure to populate.
 * @param rtspMessage Buffer containing RTSP message.
 * @param length Length of the message buffer.
 * @return RTSP_ERROR_SUCCESS on success, error code on failure.
 */
int parseRtspMessage(PRTSP_MESSAGE msg, char* rtspMessage, int length);

/**
 * @brief Free an RTSP message structure.
 * @param msg Pointer to RTSP message to free.
 */
void freeMessage(PRTSP_MESSAGE msg);

/**
 * @brief Create an RTSP response message.
 * @param msg Pointer to RTSP message structure.
 * @param messageBuffer Buffer for the message.
 * @param flags Allocation flags.
 * @param protocol Protocol string.
 * @param statusCode Response status code.
 * @param statusString Response status string.
 * @param sequenceNumber Sequence number.
 * @param optionsHead Head of option items list.
 * @param payload Message payload.
 * @param payloadLength Payload length.
 */
void createRtspResponse(PRTSP_MESSAGE msg, char* messageBuffer, int flags, char* protocol, int statusCode, char* statusString, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength);

/**
 * @brief Create an RTSP request message.
 * @param msg Pointer to RTSP message structure.
 * @param messageBuffer Buffer for the message.
 * @param flags Allocation flags.
 * @param command Request command.
 * @param target Request target.
 * @param protocol Protocol string.
 * @param sequenceNumber Sequence number.
 * @param optionsHead Head of option items list.
 * @param payload Message payload.
 * @param payloadLength Payload length.
 */
void createRtspRequest(PRTSP_MESSAGE msg, char* messageBuffer, int flags, char* command, char* target, char* protocol, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength);

/**
 * @brief Get option content from option list.
 * @param optionsHead Head of option items list.
 * @param option Option name to search for.
 * @return Pointer to option content, or NULL if not found.
 */
char* getOptionContent(POPTION_ITEM optionsHead, char* option);

/**
 * @brief Insert an option into the option list.
 * @param optionsHead Pointer to head of option items list.
 * @param opt Option item to insert.
 */
void insertOption(POPTION_ITEM* optionsHead, POPTION_ITEM opt);

/**
 * @brief Free an option list.
 * @param optionsHead Head of option items list to free.
 */
void freeOptionList(POPTION_ITEM optionsHead);

/**
 * @brief Serialize an RTSP message to a string.
 * @param msg Pointer to RTSP message structure.
 * @param serializedLength Output parameter for serialized length.
 * @return Serialized message string (caller must free), or NULL on error.
 */
char* serializeRtspMessage(PRTSP_MESSAGE msg, int* serializedLength);
