/* unittests/mp_mod_census_fixture.h: a mods folder of a census test's own, beside the program.
 *
 * The folder is named by [loader] ModDirectory with a separator at its end, the way a player may
 * write it, and filled with copies of the probe DLL (mp_mod_census_probe.c). What the test writes,
 * the two ini keys, the files and the folder, is taken away before it begins, in case an earlier
 * run was cut off, and again at the end, and the ini file itself when the test made it. The ini
 * lies beside every test program of a build folder, so each test that uses this holds the ctest
 * resource lock on it.
 */
#ifndef UNITTESTS_MP_MOD_CENSUS_FIXTURE_H
#define UNITTESTS_MP_MOD_CENSUS_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>

/* Clears what an earlier run may have left, then points [loader] ModDirectory at `folder` beside
 * this program and empties [multiplayer] AllowMods. False when the folder cannot be made. */
bool fixture_begin(const char *folder);

/* Copies the probe DLL to `relative` in the folder ("a.dll", "sub\b.dll"). */
bool fixture_copy(const char *relative);

/* Loads the copy at `relative`, through a path built as the loader builds it, folder and name
 * joined by one more separator. False when it does not load; it stays loaded until the end. */
bool fixture_load(const char *relative);

/* Renames a copy while it is loaded. */
bool fixture_rename(const char *from, const char *to);

/* Writes [multiplayer] AllowMods. */
bool fixture_allow(const char *list);

/* Frees every copy it loaded, and takes away the files, the folder, the two keys and the ini when
 * the test made it. */
void fixture_end(void);

#endif /* UNITTESTS_MP_MOD_CENSUS_FIXTURE_H */
