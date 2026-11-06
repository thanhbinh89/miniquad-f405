/**
 * @file    log.h
 * @brief   Text log forwarded to the configurator (MSG_LOG).
 *
 * log_printf() formats into a small queue (thread context only); the comms
 * task drains it. Supported conversions: %d %i %u %x %X %c %s %% and %f
 * (with optional width / precision / '-' / '0'). No heap, no libc printf.
 */
#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_MSG_MAX     120u

typedef struct {
    uint8_t level;
    uint8_t len;
    char    text[LOG_MSG_MAX];
} log_msg_t;

void   log_printf(uint8_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
bool   log_pop(log_msg_t *out);

/** Tiny vsnprintf used by the logger (also handy elsewhere). */
size_t mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
size_t mini_snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define LOG_D(...)  log_printf(0u, __VA_ARGS__)
#define LOG_I(...)  log_printf(1u, __VA_ARGS__)
#define LOG_W(...)  log_printf(2u, __VA_ARGS__)
#define LOG_E(...)  log_printf(3u, __VA_ARGS__)

#endif /* LOG_H */
