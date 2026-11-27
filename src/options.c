/** @file options.c
 * @brief Command line parsing; see options.h for the contract.
 */

#include "options.h"

#include <errno.h>
#include <getopt.h> // NOLINT(misc-include-cleaner): POSIX option parsing
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "logging.h"
#include "uart.h"

#define APP_NAME "rt4d-cli"

#define REPLY_TIMEOUT_DEFAULT 5.0
#define REPLY_TIMEOUT_MAX 3600.0

enum
{
    OPT_BAUD = 256,
    OPT_CHECK,
    OPT_BACKUP,
};

static void usage(void)
{
    (void)fprintf(
        stderr,
        APP_NAME ", SPI flash and MCU firmware tool for the Radtel RT-4D\n"
                 "\n"
                 "Use:\t" APP_NAME " -p DEVICE --backup -o FILE\n"
                 "\t" APP_NAME " -p DEVICE --check\n"
                 "\t--backup           read the whole 4 MiB SPI flash into -o FILE\n"
                 "\t--check            open the port, report its settings, close it;\n"
                 "\t                   sends nothing to the radio\n"
                 "\t-p, --port DEVICE  serial port the radio is on (/dev/ttyUSB0)\n"
                 "\t-o, --output FILE  --backup destination, overwritten\n"
                 "\t-t, --timeout SEC  reply deadline in seconds (default: 5; a bare\n"
                 "\t                   number is already seconds, s/m/h suffixes\n"
                 "\t                   are accepted)\n"
                 "\t    --baud RATE    line speed (default: 115200)\n"
                 "\t-v [N]             verbosity: 1 (or bare -v) adds DEBUG, 2 adds TRACE\n"
                 "\t-q [N]             quietness: 1 drops INFO, 2 drops WARN, 3 silences stderr\n"
                 "\t-h, --help         this text\n"
                 "\n");
    (void)fprintf(stderr, "\t    --baud accepts: %s\n\n", uart_rate_names());
}

// true if s is a nonempty run of decimal digits
static int digits_only(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

// -v/-v1/-v 1, and the same for -q: a level in argv counts only as digits
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) // (opt, argc, argv) is getopt's shape
static void log_verbosity_opt(int opt, int argc, char **argv)
{
    int n = 1;
    if (optarg)
        n = (int)strtol(optarg, NULL, 10);
    else if (optind < argc && digits_only(argv[optind]))
        n = (int)strtol(argv[optind++], NULL, 10);
    if (n < 1)
        n = 1;
    if (opt == 'v')
        log_set_level(LOG_INFO - (n > 2 ? 2 : n));
    else
        log_set_level(LOG_INFO + (n > 3 ? 3 : n));
}

// seconds, with an optional s/m/h suffix; -1 on a malformed value
static double parse_duration(const char *s)
{
    char *end;
    double v = strtod(s, &end);
    if (end == s || v < 0.0)
        return -1.0;
    if (*end == '\0' || ((*end == 's' || *end == 'S') && end[1] == '\0'))
        return v;
    if ((*end == 'm' || *end == 'M') && end[1] == '\0')
        return v * 60.0;
    if ((*end == 'h' || *end == 'H') && end[1] == '\0')
        return v * 3600.0;
    return -1.0;
}

static int parse_rate(const char *s, unsigned *out)
{
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > UINT_MAX)
        return -1;
    *out = (unsigned)v;
    return 0;
}

enum options_status options_parse(int argc, char **argv, struct options *o)
{
    o->action = ACTION_NONE;
    o->port = NULL;
    o->output = NULL;
    o->rate = UART_RATE_DEFAULT;
    o->timeout_ms = (unsigned)(REPLY_TIMEOUT_DEFAULT * 1000.0 + 0.5);

    const char *baud_text = NULL;
    double timeout_s = REPLY_TIMEOUT_DEFAULT;

    static const struct option long_opts[] = {
        {"port", required_argument, NULL, 'p'},
        {"output", required_argument, NULL, 'o'},
        {"check", no_argument, NULL, OPT_CHECK},
        {"backup", no_argument, NULL, OPT_BACKUP},
        {"timeout", required_argument, NULL, 't'},
        {"baud", required_argument, NULL, OPT_BAUD},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:o:t:v::q::h", long_opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'p':
            o->port = optarg;
            break;
        case 'o':
            o->output = optarg;
            break;
        case OPT_CHECK:
            o->action = ACTION_CHECK;
            break;
        case OPT_BACKUP:
            o->action = ACTION_BACKUP;
            break;
        case 't':
            timeout_s = parse_duration(optarg);
            break;
        case OPT_BAUD:
            baud_text = optarg;
            break;
        case 'v':
        case 'q':
            log_verbosity_opt(opt, argc, argv);
            break;
        case 'h':
            usage();
            return OPTIONS_HELP;
        default:
            usage();
            return OPTIONS_USAGE;
        }
    }

    if (o->action == ACTION_NONE)
    {
        usage();
        return OPTIONS_USAGE;
    }
    if (!o->port)
    {
        log_error("no serial port given (use -p DEVICE)\n");
        usage();
        return OPTIONS_USAGE;
    }
    if (o->action == ACTION_BACKUP && !o->output)
    {
        log_error("no output file given (use -o FILE)\n");
        return OPTIONS_USAGE;
    }
    if (o->action != ACTION_BACKUP && o->output)
    {
        log_error("output is only used by --backup\n");
        return OPTIONS_USAGE;
    }
    if (timeout_s <= 0.0 || timeout_s > REPLY_TIMEOUT_MAX)
    {
        log_error(
            "bad -t duration (use 0 < seconds <= %.0f, optionally s/m/h)\n", REPLY_TIMEOUT_MAX);
        return OPTIONS_USAGE;
    }

    unsigned rate = UART_RATE_DEFAULT;
    if (baud_text && parse_rate(baud_text, &rate) != 0)
    {
        log_error("bad --baud '%s': not a number\n", baud_text);
        return OPTIONS_USAGE;
    }
    if (!uart_rate_supported(rate))
    {
        log_error("unsupported baud rate %u (use %s)\n", rate, uart_rate_names());
        return OPTIONS_USAGE;
    }

    if (optind < argc)
    {
        log_error("unexpected argument '%s'\n", argv[optind]);
        usage();
        return OPTIONS_USAGE;
    }

    o->rate = rate;
    o->timeout_ms = (unsigned)(timeout_s * 1000.0 + 0.5);
    return OPTIONS_OK;
}
