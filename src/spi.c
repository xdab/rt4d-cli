/** @file spi.c
 * @brief Whole-image SPI backup; see spi.h.
 */

#include "spi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "frame.h"
#include "logging.h"
#include "sha256.h"
#include "uart.h"

#define ATTEMPTS 3
#define DRAIN_QUIET_MS 100
#define DRAIN_MAX_BYTES (2u * FRAME_REPLY_BYTES)
#define PROGRESS_BLOCKS 256u

struct attempt
{
    enum uart_status io;
    enum frame_reply_status check;
    size_t moved;
};

static struct attempt read_block(struct uart *u, unsigned block, unsigned char *reply)
{
    struct attempt a = {.io = UART_OK, .check = FRAME_REPLY_OK, .moved = 0};
    unsigned char request[FRAME_REQUEST_BYTES];
    size_t moved = 0;

    a.io = uart_write(u, request, frame_read_request(block, request), &moved);
    if (a.io != UART_OK)
    {
        a.moved = moved;
        return a;
    }

    // the first byte alone can refuse the read: a bootloader answers FF and
    // goes quiet, and waiting for the other 1027 bytes would call that a timeout
    a.io = uart_read(u, reply, 1, &a.moved);
    if (a.io == UART_OK && reply[0] == FRAME_REPLY_BOOTLOADER)
    {
        a.check = FRAME_REPLY_REFUSED;
        return a;
    }
    if (a.io != UART_OK)
        return a;

    a.io = uart_read(u, reply + 1, FRAME_REPLY_BYTES - 1, &moved);
    a.moved = 1 + moved;
    if (a.io != UART_OK)
        return a;

    a.check = frame_check_read_reply(reply, FRAME_REPLY_BYTES, request);
    return a;
}

static unsigned long block_address(unsigned block)
{
    return (unsigned long)block * SPI_BLOCK_BYTES;
}

static double monotonic_s(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void report_failure(unsigned block, const struct attempt *a, unsigned try)
{
    if (a->io == UART_ERROR)
        log_warn(
            "block %u (0x%06lX): %s: %s (try %u of %d)\n",
            block,
            block_address(block),
            uart_status_name(a->io),
            strerror(errno),
            try,
            ATTEMPTS);
    else
        log_warn(
            "block %u (0x%06lX): %s after %zu of %u bytes (try %u of %d)\n",
            block,
            block_address(block),
            a->io == UART_OK ? frame_reply_status_name(a->check) : uart_status_name(a->io),
            a->moved,
            FRAME_REPLY_BYTES,
            try,
            ATTEMPTS);
}

static void first_block_hint(const struct attempt *a)
{
    if (a->io == UART_TIMEOUT)
        log_error(
            "the radio did not answer: check the cable, that it is switched on, "
            "and that -p names its port\n");
    else if (a->check == FRAME_REPLY_REFUSED)
        log_error(
            "the radio is in its flashing mode, where flash cannot be read: switch it "
            "off, then on again normally\n");
    else
        log_error("the radio answered, but not with a valid block: check the cable and the port\n");
}

int spi_backup(
    struct uart *u, FILE *out, const char *out_path, unsigned char digest[SHA256_BLOCK_SIZE])
{
    unsigned char reply[FRAME_REPLY_BYTES];
    SHA256_CTX hash;
    double started = monotonic_s();
    sha256_init(&hash);

    for (unsigned block = 0; block < SPI_IMAGE_BLOCKS; block++)
    {
        struct attempt a = {.io = UART_TIMEOUT, .check = FRAME_REPLY_OK, .moved = 0};
        unsigned try = 0;

        while (try < ATTEMPTS)
        {
            try++;
            a = read_block(u, block, reply);
            if (a.io == UART_OK && a.check == FRAME_REPLY_OK)
                break;
            report_failure(block, &a, try);
            // whatever is left of a broken answer must not be read as the next one
            uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);
        }
        if (a.io != UART_OK || a.check != FRAME_REPLY_OK)
        {
            log_error("backup stopped at block %u (0x%06lX)\n", block, block_address(block));
            if (block == 0)
                first_block_hint(&a);
            return -1;
        }

        const unsigned char *data = reply + 3;
        if (fwrite(data, 1, FRAME_BLOCK_BYTES, out) != FRAME_BLOCK_BYTES)
        {
            log_error("writing %s: %s\n", out_path, strerror(errno));
            return -1;
        }
        sha256_update(&hash, data, FRAME_BLOCK_BYTES);

        if ((block + 1) % PROGRESS_BLOCKS == 0)
        {
            double elapsed = monotonic_s() - started;
            log_info(
                "%u%% (%u/%u blocks), %.0f KiB/s\n",
                (block + 1) * 100u / SPI_IMAGE_BLOCKS,
                block + 1,
                SPI_IMAGE_BLOCKS,
                elapsed > 0.0 ? (double)(block + 1) * (double)SPI_BLOCK_BYTES / 1024.0 / elapsed
                              : 0.0);
        }
    }

    sha256_final(&hash, digest);

    double elapsed = monotonic_s() - started;
    log_info(
        "%u bytes in %.1f s (%.0f KiB/s)\n",
        SPI_IMAGE_BYTES,
        elapsed,
        elapsed > 0.0 ? (double)SPI_IMAGE_BYTES / 1024.0 / elapsed : 0.0);
    return 0;
}
