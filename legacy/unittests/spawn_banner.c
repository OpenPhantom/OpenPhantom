/* spawn_banner.c: the words the placement mode puts at the top of the picture.
 *
 * Pure, and that is why it is a file of its own rather than four lines inside spawn_mode.c: the
 * numbers come from a running mode and the drawing needs a screen, so the only half of this a
 * test can reach is whether the sentence says the right thing. The cases worth pinning are the
 * two where it must NOT say a number: a session whose host has published no cap, where this
 * machine's own limit would be read as the one that applies, and a mode with nothing chosen.
 */
#include "unittest.h"

#include "spawn_banner.h"

#include <stdbool.h>

#include <string.h>

static char line[96];

static void test_placing(void)
{
    ut_section("what is being placed, and how many stand already");

    spawn_banner_placing(line, sizeof line, "Tusken Raider", 4u, 16u);
    ut_check(strcmp(line, "Placing: Tusken Raider   4 of 16 alive") == 0,
             "the kind and the count against the limit, which is the pair of questions a player "
             "standing in the mode has");

    spawn_banner_placing(line, sizeof line, "Tusken Raider", 0u, 16u);
    ut_check(strcmp(line, "Placing: Tusken Raider   0 of 16 alive") == 0,
             "none placed yet reads as none, not as an absent line");

    spawn_banner_placing(line, sizeof line, "Battle Droid", 3u, 0u);
    ut_check(strcmp(line, "Placing: Battle Droid   3 alive") == 0,
             "a session whose host has published no cap gets no limit at all: this machine's own "
             "sixteen is not the number that applies there");

    spawn_banner_placing(line, sizeof line, "", 3u, 16u);
    ut_check(strcmp(line, "Placing: nothing is chosen yet") == 0,
             "and with nothing chosen it says so rather than naming an empty kind");

    spawn_banner_placing(line, sizeof line, NULL, 3u, 16u);
    ut_check(strcmp(line, "Placing: nothing is chosen yet") == 0, "no label reads the same way");
}

static void test_ended(void)
{
    ut_section("what the mode did, once it is over");

    spawn_banner_ended(line, sizeof line, 4u, 1u, 2u);
    ut_check(strcmp(line, "Placement ended: placed 4, removed 1, 2 refused") == 0,
             "the same tally the log writes, so a player who saw the line and a reader who has "
             "the log are looking at one set of numbers");

    spawn_banner_ended(line, sizeof line, 0u, 0u, 0u);
    ut_check(strcmp(line, "Placement ended") == 0,
             "a mode that was entered and left again says only that, rather than three zeroes");

    spawn_banner_ended(line, sizeof line, 0u, 0u, 1u);
    ut_check(strcmp(line, "Placement ended: placed 0, removed 0, 1 refused") == 0,
             "a single refused click is worth the whole line: it is the case where the player "
             "could not tell whether anything had happened at all");
}

static void test_truncation(void)
{
    char small[16];

    ut_section("a buffer too small truncates and still terminates");
    memset(small, 'x', sizeof small);
    spawn_banner_placing(small, sizeof small, "Tusken Raider", 4u, 16u);
    ut_check(small[sizeof small - 1] == '\0', "the line is terminated inside the buffer it was "
                                              "given");
    ut_check(strncmp(small, "Placing: ", 9) == 0, "and what fits is the start of the sentence");

    spawn_banner_placing(NULL, 0u, "Tusken Raider", 4u, 16u);
    spawn_banner_ended(NULL, 0u, 1u, 1u, 1u);
    ut_check(true, "no buffer at all is refused rather than written through");
}

static void test_waiting(void)
{
    ut_section("the mode standing still");
    ut_check(strcmp(spawn_banner_waiting(),
                    "Placement waits: the free camera has the mouse") == 0,
             "it names what has the pointer, as the log does");
}

int main(void)
{
    test_placing();
    test_ended();
    test_truncation();
    test_waiting();
    return ut_summary("the placement mode's banner");
}
