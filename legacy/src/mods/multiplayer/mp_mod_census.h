/* mp_mod_census.h: every DLL of the mods folder that runs in this process, and what it is to a
 * session.
 *
 * Layer 3. Taken once per process, at the first question, which is the first arming, the join
 * screen or the first reading of this side's statement, long after the loader. The modules are
 * the ones mp_mod_folder finds loaded from directly in the mods folder, and each is classified from
 * its mapped image with no file access: one of this release's own, or outside this release, which
 * a session takes only when the host's [multiplayer] AllowMods names it (mp_mod_allow).
 *
 * It also holds the builds of the required mods, which a join request states, and the release
 * number of this multiplayer, which a refusal carries.
 *
 * A build without a version resource has no release number of its own: the census then judges
 * nothing and says so, rather than calling every DLL a stranger.
 */
#ifndef MULTIPLAYER_MP_MOD_CENSUS_H
#define MULTIPLAYER_MP_MOD_CENSUS_H

#include "mp_mod_manifest_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many DLLs outside this release the census holds by name. More than this are counted, and a
 * machine with more cannot host, since the rest cannot be held against the host's list. */
#define MP_MOD_CENSUS_FOREIGN_MAX 16u

/* Takes the census, once per process; a second call does nothing. Writes its lines. */
void mp_mod_census_take(void);

/* For a test, and for the census itself: takes the census as a build whose release number is
 * `own_version`, from the process's module list, or from the mods folder by name when
 * `module_list` is false, which is the way the census goes when the list cannot be read. Once per
 * process, like mp_mod_census_take; whichever comes first decides. */
void mp_mod_census_take_against(const char *own_version, bool module_list);

/* The DLLs outside this release, as a statement names them: the names the census holds, in the
 * loader's order (sorted without regard to case), into `names`, how many there are in all into
 * `count`, which can be more than it holds, and whether this build judged at all into `judged`;
 * either may be NULL. Answers how many names. Takes the census first. The names stay valid for
 * the life of the process. */
size_t mp_mod_census_foreign_names(const char **names, size_t capacity, unsigned *count,
                                   bool *judged);

/* A DLL outside this release by its file name, compared without regard to case, for a refusal
 * that names it: its linker stamp, and its release number only when it is another release of this
 * project. False, with 0 and "", when the census does not hold that name. */
bool mp_mod_census_foreign_by_name(const char *name, uint32_t *stamp, char *version,
                                   size_t capacity);

/* What a DLL outside this release is, as the census's line words it ("foreign, no version
 * resource of this project", "another release", ...), for a line that names it; "not held by the
 * census" for a name the census does not hold. Never NULL. */
const char *mp_mod_census_class_of(const char *name);

/* The first DLL outside this release past the census's room, "" when it held them all. */
const char *mp_mod_census_first_unheld(void);

/* The builds of the required mods loaded in this process, into `out->count` and `out->mods`; the
 * data fingerprints are left as they were. Takes the census first if it has not been taken. */
void mp_mod_census_required_builds(mp_mod_manifest_t *out);

/* The build of a required mod on this side: its stamp, its image size and its release number.
 * False, with everything zero and empty, when it is not loaded here. */
bool mp_mod_census_build_of(uint8_t id, uint32_t *stamp, uint32_t *image, char *version,
                            size_t capacity);

/* A linker's time stamp as the local date and time it was built, "2026-09-29 04:27", or
 * "id 6ABB2222" for a stamp that is no plausible date (a reproducible build writes a hash
 * there). */
void mp_mod_census_describe_stamp(uint32_t stamp, char *out, size_t capacity);

/* The census's lines again, for the run report. */
void mp_mod_census_report(void);

#endif /* MULTIPLAYER_MP_MOD_CENSUS_H */
