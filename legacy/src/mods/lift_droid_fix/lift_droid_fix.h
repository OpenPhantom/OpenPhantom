/* lift_droid_fix.h: closes out a field-reported frame-rate stall (down to single-digit fps) that
 * started as a report about two specific lift/elevator platforms, each carrying a small cluster of
 * droids, and turned out to have one general cause and one that stayed local to those two lifts.
 *
 * Produces: lift_droid_fix.dll
 *
 * ==============================================================================================
 * THE FIELD REPORT, and what it actually was
 *
 * Two lift platforms, five known placements total (enemy045/046/048 riding one, enemy076/077 the
 * other), each found live via a player-position-and-nearby-placement capture rather than guessed
 * at; two earlier guesses by loose name/position matching were both wrong. Full suppression
 * (never letting any of the five spawn) was tried first and proved the mechanism completely
 * accounts for both stalls: zero of the five present, zero frame drop, at either lift. That is a
 * bigger change than the bug calls for, though, so it was replaced with two narrow, independent
 * mechanism fixes:
 *
 *   1. activation_race_fix, see activation_race_fix.h. A genuine engine race between the
 *      activation scan (creates from a placement's static position/radius) and a separate per-tick
 *      deactivation check (destroys from the actor's live position/a different radius), close
 *      enough on a moving lift platform to disagree and tear an actor down that gets recreated the
 *      very next tick, forever. Fixed by refusing to forward the specific reason-0 destroy this
 *      produces, for these five placements only. A general, mover-based version was tried next
 *      (any actor riding any mover, anywhere in the game) and shipped, but a played session showed
 *      it refusing zero of over two thousand reason-0 destroys it should have caught; reverted back
 *      to the five-placement version, which stayed field-confirmed throughout.
 *
 *   2. projectile_cleanup_fix, see projectile_cleanup_fix.h. Even with the race fixed, fps still
 *      tanked during a fight at either lift. Debris/spark physics entries (the same global ballistic
 *      list blaster bolts use) that persist on impact stop generating the collision events that are
 *      the only trigger for their own removal once they settle, an event-starvation bug rather than
 *      a position check, and nothing about it is specific to a moving surface. First shipped scoped
 *      to just the five placements it was field-confirmed at, on the reasoning that the mechanism
 *      not being lift-specific was plausible but unconfirmed; a later full playthrough refuted that
 *      caution directly, piling the list up to 89 entries scattered across the whole level, at
 *      droid deaths nowhere near either lift. Now fixed by force-setting the engine's own removal
 *      flag on any sufficiently old entry, anywhere on the list.
 *
 * Both fixes were extracted from view_distance_fix.dll into this DLL of their own, per this
 * project's "one DLL per independent fix" rule: the two mechanisms are independent of each other
 * and of view_distance_fix's own actual view-distance work, and neither depends on the other at
 * run time (each resolves its own signature and installs its own hook/detour). */
#ifndef LIFT_DROID_FIX_H
#define LIFT_DROID_FIX_H

void lift_droid_fix_install(void);

#endif /* LIFT_DROID_FIX_H */
