/* mp_mod_census_folder.c: the census the way it goes when the module list cannot be read.
 *
 * A folder of the test's own (mp_mod_census_fixture.c) holds a.dll and d.dll, both loaded, and
 * a.dll is renamed while it is loaded. Taken from the folder, the census asks the system for each
 * DLL it finds there by name: d.dll is found, and a.dll, whose file has another name now, is not.
 * That is the gap the module list closes, and the proof that mp_mod_census.c sees a.dll through the
 * list and not through the folder. Its own program, because a census is taken once per process.
 */
#include "unittest.h"

#include "mp_mod_census_fixture.h"

#include "mp_mod_census.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    const char *names[MP_MOD_CENSUS_FOREIGN_MAX];
    unsigned    count = 0u;
    bool        judged = false;
    size_t      listed;
    uint32_t    stamp = 1u;

    ut_section("the census from the folder, by name");
    ut_check(fixture_begin("mods_census_folder_test") && fixture_copy("a.dll") &&
                 fixture_copy("d.dll") && fixture_load("a.dll") && fixture_load("d.dll"),
             "a.dll and d.dll lie in the test's folder and are loaded");
    ut_check(fixture_rename("a.dll", "a.dll.off"), "a.dll is renamed while it is loaded");

    mp_mod_census_take_against("9.9.9", false);
    listed = mp_mod_census_foreign_names(names, MP_MOD_CENSUS_FOREIGN_MAX, &count, &judged);
    ut_checkf(judged && count == 1u && listed == 1u && _stricmp(names[0], "d.dll") == 0,
              "d.dll, loaded under its own name, is found (%u outside, the first '%s')", count,
              listed != 0u ? names[0] : "");
    ut_check(!mp_mod_census_foreign_by_name("a.dll", &stamp, NULL, 0u) && stamp == 0u,
             "a.dll, renamed, is not seen this way");
    fixture_end();
    return ut_summary("mp_mod_census_folder");
}
