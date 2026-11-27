/**
 * @file logging.h
 * @brief Leveled logging to stderr. stdout stays free for data: a dump's bytes
 *        go to a file, not down the terminal.
 *
 * The threshold is a single global, set once from the command line (-v/-q).
 */

#ifndef LOGGING_H
#define LOGGING_H

enum log_level
{
    LOG_TRACE = 0, // per-frame byte dumps and retry internals, can spam
    LOG_DEBUG = 1, // stage handovers and decisions
    LOG_INFO = 2,  // routine progress, the default
    LOG_WARN = 3,  // handled, execution continues
    LOG_ERROR = 4, // infrastructure or internal failure
    LOG_OFF = 5,   // silences everything
};

void log_set_level(enum log_level level);

void log_trace(const char *fmt, ...);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif
