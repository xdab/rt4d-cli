/** @file firmware.c
 * @brief The MCU firmware flash path; see firmware.h.
 */

#include "firmware.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "frame.h"
#include "logging.h"
#include "uart.h"

#define ATTEMPTS 3
#define DRAIN_QUIET_MS 100
#define DRAIN_MAX_BYTES (2u * FRAME_REPLY_BYTES)
// ten blocks is about one second of traffic at 115200 8N1, so a progress line
// lands roughly once a second on every operation
#define PROGRESS_BLOCKS 10u

// the two halves of the erase: the same frame but for this byte
#define ERASE_PART_SETUP 0x10u
#define ERASE_PART_TRIGGER 0x55u

struct ack
{
    enum uart_status io;
    size_t moved;
    unsigned char value;
};

static struct ack send_frame(struct uart *u, const unsigned char *frame, size_t len)
{
    struct ack r = {.io = UART_OK, .moved = 0, .value = 0};

    r.io = uart_write(u, frame, len, &r.moved);
    if (r.io != UART_OK)
        return r;

    r.moved = 0;
    r.io = uart_read(u, &r.value, 1, &r.moved);
    return r;
}

static void report_ack(const char *what, const struct ack *r, unsigned attempt)
{
    if (r->io == UART_ERROR)
        log_warn(
            "%s: %s: %s (try %u of %d)\n",
            what,
            uart_status_name(r->io),
            strerror(errno),
            attempt,
            ATTEMPTS);
    else if (r->io != UART_OK)
        log_warn(
            "%s: %s after %zu of 1 byte (try %u of %d)\n",
            what,
            uart_status_name(r->io),
            r->moved,
            attempt,
            ATTEMPTS);
    else
        log_warn(
            "%s: the radio answered 0x%02X, not 0x%02X (try %u of %d)\n",
            what,
            r->value,
            FRAME_ACK,
            attempt,
            ATTEMPTS);
}

// send one frame and require the ACK: a line fault is retried, a wrong byte
// is the bootloader refusing it
static int expect_ack(struct uart *u, const unsigned char *frame, size_t len, const char *what)
{
    struct ack r = {.io = UART_TIMEOUT, .moved = 0, .value = 0};
    unsigned attempt = 0;

    while (attempt < ATTEMPTS)
    {
        attempt++;
        r = send_frame(u, frame, len);
        if (r.io == UART_OK && r.value == FRAME_ACK)
            return 0;
        report_ack(what, &r, attempt);
        if (r.io == UART_OK)
            return -1;
        uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);
    }

    return -1;
}

static int probe(struct uart *u)
{
    unsigned char frame[FRAME_REQUEST_BYTES];
    size_t moved = 0;
    unsigned char answer = 0;

    uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);

    enum uart_status io =
        uart_write(u, frame, frame_read_request(0, FRAME_SEED_FIRMWARE, frame), &moved);
    if (io != UART_OK)
    {
        log_error("cannot send the probe: %s\n", uart_status_name(io));
        return -1;
    }

    io = uart_read(u, &answer, 1, &moved);
    if (io != UART_OK)
    {
        log_error(
            "the radio did not answer: %s. Switch it off, then on with PTT held\n",
            uart_status_name(io));
        return -1;
    }
    if (answer != FW_MODE_BYTE)
    {
        log_error(
            "the radio answered 0x%02X, not 0x%02X: it is not in its firmware mode. "
            "Switch it off, then on with PTT held\n",
            answer,
            FW_MODE_BYTE);
        return -1;
    }

    // the probe is a read request: a bootloader may answer it with more than the
    // one byte this reads, and that must not be mistaken for the erase's ACK
    uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);

    log_info("the radio is in its firmware mode\n");
    return 0;
}

static int erase(struct uart *u)
{
    static const struct
    {
        unsigned char part;
        const char *what;
    } PARTS[] = {
        {ERASE_PART_SETUP, "erase setup"},
        {ERASE_PART_TRIGGER, "erase"},
    };
    unsigned char frame[FRAME_ERASE_BYTES];

    for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); i++)
    {
        frame_erase_request(PARTS[i].part, frame);
        if (expect_ack(u, frame, FRAME_ERASE_BYTES, PARTS[i].what) != 0)
        {
            log_error("the radio did not acknowledge the erase; nothing was written\n");
            return -1;
        }
    }

    log_info("MCU flash erased\n");
    return 0;
}

static double monotonic_s(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int firmware_flash(struct uart *u, const unsigned char *image, size_t len)
{
    unsigned char frame[FRAME_WRITE_BYTES];
    unsigned char padded[FW_BLOCK_BYTES];

    if (len == 0)
    {
        log_error("the image is empty\n");
        return -1;
    }
    if (len > FW_IMAGE_BYTES)
    {
        log_error("the image is %zu bytes; the MCU region holds %u\n", len, FW_IMAGE_BYTES);
        return -1;
    }

    if (probe(u) != 0 || erase(u) != 0)
        return -1;

    log_info(
        "writing %u blocks of %u bytes%s\n",
        FW_BLOCKS,
        FW_BLOCK_BYTES,
        len < FW_IMAGE_BYTES ? ", padded with zeros" : "");

    double started = monotonic_s();

    for (unsigned block = 0; block < FW_BLOCKS; block++)
    {
        size_t offset = (size_t)block * FW_BLOCK_BYTES;
        const unsigned char *data = image + offset;

        if (offset + FW_BLOCK_BYTES > len)
        {
            size_t have = len > offset ? len - offset : 0;
            (void)memset(padded, 0, sizeof padded);
            (void)memcpy(padded, image + offset, have);
            data = padded;
        }

        frame_write_request(
            FRAME_CMD_WRITE_FW, (unsigned)(offset & 0xFFFFu), FRAME_SEED_FIRMWARE, data, frame);
        if (expect_ack(u, frame, FRAME_WRITE_BYTES, "write") != 0)
        {
            log_error(
                "flashing stopped at block %u (0x%06zX); the MCU may hold a partial image\n",
                block,
                offset);
            return -1;
        }

        if ((block + 1) % PROGRESS_BLOCKS == 0)
        {
            double elapsed = monotonic_s() - started;
            // every block is 1 KiB, so blocks done is KiB done
            log_info(
                "%u%% (%u/%u blocks), %.0f KiB/s\n",
                (block + 1) * 100u / FW_BLOCKS,
                block + 1,
                FW_BLOCKS,
                elapsed > 0.0 ? (double)(block + 1) / elapsed : 0.0);
        }
    }

    double elapsed = monotonic_s() - started;
    log_info(
        "%u blocks acknowledged in %.1f s (%.0f KiB/s); "
        "MCU flash cannot be read back, so the count is the evidence\n",
        FW_BLOCKS,
        elapsed,
        elapsed > 0.0 ? (double)FW_IMAGE_BYTES / 1024.0 / elapsed : 0.0);
    return 0;
}
