/** @file frame.c
 * @brief SPI frame building and reply checks; see frame.h for the contract.
 */

#include "frame.h"

#include <stdio.h>

unsigned char frame_checksum(const unsigned char *bytes, size_t len)
{
    unsigned sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += bytes[i];
    return (unsigned char)(sum & 0xFFu);
}

size_t frame_read_request(unsigned block, unsigned char out[FRAME_REQUEST_BYTES])
{
    out[0] = FRAME_CMD_READ_SPI;
    out[1] = (unsigned char)((block >> 8) & 0xFFu);
    out[2] = (unsigned char)(block & 0xFFu);
    out[3] = frame_checksum(out, 3);
    return FRAME_REQUEST_BYTES;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) // (len, request) are the check's input
enum frame_reply_status
frame_check_read_reply(const unsigned char *reply, size_t len, const unsigned char *request)
{
    if (len < FRAME_REPLY_BYTES)
        return FRAME_REPLY_SHORT;
    if (reply[0] == FRAME_REPLY_BOOTLOADER)
        return FRAME_REPLY_REFUSED;
    if (frame_checksum(reply, FRAME_REPLY_BYTES - 1) != reply[FRAME_REPLY_BYTES - 1])
        return FRAME_REPLY_CHECKSUM;
    if (reply[0] != request[0] || reply[1] != request[1] || reply[2] != request[2])
        return FRAME_REPLY_HEADER;
    return FRAME_REPLY_OK;
}

const char *frame_reply_status_name(enum frame_reply_status status)
{
    switch (status)
    {
    case FRAME_REPLY_OK:
        return "ok";
    case FRAME_REPLY_REFUSED:
        return "the radio is in its flashing mode";
    case FRAME_REPLY_SHORT:
        return "short reply";
    case FRAME_REPLY_CHECKSUM:
        return "bad checksum";
    case FRAME_REPLY_HEADER:
        return "the header does not echo the request";
    default:
        break;
    }
    return "unknown";
}
