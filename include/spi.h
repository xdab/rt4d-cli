/**
 * @file spi.h
 * @brief The external SPI flash: image size, and the whole-image backup.
 *
 * A read addresses flash by 1 KiB block number, 0 to 4095, and the radio answers
 * with the whole block, so a backup is 4096 reads from block 0 upward. Writes
 * are region-relative and never reach 0x400000 and above: that area holds the
 * runtime Query-ID database, and padding an image out to the chip size destroys
 * it.
 */

#ifndef SPI_H
#define SPI_H

#include <stdio.h>

#include "sha256.h"
#include "uart.h"

#define SPI_BLOCK_BYTES 1024u
#define SPI_IMAGE_BYTES (4u * 1024u * 1024u)
#define SPI_IMAGE_BLOCKS (SPI_IMAGE_BYTES / SPI_BLOCK_BYTES)

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

#endif
