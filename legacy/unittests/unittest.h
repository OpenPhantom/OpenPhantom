#ifndef UNITTEST_H
#define UNITTEST_H

#include <stddef.h>

/* Minimal check harness shared by every test in this directory.
 *
 * The tests here are plain console programs that ctest runs: no fixtures, no discovery, no
 * mocking. That fits what there is to test. Almost everything in this project is either pure
 * arithmetic with a known right answer, or a byte layout that either matches the retail image or
 * does not, and neither needs a framework. What they did all need was the same twenty lines of
 * pass/fail bookkeeping, which had been copy-pasted thirteen times and had already drifted into
 * three spellings of the same float comparison.
 *
 * Writing a test:
 *
 *     #include "unittest.h"
 *
 *     int main(void)
 *     {
 *         ut_section("the damper");
 *         ut_check(step > 0.0f, "a positive gap turns the body toward the target");
 *         ut_near(vertical_fov(4.0f / 3.0f), 60.0f, 0.001f, "4:3 gives the authored 60 degrees");
 *
 *         return ut_summary("strafe walk");
 *     }
 *
 * Every check prints its line either way. That is deliberate: these run in a terminal during a
 * build, and a passing run that lists what it proved is worth more than a silent one, especially
 * where the check text is the only place a byte-level assumption is written down in English.
 */

/* Prints a heading and groups the checks that follow it. Optional. */
void ut_section(const char *name);

/* The one check. Records a failure when `condition` is zero and prints the line either way.
 * `what` should read as a claim about the code, not as a label: "an empty list is refused"
 * rather than "test empty list". */
void ut_check(int condition, const char *what);

/* Same, with a printf-style message, for checks inside a loop where the case has to be named by
 * its numbers ("mode 12: 1280x1024 keeps its aspect"). */
void ut_checkf(int condition, const char *format, ...);

/* Float comparison. Doubles in the signature so that float arguments promote cleanly; the
 * tolerance is always explicit, because the right one is a property of the quantity being checked
 * and never a default. NaN fails, including when both sides are NaN. */
void ut_near(double actual, double expected, double tolerance, const char *what);

/* Prints the totals and returns the process exit code: 0 when everything passed, 1 otherwise.
 * `suite` names the thing under test and appears in the summary line. */
int ut_summary(const char *suite);

/* How many checks have failed so far. For the rare test that has to stop early rather than run on
 * into a state its earlier checks just proved is broken. */
size_t ut_failures(void);

#endif /* UNITTEST_H */
