/* The level list's awkward halves, driven without a game: the name a file gets when nobody named
 * it, and the join that puts this machine's root in front of a relative path.
 *
 * The two halves that need the engine, the table read and the folder scan, are checked for
 * refusing rather than for answering, because nothing resolves in a test process.
 */
#include "unittest.h"

#include "mp_levels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_title_from_a_file(void)
{
    char title[MP_LOBBY_TITLE_MAX];

    ut_section("the name a level gets when nobody named it");
    ut_check(mp_levels_title_from_file("ARENA.B3D", title, sizeof title) &&
                 strcmp(title, "Arena") == 0,
             "the shipped shouting becomes one capital and the rest small");
    ut_check(mp_levels_title_from_file("my_arena.b3d", title, sizeof title) &&
                 strcmp(title, "My_arena") == 0,
             "and a lower case file keeps its shape");
    ut_check(mp_levels_title_from_file("Duel.B3d", title, sizeof title) &&
                 strcmp(title, "Duel") == 0,
             "the extension is compared without regard to case, as the engine compares it");

    ut_section("and what is not a level at all");
    ut_check(!mp_levels_title_from_file("readme.txt", title, sizeof title), "a text file");
    ut_check(!mp_levels_title_from_file(".b3d", title, sizeof title), "an extension with no name");
    ut_check(!mp_levels_title_from_file("x.b3", title, sizeof title),
             "a name too short to hold one");
    ut_check(!mp_levels_title_from_file(NULL, title, sizeof title), "nothing");

    ut_section("a long name is cut to the field it is drawn in");
    ut_check(mp_levels_title_from_file("a_very_long_level_name_indeed.b3d", title, sizeof title),
             "it is still a level");
    ut_check(strlen(title) == sizeof title - 1u, "and its name stops at the field");
}

static void check_the_join(void)
{
    char out[MP_LEVELS_ABSOLUTE_MAX];

    ut_section("this machine's root in front of a relative path");
    ut_check(mp_levels_join("C:\\Games\\TPM\\gamedata", "level\\swamp.b3d", out, sizeof out) &&
                 strcmp(out, "C:\\Games\\TPM\\gamedata\\level\\swamp.b3d") == 0,
             "one separator between them");
    ut_check(mp_levels_join("C:\\Games\\TPM\\gamedata\\", "level\\swamp.b3d", out, sizeof out) &&
                 strcmp(out, "C:\\Games\\TPM\\gamedata\\level\\swamp.b3d") == 0,
             "a root that already ends in one does not get a second");
    ut_check(mp_levels_join("C:\\gamedata", "\\level\\swamp.b3d", out, sizeof out) &&
                 strcmp(out, "C:\\gamedata\\level\\swamp.b3d") == 0,
             "and neither does a relative path that starts with one");

    ut_section("what the join refuses");
    ut_check(!mp_levels_join(NULL, "level\\x.b3d", out, sizeof out), "no root");
    ut_check(!mp_levels_join("C:\\gamedata", "", out, sizeof out), "an empty path");
    ut_check(!mp_levels_join("C:\\gamedata", "\\\\", out, sizeof out),
             "a path of nothing but separators");
    ut_check(!mp_levels_join("C:\\gamedata", "level\\swamp.b3d", out, 8u),
             "a buffer that could not hold the answer");
    ut_check(out[0] == '\0', "and it wrote nothing into it");
}

static void check_it_refuses_without_a_game(void)
{
    char out[MP_LEVELS_ABSOLUTE_MAX];

    ut_section("with no engine in the process, every engine-side answer is honest about it");
    ut_check(mp_levels_scan() == 0u, "the scan finds nothing rather than inventing it");
    ut_check(mp_levels_count() == 0u, "the list is empty");
    ut_check(mp_levels_at(0) == NULL, "and asking for a row answers nothing");
    ut_check(mp_levels_find("level\\swamp.b3d") == NULL, "a path nobody listed is not found");
    ut_check(mp_levels_shipped(0) == NULL, "and neither is a table index");
    ut_check(!mp_levels_absolute("level\\swamp.b3d", out, sizeof out),
             "without a data root there is no absolute path");
    ut_check(!mp_levels_present("level\\swamp.b3d"),
             "and a file cannot be said to be there, which is what keeps the loader, whose own "
             "branch does not check, from being handed a path that names nothing");
}

int main(void)
{
    check_the_title_from_a_file();
    check_the_join();
    check_it_refuses_without_a_game();
    return ut_summary("mp_levels");
}
