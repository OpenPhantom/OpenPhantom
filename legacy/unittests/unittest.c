#include "unittest.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static size_t checks_run;
static size_t checks_failed;

void ut_section(const char *name)
{
    printf("\n-- %s --\n", (name != NULL) ? name : "");
}

static void report(int condition, const char *what)
{
    ++checks_run;
    if (condition) {
        printf("ok    %s\n", what);
        return;
    }
    ++checks_failed;
    printf("FAIL  %s\n", what);
}

void ut_check(int condition, const char *what)
{
    report(condition, (what != NULL) ? what : "(unnamed check)");
}

void ut_checkf(int condition, const char *format, ...)
{
    char    message[512];
    va_list arguments;

    va_start(arguments, format);
    /* _vsnprintf does not terminate on truncation, so the buffer is one short and the last byte
     * is written by hand. A truncated check name is a cosmetic problem; an unterminated one walks
     * off the end of the buffer while reporting a failure, which is the worst possible moment. */
    _vsnprintf(message, sizeof(message) - 1, format, arguments);
    va_end(arguments);
    message[sizeof(message) - 1] = '\0';

    report(condition, message);
}

void ut_near(double actual, double expected, double tolerance, const char *what)
{
    double difference = actual - expected;

    /* Written as a positive test rather than as !(difference > tolerance), so that a NaN on either
     * side fails instead of passing. NaN compares false against everything, and a test that goes
     * green because a number stopped being a number is worse than no test. */
    int close = (difference <= tolerance) && (-difference <= tolerance);

    if (close) {
        ut_check(1, what);
        return;
    }

    ut_checkf(0, "%s  (got %.6f, wanted %.6f +/- %.6f)", what, actual, expected, tolerance);
}

int ut_summary(const char *suite)
{
    const char *name = (suite != NULL) ? suite : "";

    printf("\n");
    if (checks_failed == 0) {
        printf("%s: all %u checks passed\n", name, (unsigned)checks_run);
        return 0;
    }

    printf("%s: %u of %u checks FAILED\n", name, (unsigned)checks_failed, (unsigned)checks_run);
    return 1;
}

size_t ut_failures(void)
{
    return checks_failed;
}
