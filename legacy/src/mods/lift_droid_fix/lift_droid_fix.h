/* lift_droid_fix.h: closes out a field-reported frame-rate stall (down to single-digit fps) at two
 * specific lift/elevator platforms, each carrying a small cluster of droids.
 *
 * Produces: lift_droid_fix.dll
 *
 * ==============================================================================================
 * THE FIELD REPORT, and what it actually was
 *
 * Two lift platforms, five known placements total (enemy045/046/048 riding one, enemy076/077 the
 * other), each found live via a player-position-and-nearby-placement capture rather than guessed
 * at - two earlier guesses by loose name/position matching were both wrong. Full suppression
 * (never letting any of the five spawn) was tried first and proved the mechanism completely
 * accounts for both stalls: zero of the five present, zero frame drop, at either lift. That is a
 * bigger change than the bug calls for, though, so it was replaced with two narrow, independent
 * mechanism fixes, both scoped to just these five placements and nothing else in the game:
 *
 *   1. activation_race_fix - see activation_race_fix.h. A genuine engine race between the
 *      activation scan (creates from a placement's static position/radius) and a separate per-tick
 *      deactivation check (destroys from the actor's live position/a different radius), close
 *      enough on a moving lift platform to disagree and tear an actor down that gets recreated the
 *      very next tick, forever. Fixed by refusing to forward the specific reason-0 destroy this
 *      produces, for these five placements only.
 *
 *   2. projectile_cleanup_fix - see projectile_cleanup_fix.h. Even with the race fixed, fps still
 *      tanked during a fight at either lift. Debris/spark physics entries (the same global ballistic
 *      list blaster bolts use) that persist on impact have no lifetime timer of their own and are
 *      removed only by an external check that appears to depend on the entry's position having
 *      settled - which it never does while resting on a platform that keeps moving. Measured live:
 *      the list climbs steadily during a fight at either lift and gets stuck at 44-57 entries
 *      instead of draining, directly correlating with the worst fps windows. Fixed by force-setting
 *      the engine's own removal flag on any sufficiently old entry near one of the five placements.
 *
 * Both fixes were extracted from view_distance_fix.dll into this DLL of their own, per this
 * project's "one DLL per independent fix" rule - the two mechanisms are independent of each other
 * and of view_distance_fix's own actual view-distance work, and neither depends on the other at
 * run time (each resolves its own signature and installs its own hook/detour). */
#ifndef LIFT_DROID_FIX_H
#define LIFT_DROID_FIX_H

void lift_droid_fix_install(void);

#endif /* LIFT_DROID_FIX_H */
