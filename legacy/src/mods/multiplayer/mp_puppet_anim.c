/* mp_puppet_anim.c: the puppet's two animation channels, decided without the engine and applied
 * through it.
 *
 * SIZE NOTE: a little under 800 lines, in two halves of one subject. The first half is
 * arithmetic over the numbers the header describes and calls nothing; the second half reads the
 * puppet's tracks, asks the first half, and carries the answer out through the engine's clip
 * players. The split is what lets the decisions be pinned in a test with no game in the process.
 * The swing table read has already left for mp_puppet_anim_engine.c, and the moves of a running
 * track, the seed, the seek forward with its events and the hold instead of a seek back, for
 * mp_puppet_anim_seek.c, where the clip events are taken care of. The next seam is between the
 * two halves: the engine half is a module of its own, the track reads and the clip players, and
 * the decisions stay here where the test finds them.
 *
 * Why the base channel is seeded and tracked rather than restarted: the puppet's track advances
 * in the engine's draw loop at this machine's frame rate, exactly as the sender's does on its
 * machine, so once the two agree they stay in step and only drift by the two clocks. A clip is
 * therefore started with the sender's playhead, and a running clip is only touched when the two
 * heads have drifted apart by more than a frame and a half. Restarting a clip every substep would
 * hold it at frame zero.
 *
 * Why an overlay is judged by the puppet's slot and not by a remembered ordinal: the object keeps
 * the last overlay ordinal after the clip has ended, so "is it playing" can only be answered by
 * the track the slot names, and only by whether that track still carries the ordinal's keyframe.
 * That is the same test the engine's own overlay player makes before retiring a slot.
 */
#include "mp_puppet_anim.h"

#include "mp_bank.h"
#include "mp_clip_rule.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A wire head this far behind the last one on the same clip is a restart of a clip that does not
 * wrap; a puppet head this far from the wire's is seeked; an overlay this close to its end on the
 * sender is not worth starting, because the puppet's own copy retires at the same moment and a
 * start now would replay its last frames; and an event this many ticks ahead of the render tick
 * is performed at once. The tolerance and the end margins are design values and not measurements:
 * the two tracks advance at their machines' frame rates and drift only by the clocks, and a
 * threshold below a frame would seek on the quantisation of the wire alone. */
#define RESTART_FRAMES   2.0f
#define TRACK_TOLERANCE  1.5f
#define END_MARGIN       2.0f
#define SEED_END_MARGIN  0.5f
#define FORCED_TICKS     32

#define DEGREES_TO_RADIANS 0.017453292f

/* The overlay ordinals only a starter or an event begins: the four draw and holster clips the
 * weapon setter plays, the midair attack the swing starter plays, the parry and block clips the
 * sabre starters play, and the push clip the push starter plays. */
#define OVERLAY_DRAW_FIRST  0x20u
#define OVERLAY_DRAW_LAST   0x23u
#define OVERLAY_MIDAIR      0x55u
#define OVERLAY_PARRY_FIRST 0x5Du
#define OVERLAY_PARRY_LAST  0x5Fu
#define OVERLAY_BLOCK_FIRST 0x60u
#define OVERLAY_BLOCK_LAST  0x69u
#define OVERLAY_FORCE_PUSH  0x71u

/* ==============================================================================================
 * The decisions. Nothing below this banner touches the engine.
 * ============================================================================================ */

static bool in_swing_table(const mp_puppet_anim_exclusions_t *exclusions, uint32_t clip)
{
    size_t row;

    if (exclusions == NULL) {
        return false;
    }
    for (row = 0; row < exclusions->swing_count && row < MP_PUPPET_ANIM_SWING_ROWS; ++row) {
        if (exclusions->swing_clip[row] == clip) {
            return true;
        }
    }
    return false;
}

bool mp_puppet_anim_base_event_owned(const mp_puppet_anim_exclusions_t *exclusions, uint32_t clip)
{
    /* The midair row's clip plays on the overlay channel; on the base channel it is nobody's. */
    return clip != OVERLAY_MIDAIR && in_swing_table(exclusions, clip);
}

bool mp_puppet_anim_overlay_event_owned(const mp_puppet_anim_exclusions_t *exclusions,
                                        uint32_t clip)
{
    (void)exclusions;
    return (clip >= OVERLAY_DRAW_FIRST && clip <= OVERLAY_DRAW_LAST) || clip == OVERLAY_MIDAIR ||
           (clip >= OVERLAY_PARRY_FIRST && clip <= OVERLAY_PARRY_LAST) ||
           (clip >= OVERLAY_BLOCK_FIRST && clip <= OVERLAY_BLOCK_LAST) ||
           clip == OVERLAY_FORCE_PUSH;
}

bool mp_puppet_anim_overlay_seeds(uint32_t clip)
{
    return !(clip >= OVERLAY_DRAW_FIRST && clip <= OVERLAY_DRAW_LAST) &&
           clip != OVERLAY_FORCE_PUSH;
}

bool mp_puppet_anim_loops(uint32_t mode_flags)
{
    return (mode_flags & (MP_PUPPET_ANIM_MODE_HOLDEND | MP_PUPPET_ANIM_MODE_RELEASEEND)) == 0u;
}

bool mp_puppet_anim_track_free(const uint32_t flags[MP_PUPPET_ANIM_TRACKS])
{
    size_t slot;

    if (flags == NULL) {
        return false;
    }
    for (slot = 0; slot < MP_PUPPET_ANIM_TRACKS; ++slot) {
        if (flags[slot] == 0u) {
            return true;
        }
    }
    for (slot = 0; slot < MP_PUPPET_ANIM_TRACKS; ++slot) {
        if ((flags[slot] & MP_PUPPET_ANIM_TRACK_FADEOUT) != 0u &&
            (flags[slot] & (MP_PUPPET_ANIM_TRACK_ENDHOLD | MP_PUPPET_ANIM_TRACK_KEEPSLOT)) == 0u) {
            return true;
        }
    }
    return false;
}

float mp_puppet_anim_seed_frames(float head, float num_frames)
{
    float limit = num_frames - SEED_END_MARGIN;

    if (!(head > 0.0f) || !(limit > 0.0f)) {
        return 0.0f;
    }
    return head < limit ? head : limit;
}

static void decide_start(mp_puppet_anim_decision_t *out, int32_t mode, float frames)
{
    out->action = MP_PUPPET_ANIM_START;
    out->mode   = mode;
    out->frames = frames;
}

void mp_puppet_anim_base_decide(const mp_puppet_anim_base_in_t *in,
                                mp_puppet_anim_decision_t *out)
{
    int32_t mode;
    float   seed;

    memset(out, 0, sizeof *out);
    if (in == NULL) {
        return;
    }
    if (in->event_owned || !in->wire_live) {
        out->action = MP_PUPPET_ANIM_RECORD;
        return;
    }
    /* A cut is what the sender did only when its bit is clear in the sample right after the
     * last one applied; across a gap the bit may have been set in a sample that was lost, and
     * a crossfade where the sender cut is the softer of the two mistakes. */
    mode = (in->faded || !in->contiguous) ? MP_PUPPET_ANIM_MODE_FADE : MP_PUPPET_ANIM_MODE_CUT;
    seed = mp_puppet_anim_seed_frames(in->wire_head, in->num_frames);

    if ((int32_t)in->wire_clip != in->applied_clip) {
        decide_start(out, mode, seed);
        return;
    }
    /* The same clip again with the head thrown back: a restart on the sender, unless the clip
     * wraps, in which case the head came round on its own and the puppet's will too. */
    if (!in->loops && in->applied_head >= 0.0f &&
        in->wire_head < in->applied_head - RESTART_FRAMES) {
        decide_start(out, mode, seed);
        return;
    }
    if (in->puppet_head < 0.0f) {
        return;
    }
    {
        /* Taken around the wrap for a looping clip, so that a head just past the end and one
         * just before it are a frame apart, not a clip apart. */
        float distance =
            mp_clip_head_distance(in->wire_head, in->puppet_head, in->loops, in->num_frames);

        if (fabsf(distance) <= TRACK_TOLERANCE) {
            return;
        }
        out->action   = MP_PUPPET_ANIM_SEEK;
        out->distance = distance;
        out->frames   = in->wire_head;
        if (in->num_frames > SEED_END_MARGIN && out->frames > in->num_frames - SEED_END_MARGIN) {
            out->frames = in->num_frames - SEED_END_MARGIN;
        }
        if (out->frames < 0.0f) {
            out->frames = 0.0f;
        }
    }
}

/* Edges in frames: half the tolerance, the tolerance itself, and then doubling. The first two
 * buckets are the ones a drift lives in; anything past the fourth is a track that had lost its
 * place rather than crept away from it. */
size_t mp_puppet_anim_seek_bucket(float distance)
{
    static const float EDGE[MP_PUPPET_ANIM_SEEK_BUCKETS - 1u] = { 2.0f, 4.0f, 8.0f, 16.0f };
    float              span = distance < 0.0f ? -distance : distance;
    size_t             index;

    if (span != span) {
        return MP_PUPPET_ANIM_SEEK_BUCKETS - 1u;   /* no number is the widest answer there is */
    }
    for (index = 0; index < MP_PUPPET_ANIM_SEEK_BUCKETS - 1u; ++index) {
        if (span < EDGE[index]) {
            return index;
        }
    }
    return MP_PUPPET_ANIM_SEEK_BUCKETS - 1u;
}

void mp_puppet_anim_overlay_decide(const mp_puppet_anim_overlay_in_t *in,
                                   mp_puppet_anim_decision_t *out)
{
    float seed;

    memset(out, 0, sizeof *out);
    if (in == NULL) {
        return;
    }
    if (!in->wire_live) {
        /* Only what the state path started itself is stopped here. An overlay a starter or an
         * event began retires itself as a one shot when its clip ends, and an aux that is still
         * running is waiting for that clip's marker. */
        out->action = (in->puppet_clip >= 0 && !in->aux_running && !in->puppet_event_owned)
                          ? MP_PUPPET_ANIM_STOP
                          : MP_PUPPET_ANIM_RECORD;
        return;
    }
    if (in->event_owned) {
        out->action = MP_PUPPET_ANIM_RECORD;
        return;
    }
    /* No start of any kind over the overlay a running aux waits on: the overlay player would
     * fade that track out and move the slot to the new one, and the aux would then wait for the
     * marker of a clip that may carry none, refusing every later weapon change and push. An aux
     * whose own track has already retired stands in nobody's way. */
    if (in->aux_running && in->puppet_clip >= 0) {
        out->action   = MP_PUPPET_ANIM_RECORD;
        out->withheld = true;
        return;
    }
    seed = mp_puppet_anim_overlay_seeds(in->wire_clip)
               ? mp_puppet_anim_seed_frames(in->wire_head, in->num_frames)
               : 0.0f;
    if (in->puppet_clip == (int32_t)in->wire_clip) {
        /* A restart is a cut at full weight, which is how the engine's own fire starter
         * restarts the clip for every bolt (mode 2). */
        if (in->applied_head >= 0.0f && in->wire_head < in->applied_head - RESTART_FRAMES) {
            decide_start(out, MP_PUPPET_ANIM_MODE_CUT, seed);
        }
        return;
    }
    if (in->num_frames > 0.0f && in->wire_head >= in->num_frames - END_MARGIN) {
        out->action = MP_PUPPET_ANIM_RECORD;
        return;
    }
    decide_start(out, MP_PUPPET_ANIM_MODE_FADE, seed);
}

mp_puppet_anim_due_t mp_puppet_anim_event_due(uint32_t event_tick, uint32_t render_tick,
                                              bool render_known)
{
    int32_t ahead;

    if (!render_known) {
        return MP_PUPPET_ANIM_DUE;
    }
    ahead = (int32_t)(event_tick - render_tick);
    if (ahead < -1) {
        return MP_PUPPET_ANIM_LATE;
    }
    if (ahead <= 0) {
        return MP_PUPPET_ANIM_DUE;
    }
    if (ahead > FORCED_TICKS) {
        return MP_PUPPET_ANIM_FORCED;
    }
    return MP_PUPPET_ANIM_WAIT;
}

float mp_puppet_anim_wrap360(float degrees)
{
    float wrapped;

    if (degrees != degrees) {
        /* Not a number: the wire never sends one, and it must not become a heading. */
        return 0.0f;
    }
    wrapped = (float)fmod((double)degrees, 360.0);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped >= 360.0f ? 0.0f : wrapped;
}

void mp_puppet_anim_muzzle(const float position[3], const float rotation[3], const float local[3],
                           float out[3])
{
    float sx = sinf(rotation[0] * DEGREES_TO_RADIANS);
    float cx = cosf(rotation[0] * DEGREES_TO_RADIANS);
    float sy = sinf(rotation[1] * DEGREES_TO_RADIANS);
    float cy = cosf(rotation[1] * DEGREES_TO_RADIANS);
    float sz = sinf(rotation[2] * DEGREES_TO_RADIANS);
    float cz = cosf(rotation[2] * DEGREES_TO_RADIANS);
    float col0[3];
    float col1[3];
    float col2[3];
    int   axis;

    col0[0] = -sy * sz * sx + cy * cz;
    col0[1] = cy * sz * sx + sy * cz;
    col0[2] = -sz * cx;
    col1[0] = -sy * cx;
    col1[1] = cy * cx;
    col1[2] = sx;
    col2[0] = sy * cz * sx + cy * sz;
    col2[1] = -sx * cy * cz + sy * sz;
    col2[2] = cz * cx;
    for (axis = 0; axis < 3; ++axis) {
        out[axis] = position[axis] + col0[axis] * local[0] + col1[axis] * local[1] +
                    col2[axis] * local[2];
    }
}

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* The body object: its actor template, its render thing, and the four words the clip players
 * keep on it: the base clip and its slot, the overlay clip and its slot. A slot word is signed and
 * -1 means none; indexing the track array with it would address the puppet's own head. */
#define OBJECT_ACTOR        0x14u
#define OBJECT_THING        0x9Cu
#define OBJECT_CLIP_SLOT    0xECu
#define OBJECT_CUR_OVERLAY  0xF4u
#define OBJECT_OVERLAY_SLOT 0xF8u

/* The actor template: how many clips it carries, the bound the base player tests and the overlay
 * player tests only through an assert, one too far, and the descriptor table; a descriptor names
 * its keyframe, and the keyframe its frame count. */
#define ACTOR_NUM_CLIPS     0xC8u
#define ACTOR_CLIP_TABLE    0xE4u
#define CLIP_KEYFRAME       0x3Cu
#define KEYFRAME_NUM_FRAMES 0x34u

/* The render thing: its puppet, whose four tracks start eight bytes in and are 0x14C apart. */
#define THING_PUPPET        0x18u
#define PUPPET_TRACKS       0x08u
#define TRACK_STRIDE        0x14Cu

/* One track: the flag word, the time and the time before the last advance (both in frames), the
 * keyframe it plays and the clip's mode word. Read out of the track advance, which adds its frames
 * to +0x124 and writes the sum to both time words, and out of the overlay player's retire test,
 * which compares +0x128 with the keyframe at +0x3C of the clip's descriptor. The object's channel
 * words come from the same two players: the base player writes the ordinal at +0xE8 and the slot
 * at +0xEC, the overlay player +0xF4 and +0xF8. */
#define TRACK_FLAGS      0x000u
#define TRACK_TIME       0x120u
#define TRACK_TIME_PREV  0x124u
#define TRACK_KEYFRAME   0x128u
#define TRACK_MODE_FLAGS 0x13Cu

/* The player record's aux action: while one runs, its overlay is its own and is not stopped. */
#define RECORD_AUX_ACTION 0x64u

typedef int32_t(__cdecl *play_clip_fn_t)(void *obj, int32_t clip, int32_t mode);
typedef void(__cdecl *stop_overlay_fn_t)(void *obj, float seconds, int32_t mode);

/* What one far body's two channels last applied. Each far bank shows its own player, whose clips
 * the channels follow; one record for all of them would take every sample of one player for a
 * change after another's. */
typedef struct anim_peer {
    int32_t  applied_clip[2];   /* -1 until first applied; base, overlay */
    float    applied_head[2];
    bool     have_applied_tick;
    uint32_t applied_tick;      /* the sender tick of the sample the base channel last saw */
} anim_peer_t;

typedef struct anim_state {
    play_clip_fn_t    play_clip;
    play_clip_fn_t    play_overlay;
    stop_overlay_fn_t stop_overlay;

    mp_puppet_anim_exclusions_t exclusions;

    anim_peer_t peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */
    int32_t last_refused;      /* the overlay ordinal the guard last refused, logged once */

    mp_puppet_anim_counters_t counters;
} anim_state_t;

static anim_state_t anim;

/* One far body's channels by the bank that shows it. An index that is no far bank lands on the
 * spare at 0, which is what a caller outside every window, a unit test, gets. */
static anim_peer_t *peer_of(size_t bank)
{
    return &anim.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

static bool puppet_of(uint32_t object, uint32_t *puppet)
{
    uint32_t thing = 0;

    return object != 0 && memory_try_read_u32(object + OBJECT_THING, &thing) && thing != 0 &&
           memory_try_read_u32(thing + THING_PUPPET, puppet) && *puppet != 0;
}

bool mp_puppet_anim_track_of(uint32_t object, uint32_t slot_offset, uint32_t *puppet,
                             uint32_t *slot, uint32_t *track)
{
    uint32_t word = 0;

    if (!puppet_of(object, puppet) || !memory_try_read_u32(object + slot_offset, &word)) {
        return false;
    }
    if ((int32_t)word < 0 || word >= MP_PUPPET_ANIM_TRACKS) {
        return false;
    }
    *slot  = word;
    *track = *puppet + PUPPET_TRACKS + word * TRACK_STRIDE;
    return true;
}

static bool clip_keyframe(uint32_t object, uint32_t clip, uint32_t *keyframe, float *num_frames)
{
    uint32_t actor = 0;
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t desc = 0;
    uint32_t frames = 0;

    if (object == 0 || !memory_try_read_u32(object + OBJECT_ACTOR, &actor) || actor == 0 ||
        !memory_try_read_u32(actor + ACTOR_NUM_CLIPS, &count) || clip >= count ||
        !memory_try_read_u32(actor + ACTOR_CLIP_TABLE, &table) || table == 0 ||
        !memory_try_read_u32(table + clip * 4u, &desc) || desc == 0 ||
        !memory_try_read_u32(desc + CLIP_KEYFRAME, keyframe) || *keyframe == 0 ||
        !memory_try_read_u32(*keyframe + KEYFRAME_NUM_FRAMES, &frames)) {
        return false;
    }
    *num_frames = (float)frames;
    return true;
}

bool mp_puppet_anim_clip_exists(uint32_t object, uint32_t clip)
{
    uint32_t actor = 0;
    uint32_t count = 0;

    if (object == 0 || !memory_try_read_u32(object + OBJECT_ACTOR, &actor) || actor == 0 ||
        !memory_try_read_u32(actor + ACTOR_NUM_CLIPS, &count)) {
        return false;
    }
    if (clip >= count) {
        if (anim.counters.refused_ordinals == 0u) {
            log_warning("the wire named clip %u for a puppet whose actor carries %u; refused, "
                        "later refusals are counted", (unsigned)clip, (unsigned)count);
        }
        ++anim.counters.refused_ordinals;
        return false;
    }
    return true;
}

/* What the puppet's own slot on a channel holds, read once per substep. The channel is live only
 * when the slot's track still carries the clip's keyframe: the object keeps the ordinal and the
 * slot index after a track has retired. The head is the larger of the track's two time words, as
 * the sender reads its own: the clip player that carries the locomotion phase across a weapon
 * change writes only the second word, and the advance adds to that second word, so it is the one
 * a seek has to measure its difference from. */
static void read_channel(uint32_t object, uint32_t slot_offset, uint32_t clip,
                         mp_puppet_anim_channel_t *out)
{
    uint32_t flags = 0;
    uint32_t track_key = 0;
    uint32_t clip_key = 0;
    float    time_prev = 0.0f;

    memset(out, 0, sizeof *out);
    out->head = -1.0f;
    if (!mp_puppet_anim_track_of(object, slot_offset, &out->puppet, &out->slot, &out->track) ||
        !memory_try_read_u32(out->track + TRACK_FLAGS, &flags) || flags == 0u ||
        !memory_try_read_u32(out->track + TRACK_KEYFRAME, &track_key) || track_key == 0u ||
        !clip_keyframe(object, clip, &clip_key, &out->num_frames) || clip_key != track_key ||
        !memory_try_read(out->track + TRACK_TIME, &out->head, sizeof out->head) ||
        !memory_try_read(out->track + TRACK_TIME_PREV, &time_prev, sizeof time_prev) ||
        !memory_try_read_u32(out->track + TRACK_MODE_FLAGS, &out->mode_flags)) {
        out->head = -1.0f;
        return;
    }
    if (time_prev > out->head) {
        out->head = time_prev;
    }
    out->live = true;
}

/* The four flag words of the puppet's tracks, which is what the engine's own player searches. */
static bool read_track_flags(uint32_t object, uint32_t flags[MP_PUPPET_ANIM_TRACKS])
{
    uint32_t puppet = 0;
    size_t   slot;

    if (!puppet_of(object, &puppet)) {
        return false;
    }
    for (slot = 0; slot < MP_PUPPET_ANIM_TRACKS; ++slot) {
        if (!memory_try_read_u32(puppet + PUPPET_TRACKS + slot * TRACK_STRIDE + TRACK_FLAGS,
                             &flags[slot])) {
            return false;
        }
    }
    return true;
}

bool mp_puppet_anim_overlay_track_free(uint32_t object)
{
    uint32_t flags[MP_PUPPET_ANIM_TRACKS];

    return read_track_flags(object, flags) && mp_puppet_anim_track_free(flags);
}

/* The engine's overlay player finds no track when all four are busy, and then writes its slot
 * into the track before the array. Its own search is repeated here, and a refusal is logged once
 * per ordinal with the four flag words, because a guard that refuses for a wrong reason would
 * silently leave the puppet without overlays. */
static bool overlay_may_start(uint32_t object, uint32_t clip)
{
    uint32_t flags[MP_PUPPET_ANIM_TRACKS];

    if (!read_track_flags(object, flags)) {
        return false;
    }
    if (mp_puppet_anim_track_free(flags)) {
        return true;
    }
    ++anim.counters.guard_refusals;
    if (anim.last_refused != (int32_t)clip) {
        anim.last_refused = (int32_t)clip;
        log_warning("the puppet did not start overlay %u: every track is busy, flags %08X %08X "
                    "%08X %08X; further refusals of this ordinal are counted",
                    (unsigned)clip, (unsigned)flags[0], (unsigned)flags[1], (unsigned)flags[2],
                    (unsigned)flags[3]);
    }
    return false;
}

static void start_base(uint32_t object, uint32_t clip, const mp_puppet_anim_decision_t *decision)
{
    if (anim.play_clip == NULL || !mp_puppet_anim_clip_exists(object, clip)) {
        return;
    }
    if (anim.play_clip((void *)(uintptr_t)object, (int32_t)clip, decision->mode) != 1) {
        return;   /* the player found no track; nothing to seed */
    }
    if (decision->mode == MP_PUPPET_ANIM_MODE_FADE) {
        ++anim.counters.base_starts_fade;
    } else {
        ++anim.counters.base_starts_cut;
    }
    mp_puppet_anim_seed(object, OBJECT_CLIP_SLOT, decision->frames);
}

void mp_puppet_anim_apply_base(size_t bank, uint32_t object, const mp_wire_anim_t *wire,
                               uint32_t tick)
{
    anim_peer_t              *p = peer_of(bank);
    mp_puppet_anim_base_in_t  in;
    mp_puppet_anim_decision_t decision;
    mp_puppet_anim_channel_t  channel;
    uint32_t                  keyframe = 0;
    float                     num_frames = 0.0f;

    if (object == 0 || wire == NULL) {
        return;
    }
    read_channel(object, OBJECT_CLIP_SLOT, wire->clip[0], &channel);
    if (channel.live) {
        num_frames = channel.num_frames;
    } else if (!clip_keyframe(object, wire->clip[0], &keyframe, &num_frames)) {
        num_frames = 0.0f;
    }

    memset(&in, 0, sizeof in);
    in.wire_clip    = wire->clip[0];
    in.wire_head    = (float)wire->track[0] / MP_PUPPET_ANIM_HEAD_SCALE;
    in.wire_live    = (wire->channel_mask & MP_PUPPET_ANIM_BASE_LIVE) != 0u;
    in.faded        = (wire->channel_mask & MP_PUPPET_ANIM_BASE_FADED) != 0u;
    in.contiguous   = p->have_applied_tick && tick == p->applied_tick + 1u;
    in.applied_clip = p->applied_clip[0];
    in.applied_head = p->applied_head[0];
    in.puppet_head  = channel.live ? channel.head : -1.0f;
    in.loops        = channel.live && mp_puppet_anim_loops(channel.mode_flags);
    in.num_frames   = num_frames;
    in.event_owned  = mp_puppet_anim_base_event_owned(&anim.exclusions, wire->clip[0]);

    mp_puppet_anim_base_decide(&in, &decision);
    mp_puppet_anim_settle_hold(bank, object, &channel, &decision);
    if (decision.action == MP_PUPPET_ANIM_START) {
        start_base(object, wire->clip[0], &decision);
    } else if (decision.action == MP_PUPPET_ANIM_SEEK) {
        mp_puppet_anim_seek(bank, object, &channel, &decision);
    }
    p->applied_clip[0]   = (int32_t)wire->clip[0];
    p->applied_head[0]   = in.wire_head;
    p->applied_tick      = tick;
    p->have_applied_tick = true;
}

static void start_overlay(uint32_t object, uint32_t clip, const mp_puppet_anim_decision_t *decision)
{
    if (anim.play_overlay == NULL || !mp_puppet_anim_clip_exists(object, clip) ||
        !overlay_may_start(object, clip)) {
        return;
    }
    (void)anim.play_overlay((void *)(uintptr_t)object, (int32_t)clip, decision->mode);
    ++anim.counters.overlay_starts;
    mp_puppet_anim_seed(object, OBJECT_OVERLAY_SLOT, decision->frames);
}

void mp_puppet_anim_apply_overlay(size_t bank, uint32_t record, uint32_t object,
                                  const mp_wire_anim_t *wire)
{
    anim_peer_t                *p = peer_of(bank);
    mp_puppet_anim_overlay_in_t in;
    mp_puppet_anim_decision_t   decision;
    mp_puppet_anim_channel_t    channel;
    uint32_t                    current = 0;
    uint32_t                    aux = 0;
    uint32_t                    keyframe = 0;
    float                       num_frames = 0.0f;

    if (object == 0 || wire == NULL ||
        !memory_try_read_u32(object + OBJECT_CUR_OVERLAY, &current)) {
        return;
    }
    read_channel(object, OBJECT_OVERLAY_SLOT, current, &channel);
    if (!clip_keyframe(object, wire->clip[1], &keyframe, &num_frames)) {
        num_frames = 0.0f;
    }

    memset(&in, 0, sizeof in);
    in.wire_clip    = wire->clip[1];
    in.wire_head    = (float)wire->track[1] / MP_PUPPET_ANIM_HEAD_SCALE;
    in.wire_live    = (wire->channel_mask & MP_PUPPET_ANIM_OVERLAY_LIVE) != 0u;
    in.applied_clip = p->applied_clip[1];
    in.applied_head = p->applied_head[1];
    in.puppet_clip  = channel.live ? (int32_t)current : -1;
    in.puppet_head  = channel.live ? channel.head : -1.0f;
    /* An aux slot that cannot be read counts as running: a stop on a guess would end the clip an
     * aux is waiting on. */
    in.aux_running  = record == 0 || !memory_try_read_u32(record + RECORD_AUX_ACTION, &aux) ||
                      aux != 0;
    in.num_frames   = num_frames;
    in.event_owned  = mp_puppet_anim_overlay_event_owned(&anim.exclusions, wire->clip[1]);
    in.puppet_event_owned = mp_puppet_anim_overlay_event_owned(&anim.exclusions, current);

    mp_puppet_anim_overlay_decide(&in, &decision);
    if (decision.action == MP_PUPPET_ANIM_START) {
        start_overlay(object, wire->clip[1], &decision);
    } else if (decision.action == MP_PUPPET_ANIM_STOP && anim.stop_overlay != NULL) {
        anim.stop_overlay((void *)(uintptr_t)object, 0.0f, 0);
        ++anim.counters.overlay_stops;
    } else if (decision.withheld) {
        ++anim.counters.overlay_starts_withheld;
    }
    p->applied_clip[1] = (int32_t)wire->clip[1];
    p->applied_head[1] = in.wire_head;
}

void mp_puppet_anim_resolve(void)
{
    size_t bank;
    bool   advance;

    anim.play_clip    = (play_clip_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_PLAY_CLIP);
    anim.play_overlay = (play_clip_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_PLAY_OVERLAY);
    anim.stop_overlay = (stop_overlay_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_STOP_OVERLAY);
    advance           = mp_puppet_anim_seek_resolve();
    for (bank = 0u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_puppet_anim_reset(bank);
    }

    if (anim.play_clip == NULL || anim.play_overlay == NULL || anim.stop_overlay == NULL ||
        !advance) {
        log_warning("the puppet's clip channels are dressed with less than the full set: clip "
                    "player %s, overlay player %s, overlay stop %s, track advance %s; a missing "
                    "one disables its own step and nothing else",
                    anim.play_clip != NULL ? "ok" : "MISSING",
                    anim.play_overlay != NULL ? "ok" : "MISSING",
                    anim.stop_overlay != NULL ? "ok" : "MISSING",
                    advance ? "ok" : "MISSING");
    }
}

void mp_puppet_anim_reset(size_t bank)
{
    anim_peer_t *p = peer_of(bank);

    p->applied_clip[0] = -1;
    p->applied_clip[1] = -1;
    p->applied_head[0]   = -1.0f;
    p->applied_head[1]   = -1.0f;
    p->have_applied_tick = false;
    anim.last_refused      = -1;
    mp_puppet_anim_read_swing_table(&anim.exclusions);
}

void mp_puppet_anim_counters(mp_puppet_anim_counters_t *out)
{
    if (out != NULL) {
        *out = anim.counters;
        mp_puppet_anim_seek_counters(out);
    }
}
