/* known_placements.h: the five droid placements this whole DLL exists for, found live rather than
 * guessed at. See lift_droid_fix.h for the field investigation that produced them.
 *
 * enemy045 (127.1, 80.5, 23.1), enemy046 (126.5, 79.7, 23.1) and enemy048 (126.3, 80.9, 23.1) ride
 * one lift; enemy076 (122.4, 64.0, 23.0) and enemy077 (122.5, 64.5, 23.0) ride a second. Names are
 * a reused archetype label, not a per-placement ID (the same name fired around twenty times over
 * the whole level in the capture that found these); position is what actually distinguishes these
 * five from everything else.
 *
 * A `static const` array in a header, included separately by each of this DLL's two fix files,
 * rather than a shared .c/.o: both files want the SAME five points matched against two different
 * tolerances (activation_race_fix.c a tight one, for exact-placement identity; projectile_cleanup_
 * fix.c a wide one, for "somewhere on this lift's platform"), and neither needs anything else about
 * the other. */
#ifndef KNOWN_PLACEMENTS_H
#define KNOWN_PLACEMENTS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct known_placement {
    float x, y, z;
} known_placement_t;

static const known_placement_t KNOWN_PLACEMENTS[] = {
    { 127.1f, 80.5f, 23.1f },   /* enemy045, first lift */
    { 126.5f, 79.7f, 23.1f },   /* enemy046, first lift */
    { 126.3f, 80.9f, 23.1f },   /* enemy048, first lift */
    { 122.4f, 64.0f, 23.0f },   /* enemy076, second lift */
    { 122.5f, 64.5f, 23.0f },   /* enemy077, second lift */
};
#define KNOWN_PLACEMENT_COUNT (sizeof(KNOWN_PLACEMENTS) / sizeof(KNOWN_PLACEMENTS[0]))

static inline bool known_placement_is_near(const float *position, float tolerance)
{
    size_t index;

    for (index = 0; index < KNOWN_PLACEMENT_COUNT; ++index) {
        float dx = position[0] - KNOWN_PLACEMENTS[index].x;
        float dy = position[1] - KNOWN_PLACEMENTS[index].y;
        float dz = position[2] - KNOWN_PLACEMENTS[index].z;

        if ((dx * dx + dy * dy + dz * dz) < (tolerance * tolerance)) {
            return true;
        }
    }
    return false;
}

#endif /* KNOWN_PLACEMENTS_H */
