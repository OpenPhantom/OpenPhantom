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

#endif /* OPENPHANTOM_FLOOR_PROBE_H */
