/**
 * @file    tinytest.h
 * @brief   Minimal single-header unit test helpers (host only).
 */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tt_failures;
static int tt_checks;
static const char *tt_current;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        tt_checks++;                                                                \
        if (!(cond)) {                                                              \
            tt_failures++;                                                          \
            printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, tt_current, #cond);\
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                       \
    do {                                                                            \
        double _a = (double)(a), _b = (double)(b), _t = (double)(tol);              \
        tt_checks++;                                                                \
        if (!(fabs(_a - _b) <= _t)) {                                               \
            tt_failures++;                                                          \
            printf("  FAIL %s:%d [%s] %s=%g vs %s=%g (tol %g)\n", __FILE__,          \
                   __LINE__, tt_current, #a, _a, #b, _b, _t);                        \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        long long _a = (long long)(a), _b = (long long)(b);                         \
        tt_checks++;                                                                \
        if (_a != _b) {                                                             \
            tt_failures++;                                                          \
            printf("  FAIL %s:%d [%s] %s=%lld != %s=%lld\n", __FILE__, __LINE__,     \
                   tt_current, #a, _a, #b, _b);                                     \
        }                                                                           \
    } while (0)

#define CHECK_STR(a, b)                                                             \
    do {                                                                            \
        tt_checks++;                                                                \
        if (strcmp((a), (b)) != 0) {                                                \
            tt_failures++;                                                          \
            printf("  FAIL %s:%d [%s] \"%s\" != \"%s\"\n", __FILE__, __LINE__,    \
                   tt_current, (a), (b));                                           \
        }                                                                           \
    } while (0)

#define RUN(fn)                                                                     \
    do {                                                                            \
        tt_current = #fn;                                                           \
        int _before = tt_failures;                                                  \
        fn();                                                                       \
        printf("%s %s\n", (tt_failures == _before) ? "[ ok ]" : "[FAIL]", #fn);     \
    } while (0)

#define TT_REPORT()                                                                 \
    (printf("%d checks, %d failures\n", tt_checks, tt_failures), tt_failures ? 1 : 0)

#endif /* TINYTEST_H */
