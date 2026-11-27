/**
 * @file uart.h
 * @brief The serial transport: open the port, move an exact byte count, and
 *        classify a silent peer.
 *
 * The radio speaks 115200 8N1 with no flow control, which is the default here.
 * The line is configured raw because the frames are binary and the kernel must
 * not rewrite a byte on the way. A read deadline covers the whole call rather
 * than each byte, and a timeout stays distinct from a short reply: on the wire
 * they are different faults and the tool reports them differently.
 */

#ifndef UART_H
#define UART_H

#include <stddef.h>

/** The line speed every reference implementation uses, and this tool's default. */
#define UART_RATE_DEFAULT 115200u

enum uart_status
{
    UART_OK = 0,  /**< the requested byte count moved */
    UART_TIMEOUT, /**< the deadline expired before the first byte */
    UART_SHORT,   /**< some bytes arrived, the deadline expired before the rest */
    UART_EOF,     /**< the peer hung up: device unplugged or port closed */
    UART_ERROR,   /**< an I/O failure; errno holds the reason */
};

struct uart
{
    int fd;              /**< -1 while closed, so a failed open is safe to close */
    unsigned timeout_ms; /**< per-call read budget */
};

/** The line speeds uart_open() accepts, comma separated, for a usage message. */
const char *uart_rate_names(void);

/** True when @p rate is a speed this build can configure. */
int uart_rate_supported(unsigned rate);

/**
 * Open @p path at @p rate, 8N1, no flow control, discarding bytes already
 * queued in either direction.
 *
 * @param u handle to fill in; left closed when this fails
 * @return 0, or -1 with errno set: ENOENT, EACCES, ENOTTY for a path that is not
 *         a serial port, EINVAL for an unknown rate
 */
int uart_open(struct uart *u, const char *path, unsigned rate, unsigned timeout_ms);

/**
 * Read exactly @p len bytes within the handle's timeout.
 *
 * @param moved always written: bytes actually read, so a failed read still says
 *              how much of the buffer is valid
 */
enum uart_status uart_read(struct uart *u, void *buf, size_t len, size_t *moved);

/**
 * Write every byte of @p buf. A blocking write of one frame is bounded by the
 * adapter's buffer, so this needs no deadline.
 *
 * @param moved always written: bytes the kernel accepted
 */
enum uart_status uart_write(struct uart *u, const void *buf, size_t len, size_t *moved);

/** A stable short name for a status, for log messages. */
const char *uart_status_name(enum uart_status status);

/**
 * Change the read deadline for later calls.
 *
 * A region write answers only once the radio has erased the whole span, which
 * for a large region takes seconds; the caller raises the deadline for that one
 * chunk and puts it back afterwards.
 */
void uart_set_timeout(struct uart *u, unsigned timeout_ms);

/**
 * Discard input until the line has been quiet for @p quiet_ms, or until
 * @p max_bytes have been dropped.
 */
void uart_drain(struct uart *u, unsigned quiet_ms, unsigned max_bytes);

/** Close the port if it is open; safe twice, and after a failed open. */
void uart_close(struct uart *u);

#endif
