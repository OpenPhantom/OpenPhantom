/* mover_wraps.h: which movers are being treated as having wrapped their track, and whether that
 * verdict can be trusted for them.
 *
 * A free running track runs its pose up to the track length and then back to the start, and that
 * step is a jump however small the resulting angle looks. A mover that has just done it must be
 * drawn unblended for one step, because its two samples sit on opposite ends of the track.
 *
 * The verdict is a drop of more than half the track length, on this argument: a wrap subtracts a
 * whole track and a reversal subtracts at most one substep of travel, so half a track lies
 * strictly between them. That argument has a floor nobody checked. It only separates the two while
 * half the track is larger than one substep of travel, and for a mover with a short track it is
 * not, so every ordinary reversal is read as a wrap and the mover is drawn unblended each time it
 * turns round. Putting movers on the substep clock made that likelier rather than less, because a
 * substep now moves a mover a whole step instead of a clamped fraction of one.
 *
 * That matters because every refused pose measured in a play session turns out to be a wrap: the
 * six guards inside the blend account for none of them. So if the wraps concentrate on one mover,
 * that mover accounts for the residual jitter, and this says whether they do.
 *
 * A handful of entries is enough. The question is not a full census, it is whether one offender
 * dominates, and a mover that wraps a few times in ten seconds is doing what it should.
 */
#ifndef MOVER_WRAPS_H
#define MOVER_WRAPS_H

#include <stdbool.h>
#include <stdint.h>

/* Distinct movers tracked at once. */
#define MOVER_WRAPS_SLOTS 6u

/* Whether the pose test is worth having at all. This was the open question.
 *
 * The veto is expensive. A wrap marks every subnode of that mover unusable until its next
 * integrating tick, and because the draw is what integrates a mover, that is the wrap frame plus
 * every following frame until the world clock next advances: about two frames at 60 fps. The
 * busiest mover measured wraps 21 times per 600 frames, so roughly 42 of those frames are drawn
 * unblended, which is a hitch four times a second on that mover and accounts for the whole refused
 * count of 35 to 84.
 *
 * And a pose wrapping from 29 back to 1.9 is only a discontinuity if the end of the track is
 * somewhere different from its start. For a LOOPING track, a rotating or circulating platform,
 * they are the same place: the two subnode poses either side of the wrap are one step of travel
 * apart, the geometry is continuous, and refusing to blend them makes this DLL the cause of
 * stepping rather than the cure for it.
 *
 * The jumps the test was written for are already refused without it. The wraps documented as
 * genuinely discontinuous move 298 world units and 101 degrees, and the translation limit of 64
 * units and the rotation guard at 45 degrees both catch that on the geometry itself, which is
 * where a discontinuity can actually be seen. So the pose test looks redundant for the case it
 * exists for and harmful for the case nobody considered.
 *
 * Hence the switch rather than a deletion: one play session decides it, and the loser goes.
 */

/* Whether a pose that DROPPED did so because its track wrapped, rather than because the mover is
 * on its return leg.
 *
 * Direction arm 3 integrates a reversing mover as pose minus rate times dt, so a door on its way
 * back decreases on every single tick. Treating that as a wrap would leave a whole class of mover
 * smoothed on the way out and stepped on the way back.
 *
 * The two are separable rather than a matter of tolerance: a wrap subtracts a whole track length
 * and a reversal at most one substep of travel, so half a track lies strictly between them. The
 * floor on that argument is in this file's own header, and it is why the report says whether the
 * track was ever long enough for the test to mean anything.
 *
 * `track` of zero or less means the mover has no track to wrap, so nothing is a wrap. */
bool mover_wraps_is_wrap(float pose_before, float pose_after, float track);

/* Records that `mover` was judged to have wrapped, with the track length it was judged against
 * and the drop that triggered it. `mover` is used as an identity only and never dereferenced. */
void mover_wraps_note(uintptr_t mover, float track, float drop);

/* Writes the busiest offender to the log, out of `total` wraps in the window, and says whether
 * the half track rule could separate a wrap from a reversal for it. Silent when nothing wrapped.
 * `substep_travel` is how far that mover moves in one substep, and half its track has to beat
 * it; pass 0 when it is not known and the line reports the track alone. */
void mover_wraps_report(uint32_t total, float substep_travel);

/* Forgets the window. */
void mover_wraps_reset(void);

#endif /* MOVER_WRAPS_H */
