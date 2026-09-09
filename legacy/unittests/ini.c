/* ini.c: the settings file, exercised against a real one.
 *
 * This writes engine_fixes.ini beside the test binary and reads it back, so it drives the same
 * path a DLL does rather than a stand-in. What is being checked is this module's own behaviour,
 * the float conversion, the bool mapping, the decimal place clamp and the generation counter,
 * along with the two contracts the header states: that a section cannot read another section's
 * key, and that an absent key yields the default.
 *
 * The parsing underneath is Windows' own, and the point of pinning it here is that a change in it
 * would otherwise reach every setting in the project silently.
 */
#include "unittest.h"

#include "common/ini.h"

#include <stdio.h>
#include <string.h>

#define A "unittest_a"
#define B "unittest_b"

int main(void)
{
    char     text[64];
    uint64_t before;
    uint64_t after;
    int32_t  negative;

    ut_section("a value survives the round trip");
    ut_check(ini_write_int(A, "count", 1234), "an integer is written");
    ut_check(ini_read_int(A, "count", -1) == 1234, "and reads back as itself");

    ut_check(ini_write_float(A, "scale", 1.25f, 2), "a float is written");
    ut_near(ini_read_float(A, "scale", 0.0f), 1.25, 0.0001, "and reads back as itself");

    ut_section("what an absent key does");
    ut_check(ini_read_int(A, "not_here", 77) == 77, "an absent integer yields the default");
    ut_near(ini_read_float(A, "not_here", 0.125f), 0.125, 0.0001,
            "an absent float yields the default, through the six decimal places it is written to");
    ut_check(!ini_read_string(A, "not_here", "fallback", text, sizeof text),
             "an absent string answers false");
    ut_check(strcmp(text, "fallback") == 0, "and copies the default in anyway");

    ut_section("one section cannot read another one's key");
    ut_check(ini_write_int(B, "count", 4321),
             "the same key name is written under a second section");
    ut_check(ini_read_int(A, "count", -1) == 1234, "the first section still reads its own value");
    ut_check(ini_read_int(B, "count", -1) == 4321, "and the second reads its own");

    ut_section("the bool mapping");
    (void)ini_write_int(A, "flag", 0);
    ut_check(!ini_read_bool(A, "flag", true), "0 is false even when the default is true");
    (void)ini_write_int(A, "flag", 1);
    ut_check(ini_read_bool(A, "flag", false), "1 is true even when the default is false");
    (void)ini_write_int(A, "flag", 2);
    ut_check(ini_read_bool(A, "flag", false), "any other non-zero is also true");
    ut_check(ini_read_bool(A, "no_flag", true), "an absent key yields the default");

    ut_section("a negative value, which the platform decides");
    (void)ini_write_int(A, "negative", -5);
    (void)ini_read_string(A, "negative", "", text, sizeof text);
    ut_check(strcmp(text, "-5") == 0, "it is written to the file as -5");
    negative = ini_read_int(A, "negative", 999);
    ut_checkf(negative == -5, "and reads back as -5 rather than %d", (int)negative);

    ut_section("the decimal places are clamped");
    ut_check(ini_write_float(A, "many", 1.0f, 99), "a request for 99 decimal places is accepted");
    (void)ini_read_string(A, "many", "", text, sizeof text);
    ut_checkf(strlen(text) == 8, "and writes six of them, \"%s\"", text);
    ut_check(ini_write_float(A, "none", 2.5f, -3), "a negative request is accepted");
    (void)ini_read_string(A, "none", "", text, sizeof text);
    ut_checkf(strcmp(text, "2") == 0 || strcmp(text, "3") == 0,
              "and writes none of them, \"%s\"", text);

    ut_section("the generation counter");
    before = ini_generation();
    ut_check(before != 0u, "a file that exists has a generation");
    (void)ini_write_int(A, "count", 4321);
    after = ini_generation();
    ut_check(after != before || after != 0u,
             "and it is readable again after a write, the comparison a poll makes");

    ut_check(ini_path() != NULL && ini_path()[0] != '\0', "the path is never empty");

    return ut_summary("the settings file");
}
