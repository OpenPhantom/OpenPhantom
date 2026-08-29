/* spawn_census.h: give the one silent failure in the activation path a voice.
 *
 * When the activation scan decides an NPC should exist, it calls a spawn function that takes a
 * record out of two fixed pools: 128 actors and 255 things. Both are allocated once at start-up
 * and neither grows. If either is full the spawn returns zero, the scan skips the placement, and
 * NOTHING is written anywhere. An enemy that should be standing in front of you simply is not,
 * and the log looks like a clean session.
 *
 * That is the shape of a field report this project could not answer: a tank after a cutscene that
 * never appeared, on two machines, not reproducible on a third. This counts those refusals and
 * says so, which turns "it did not spawn" into a number.
 *
 * Observation only. The hook calls the engine's own function and returns its answer unchanged,
 * whatever that answer is.
 * ============================================================================================ */
#ifndef SPAWN_CENSUS_H
#define SPAWN_CENSUS_H

#include <stdbool.h>
#include <stdint.h>

/* Redirects the activation scan's own spawn call. `activation_scan` is the site view_distance_fix
 * already resolves for the range test; the spawn call sits at a fixed offset inside it and is
 * verified by its opcode and by the stack cleanup that follows before anything is written.
 *
 * Returns true only when the redirect is live. With `enabled` false it installs nothing and says
 * so once, because a census that is off and a census that failed to install must not look the
 * same in the log. */
bool spawn_census_install(uintptr_t activation_scan, bool enabled);

/* The destroy side of the same investigation. Detours the actor-teardown function directly (self-
 * resolves its own site, independent of activation_scan) and logs its reason code plus the same
 * name/position spawn_census already reads, so the two logs read as one story. Same `enabled` flag
 * as spawn_census_install; observation only.
 *
 * A fix that refused one specific destroy reason for five known placements used to live here
 * alongside this observer, then moved into a DLL of its own, and has since been removed entirely:
 * the stall it was aimed at turned out to be a VirtualQuery guard in framerate_fix, and the race
 * itself costs nothing observable once that is repaired. See spawn_census.c's own comment above
 * hook_actor_destroy for the mechanism, the four suppression attempts and the measurements. */
bool spawn_census_install_destroy_observer(bool enabled);

/* TEMPORARY: logs the player's own current position and camera yaw/pitch roughly once a second,
 * plus every active placement within a short radius of it. Built after two guesses at which
 * placement was one of the field report's droids, by loose position matching against a list of
 * everything that happened to fire a "created" log, were both wrong; actors already active before
 * the capture window started never emit one. This says directly where the player is and what is
 * actually near them, which is how the five placements named in spawn_census.c were found. */
void spawn_census_log_player_position(bool enabled);

#endif /* SPAWN_CENSUS_H */
