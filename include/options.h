/**
 * @file options.h
 * @brief The command line: one struct a verb can act on, and the parser that
 *        fills it.
 *
 * The struct carries resolved values, not the text they came from: --baud is a
 * rate uart_open() accepts, --timeout is milliseconds, and the defaults are
 * already applied. Anything but OPTIONS_OK leaves the struct unusable.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

/** What the binary was asked to do. */
enum action
{
    ACTION_NONE = 0,
    ACTION_CHECK,
    ACTION_BACKUP,
};

/** A parsed command line, ready to run. */
struct options
{
    enum action action;
    const char *port;    /**< serial device, required by every action */
    const char *output;  /**< --backup destination, NULL otherwise */
    unsigned rate;       /**< line speed, one uart_open() accepts */
    unsigned timeout_ms; /**< per-call reply deadline */
};

/** How options_parse() ended. */
enum options_status
{
    OPTIONS_OK = 0, /**< the struct is filled */
    OPTIONS_HELP,   /**< usage printed; the caller exits 0 */
    OPTIONS_USAGE,  /**< message and usage printed; the caller exits 1 */
};

/**
 * Parse @p argv into @p o, printing any diagnostic and the usage text itself.
 *
 * -v/-q take effect here: they set the logging threshold as the line is read.
 */
enum options_status options_parse(int argc, char **argv, struct options *o);

#endif
