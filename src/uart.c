/** @file uart.c
 * @brief The serial transport; see uart.h for the contract.
 */

#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// glibc defines the errno names and poll's types below bits/, which
// include-cleaner cannot see through; <errno.h> and <poll.h> are the POSIX
// headers that provide them
// NOLINTBEGIN(misc-include-cleaner)

static const struct
{
    unsigned rate;
    speed_t speed;
} rates[] = {
    {9600, B9600},
    {19200, B19200},
    {38400, B38400},
    {57600, B57600},
    {115200, B115200},
    {230400, B230400},
    {460800, B460800},
    {921600, B921600},
};

#define RATE_COUNT (sizeof(rates) / sizeof(rates[0]))

const char *uart_rate_names(void)
{
    static char names[128];
    if (names[0] == '\0')
    {
        size_t used = 0;
        for (size_t i = 0; i < RATE_COUNT && used < sizeof(names); i++)
        {
            int n = snprintf(
                names + used, sizeof(names) - used, "%s%u", i == 0 ? "" : ", ", rates[i].rate);
            if (n < 0)
                break;
            used += (size_t)n;
        }
    }
    return names;
}

int uart_rate_supported(unsigned rate)
{
    for (size_t i = 0; i < RATE_COUNT; i++)
    {
        if (rates[i].rate == rate)
            return 1;
    }
    return 0;
}

static int rate_lookup(unsigned rate, speed_t *speed)
{
    for (size_t i = 0; i < RATE_COUNT; i++)
    {
        if (rates[i].rate == rate)
        {
            *speed = rates[i].speed;
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) // (fd, speed) is termios' pair
static int configure(int fd, speed_t speed)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0)
        return -1;

    tio.c_iflag &=
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    tio.c_cflag |= CS8 | CREAD | CLOCAL;
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    // read() returns what has arrived and never waits: poll() owns the waiting,
    // so one deadline covers the whole transfer
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0)
        return -1;
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
        return -1;

    // a previous session may have left a partial frame in either queue
    return tcflush(fd, TCIOFLUSH);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) // (rate, timeout) read as one config
int uart_open(struct uart *u, const char *path, unsigned rate, unsigned timeout_ms)
{
    u->fd = -1;
    u->timeout_ms = timeout_ms;

    speed_t speed;
    if (rate_lookup(rate, &speed) != 0)
        return -1;

    // O_NONBLOCK for the open only: some adapters block here until a carrier
    // appears, and this one never waits for it
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;

    if (configure(fd, speed) != 0)
        goto fail;

    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
        goto fail;

    u->fd = fd;
    return 0;

fail:
    {
        int saved = errno;
        (void)close(fd);
        errno = saved;
    }
    return -1;
}

enum uart_status uart_read(struct uart *u, void *buf, size_t len, size_t *moved)
{
    unsigned char *p = buf;
    size_t done = 0;
    long long deadline = now_ms() + (long long)u->timeout_ms;

    *moved = 0;
    while (done < len)
    {
        long long left = deadline - now_ms();
        if (left < 0)
            left = 0; // poll(0) still reports bytes already buffered
        if (left > INT_MAX)
            left = INT_MAX;

        struct pollfd pfd = {.fd = u->fd, .events = POLLIN, .revents = 0};
        int ready = poll(&pfd, 1, (int)left);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            *moved = done;
            return UART_ERROR;
        }
        if (ready == 0)
            break;

        ssize_t n = read(u->fd, p + done, len - done);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            *moved = done;
            return UART_ERROR;
        }
        if (n == 0)
        {
            *moved = done;
            return UART_EOF;
        }
        done += (size_t)n;
    }

    *moved = done;
    if (done == len)
        return UART_OK;
    return done == 0 ? UART_TIMEOUT : UART_SHORT;
}

enum uart_status uart_write(struct uart *u, const void *buf, size_t len, size_t *moved)
{
    const unsigned char *p = buf;
    size_t done = 0;

    *moved = 0;
    while (done < len)
    {
        ssize_t n = write(u->fd, p + done, len - done);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            *moved = done;
            return UART_ERROR;
        }
        if (n == 0)
        {
            errno = EIO; // a zero-byte write would spin forever
            *moved = done;
            return UART_ERROR;
        }
        done += (size_t)n;
    }

    *moved = done;
    return UART_OK;
}

const char *uart_status_name(enum uart_status status)
{
    switch (status)
    {
    case UART_OK:
        return "ok";
    case UART_TIMEOUT:
        return "timeout";
    case UART_SHORT:
        return "short read";
    case UART_EOF:
        return "end of stream";
    case UART_ERROR:
        return "I/O error";
    default:
        break;
    }
    return "unknown";
}

void uart_set_timeout(struct uart *u, unsigned timeout_ms) { u->timeout_ms = timeout_ms; }

void uart_close(struct uart *u)
{
    if (u->fd >= 0)
    {
        (void)close(u->fd);
        u->fd = -1;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) // (quiet_ms, max_bytes) is the pair
void uart_drain(struct uart *u, unsigned quiet_ms, unsigned max_bytes)
{
    unsigned char scratch[256];
    unsigned dropped = 0;

    while (dropped < max_bytes)
    {
        struct pollfd pfd = {.fd = u->fd, .events = POLLIN, .revents = 0};
        int ready = poll(&pfd, 1, (int)(quiet_ms > INT_MAX ? INT_MAX : quiet_ms));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            break;

        ssize_t n = read(u->fd, scratch, sizeof scratch);
        if (n <= 0)
            break;
        dropped += (unsigned)n;
    }
}

// NOLINTEND(misc-include-cleaner)
