/** @file main.c
 * @brief One parsed command line in, one verb out.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "options.h"
#include "sha256.h"
#include "spi.h"
#include "uart.h"

static int run_check(const struct options *o)
{
    struct uart u;
    if (uart_open(&u, o->port, o->rate, o->timeout_ms) != 0)
    {
        log_error("cannot open %s: %s\n", o->port, strerror(errno));
        return 1;
    }

    log_info(
        "opened %s: %u baud, 8N1, no flow control, %u ms reply deadline\n",
        o->port,
        o->rate,
        o->timeout_ms);

    uart_close(&u);
    log_info("closed %s\n", o->port);

    return 0;
}

static int run_backup(const struct options *o)
{
    struct uart u;
    if (uart_open(&u, o->port, o->rate, o->timeout_ms) != 0)
    {
        log_error("cannot open %s: %s\n", o->port, strerror(errno));
        return 1;
    }

    FILE *out = fopen(o->output, "wb");
    if (!out)
    {
        log_error("cannot create %s: %s\n", o->output, strerror(errno));
        uart_close(&u);
        return 1;
    }

    log_info(
        "backing up %lu MiB from %s to %s\n",
        SPI_IMAGE_BYTES / (1024ul * 1024ul),
        o->port,
        o->output);

    unsigned char digest[SHA256_BLOCK_SIZE];
    int failed = spi_backup(&u, out, o->output, digest) != 0;
    int close_failed = fclose(out) != 0;

    uart_close(&u);

    if (failed)
    {
        log_error("backup incomplete; %s holds a partial image\n", o->output);
        return 1;
    }
    if (close_failed)
    {
        log_error("cannot finish %s: %s\n", o->output, strerror(errno));
        return 1;
    }

    char hex[SHA256_HEX_SIZE];
    sha256_hex(digest, hex);
    log_info("sha256 %s\n", hex);

    return 0;
}

// the whole image, or NULL after saying why
static unsigned char *read_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        log_error("cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    unsigned char *image = malloc(SPI_IMAGE_BYTES);
    if (!image)
    {
        log_error("cannot hold %lu bytes: %s\n", SPI_IMAGE_BYTES, strerror(errno));
        (void)fclose(f);
        return NULL;
    }

    size_t got = fread(image, 1, SPI_IMAGE_BYTES, f);
    int extra = fgetc(f) != EOF;
    int failed = ferror(f) != 0;
    (void)fclose(f);

    if (failed || extra || got != SPI_IMAGE_BYTES)
    {
        log_error(
            "%s is not a %lu MiB image: %zu bytes%s\n",
            path,
            SPI_IMAGE_BYTES / (1024ul * 1024ul),
            got,
            extra ? " and more" : "");
        free(image);
        return NULL;
    }

    return image;
}

static int run_restore(const struct options *o)
{
    unsigned char *image = read_image(o->input);
    if (!image)
        return 1;

    char hex[SHA256_HEX_SIZE];
    unsigned char digest[SHA256_BLOCK_SIZE];
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, image, SPI_IMAGE_BYTES);
    sha256_final(&hash, digest);
    sha256_hex(digest, hex);
    log_info("restoring %s (sha256 %s) to %s\n", o->input, hex, o->port);

    struct uart u;
    if (uart_open(&u, o->port, o->rate, o->timeout_ms) != 0)
    {
        log_error("cannot open %s: %s\n", o->port, strerror(errno));
        free(image);
        return 1;
    }

    int failed = spi_restore(&u, image, SPI_IMAGE_BYTES, o->calibration) != 0;

    uart_close(&u);
    free(image);

    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    struct options o;
    enum options_status status = options_parse(argc, argv, &o);
    if (status != OPTIONS_OK)
        return status == OPTIONS_HELP ? 0 : 1;

    switch (o.action)
    {
    case ACTION_CHECK:
        return run_check(&o);
    case ACTION_BACKUP:
        return run_backup(&o);
    case ACTION_RESTORE:
        return run_restore(&o);
    case ACTION_NONE:
        break; // rejected by options_parse()
    }

    return 1;
}
