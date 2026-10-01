/* spawn_scripts_archive.c: the two claims about the spawn scripts that need the game's archive.
 *
 * The clip filling reads big.lab, beside the host or in the working directory, and a test process
 * has neither. So this program looks for the installed game, OPENPHANTOM_GAME_DIR or the shipped
 * install path, and works from its directory when it finds one. There it checks the second gate
 * against the crash a clipless file causes, the builder's own: a file with no clip is refused
 * before any record is laid out, whichever road asked for it. And that a pickup runs the still
 * script whatever the behaviour row says, which is what keeps it a pickup. Without the game it says
 * so in one line and exits as skipped.
 */
#include "unittest.h"

#include "common/text.h"

#include "spawn_scripts.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_GAME_DIR "C:\\Program Files (x86)\\LucasArts\\The Phantom Menace"
/* What ctest reads as skipped (SKIP_RETURN_CODE in unittests/CMakeLists.txt): a machine without the
 * game never gives a green run that checked nothing. */
#define SKIPPED_EXIT 77

/* Into the game's directory; `looked` receives where it was looked for. */
static bool into_the_game(const char **looked)
{
    const char *dir = getenv("OPENPHANTOM_GAME_DIR");
    char        lab[MAX_PATH];

    if (dir == NULL || dir[0] == '\0') {
        dir = DEFAULT_GAME_DIR;
    }
    *looked = dir;
    text_format(lab, sizeof lab, "%s\\big.lab", dir);
    if (GetFileAttributesA(lab) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return SetCurrentDirectoryA(dir) != 0;
}

/* The record laid out for `file` and `which`, by the name its note begins with. */
static bool laid_out_as(spawn_behaviour_t which, const char *file, const char *record)
{
    static uint8_t buffer[SPAWN_SCRIPT_BYTES];
    char           note[160];
    size_t         length = strlen(record);

    note[0] = '\0';
    if (spawn_script_prepare(which, file, false, buffer, sizeof buffer, note, sizeof note,
                             NULL) == NULL) {
        return false;
    }
    return strncmp(note, record, length) == 0 && note[length] == ':';
}

int main(void)
{
    static uint8_t buffer[SPAWN_SCRIPT_BYTES];
    const char    *looked = DEFAULT_GAME_DIR;

    if (!into_the_game(&looked)) {
        printf("spawn scripts, archive: SKIPPED, no big.lab in %s (OPENPHANTOM_GAME_DIR names "
               "another), so neither claim is checked on this machine\n", looked);
        return SKIPPED_EXIT;
    }
    ut_section("with the installed game's archive");
    ut_check(laid_out_as(SPAWN_BEHAVIOUR_STAND, "tusken.baf", "stand"),
             "the archive reads: a figure standing is laid out as the stand record");
    ut_check(spawn_script_prepare(SPAWN_BEHAVIOUR_STAND, "partblu.baf", false, buffer,
                                  sizeof buffer, NULL, 0u, NULL) == NULL,
             "a file with no clip is refused before any record is laid out: the spawn would play "
             "clip 0 on it and the script would write in front of its tracks");
    ut_check(laid_out_as(SPAWN_BEHAVIOUR_ATTACK, "pwrhlth1.baf", "still"),
             "the large medpack asked to attack runs the still script: the behaviour is ignored");
    ut_check(laid_out_as(SPAWN_BEHAVIOUR_FOLLOW, "PWRBACTA.BAF", "still"),
             "and so does the small one asked to follow, case aside");
    ut_check(laid_out_as(SPAWN_BEHAVIOUR_ATTACK, "tripod.baf", "still"),
             "and the tripod gun, which the player mounts and fires");
    return ut_summary("spawn scripts, archive");
}
