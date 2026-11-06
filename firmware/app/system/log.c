/**
 * @file    log.c
 * @brief   Log queue and a minimal printf implementation.
 */
#include "log.h"

#include <string.h>

#define LOG_QUEUE_LEN   8u

static log_msg_t s_queue[LOG_QUEUE_LEN];
static uint32_t  s_head;
static uint32_t  s_tail;

/* ------------------------------------------------------------------------ */
typedef struct {
    char  *buf;
    size_t size;
    size_t n;
} out_t;

static void put(out_t *o, char c)
{
    if (o->n + 1u < o->size) {
        o->buf[o->n] = c;
    }
    o->n++;
}

static void put_padded(out_t *o, const char *s, size_t len, int width, bool left, char pad)
{
    int fill = width - (int)len;
    if (!left) {
        while (fill-- > 0) {
            put(o, pad);
        }
    }
    for (size_t i = 0u; i < len; i++) {
        put(o, s[i]);
    }
    if (left) {
        while (fill-- > 0) {
            put(o, ' ');
        }
    }
}

static size_t utoa_rev(uint32_t v, unsigned base, bool upper, char *tmp)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t n = 0u;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0u);
    return n;
}

static void put_number(out_t *o, uint32_t v, bool neg, unsigned base, bool upper, int width,
                       bool left, char pad)
{
    char tmp[12];
    char out[13];
    size_t n = utoa_rev(v, base, upper, tmp);
    size_t k = 0u;
    if (neg) {
        out[k++] = '-';
    }
    while (n > 0u) {
        out[k++] = tmp[--n];
    }
    if (neg && pad == '0' && !left) {
        put(o, '-');
        put_padded(o, out + 1, k - 1u, width - 1, left, pad);
    } else {
        put_padded(o, out, k, width, left, pad);
    }
}

/* single precision on purpose: the FPU has no double support */
static void put_float(out_t *o, float v, int prec, int width, bool left, char pad)
{
    char tmp[32];
    size_t k = 0u;
    if (v != v) {
        put_padded(o, "nan", 3u, width, left, ' ');
        return;
    }
    if (prec < 0) {
        prec = 3;
    }
    if (prec > 6) {
        prec = 6;
    }
    bool neg = v < 0.0f;
    if (neg) {
        v = -v;
    }
    uint32_t scale = 1u;
    for (int i = 0; i < prec; i++) {
        scale *= 10u;
    }
    if (v > 4.0e9f / (float)scale) {
        put_padded(o, neg ? "-big" : "big", neg ? 4u : 3u, width, left, ' ');
        return;
    }
    uint32_t ip = (uint32_t)v;
    uint32_t fp = (uint32_t)((v - (float)ip) * (float)scale + 0.5f);
    if (fp >= scale) {                      /* rounding carried into the integer part */
        fp -= scale;
        ip++;
    }

    char d[12];
    if (neg) {
        tmp[k++] = '-';
    }
    size_t n = utoa_rev(ip, 10u, false, d);
    while (n > 0u) {
        tmp[k++] = d[--n];
    }
    if (prec > 0) {
        tmp[k++] = '.';
        uint32_t div = scale / 10u;
        for (int i = 0; i < prec; i++) {
            tmp[k++] = (char)('0' + (fp / div) % 10u);
            div /= 10u;
        }
    }
    put_padded(o, tmp, k, width, left, pad);
}

size_t mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t o = { buf, size, 0u };

    while (*fmt != '\0') {
        char c = *fmt++;
        if (c != '%') {
            put(&o, c);
            continue;
        }
        bool left = false;
        char pad = ' ';
        int width = 0;
        int prec = -1;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') {
                left = true;
            } else {
                pad = '0';
            }
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt++ - '0');
            }
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            fmt++;                          /* length modifiers: 32-bit target */
        }
        char conv = *fmt++;
        switch (conv) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            uint32_t mag = (v < 0) ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v;
            put_number(&o, mag, v < 0, 10u, false, width, left, pad);
            break;
        }
        case 'u':
            put_number(&o, va_arg(ap, uint32_t), false, 10u, false, width, left, pad);
            break;
        case 'x':
        case 'X':
            put_number(&o, va_arg(ap, uint32_t), false, 16u, conv == 'X', width, left, pad);
            break;
        case 'c': {
            char ch = (char)va_arg(ap, int);
            put_padded(&o, &ch, 1u, width, left, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            size_t len = strlen(s);
            if (prec >= 0 && (size_t)prec < len) {
                len = (size_t)prec;
            }
            put_padded(&o, s, len, width, left, ' ');
            break;
        }
        case 'f':
            put_float(&o, (float)va_arg(ap, double), prec, width, left, pad);
            break;
        case '%':
            put(&o, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, conv);
            break;
        }
    }
    if (size > 0u) {
        buf[(o.n < size) ? o.n : size - 1u] = '\0';
    }
    return o.n;
}

size_t mini_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = mini_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void log_printf(uint8_t level, const char *fmt, ...)
{
    uint32_t next = (s_head + 1u) % LOG_QUEUE_LEN;
    if (next == s_tail) {
        s_tail = (s_tail + 1u) % LOG_QUEUE_LEN;     /* drop the oldest */
    }
    log_msg_t *m = &s_queue[s_head];
    va_list ap;
    va_start(ap, fmt);
    size_t n = mini_vsnprintf(m->text, sizeof(m->text), fmt, ap);
    va_end(ap);
    m->len = (uint8_t)((n < sizeof(m->text)) ? n : sizeof(m->text) - 1u);
    m->level = level;
    s_head = next;
}

bool log_pop(log_msg_t *out)
{
    if (s_tail == s_head) {
        return false;
    }
    *out = s_queue[s_tail];
    s_tail = (s_tail + 1u) % LOG_QUEUE_LEN;
    return true;
}
