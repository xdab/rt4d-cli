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
/** Command byte of an MCU firmware write, and of the two erase frames. */
#define FRAME_CMD_WRITE_FW 0x57
#define FRAME_CMD_ERASE_FW 0x39

/** Checksum seed of every SPI frame. */
#define FRAME_SEED_SPI 0x00u
/** Checksum seed of the bootloader's frames. */
#define FRAME_SEED_FIRMWARE 0x48u

/** Bytes of an erase frame: command, two fixed bytes, the part, the checksum. */
#define FRAME_ERASE_BYTES 5u

/** The single byte a successful write is answered with. */
#define FRAME_ACK 0x06

/** Bytes of a write frame: three header bytes, one block, one checksum. */
#define FRAME_WRITE_BYTES (3u + FRAME_BLOCK_BYTES + 1u)

/** First reply byte of a radio in its flashing mode: it cannot read flash. */
#define FRAME_REPLY_BOOTLOADER 0xFF

/** Low 8 bits of the sum of @p len bytes. */
unsigned char frame_checksum(const unsigned char *bytes, size_t len);

/** Build `52 blkH blkL checksum` for @p block; returns the frame length. */
size_t
frame_read_request(unsigned block, unsigned char seed, unsigned char out[FRAME_REQUEST_BYTES]);

/** Build `39 33 05 part checksum` for one half of the MCU erase. */
size_t frame_erase_request(unsigned char part, unsigned char out[FRAME_ERASE_BYTES]);

/**
 * Build `command idxH idxL data checksum` for one block of @p data.
 *
 * @p index counts payload-length chunks from the start of the region the
 * command addresses, and @p data is exactly FRAME_BLOCK_BYTES long.
 */
size_t frame_write_request(
    unsigned char command,
    unsigned index,
    unsigned char seed,
    const unsigned char *data,
    unsigned char out[FRAME_WRITE_BYTES]);

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
