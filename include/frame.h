/**
 * @file frame.h
 * @brief Frame bytes for the radio's SPI protocol: the read request,
 *        the checksum, and the checks a read reply must pass.
 *
 * A frame is [command][index hi][index lo][payload...][checksum], and the
 * checksum is the low 8 bits of the sum of every preceding byte. SPI frames are
 * seeded with 0; the bootloader's firmware frames seed the same sum with 0x48.
 *
 * A read addresses one 1 KiB block by number, and the radio answers 1028 bytes:
 * three header bytes that echo the request, the 1024 data bytes, then a
 * checksum over all preceding bytes.
 */

#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>

#define FRAME_BLOCK_BYTES 1024u
#define FRAME_REPLY_BYTES (3u + FRAME_BLOCK_BYTES + 1u)
#define FRAME_REQUEST_BYTES 4u

/** Command byte of a block read. */
#define FRAME_CMD_READ_SPI 0x52

/** First reply byte of a radio in its flashing mode: it cannot read flash. */
#define FRAME_REPLY_BOOTLOADER 0xFF

/** Low 8 bits of the sum of @p len bytes. */
unsigned char frame_checksum(const unsigned char *bytes, size_t len);

/** Build `52 blkH blkL checksum` for @p block; returns the frame length. */
size_t frame_read_request(unsigned block, unsigned char out[FRAME_REQUEST_BYTES]);

/** Why a read reply was rejected. */
enum frame_reply_status
{
    FRAME_REPLY_OK = 0,
    FRAME_REPLY_REFUSED,  /**< the first byte is 0xFF: the radio is in flashing mode */
    FRAME_REPLY_SHORT,    /**< fewer than FRAME_REPLY_BYTES arrived */
    FRAME_REPLY_CHECKSUM, /**< the trailing byte is not the sum of the rest */
    FRAME_REPLY_HEADER,   /**< the header does not echo the request */
};

/**
 * Check a reply of @p len bytes against the @p request that produced it: the
 * reply's first three bytes must repeat the request's. The caller has already
 * established that @p len bytes arrived; anything shorter is FRAME_REPLY_SHORT.
 */
enum frame_reply_status
frame_check_read_reply(const unsigned char *reply, size_t len, const unsigned char *request);

const char *frame_reply_status_name(enum frame_reply_status status);

#endif
