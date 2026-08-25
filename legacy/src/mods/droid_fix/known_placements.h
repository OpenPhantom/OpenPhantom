/* known_placements.h: the five droid placements activation_race_fix.c exists for, found live rather
 * than guessed at. See droid_fix.h for the field investigation that produced them.
 *
 * enemy045 (127.1, 80.5, 23.1), enemy046 (126.5, 79.7, 23.1) and enemy048 (126.3, 80.9, 23.1) ride
 * one lift; enemy076 (122.4, 64.0, 23.0) and enemy077 (122.5, 64.5, 23.0) ride a second. Names are
 * a reused archetype label, not a per-placement ID (the same name fired around twenty times over
 * the whole level in the capture that found these); position is what actually distinguishes these
 * five from everything else.
 *
 * A `static const` array in a header, kept apart from a shared .c/.o since only one file needs it
 * any more: activation_race_fix.c matches an actor's placement against these five points to decide
 * whether to refuse a reason-0 destroy. projectile_cleanup_fix.c used to match against this same
 * table too, before its own fix was widened to the whole ballistic list; see that file's own header
 * for why the position gate came out entirely rather than merely widening the tolerance. */
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
