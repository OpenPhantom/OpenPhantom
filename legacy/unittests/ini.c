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
 *
 * ini_read_section adds one rule worth pinning: the profile API cannot say "the buffer was too
 * small"; it fills what it can and returns a length two short of the buffer, and a caller that took
 * that as a short answer would judge half a section as if it were the whole one.
 */
#include "unittest.h"

#include "common/ini.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#define A "unittest_a"
#define B "unittest_b"
#define SECTION "unittest_section"

/* alpha=1 NUL beta=22 NUL, and then the second NUL that ends the run. */
static const char EXPECTED[] = "alpha=1\0beta=22\0";
#define EXPECTED_LENGTH 16u

static void check_a_whole_section(void)
{
    char   buffer[64];
    size_t got;

    ut_section("a section that is not there");
    memset(buffer, 0x5A, sizeof buffer);
    got = ini_read_section("unittest_absent", buffer, sizeof buffer);
    ut_check(got == 0u, "an absent section reads as empty");
    ut_check(buffer[0] == '\0' && buffer[1] == '\0',
             "and the buffer carries the double terminator, so a caller may walk it regardless");

    ut_section("a section written and read back whole");
    ut_check(ini_write_int(SECTION, "alpha", 1), "the first key is written");
    ut_check(ini_write_int(SECTION, "beta", 22), "and the second");
    memset(buffer, 0x5A, sizeof buffer);
    got = ini_read_section(SECTION, buffer, sizeof buffer);
    ut_check(got == EXPECTED_LENGTH,
             "the length counts every key=value run and its terminator, not the final one");
    ut_check(memcmp(buffer, EXPECTED, EXPECTED_LENGTH + 1u) == 0,
             "the runs come back in file order, each ended by a NUL and the last by a second one");

    ut_section("a buffer too small for the section");
    memset(buffer, 0x5A, sizeof buffer);
    got = ini_read_section(SECTION, buffer, 10u);
    ut_check(got == 0u,
             "a section that does not fit reads as empty rather than as its first few keys");
    ut_check(buffer[0] == '\0' && buffer[1] == '\0', "and the buffer is the empty run");

    /* The profile API returns exactly two less than the buffer when it truncated, and the same
     * number for a section that happens to be that long, so the two cannot be told apart. The rule
     * refuses both: a caller that needs the difference passes a buffer it knows is large enough. */
    got = ini_read_section(SECTION, buffer, EXPECTED_LENGTH + 2u);
    ut_check(got == 0u,
             "a section exactly two short of the buffer is refused, since that length is also what "
             "a truncation returns");
    got = ini_read_section(SECTION, buffer, EXPECTED_LENGTH + 3u);
    ut_check(got == EXPECTED_LENGTH, "one byte more and the whole section is accepted");

    ut_section("arguments that cannot hold a section");
    ut_check(ini_read_section(SECTION, NULL, sizeof buffer) == 0u, "no buffer reads as empty");
    ut_check(ini_read_section(SECTION, buffer, 1u) == 0u,
             "a buffer with no room for the double terminator reads as empty");

    WritePrivateProfileStringA(SECTION, NULL, NULL, ini_path());
    ut_check(ini_read_section(SECTION, buffer, sizeof buffer) == 0u,
             "with the section removed it reads as empty again");
}

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

    ut_section("a string written is read back at once, past the read cache");
    ut_check(ini_write_string(A, "word", "first"), "a string is written");
    (void)ini_read_string(A, "word", "", text, sizeof text);
    ut_check(strcmp(text, "first") == 0, "and reads back as itself");
    ut_check(ini_write_string(A, "word", "second"), "a second string over it is written");
    (void)ini_read_string(A, "word", "", text, sizeof text);
    ut_checkf(strcmp(text, "second") == 0,
              "and the very next read gets it, not the cached \"first\" (read \"%s\")", text);
    ut_check(ini_write_string(A, "word", NULL), "writing NULL removes the key");
    ut_check(!ini_read_string(A, "word", "gone", text, sizeof text) && strcmp(text, "gone") == 0,
             "and the very next read answers the default");

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
    ut_check(ini_generation() == before,
             "and it stands still while nothing writes, so a poll that sees it move can believe "
             "the move");
    /* The number is the file's last write time, and that moves with the system clock, which ticks
     * in whole milliseconds at best. A write landing in the same tick as the reading above would
     * carry the same stamp, so the tick is waited out first: the claim is that a write moves the
     * number, not that two writes inside one tick can be told apart. */
    Sleep(20);
    (void)ini_write_int(A, "count", 4321);
    after = ini_generation();
    ut_check(after != before, "and a write moves it, which is the comparison a poll makes");

    ut_check(ini_path() != NULL && ini_path()[0] != '\0', "the path is never empty");

    check_a_whole_section();

    return ut_summary("the settings file");
}
