/* face_latch.h: give the scripted facing command back the in-progress answer it loses above 32 fps.
 *
 * A scripted actor asks whether its turn has finished by polling a flag that the animation system
 * raises once per rendered frame but that the script reads once per simulation step. Below 32 fps
 * some frames run two simulation steps back to back, the second one still sees the flag clear, and
 * the script takes its in-progress branch. At or above 32 fps that never happens, the answer is
 * always "finished", and a scripted state whose only work hangs off the other branch stops making
 * progress for good.
 *
 * This hands the script an in-progress answer on one simulation step in every N, which is what the
 * shipped frame rate produced by accident.
 */
#ifndef FRAMERATE_FIX_FACE_LATCH_H
#define FRAMERATE_FIX_FACE_LATCH_H

#include <stdbool.h>
#include <stdint.h>

/* yield_period is the simulation step period at which an in-progress answer is handed back, and 0
 * switches the feature off. Returns false when the site or the substep counter did not resolve, in
 * which case nothing is patched and the caller has to say so in the log. */
bool face_latch_install(int yield_period);

/* The decision, separated from the engine call so it can be driven without the game.
 *
 * engine_result is what the engine's own completion test answered, non-zero meaning finished.
 * Returns true when the hook should answer "still turning" instead. It must never do so when the
 * engine already said "still running", because then there is nothing to withhold, and it must
 * never do so for a clip that has not clamped at its last frame, because that clip is still moving
 * and delaying it would hold up whatever the script does next. */
bool face_latch_should_yield(int32_t engine_result, uint32_t substep_counter,
                             uint32_t yield_period, bool clip_holds_at_end);

#endif /* FRAMERATE_FIX_FACE_LATCH_H */
