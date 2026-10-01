/* floor_probe.h: "is there a floor under this point, and how far down is it".
 *
 * Two cheats need the same answer for different reasons. Jump boost asks whether a fall that has
 * just begun has anywhere to land, because a fall with nothing beneath it must be handed back to
 * the engine untouched. The free camera asks how far the player would drop if it put them where
 * the camera is, because a drop past a certain height is one the engine was never built to finish.
 * One signature, resolved once, rather than the same twenty-nine bytes written out twice.
 */
#ifndef OPENPHANTOM_FLOOR_PROBE_H
#define OPENPHANTOM_FLOOR_PROBE_H

#include <stdbool.h>

typedef enum {
    /* The probe could not be resolved on this executable, so nothing is known. Callers must keep
     * whatever behaviour they had rather than treating this as "no floor": a build that cannot ask
     * the question must not start acting as though the answer were bad news. */
    FLOOR_PROBE_UNAVAILABLE = 0,
    FLOOR_PROBE_NONE,           /* asked, and there is nothing beneath the point at all */
    FLOOR_PROBE_FOUND           /* asked, and *out_drop holds how far below the point it is */
} floor_probe_result_t;

/* position is three floats, x/y/z, in the engine's own world units. out_drop may be NULL when only
 * the yes/no matters; when it is not, it is written ONLY on FLOOR_PROBE_FOUND, and is never
 * negative: a floor at or above the point reads as a drop of zero. */
floor_probe_result_t floor_probe_below(const float *position, float *out_drop);

/* The same probe, asked for the engine's own answer rather than for a drop.
 *
 * `out_offset` is SIGNED and is what has to be ADDED to the point to stand on that floor: negative
 * for a floor below it, positive for one above. The engine's own respawn seats a body exactly so.
 * A caller that wants to know how far something would FALL wants floor_probe_below; a caller that
 * wants to PUT something on the floor wants this one. The difference is a step UP, which reads as
 * a drop of zero above and would leave a body standing inside the stair.
 *
 * `out_on_mover` says the floor belongs to a moving platform. A caller that cares where its point
 * will be in a second has to ask, because none of the line probes can see a mover at all.
 *
 * Either out may be NULL. Both are written ONLY on FLOOR_PROBE_FOUND. */
floor_probe_result_t floor_probe_offset(const float *position, float *out_offset,
                                        bool *out_on_mover);

#endif /* OPENPHANTOM_FLOOR_PROBE_H */
