/* mp_clip_rule.c: where a body's clip stands against the sender's. See the header. */
#include "mp_clip_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

float mp_clip_head_distance(float wire_head, float local_head, bool loops, float num_frames)
{
    float delta = wire_head - local_head;

    if (loops && num_frames > 0.0f) {
        delta = (float)fmod((double)delta + (double)num_frames * 0.5, (double)num_frames);
        if (delta < 0.0f) {
            delta += num_frames;
        }
        delta -= num_frames * 0.5f;
    }
    return delta;
}

/* Written as NOTs, so that a rate, a length or a substep that is not a number answers no rather
 * than yes: a head nobody can measure is not a reason to leave events behind. */
bool mp_clip_beyond_window(float frames, float fps, float substep_seconds)
{
    float window;

    if (!(frames > 0.0f) || !(fps > 0.0f)) {
        return false;
    }
    if (!(substep_seconds > 0.0f)) {
        substep_seconds = MP_CLIP_DEFAULT_SUBSTEP_SECONDS;
    }
    window = (float)MP_CLIP_ENTRY_WINDOW_SUBSTEPS * substep_seconds;
    return frames / fps > window;
}

bool mp_clip_event_in_span(int32_t event_frame, float from, float to)
{
    float frame = (float)event_frame;

    return from <= frame && frame < to;
}
