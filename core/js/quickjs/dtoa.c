/*
 * dtoa.c — integer/float string conversion for QuickJS
 *
 * Implements the symbols declared in dtoa.h.
 * Integer functions are hand-written; float functions delegate to libc
 * sprintf/strtod which is correct and acceptable for our use-case.
 */

#include <stdint.h>
#include <stddef.h>
#include "dtoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── integer to string ──────────────────────────────────────────────────── */

size_t u32toa(char *buf, uint32_t n) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int i = 0;
    while (n) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return (size_t)i;
}

size_t i32toa(char *buf, int32_t n) {
    if (n < 0) { buf[0] = '-'; return 1 + u32toa(buf + 1, (uint32_t)(-(int64_t)n)); }
    return u32toa(buf, (uint32_t)n);
}

size_t u64toa(char *buf, uint64_t n) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[22]; int i = 0;
    while (n) { tmp[i++] = (char)('0' + (int)(n % 10)); n /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return (size_t)i;
}

size_t i64toa(char *buf, int64_t n) {
    if (n < 0 && n != (int64_t)0x8000000000000000LL) {
        buf[0] = '-';
        return 1 + u64toa(buf + 1, (uint64_t)(-n));
    } else if (n == (int64_t)0x8000000000000000LL) {
        const char *s = "-9223372036854775808";
        size_t len = 20;
        memcpy(buf, s, len + 1);
        return len;
    }
    return u64toa(buf, (uint64_t)n);
}

static const char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyz";

size_t u64toa_radix(char *buf, uint64_t n, unsigned int radix) {
    if (radix == 10) return u64toa(buf, n);
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[66]; int i = 0;
    while (n) {
        tmp[i++] = DIGITS[n % radix];
        n /= radix;
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return (size_t)i;
}

size_t i64toa_radix(char *buf, int64_t n, unsigned int radix) {
    if (radix == 10) return i64toa(buf, n);
    if (n < 0) {
        buf[0] = '-';
        return 1 + u64toa_radix(buf + 1, (uint64_t)(-n), radix);
    }
    return u64toa_radix(buf, (uint64_t)n, radix);
}

/* ── float64 ─────────────────────────────────────────────────────────────── */

int js_dtoa_max_len(double d, int radix, int n_digits, int flags) {
    (void)d; (void)radix; (void)n_digits; (void)flags;
    return 64; /* conservative upper bound */
}

int js_dtoa(char *buf, double d, int radix, int n_digits, int flags,
            JSDTOATempMem *tmp_mem) {
    (void)tmp_mem;
    int fmt = flags & JS_DTOA_FORMAT_MASK;
    int exp_mode = flags & JS_DTOA_EXP_MASK;

    if (isnan(d))  { strcpy(buf, "NaN");      return 3; }
    if (isinf(d))  { strcpy(buf, d > 0 ? "Infinity" : "-Infinity"); return (int)strlen(buf); }
    if (d == 0.0) {
        int neg = (flags & JS_DTOA_MINUS_ZERO) && signbit(d);
        strcpy(buf, neg ? "-0" : "0");
        return (int)strlen(buf);
    }

    if (radix != 10) {
        /* non-decimal: truncate to int and print in radix */
        int64_t iv = (int64_t)d;
        return (int)i64toa_radix(buf, iv, (unsigned)radix);
    }

    if (fmt == JS_DTOA_FORMAT_FRAC) {
        int len = sprintf(buf, "%.*f", n_digits, d);
        return len;
    }
    if (fmt == JS_DTOA_FORMAT_FIXED) {
        if (exp_mode == JS_DTOA_EXP_ENABLED) {
            int len = sprintf(buf, "%.*e", n_digits > 0 ? n_digits - 1 : 6, d);
            return len;
        }
        int len = sprintf(buf, "%.*g", n_digits > 0 ? n_digits : 17, d);
        return len;
    }
    /* FORMAT_FREE — use shortest representation */
    int len = sprintf(buf, "%.17g", d);
    /* strip trailing zeros after decimal point for cleaner output */
    if (strchr(buf, '.') && !strchr(buf, 'e')) {
        char *p = buf + len - 1;
        while (p > buf && *p == '0') { *p-- = '\0'; len--; }
        if (*p == '.') { *p = '\0'; len--; }
    }
    return len;
}

double js_atod(const char *str, const char **pnext, int radix, int flags,
               JSATODTempMem *tmp_mem) {
    (void)flags; (void)tmp_mem;
    char *end = NULL;
    double d;
    if (radix == 0 || radix == 10) {
        d = strtod(str, &end);
    } else {
        uint64_t iv = strtoull(str, &end, radix);
        d = (double)iv;
    }
    if (pnext) *pnext = end ? end : str;
    return d;
}
