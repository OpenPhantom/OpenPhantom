/* mp_puppet_anim_seek.c: a puppet's track moved to the sender's head, with its clip events taken
 * care of. See the second half of mp_puppet_anim.h.
 *
 * Left mp_puppet_anim.c when the moves had to learn about the events and that file stood near its
 * size limit; the moves were the part that changed, so moving them cost nothing the change did not
 * cost already. The decisions stay there: this file is told what to do and does it.
 */
#include "mp_puppet_anim.h"

#include "mp_bank.h"
#include "mp_clip_events.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One track: the flag word, its rate, the time and the time before the last advance (both in
 * frames), the keyframe it plays and the marker frame, an integer the advance compares as a
 * float. */
#define TRACK_FLAGS      0x000u
#define TRACK_FPS        0x010u
#define TRACK_TIME       0x120u
#define TRACK_TIME_PREV  0x124u
#define TRACK_KEYFRAME   0x128u
#define TRACK_MARKER     0x138u

/* The engine's hold bit on a track. The draw's update leaves a held track where it is, advances
 * nothing and so fires nothing; the engine sets it itself at the end of a clip that releases there
 * and for the hold of a clip on a marker. */
#define TRACK_HELD       0x010u

typedef float(__cdecl *update_track_fn_t)(void *puppet, float dt, uint32_t slot, float frames);

/* A far body's base track held back by this module, for the bank that shows it. */
typedef struct hold {
    bool     active;
    uint32_t object;
    uint32_t track;
    uint32_t keyframe;
} hold_t;

/* The counters are this module's share of the puppet's, in their shape. */
typedef struct seek_state {
    update_track_fn_t         update_track;
    hold_t                    hold[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */
    mp_puppet_anim_counters_t n;
} seek_state_t;

static seek_state_t seek;

static hold_t *hold_of(size_t bank)
{
    return &seek.hold[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

bool mp_puppet_anim_seek_resolve(void)
{
    seek.update_track =
        (update_track_fn_t)mp_signatures_address(MP_SITE_RDPUPPET_UPDATE_TRACK);
    (void)mp_clip_events_install();
    return seek.update_track != NULL;
}

static void note_write_fault(const char *what)
{
    if (seek.n.write_faults == 0u) {
        log_warning("the puppet's %s write was refused; later refusals are counted", what);
    }
    ++seek.n.write_faults;
}

/* A fresh track is put at the sender's playhead through the engine's own advance with a zero time
 * step and the frames as the override: the marker and event latches arm on the way exactly as a
 * real advance would arm them, and the weight ramps by nothing. The slot is read again after the
 * start, because the player chose it. Inside the window the track is left at frame 0, so that the
 * draw fires the clip's first events as it would for the sender's own start a moment ago; the
 * state path's tracking then brings it up to the sender, with the events on the way. */
void mp_puppet_anim_seed(uint32_t object, uint32_t slot_offset, float frames)
{
    uint32_t puppet = 0;
    uint32_t slot   = 0;
    uint32_t track  = 0;
    float    fps    = 0.0f;

    if (!(frames > 0.0f) || !mp_puppet_anim_track_of(object, slot_offset, &puppet, &slot, &track)) {
        return;
    }
    if (!memory_try_read((uintptr_t)track + TRACK_FPS, &fps, sizeof fps) ||
        !mp_clip_events_beyond_window(frames, fps)) {
        ++seek.n.seeds_in_window;
        return;
    }
    if (seek.update_track == NULL) {
        return;
    }
    seek.n.seed_events_left += mp_clip_events_between(object, track, 0.0f, frames);
    (void)seek.update_track((void *)(uintptr_t)puppet, 0.0f, slot, frames);
    ++seek.n.seeds;
}

/* Both time words at once: the advance reads the second and the compositor the first, and a
 * pose build can run between this write and the next advance. Only for a build without the
 * track advance, which then latches nothing and fires nothing. */
static void write_time_words(const mp_puppet_anim_channel_t *channel, float frames)
{
    if (patch_write_f32(channel->track + TRACK_TIME, frames) != PATCH_RESULT_OK ||
        patch_write_f32(channel->track + TRACK_TIME_PREV, frames) != PATCH_RESULT_OK) {
        note_write_fault("track time");
        return;
    }
    ++seek.n.seeks;
}

/* A running track is moved forwards through the engine's own advance, by the difference and
 * with a zero time step, exactly as a fresh track is seeded: the advance adds the difference to
 * the time it carried, so the track lands on the target, and the marker and parameter event
 * latches arm when the marker frame lies inside the span, which a direct write of the time words
 * would skip. On the base channel that latch is what strips a corpse's collision at the death
 * clip's marker; a seek that jumped it would leave the far player's corpse solid. The cursors
 * need no help forwards: the compositor walks them up on the next frame.
 *
 * Then the dispatcher, over the same span and after the advance, which is where the draw calls
 * it: the events the jump passed fire once, as the draw would have fired them had it advanced the
 * track that far itself. On a clip that loops, a sender a little past the wrap is ahead by the
 * distance measured around it, and the advance wraps the track there as the draw would.
 *
 * Only when the track moved. The engine holds a track itself at the end of a clip that releases
 * there, at the end of a fade that keeps its slot and for a held clip, and the advance then moves
 * nothing and still answers the frames it was handed, so its answer says nothing. The dispatcher
 * over a head that stood still would fire the span behind it, events the draw already fired. */
static void seek_forward(uint32_t object, const mp_puppet_anim_channel_t *channel,
                         const mp_puppet_anim_decision_t *decision)
{
    float   delta  = decision->frames - channel->head;
    float   before = 0.0f;
    float   time   = 0.0f;
    int32_t marker = 0;

    if (!(delta > 0.0f) && mp_puppet_anim_loops(channel->mode_flags)) {
        delta = decision->distance;
    }
    if (!(delta > 0.0f)) {
        return;   /* the track carries the target already; the advance would refuse a zero step */
    }
    if (seek.update_track == NULL) {
        write_time_words(channel, decision->frames);
        return;
    }
    if (!memory_try_read((uintptr_t)channel->track + TRACK_TIME, &before, sizeof before)) {
        return;
    }
    (void)seek.update_track((void *)(uintptr_t)channel->puppet, 0.0f, channel->slot, delta);
    ++seek.n.seeks;
    if (!memory_try_read((uintptr_t)channel->track + TRACK_TIME, &time, sizeof time)) {
        return;
    }
    if (time == before) {
        ++seek.n.held_in_place;
        return;
    }
    if (memory_try_read((uintptr_t)channel->track + TRACK_MARKER, &marker, sizeof marker) &&
        channel->head < (float)marker && (float)marker <= channel->head + delta) {
        ++seek.n.seeks_across_marker;
    }
    if (mp_clip_events_fire(object, channel->slot, delta)) {
        seek.n.events_fired += mp_clip_events_between(object, channel->track, time - delta, time);
        ++seek.n.jumps_fired;
    } else {
        ++seek.n.no_dispatch;
    }
}

/* The engine never advances a track backwards, and a head written back makes the next advance
 * fire the span from there once more, a second footfall or a second clang. So the track is held
 * where it is, by the engine's own hold bit, until the sender's head has come up to it; the pose
 * stands for those few frames instead of jumping back. */
static void hold_back(size_t bank, uint32_t object, const mp_puppet_anim_channel_t *channel,
                      float frames)
{
    hold_t  *h        = hold_of(bank);
    uint32_t flags    = 0;
    uint32_t keyframe = 0;

    if (h->active) {
        return;   /* held since an earlier substep, and still wanted back */
    }
    if (!memory_try_read_u32((uintptr_t)channel->track + TRACK_FLAGS, &flags) ||
        !memory_try_read_u32((uintptr_t)channel->track + TRACK_KEYFRAME, &keyframe) ||
        patch_write_u32((uintptr_t)channel->track + TRACK_FLAGS, flags | TRACK_HELD) !=
            PATCH_RESULT_OK) {
        note_write_fault("track hold");
        return;
    }
    seek.n.events_not_twice +=
        mp_clip_events_between(object, channel->track, frames, channel->head);
    h->active   = true;
    h->object   = object;
    h->track    = channel->track;
    h->keyframe = keyframe;
    ++seek.n.holds;
}

/* The distance is what the decision measured, not what the move covers: the target is clamped
 * away from the clip's end, and a histogram of the clamped value would describe the clamp rather
 * than the drift. Its sign says the way, because on a clip that loops a sender just past the wrap
 * has the smaller head and is ahead all the same. */
void mp_puppet_anim_seek(size_t bank, uint32_t object, const mp_puppet_anim_channel_t *channel,
                         const mp_puppet_anim_decision_t *decision)
{
    if (channel == NULL || decision == NULL || !channel->live) {
        return;
    }
    ++seek.n.seek_bucket[mp_puppet_anim_seek_bucket(decision->distance)];
    if (decision->distance >= 0.0f) {
        seek_forward(object, channel, decision);
    } else {
        ++seek.n.seeks_back;
        hold_back(bank, object, channel, decision->frames);
    }
}

/* A hold is released by clearing the bit it set, and only on the track it set it on: the same
 * object, the same address and the same keyframe. Another body in the bank drops it unwritten. */
void mp_puppet_anim_settle_hold(size_t bank, uint32_t object,
                                const mp_puppet_anim_channel_t *channel,
                                const mp_puppet_anim_decision_t *decision)
{
    hold_t  *h        = hold_of(bank);
    uint32_t flags    = 0;
    uint32_t keyframe = 0;

    if (!h->active) {
        return;
    }
    if (object == h->object && channel != NULL && channel->live && channel->track == h->track &&
        decision != NULL && decision->action == MP_PUPPET_ANIM_SEEK && decision->distance < 0.0f) {
        return;   /* the sender is still behind the held head */
    }
    if (object == h->object && memory_try_read_u32((uintptr_t)h->track + TRACK_FLAGS, &flags) &&
        memory_try_read_u32((uintptr_t)h->track + TRACK_KEYFRAME, &keyframe) &&
        keyframe == h->keyframe && (flags & TRACK_HELD) != 0u) {
        if (patch_write_u32((uintptr_t)h->track + TRACK_FLAGS, flags & ~TRACK_HELD) ==
            PATCH_RESULT_OK) {
            ++seek.n.released;
        } else {
            note_write_fault("track release");
        }
    }
    h->active = false;
}

void mp_puppet_anim_seek_counters(mp_puppet_anim_counters_t *out)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    out->seeds               = seek.n.seeds;
    out->seeks               = seek.n.seeks;
    out->seeks_across_marker = seek.n.seeks_across_marker;
    out->seeks_back          = seek.n.seeks_back;
    out->write_faults        = seek.n.write_faults;
    out->seeds_in_window     = seek.n.seeds_in_window;
    out->seed_events_left    = seek.n.seed_events_left;
    out->events_fired        = seek.n.events_fired;
    out->jumps_fired         = seek.n.jumps_fired;
    out->no_dispatch         = seek.n.no_dispatch;
    out->holds               = seek.n.holds;
    out->events_not_twice    = seek.n.events_not_twice;
    out->released            = seek.n.released;
    out->held_in_place       = seek.n.held_in_place;
    for (i = 0; i < MP_PUPPET_ANIM_SEEK_BUCKETS; ++i) {
        out->seek_bucket[i] = seek.n.seek_bucket[i];
    }
}

void mp_puppet_anim_seek_report(void)
{
    log_info("the far bodies' clip events at a seek: %u fired over %u jump(s) forward, %u jump(s) "
             "with no dispatcher to fire them, %u over a track the engine itself held, which moved "
             "nothing and fired nothing; %u hold(s) instead of going back, which kept %u "
             "event(s) from playing twice, %u released when the sender caught up; %u seed(s) "
             "inside the window started at frame 0, %u past it, which left %u event(s) behind",
             (unsigned)seek.n.events_fired, (unsigned)seek.n.jumps_fired,
             (unsigned)seek.n.no_dispatch, (unsigned)seek.n.held_in_place, (unsigned)seek.n.holds,
             (unsigned)seek.n.events_not_twice, (unsigned)seek.n.released,
             (unsigned)seek.n.seeds_in_window, (unsigned)seek.n.seeds,
             (unsigned)seek.n.seed_events_left);
}
