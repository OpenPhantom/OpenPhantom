/* imuse_guard.h: stop the music lock from breaking, instead of noticing that it has.
 *
 * The watchdog in music_probe.c repairs the symptom: it releases a gate nobody can still be
 * holding. This closes the two ways it gets into that state in the first place.
 */
#ifndef IMUSE_FIX_IMUSE_GUARD_H
#define IMUSE_FIX_IMUSE_GUARD_H

#include "imuse_sites.h"

#include <stdbool.h>

/* Installs both repairs. Returns true when at least one of them stands; the log names each.
 * `sites` must already be resolved. */
bool imuse_guard_install(const imuse_sites_t *sites);

/* PURE LOGIC, tested without the game: would this (parameter, value) pair reach one of the five
 * refusals that return without releasing the lock? */
bool imuse_guard_set_param_leaks(int32_t parameter, int32_t value);

#endif /* IMUSE_FIX_IMUSE_GUARD_H */
