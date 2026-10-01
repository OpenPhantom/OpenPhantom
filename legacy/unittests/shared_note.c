/* shared_note.c: the note two mods leave for each other without either knowing the other exists.
 *
 * The channel is testable in one process because that is exactly the situation it serves: both
 * mods are DLLs in the game's process, and a note published by one is read by the other through
 * the operating system rather than through a symbol. A test that publishes and then reads is
 * therefore not a simulation of the real case, it IS the real case with one process doing both
 * halves.
 *
 * What cannot be tested here is the losing side of the race, because provoking it needs a second
 * thread writing while this one reads, and a test that spins two threads to observe a two
 * instruction window would fail on a busy machine rather than on a broken one.
 */
#include "unittest.h"

#include "common/shared_note.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_naming_rule(void)
{
    char too_long[SHARED_NOTE_NAME_MAX + 2u];

    ut_section("what may be written on a note");
    ut_check(shared_note_name_is_sound("appearance"), "letters");
    ut_check(shared_note_name_is_sound("hero_2"), "digits and the underscore");
    ut_check(!shared_note_name_is_sound(""), "a name of nothing is not a name");
    ut_check(!shared_note_name_is_sound(NULL), "and neither is no name at all");
    ut_check(!shared_note_name_is_sound("with space"), "a blank would end the object name early");
    ut_check(!shared_note_name_is_sound("back\\slash"),
             "a separator would file the note in another namespace, which is worse than failing");
    memset(too_long, 'a', sizeof too_long);
    too_long[sizeof too_long - 1u] = '\0';
    ut_check(!shared_note_name_is_sound(too_long), "a name longer than the field");
}

static void check_a_note_survives_the_trip(void)
{
    const char payload[] = "queen.baf";
    char       back[SHARED_NOTE_BYTES];
    size_t     count = 0;
    uint32_t   serial = 0;
    uint32_t   again = 0;

    ut_section("publishing and reading back");

    ut_check(!shared_note_read("ut_unwritten", back, sizeof back, &count, &serial),
             "reading a note nobody wrote answers false rather than an empty note: a reader has "
             "to be able to tell 'not loaded' from 'loaded and says nothing'");

    ut_check(shared_note_publish("ut_trip", payload, sizeof payload), "publishing");
    ut_check(shared_note_read("ut_trip", back, sizeof back, &count, &serial), "reading");
    ut_check(count == sizeof payload, "the length comes back");
    ut_check(strcmp(back, payload) == 0, "and so do the bytes");

    ut_check(shared_note_publish("ut_trip", "panaka.baf", 11u), "publishing over it");
    ut_check(shared_note_read("ut_trip", back, sizeof back, &count, &again), "reading again");
    ut_check(strcmp(back, "panaka.baf") == 0, "the newer note wins");
    ut_check(again != serial,
             "and the serial moved, which is how a reader tells a change from a repeat");

    shared_note_forget("ut_trip");
}

static void check_the_edges(void)
{
    uint8_t  big[SHARED_NOTE_BYTES + 1u];
    char     small[4];
    char     back[SHARED_NOTE_BYTES];
    size_t   count = 0;

    ut_section("what is refused");

    memset(big, 0x5A, sizeof big);
    ut_check(!shared_note_publish("ut_edge", big, sizeof big),
             "a note larger than the record is refused, not cut: half a name is a name");
    ut_check(shared_note_publish("ut_edge", big, SHARED_NOTE_BYTES), "one that exactly fits");
    ut_check(!shared_note_publish(NULL, big, 4u), "no name");
    ut_check(!shared_note_publish("ut edge", big, 4u), "a name the rule refuses");

    ut_check(!shared_note_read("ut_edge", small, sizeof small, &count, NULL),
             "a buffer smaller than the note is refused rather than filled with the front of it");
    ut_check(shared_note_read("ut_edge", back, sizeof back, &count, NULL), "a buffer that fits");
    ut_check(count == SHARED_NOTE_BYTES, "and the whole note is there");

    ut_check(shared_note_publish("ut_edge", NULL, 0u), "a note of nothing is a legal note");
    ut_check(shared_note_read("ut_edge", back, sizeof back, &count, NULL), "it reads back");
    ut_check(count == 0u, "as nothing, which is not the same as never published");

    shared_note_forget("ut_edge");
}

static void check_forgetting(void)
{
    char   back[SHARED_NOTE_BYTES];
    size_t count = 0;

    ut_section("letting go of a note");
    ut_check(shared_note_publish("ut_forget", "x", 2u), "published");
    shared_note_forget("ut_forget");
    shared_note_forget("ut_forget");   /* twice, because a second call must not fault */
    ut_check(!shared_note_read("ut_forget", back, sizeof back, &count, NULL),
             "with the last handle gone the note is gone: nothing in this process holds it open");
}

int main(void)
{
    check_the_naming_rule();
    check_a_note_survives_the_trip();
    check_the_edges();
    check_forgetting();
    return ut_summary("shared_note");
}
