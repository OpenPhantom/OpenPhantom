/* mp_mod_census.c: the census against the process's real module list, and whether this machine
 * may host.
 *
 * Where a file lies against the mods folder is pure and comes first, with the folder written every
 * way a player may write it. Then a DLL of this test's own making, with no version resource, is
 * copied into a folder beside the program as a.dll, sub\b.dll and c.dll (mp_mod_census_fixture.c);
 * a.dll and sub\b.dll are loaded, and a.dll is renamed while it is loaded. The census, taken as a
 * build of release 9.9.9, because a test program has no release number of its own, reads the module
 * list: a.dll runs and is outside this release under the name it was loaded by; b.dll lies in a
 * folder below and is not judged; c.dll was never loaded. With [multiplayer] AllowMods empty this
 * machine may not host and a.dll is the one that blocks it; with AllowMods=a it may.
 * mp_mod_census_folder.c takes the same census from the folder, which does not see a.dll.
 */
#include "unittest.h"

#include "mp_mod_census_fixture.h"

#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_folder.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FOLDER "C:\\Game\\mods"

static void check_where_a_file_lies(void)
{
    ut_section("where a file lies against the mods folder");
    ut_check(mp_mod_folder_place(FOLDER "\\x.dll", FOLDER) == MP_MOD_PLACE_DIRECTLY,
             "a file in the folder lies directly in it");
    ut_check(mp_mod_folder_place("c:\\GAME\\MODS\\X.DLL", FOLDER) == MP_MOD_PLACE_DIRECTLY,
             "in any case");
    ut_check(mp_mod_folder_place(FOLDER "\\\\x.dll", FOLDER) == MP_MOD_PLACE_DIRECTLY,
             "and through the doubled separator a loader writes for ModDirectory=mods\\");
    ut_check(mp_mod_folder_place(FOLDER "\\fmv\\libvlc.dll", FOLDER) == MP_MOD_PLACE_BELOW,
             "the movie player's runtime in mods\\fmv lies below it, not in it");
    ut_check(mp_mod_folder_place("C:\\Game\\mods2\\x.dll", FOLDER) == MP_MOD_PLACE_ELSEWHERE,
             "a folder whose name only begins the same is another folder");
    ut_check(mp_mod_folder_place(FOLDER, FOLDER) == MP_MOD_PLACE_ELSEWHERE &&
                 mp_mod_folder_place("C:\\Game\\dxwrapper.dll", FOLDER) == MP_MOD_PLACE_ELSEWHERE,
             "the folder itself and a wrapper beside the executable lie elsewhere");
    ut_check(mp_mod_folder_place(FOLDER "\\x.dll", "C:\\Game\\mods\\") == MP_MOD_PLACE_DIRECTLY &&
                 mp_mod_folder_place(FOLDER "\\x.dll", "C:/Game/mods/") == MP_MOD_PLACE_DIRECTLY &&
                 mp_mod_folder_place(FOLDER "\\x.dll", "C:\\Game\\.\\mods") ==
                     MP_MOD_PLACE_DIRECTLY &&
                 mp_mod_folder_place(FOLDER "\\x.dll", "C:\\Game\\sub\\..\\mods") ==
                     MP_MOD_PLACE_DIRECTLY,
             "the folder written as mods\\, mods/, .\\mods and sub\\..\\mods is one folder");
    ut_check(mp_mod_folder_place(NULL, FOLDER) == MP_MOD_PLACE_ELSEWHERE &&
                 mp_mod_folder_place(FOLDER "\\x.dll", NULL) == MP_MOD_PLACE_ELSEWHERE,
             "no path at all lies nowhere");
}

static bool named(const char *const *names, size_t count, const char *name)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (_stricmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static void check_what_runs(void)
{
    const char *names[MP_MOD_CENSUS_FOREIGN_MAX];
    unsigned    count = 0u;
    bool        judged = false;
    size_t      listed;
    uint32_t    stamp = 0u;
    char        version[32];
    char        blocked[64];

    ut_section("the census reads what runs, from the module list");
    ut_check(fixture_begin("mods_census_test") && fixture_copy("a.dll") &&
                 fixture_copy("sub\\b.dll") && fixture_copy("c.dll"),
             "a folder of the test's own holds a.dll, sub\\b.dll and c.dll");
    ut_check(fixture_load("a.dll") && fixture_load("sub\\b.dll"),
             "a.dll and sub\\b.dll are loaded");
    ut_check(fixture_rename("a.dll", "a.dll.off"), "a.dll is renamed while it is loaded");

    mp_mod_census_take_against("9.9.9", true);
    listed = mp_mod_census_foreign_names(names, MP_MOD_CENSUS_FOREIGN_MAX, &count, &judged);
    ut_checkf(judged && count == 1u && listed == 1u && named(names, listed, "a.dll"),
              "a.dll runs and is outside this release under the name it was loaded by, though no "
              "file of that name is left (%u outside, the first '%s')", count,
              listed != 0u ? names[0] : "");
    ut_check(!named(names, listed, "b.dll") && !named(names, listed, "c.dll"),
             "b.dll below the folder is not judged, and c.dll was never loaded");
    ut_check(strcmp(mp_mod_census_class_of("a.dll"),
                    "foreign, no version resource of this project") == 0,
             "a DLL without this project's resource is foreign");
    ut_check(mp_mod_census_foreign_by_name("A.DLL", &stamp, version, sizeof version) &&
                 stamp != 0u && version[0] == '\0',
             "found by its name in any case, with its stamp and without a release number to show");

    ut_section("whether this machine may host");
    ut_check(!mp_mod_allow_may_host(), "with [multiplayer] AllowMods empty it may not");
    ut_checkf(mp_mod_allow_hosting_blocked(blocked, sizeof blocked) &&
                  _stricmp(blocked, "a.dll") == 0,
              "and a.dll is the DLL that blocks it (%s)", blocked);
    ut_check(fixture_allow("a") && mp_mod_allow_may_host(), "with AllowMods=a it may");
    ut_check(!mp_mod_allow_hosting_blocked(blocked, sizeof blocked) && blocked[0] == '\0',
             "and nothing blocks it");
    ut_check(strcmp(mp_mod_allow_list(), "a") == 0, "the judge holds the list as it was read");
    fixture_end();
}

int main(void)
{
    check_where_a_file_lies();
    check_what_runs();
    return ut_summary("mp_mod_census");
}
