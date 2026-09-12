/* music_volume_latch.c: see music_volume_latch.h. */
#include "music_volume_latch.h"

#include <stddef.h>

void music_volume_latch_set(music_volume_latch_t *latch, float volume)
{
    latch->previous = latch->seen_any ? latch->current : volume;
    latch->current  = volume;
    latch->seen_any = true;
}

void music_volume_latch_detach(music_volume_latch_t *latch)
{
    /* A zero standing immediately in front of a detach is the screen silencing the music for a
     * provider change, never the player, because a player who drags to silence has already put
     * their own zero into `previous` on the call before this one. Either way `previous` is what
     * they asked for, so this needs no rule about what zero means. */
    if (latch->seen_any && latch->current == 0.0f) {
        latch->restore_value = latch->previous;
        latch->restore_valid = true;
    }
}

bool music_volume_latch_take_restore(music_volume_latch_t *latch, float *out_value)
{
    if (!latch->restore_valid) {
        return false;
    }
    latch->restore_valid = false;
    if (out_value != NULL) {
        *out_value = latch->restore_value;
    }
    return true;
}

void music_volume_latch_restored(music_volume_latch_t *latch, float value)
{
    latch->current  = value;
    latch->previous = value;
    latch->seen_any = true;
}
