/**
 * @file spi.h
 * @brief The external SPI flash: image size, the write contract, the whole-image
 *        backup and the region-by-region restore.
 *
 * Reading is whole-image. A read addresses flash by 1 KiB block number, 0 to
 * 4095, and the radio answers with the whole block, so a backup is 4096 reads
 * from block 0 upward.
 *
 * Writing is per region. A write frame names a region by its command byte and a
 * 1 KiB chunk by its index inside that region:
 *
 *     [command][index hi][index lo][1024 data bytes][checksum]
 *
 * The index is the chunk number, `(byte offset - region start) / 1024` — the
 * payload, never the frame length. Writing index 0 also erases the region's
 * whole span, and the radio answers only once the erase has finished, so a
 * region is written from its start and its first chunk can take tens of seconds
 * to be acknowledged.
 *
 * The radio knows 19 region commands. Together they address 2,236,416 bytes,
 * a little over half the image; the rest is the radio's own backup banks, its
 * live message records and unused flash, which no command writes. A command
 * outside those 19 is not a region at all: the radio programs it at address 0
 * without erasing, clearing bits wherever the payload has a zero, which is how
 * a restore destroys the calibration sector. The table itself, with the span
 * each command erases and what the flash holds there, is REGION_TABLE in spi.c
 * and comes back through spi_regions().
 *
 * No write reaches 0x400000 or above either: the runtime Query-ID database
 * starts there, and an image padded out to the chip's full size destroys it.
 */

#ifndef SPI_H
#define SPI_H

#include <stddef.h>
#include <stdio.h>

#include "sha256.h"
#include "uart.h"

#define SPI_BLOCK_BYTES 1024u
#define SPI_IMAGE_BYTES (4ul * 1024ul * 1024ul)
#define SPI_IMAGE_BLOCKS (SPI_IMAGE_BYTES / SPI_BLOCK_BYTES)

/** First address a region write must leave alone: the Query-ID database. */
#define SPI_WRITABLE_LIMIT 0x400000ul

/**
 * One region command: the radio's own unit of erase and write.
 *
 * @c bytes is the erase span, so writing index 0 erases @c bytes starting at
 * @c address and every later chunk of the region lands in erased flash.
 */
struct spi_region
{
    unsigned char command; /**< command byte, the region id on the wire */
    unsigned long address; /**< first byte, and where index 0 writes */
    unsigned long bytes;   /**< erase span; a multiple of the block size */
    const char *content;   /**< what the flash holds there, one word */
};

/** The region table, ascending by address; @p count receives its length. */
const struct spi_region *spi_regions(size_t *count);

/**
 * Read the whole image into @p out, block by block, checking every reply.
 *
 * A block is asked for three times before the backup gives up; the bytes read
 * so far stay in @p out. @p out_path only names the file in messages.
 *
 * @param digest receives the SHA-256 of the image, on success only
 * @return 0, or -1 after logging the first fault that survived the retries
 */
int spi_backup(
    struct uart *u, FILE *out, const char *out_path, unsigned char digest[SHA256_BLOCK_SIZE]);

/**
 * Write @p image back to the radio, region by region, then read it back and
 * compare.
 *
 * Every byte of @p image that a region command addresses is written, in
 * ascending order; nothing else is sent, so the bytes no command addresses are
 * left as the radio holds them and reported at the end. The first exchange is a
 * read, never a write, so a wrong port or a radio in its flashing mode fails
 * before anything changes.
 *
 * @param image a whole image, SPI_IMAGE_BYTES long
 * @param include_calibration also write region 0x40, which differs between units
 * @return 0 when every written byte was read back identical, else -1
 */
int spi_restore(struct uart *u, const unsigned char *image, size_t len, int include_calibration);

#endif
