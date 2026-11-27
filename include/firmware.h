/**
 * @file firmware.h
 * @brief The MCU firmware flash path: the bootloader's side of the protocol.
 *
 * This is a different conversation from the SPI one, on a different port state:
 * the radio is switched off, then on with PTT held, and its bootloader answers.
 * Every frame is checksummed with the same low-8-bit sum but seeded with 0x48
 * instead of 0, the probe included:
 *
 *     probe         52 00 00 9A              first answer byte 0xFF
 *     erase, part 0 39 33 05 10 C9           06
 *     erase, part 1 39 33 05 55 0E           06
 *     write         57 <off hi> <off lo> <1024 data> <sum>   06, per block
 *
 * The probe is a block-0 read and its answer is one byte; whatever the bootloader
 * sends after it is drained before the erase, so a longer answer cannot be
 * mistaken for an acknowledgement.
 *
 * The write offset is the low 16 bits of the byte offset, so it wraps every
 * 64 KiB and the bootloader counts blocks itself. The image is written whole:
 * 0x3D800 bytes in blocks of 0x400, a short image padded with zeros, because
 * there is no partial write and no short final block.
 *
 * MCU flash cannot be read back, so a flash is checked by what it is: one
 * acknowledgement per block, and a count. Nothing here compares bytes, and the
 * log says so rather than implying otherwise.
 */

#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stddef.h>

#include "uart.h"

#define FW_BLOCK_BYTES 1024u
#define FW_IMAGE_BYTES 0x3d800u
#define FW_BLOCKS (FW_IMAGE_BYTES / FW_BLOCK_BYTES)

/** The byte a radio in its firmware mode answers the probe with. */
#define FW_MODE_BYTE 0xFFu

/**
 * Write @p image into the MCU's flash: probe, erase both halves, then all
 * FW_BLOCKS blocks in order, each acknowledged.
 *
 * A timeout or a short answer is retried three times; a byte that is not the
 * ACK is the bootloader refusing the frame, and stops the run. With nothing
 * written back to compare, the block count is the whole of the evidence.
 *
 * Every frame gets the handle's deadline, the erase acknowledgements included:
 * they arrive once the flash has finished, so a slow unit is handled by raising
 * `-t` rather than by a second hidden budget inside this module.
 *
 * @param image at most FW_IMAGE_BYTES; a shorter one is padded with zeros
 * @return 0 when every block was acknowledged, else -1
 */
int firmware_flash(struct uart *u, const unsigned char *image, size_t len);

#endif
