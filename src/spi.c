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
// ten blocks is about one second of traffic at 115200 8N1, so a progress line
// lands roughly once a second on every operation
#define PROGRESS_BLOCKS 10u

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

    a.io = uart_write(u, request, frame_read_request(block, FRAME_SEED_SPI, request), &moved);
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
                "%lu%% (%lu/%lu blocks), %.0f KiB/s\n",
                ((unsigned long)block + 1ul) * 100ul / SPI_IMAGE_BLOCKS,
                (unsigned long)block + 1ul,
                SPI_IMAGE_BLOCKS,
                elapsed > 0.0 ? (double)(block + 1) * (double)SPI_BLOCK_BYTES / 1024.0 / elapsed
                              : 0.0);
        }
    }

    sha256_final(&hash, digest);

    double elapsed = monotonic_s() - started;
    log_info(
        "%lu bytes in %.1f s (%.0f KiB/s)\n",
        SPI_IMAGE_BYTES,
        elapsed,
        elapsed > 0.0 ? (double)SPI_IMAGE_BYTES / 1024.0 / elapsed : 0.0);
    return 0;
}

// Every region the radio can write, ascending by address: the command byte, the
// first byte it addresses, the span its erase covers, and what the flash holds
// there. Index 0 erases the span and later chunks program into it, so a region
// is written from the front and its span is what a restore rewrites.
static const struct spi_region REGION_TABLE[] = {
    {0x40, 0x000000ul, 0x001000ul, "calibration"},
    {0x90, 0x002000ul, 0x001000ul, "settings"},
    {0x91, 0x004000ul, 0x00c000ul, "channels"},
    {0x92, 0x01c000ul, 0x001000ul, "VFO head"},
    {0x93, 0x01e000ul, 0x020000ul, "zones"},
    {0x94, 0x05e000ul, 0x034000ul, "contacts"},
    {0x95, 0x0c6000ul, 0x005000ul, "schedules"},
    {0x96, 0x0d0000ul, 0x003000ul, "unmapped"},
    {0x97, 0x0d6000ul, 0x001000ul, "FM settings"},
    {0x98, 0x0f0000ul, 0x001000ul, "FM presets"},
    {0x9a, 0x100000ul, 0x001000ul, "unmapped block"},
    {0x9c, 0x14c000ul, 0x018000ul, "charset"},
    {0x9d, 0x164000ul, 0x024000ul, "charset"},
    {0x9e, 0x188000ul, 0x010000ul, "charset"},
    {0x9f, 0x198000ul, 0x004000ul, "unmapped block"},
    {0xa0, 0x19c000ul, 0x002000ul, "14-byte glyphs"},
    {0xa1, 0x19e000ul, 0x0b4000ul, "28-byte glyphs"},
    {0xa3, 0x352000ul, 0x09e000ul, "voice prompts"},
    {0xa5, 0x3f0000ul, 0x010000ul, "unmapped block"},
};

const struct spi_region *spi_regions(size_t *count)
{
    *count = sizeof(REGION_TABLE) / sizeof(REGION_TABLE[0]);
    return REGION_TABLE;
}

struct write_result
{
    enum uart_status io;
    size_t moved;
    unsigned char ack;
};

struct progress
{
    const char *phase;
    unsigned chunks_done;
    unsigned chunks_total;
    unsigned long bytes_done;
    double started;
};

static struct write_result
write_chunk(struct uart *u, unsigned char command, unsigned index, const unsigned char *data)
{
    struct write_result w = {.io = UART_OK, .moved = 0, .ack = 0};
    unsigned char frame[FRAME_WRITE_BYTES];

    w.io = uart_write(
        u, frame, frame_write_request(command, index, FRAME_SEED_SPI, data, frame), &w.moved);
    if (w.io != UART_OK)
        return w;

    w.moved = 0;
    w.io = uart_read(u, &w.ack, 1, &w.moved);
    return w;
}

// index 0 erases the region's whole span before it answers, so its deadline
// grows with the span instead of the configured per-call budget
static unsigned erase_timeout_ms(unsigned long bytes)
{
    unsigned long sectors = bytes / 4096ul;
    unsigned long ms = (sectors + 1ul) * 1000ul;

    if (ms > 60000ul)
        ms = 60000ul;
    return (unsigned)ms;
}

static void report_write_failure(
    const struct spi_region *r, unsigned index, const struct write_result *w, unsigned attempt)
{
    unsigned long address = r->address + (unsigned long)index * SPI_BLOCK_BYTES;

    if (w->io == UART_ERROR)
        log_warn(
            "region 0x%02X at 0x%06lX: %s: %s (try %u of %d)\n",
            r->command,
            address,
            uart_status_name(w->io),
            strerror(errno),
            attempt,
            ATTEMPTS);
    else if (w->io != UART_OK)
        log_warn(
            "region 0x%02X at 0x%06lX: %s after %zu of 1 byte (try %u of %d)\n",
            r->command,
            address,
            uart_status_name(w->io),
            w->moved,
            attempt,
            ATTEMPTS);
    else
        log_warn(
            "region 0x%02X at 0x%06lX: the radio answered 0x%02X, not 0x%02X (try %u of %d)\n",
            r->command,
            address,
            w->ack,
            FRAME_ACK,
            attempt,
            ATTEMPTS);
}

static void report_progress(struct progress *p)
{
    double elapsed = monotonic_s() - p->started;

    log_info(
        "%s: %u%% (%u/%u chunks), %.0f KiB/s\n",
        p->phase,
        p->chunks_done * 100u / p->chunks_total,
        p->chunks_done,
        p->chunks_total,
        elapsed > 0.0 ? (double)p->bytes_done / 1024.0 / elapsed : 0.0);
}

static int write_region(
    struct uart *u, const struct spi_region *r, const unsigned char *image, struct progress *p)
{
    unsigned budget = u->timeout_ms;

    for (unsigned long offset = 0; offset < r->bytes; offset += SPI_BLOCK_BYTES)
    {
        unsigned index = (unsigned)(offset / SPI_BLOCK_BYTES);
        unsigned erase_budget = erase_timeout_ms(r->bytes);
        struct write_result w = {.io = UART_TIMEOUT, .moved = 0, .ack = 0};
        unsigned attempt = 0;

        while (attempt < ATTEMPTS)
        {
            attempt++;
            uart_set_timeout(
                u, index == 0 ? (erase_budget > budget ? erase_budget : budget) : budget);
            w = write_chunk(u, r->command, index, image + r->address + offset);
            uart_set_timeout(u, budget);
            if (w.io == UART_OK && w.ack == FRAME_ACK)
                break;
            report_write_failure(r, index, &w, attempt);
            // an answer that is not the ACK is the radio refusing this chunk;
            // only a line fault is worth another try
            if (w.io == UART_OK)
                return -1;
            // an answer cut short must not be read as the next one
            uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);
        }
        if (w.io != UART_OK || w.ack != FRAME_ACK)
            return -1;

        p->chunks_done++;
        p->bytes_done += SPI_BLOCK_BYTES;
        if (p->chunks_done % PROGRESS_BLOCKS == 0)
            report_progress(p);
    }

    return 0;
}

static int verify_region(
    struct uart *u, const struct spi_region *r, const unsigned char *image, struct progress *p)
{
    unsigned char reply[FRAME_REPLY_BYTES];

    for (unsigned long offset = 0; offset < r->bytes; offset += SPI_BLOCK_BYTES)
    {
        unsigned long address = r->address + offset;
        unsigned block = (unsigned)(address / SPI_BLOCK_BYTES);
        struct attempt a = {.io = UART_TIMEOUT, .check = FRAME_REPLY_OK, .moved = 0};
        unsigned attempt = 0;

        while (attempt < ATTEMPTS)
        {
            attempt++;
            a = read_block(u, block, reply);
            if (a.io == UART_OK && a.check == FRAME_REPLY_OK)
                break;
            report_failure(block, &a, attempt);
            uart_drain(u, DRAIN_QUIET_MS, DRAIN_MAX_BYTES);
        }
        if (a.io != UART_OK || a.check != FRAME_REPLY_OK)
        {
            log_error("read-back stopped at 0x%06lX\n", address);
            return -1;
        }

        const unsigned char *data = reply + 3;
        if (memcmp(data, image + address, SPI_BLOCK_BYTES) != 0)
        {
            size_t i = 0;
            while (i < SPI_BLOCK_BYTES && data[i] == image[address + i])
                i++;
            log_error(
                "read-back differs at 0x%06lX: wrote 0x%02X, the radio answered 0x%02X\n",
                address + i,
                image[address + i],
                data[i]);
            return -1;
        }

        p->chunks_done++;
        p->bytes_done += SPI_BLOCK_BYTES;
        if (p->chunks_done % PROGRESS_BLOCKS == 0)
            report_progress(p);
    }

    return 0;
}

static void report_gaps(const struct spi_region *table, size_t count)
{
    unsigned long cursor = 0;
    unsigned long total = 0;
    unsigned ranges = 0;

    for (size_t i = 0; i <= count; i++)
    {
        unsigned long next = i < count ? table[i].address : SPI_IMAGE_BYTES;
        if (next > cursor)
        {
            total += next - cursor;
            ranges++;
        }
        if (i < count && table[i].address + table[i].bytes > cursor)
            cursor = table[i].address + table[i].bytes;
    }

    log_info("%lu bytes have no region command and were left alone (%u ranges)\n", total, ranges);

    cursor = 0;
    for (size_t i = 0; i <= count; i++)
    {
        unsigned long next = i < count ? table[i].address : SPI_IMAGE_BYTES;
        if (next > cursor)
            log_info("  0x%06lX-0x%06lX %5lu KiB\n", cursor, next, (next - cursor) / 1024ul);
        if (i < count && table[i].address + table[i].bytes > cursor)
            cursor = table[i].address + table[i].bytes;
    }

    log_info("those ranges hold the radio's own backup banks and live records\n");
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) // the pair is length and one flag
int spi_restore(struct uart *u, const unsigned char *image, size_t len, int include_calibration)
{
    if (len != SPI_IMAGE_BYTES)
    {
        log_error(
            "the image is %zu bytes, not %lu: a restore needs a whole dump\n",
            len,
            SPI_IMAGE_BYTES);
        return -1;
    }

    // a session starts with a read, so a wrong port or a radio in its flashing
    // mode is caught before anything is written
    unsigned char reply[FRAME_REPLY_BYTES];
    struct attempt first = read_block(u, 0, reply);
    if (first.io != UART_OK || first.check != FRAME_REPLY_OK)
    {
        report_failure(0, &first, 1);
        first_block_hint(&first);
        return -1;
    }
    if (memcmp(reply + 3, image, SPI_BLOCK_BYTES) == 0)
        log_info("the radio already holds this image at block 0\n");
    else
        log_info("the radio holds something else at block 0; writing the image over it\n");

    size_t count = 0;
    const struct spi_region *table = spi_regions(&count);
    unsigned chunks = 0;
    unsigned long planned = 0;

    for (size_t i = 0; i < count; i++)
    {
        if (table[i].address + table[i].bytes > SPI_WRITABLE_LIMIT)
        {
            log_error(
                "region 0x%02X ends at 0x%06lX, above the writable limit 0x%06lX\n",
                table[i].command,
                table[i].address + table[i].bytes,
                SPI_WRITABLE_LIMIT);
            return -1;
        }
        if (table[i].command == 0x40 && !include_calibration)
            continue;
        planned += table[i].bytes;
        chunks += (unsigned)(table[i].bytes / SPI_BLOCK_BYTES);
    }

    log_info(
        "writing %lu bytes in %u chunks; calibration %s\n",
        planned,
        chunks,
        include_calibration ? "included" : "skipped (region 0x40 differs between units)");

    struct progress p = {
        .phase = "write",
        .chunks_done = 0,
        .chunks_total = chunks,
        .bytes_done = 0,
        .started = monotonic_s(),
    };

    for (size_t i = 0; i < count; i++)
    {
        if (table[i].command == 0x40 && !include_calibration)
            continue;
        if (write_region(u, &table[i], image, &p) != 0)
        {
            log_error(
                "restore stopped in region 0x%02X (%s); the radio's flash is part-written\n",
                table[i].command,
                table[i].content);
            return -1;
        }
        log_info(
            "wrote 0x%02X %s: %lu bytes from 0x%06lX\n",
            table[i].command,
            table[i].content,
            table[i].bytes,
            table[i].address);
    }

    double wrote = monotonic_s() - p.started;
    log_info(
        "wrote %lu bytes in %.1f s (%.0f KiB/s)\n",
        p.bytes_done,
        wrote,
        wrote > 0.0 ? (double)p.bytes_done / 1024.0 / wrote : 0.0);

    p.phase = "read back";
    p.chunks_done = 0;
    p.bytes_done = 0;
    p.started = monotonic_s();

    for (size_t i = 0; i < count; i++)
    {
        if (table[i].command == 0x40 && !include_calibration)
            continue;
        if (verify_region(u, &table[i], image, &p) != 0)
        {
            log_error(
                "region 0x%02X (%s) did not read back identical\n",
                table[i].command,
                table[i].content);
            return -1;
        }
    }

    double read = monotonic_s() - p.started;
    log_info(
        "read back %lu bytes in %.1f s (%.0f KiB/s): identical\n",
        p.bytes_done,
        read,
        read > 0.0 ? (double)p.bytes_done / 1024.0 / read : 0.0);

    report_gaps(table, count);

    return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters)
