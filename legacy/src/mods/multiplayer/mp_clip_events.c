/* mp_clip_events.c: a body's own clip events, counted and fired. See the header. */
#include "mp_clip_events.h"

#include "mp_cells.h"
#include "mp_clip_rule.h"
#include "mp_signatures_clip.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OBJECT_ACTOR      0x14u
#define ACTOR_NUM_CLIPS   0xC8u
#define ACTOR_CLIP_TABLE  0xE4u
#define CLIP_NUM_EVENTS   0x1Cu
#define CLIP_EVENTS       0x38u
#define CLIP_KEYFRAME     0x3Cu
#define EVENT_STRIDE      0x60u
#define TRACK_KEYFRAME    0x128u

/* A clip count or an event count past these is not a table this engine builds, and a walk that
 * trusted it would read far past it. */
#define CLIPS_MAX  512u
#define EVENTS_MAX 256u

typedef void(__cdecl *dispatch_fn_t)(void *object, int32_t slot, float frames);

static struct {
    bool          installed;
    dispatch_fn_t dispatch;
} events;

bool mp_clip_events_install(void)
{
    if (events.installed) {
        return events.dispatch != NULL;
    }
    events.installed = true;
    events.dispatch  =
        (dispatch_fn_t)mp_signatures_clip_address(MP_CLIP_SITE_DISPATCH_EVENTS);
    if (events.dispatch == NULL) {
        log_warning("the clip event dispatcher did not resolve, so a far body's clip that is moved "
                    "forward fires none of the events it passes; the moves still happen");
        return false;
    }
    return true;
}

bool mp_clip_events_beyond_window(float frames, float fps)
{
    uintptr_t cell    = mp_cells_address(MP_CELL_FRAME_DELTA);
    float     seconds = 0.0f;

    if (cell == 0u || !memory_try_read(cell, &seconds, sizeof seconds)) {
        seconds = 0.0f;   /* the rule falls back to the engine's own 1/32 s */
    }
    return mp_clip_beyond_window(frames, fps, seconds);
}

/* The descriptor of the clip whose keyframe the track plays, found the way the dispatcher finds
 * it: the first clip of the actor with that keyframe. */
static uint32_t clip_of_track(uint32_t object, uint32_t track)
{
    uint32_t actor = 0;
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t keyframe = 0;
    uint32_t i;

    if (object == 0u || track == 0u ||
        !memory_try_read_u32((uintptr_t)track + TRACK_KEYFRAME, &keyframe) || keyframe == 0u ||
        !memory_try_read_u32((uintptr_t)object + OBJECT_ACTOR, &actor) || actor == 0u ||
        !memory_try_read_u32((uintptr_t)actor + ACTOR_NUM_CLIPS, &count) || count > CLIPS_MAX ||
        !memory_try_read_u32((uintptr_t)actor + ACTOR_CLIP_TABLE, &table) || table == 0u) {
        return 0u;
    }
    for (i = 0; i < count; ++i) {
        uint32_t desc = 0;
        uint32_t key  = 0;

        if (memory_try_read_u32((uintptr_t)table + i * 4u, &desc) && desc != 0u &&
            memory_try_read_u32((uintptr_t)desc + CLIP_KEYFRAME, &key) && key == keyframe) {
            return desc;
        }
    }
    return 0u;
}

uint32_t mp_clip_events_between(uint32_t object, uint32_t track, float from, float to)
{
    uint32_t desc = clip_of_track(object, track);
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t found = 0;
    uint32_t i;

    if (desc == 0u || !(to > from) ||
        !memory_try_read_u32((uintptr_t)desc + CLIP_NUM_EVENTS, &count) || count > EVENTS_MAX ||
        !memory_try_read_u32((uintptr_t)desc + CLIP_EVENTS, &table) || table == 0u) {
        return 0u;
    }
    for (i = 0; i < count; ++i) {
        int32_t frame = 0;

        if (memory_try_read((uintptr_t)table + i * EVENT_STRIDE, &frame, sizeof frame) &&
            mp_clip_event_in_span(frame, from, to)) {
            ++found;
        }
    }
    return found;
}

bool mp_clip_events_fire(uint32_t object, uint32_t slot, float frames)
{
    if (!mp_clip_events_install() || object == 0u || !(frames > 0.0f)) {
        return false;
    }
    events.dispatch((void *)(uintptr_t)object, (int32_t)slot, frames);
    return true;
}
