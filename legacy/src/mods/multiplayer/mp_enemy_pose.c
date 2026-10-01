/* mp_enemy_pose.c: the keyframe player behind an actor, read by arithmetic, moved by two calls. */
#include "mp_enemy_pose.h"

#include "mp_clip_events.h"
#include "mp_clip_rule.h"
#include "mp_enemy_body_rule.h"
#include "mp_enemy_wire.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The body. The clip is read from the body's own field and not from the actor's copy at
 * actor+0x1BC: the pose commit synchronises that copy, and it runs before the collision pass, so
 * a hit reaction clip appears there a substep later than in the body. */
#define B_ACTOR         0x14u   /* the authored asset: model, clip table, node count */
#define B_THING         0x9Cu
#define B_CUR_CLIP      0xE8u
#define B_BASE_SLOT     0xECu   /* an INDEX into the puppet's tracks, -1 when there is none */
#define B_OVERLAY_CLIP  0xF4u
#define B_OVERLAY_SLOT  0xF8u

/* The thing, and the puppet behind it. */
#define THING_ACTOR     0x04u
#define THING_PUPPET    0x18u
#define THING_NODE_ROT  0x24u   /* one entry per node, stride 0x0c, pitch at +0 and yaw at +4 */
#define ACTOR_NODES     0x54u
#define ACTOR_TRACKS    0xC8u
#define ACTOR_CLIP_TABLE 0xE4u
#define CLIP_USAGE      0x18u   /* what a clip is for; 1 is the death */
#define KEYFRAME_FRAMES 0x34u

/* The node layout has a second, independent reading in this tree: the engine's node yaw setter
 * computes thing+0x24 + node*0x0c with `6B C9 0C` for the stride and stores at [eax+ecx+4], and
 * the pitch setter is byte identical except that its store is [eax+ecx]. */
#define NODE_ROT_STRIDE 0x0Cu
#define NODE_ROT_YAW    0x04u

/* The puppet's own track array and one track: its rate, its two playheads in frames, the keyframe
 * it plays and the clip's mode word. The advance adds its frames to +0x124 and writes the sum to
 * +0x120, then copies it back to +0x124. */
#define PUPPET_TRACKS    0x08u
#define TRACK_STRIDE     0x14Cu
#define TRACK_FPS        0x10u
#define TRACK_TIME       0x120u
#define TRACK_TIME_PREV  0x124u
#define TRACK_KEYFRAME   0x128u
#define TRACK_MODE_FLAGS 0x13Cu
#define PUPPET_SLOTS     4

/* The two end modes of a clip: hold at the last frame, or hold and mark the track held. A clip with
 * neither wraps. */
#define MODE_HOLDEND    0x01u
#define MODE_RELEASEEND 0x02u

#define USAGE_DEATH     1
#define CLIPS_MAX       512u

/* How far apart the two playheads are before the report calls them far apart, and how far the
 * host's may lie behind this side's before the report calls it behind, in frames. Below one frame
 * the two sides are rounding against each other. */
#define HEAD_FAR_FRAMES    15.0f
#define HEAD_BEHIND_FRAMES 1.0f

/* The clip player's two modes: 4 crossfades from the clip before, every other value cuts to the
 * new one at full weight. Every site in the NPC code passes 4. A replica's first record cuts, as
 * the engine's savegame restore does, or a corpse built late sinks out of its spawn clip. */
#define PLAY_CLIP_FADE 4
#define PLAY_CLIP_CUT  2

/* Degrees to the wire's sixteen bit angle and back. */
#define ANGLE_SCALE 182.044444f

typedef int32_t(__cdecl *play_clip_fn_t)(void *obj, int32_t clip, int32_t mode);
typedef float(__cdecl *update_track_fn_t)(void *puppet, float dt, uint32_t slot, float frames);

typedef struct enemy_pose_state {
    play_clip_fn_t    play_clip;
    update_track_fn_t update_track;
    uint32_t          clip_starts;
    uint32_t          clip_refused;
    uint32_t          head_far;
    uint32_t          head_behind;
    uint32_t          rotations;

    /* The entry into a running clip. */
    uint32_t          seeks;
    uint32_t          seeks_first;       /* on a replica's first record */
    uint32_t          seeks_restart;     /* on a restart of the clip already playing */
    uint32_t          events_left;       /* clip events the seeks moved past */
    uint32_t          in_window;         /* starts left to run from their first frame */
    uint32_t          ahead;             /* this side's track was as far as the host's already */
    uint32_t          cut_in;            /* first records that cut rather than faded */
    uint32_t          restarts;          /* clips the host began again */
    uint32_t          held_death;        /* death tracks held at their end */
    uint32_t          entry_unread;      /* no base track to enter */
    uint32_t          no_advance;        /* past the window with no track advance resolved */
} enemy_pose_state_t;

static enemy_pose_state_t pose;

bool mp_enemy_pose_install(void)
{
    pose.play_clip    = (play_clip_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_PLAY_CLIP);
    pose.update_track =
        (update_track_fn_t)mp_signatures_address(MP_SITE_RDPUPPET_UPDATE_TRACK);
    if (pose.play_clip == NULL) {
        log_warning("the clip player did not resolve, so a replica will hold whatever pose it was "
                    "left in: it will stand in the right place and not move");
        return false;
    }
    if (pose.update_track == NULL) {
        log_warning("the track advance did not resolve, so a replica starts every clip at its "
                    "first frame, and a corpse built late plays its death from the start");
    }
    return true;
}

/* ==============================================================================================
 * Reaching the three things, all by adding offsets.
 * ============================================================================================ */

static bool thing_of(uint32_t body, uint32_t *out)
{
    return memory_try_read_u32((uintptr_t)body + B_THING, out) && *out != 0u;
}

/* The address of one track, or zero when the slot is empty. */
static uintptr_t track_address(uint32_t body, uint32_t slot_offset, int32_t *slot_out,
                               uint32_t *puppet_out)
{
    uint32_t thing  = 0;
    uint32_t puppet = 0;
    int32_t  slot   = 0;

    if (!memory_try_read_u32((uintptr_t)body + slot_offset, (uint32_t *)&slot) ||
        slot < 0 || slot >= PUPPET_SLOTS) {
        return 0;   /* -1 is the engine's own "no track here", and it is the common case */
    }
    if (!thing_of(body, &thing) ||
        !memory_try_read_u32((uintptr_t)thing + THING_PUPPET, &puppet) || puppet == 0u) {
        return 0;
    }
    if (slot_out != NULL) {
        *slot_out = slot;
    }
    if (puppet_out != NULL) {
        *puppet_out = puppet;
    }
    return (uintptr_t)puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_STRIDE;
}

bool mp_enemy_pose_base_track(uint32_t body, mp_enemy_pose_track_t *out)
{
    uintptr_t track;
    uint32_t  frames = 0;

    if (out == NULL) {
        return false;
    }
    track = track_address(body, B_BASE_SLOT, &out->slot, &out->puppet);
    out->track = (uint32_t)track;
    if (track == 0 || !memory_try_read_u32(track + TRACK_KEYFRAME, &out->keyframe) ||
        out->keyframe == 0u ||
        !memory_try_read(track + TRACK_FPS, &out->fps, sizeof out->fps) ||
        !memory_try_read(track + TRACK_TIME, &out->head, sizeof out->head) ||
        !memory_try_read(track + TRACK_TIME_PREV, &out->time_prev, sizeof out->time_prev) ||
        !memory_try_read_u32(track + TRACK_MODE_FLAGS, &out->mode_flags)) {
        return false;
    }
    if (out->head != out->head || out->time_prev != out->time_prev) {
        return false;   /* a NaN playhead is not a position */
    }
    if (out->time_prev > out->head) {
        out->head = out->time_prev;
    }
    out->num_frames = memory_try_read_u32((uintptr_t)out->keyframe + KEYFRAME_FRAMES, &frames)
                          ? (float)frames
                          : 0.0f;
    out->loops = (out->mode_flags & (MODE_HOLDEND | MODE_RELEASEEND)) == 0u;
    return true;
}

/* How many nodes the model behind a body has, which is the bound both directions need. */
static uint32_t node_count(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t asset = 0;
    uint32_t count = 0;

    if (!thing_of(body, &thing) ||
        !memory_try_read_u32((uintptr_t)thing + THING_ACTOR, &asset) || asset == 0u ||
        !memory_try_read_u32((uintptr_t)asset + ACTOR_NODES, &count)) {
        return 0;
    }
    return count;
}

static uintptr_t node_rotation_address(uint32_t body, uint32_t node)
{
    uint32_t thing = 0;
    uint32_t table = 0;

    if (node >= node_count(body) || !thing_of(body, &thing) ||
        !memory_try_read_u32((uintptr_t)thing + THING_NODE_ROT, &table) || table == 0u) {
        return 0;
    }
    return (uintptr_t)table + (uintptr_t)node * NODE_ROT_STRIDE;
}

static bool read_playhead(uint32_t body, uint32_t slot_offset, uint32_t *out)
{
    uintptr_t track  = track_address(body, slot_offset, NULL, NULL);
    float     frames = 0.0f;
    int32_t   ticks;

    if (track == 0 || !memory_try_read(track + TRACK_TIME, &frames, sizeof frames)) {
        return false;
    }
    if (frames != frames || frames < 0.0f) {
        return false;   /* a NaN playhead is not a position, and neither is a negative one */
    }
    /* Sixteenths of a frame in a u16, which reaches 4096 frames. */
    ticks = (int32_t)(frames * 16.0f + 0.5f);
    *out  = (ticks > 65535) ? 65535u : (uint32_t)ticks;
    return true;
}

/* ==============================================================================================
 * Reading.
 * ============================================================================================ */

/* The node rotations, which are what a head turning to watch somebody actually is.
 *
 * They were left out of the first version of this feature, and the omission was not merely a
 * missing field: the budget figure that made the enemy record look cheap was measured on records
 * that never carried a rotation, so the number and the gap were the same mistake seen twice.
 *
 * Only nodes that are actually turned travel. A model has up to sixty four and almost all of them
 * sit at zero, so sending the array would cost twenty times what sending the difference does. */
static void read_twists(uint32_t body, mp_enemy_record_t *record)
{
    uint32_t nodes = node_count(body);
    uint32_t node;
    uint32_t taken = 0;

    if (nodes == 0u || nodes > 256u) {
        return;
    }
    for (node = 0; node < nodes && taken < MP_ENEMY_MAX_TWISTS; ++node) {
        uintptr_t at = node_rotation_address(body, node);
        float     angles[2];

        if (at == 0 || !memory_try_read(at, angles, sizeof angles)) {
            continue;
        }
        if (angles[0] == 0.0f && angles[1] == 0.0f) {
            continue;
        }
        if (angles[0] != angles[0] || angles[1] != angles[1]) {
            continue;   /* a NaN angle is not a rotation */
        }
        record->twist[taken].node  = (uint8_t)node;
        record->twist[taken].pitch = (uint16_t)((int32_t)(angles[0] * ANGLE_SCALE) & 0xFFFF);
        record->twist[taken].yaw   = (uint16_t)((int32_t)(angles[1] * ANGLE_SCALE) & 0xFFFF);
        ++taken;
    }
    record->value[MP_ENEMY_F_TWISTS] = taken;
}

void mp_enemy_pose_read(uint32_t body, mp_enemy_record_t *record)
{
    uint32_t raw = 0;

    if (record == NULL) {
        return;
    }
    if (memory_try_read_u32((uintptr_t)body + B_CUR_CLIP, &raw)) {
        record->value[MP_ENEMY_F_CLIP] = raw & 0xFFu;
    }
    if (read_playhead(body, B_BASE_SLOT, &raw)) {
        record->value[MP_ENEMY_F_HEAD] = raw;
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_HEAD;
    }
    if (read_playhead(body, B_OVERLAY_SLOT, &raw)) {
        uint32_t clip = 0;

        record->value[MP_ENEMY_F_OVERLAY_HEAD] = raw;
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_OVERLAY;
        if (memory_try_read_u32((uintptr_t)body + B_OVERLAY_CLIP, &clip)) {
            record->value[MP_ENEMY_F_OVERLAY_CLIP] = clip & 0xFFu;
        }
    }
    read_twists(body, record);
}

/* ==============================================================================================
 * Applying.
 * ============================================================================================ */

static void apply_twists(uint32_t body, const mp_enemy_record_t *record)
{
    uint32_t count = record->value[MP_ENEMY_F_TWISTS];
    uint32_t i;

    if (count > MP_ENEMY_MAX_TWISTS) {
        return;   /* a count the record cannot carry did not come from this build */
    }
    for (i = 0; i < count; ++i) {
        uintptr_t at = node_rotation_address(body, record->twist[i].node);
        float     angles[2];

        /* The bound is checked here rather than trusted, and the engine's own setters check the
         * same thing, so a node index off the wire cannot reach past the array either way. */
        if (at == 0) {
            continue;
        }
        angles[0] = (float)(int16_t)record->twist[i].pitch / ANGLE_SCALE;
        angles[1] = (float)(int16_t)record->twist[i].yaw / ANGLE_SCALE;
        if (memory_try_write(at, angles, sizeof angles)) {
            ++pose.rotations;
        }
    }
}

/* The clip the engine's death arm holds: the first of the model's clips whose use is the death,
 * and clip 0 when none is, which is what the engine's own search answers. */
static int32_t death_clip(uint32_t body)
{
    uint32_t asset = 0;
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t i;

    if (!memory_try_read_u32((uintptr_t)body + B_ACTOR, &asset) || asset == 0u ||
        !memory_try_read_u32((uintptr_t)asset + ACTOR_TRACKS, &count) || count > CLIPS_MAX ||
        !memory_try_read_u32((uintptr_t)asset + ACTOR_CLIP_TABLE, &table) || table == 0u) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        uint32_t desc  = 0;
        int32_t  usage = 0;

        if (memory_try_read_u32((uintptr_t)table + i * 4u, &desc) && desc != 0u &&
            memory_try_read((uintptr_t)desc + CLIP_USAGE, &usage, sizeof usage) &&
            usage == USAGE_DEATH) {
            return (int32_t)i;
        }
    }
    return 0;
}

/* The host's death arm holds the track of the death clip at its end, whatever the clip's own
 * mode word says, and it runs only on the host. A death clip authored without the hold would wrap
 * on the replica and play the death in a loop. So while the host reports a death state and the
 * replica plays that clip, its base track gets the same bit. Before the entry, so that a corpse
 * entered at its last frame stops there instead of wrapping to its first. */
static void hold_death_track(uint32_t body, const mp_enemy_record_t *record)
{
    uint32_t              state = record->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK;
    uint32_t              current = 0;
    uint32_t              flags;
    mp_enemy_pose_track_t track;

    if (state < MP_ENEMY_STATE_DEATH || state > MP_ENEMY_STATE_CORPSE ||
        !mp_enemy_pose_base_track(body, &track) || (track.mode_flags & MODE_HOLDEND) != 0u ||
        !memory_try_read_u32((uintptr_t)body + B_CUR_CLIP, &current) ||
        (int32_t)current != death_clip(body)) {
        return;
    }
    flags = track.mode_flags | MODE_HOLDEND;
    if (memory_try_write((uintptr_t)track.track + TRACK_MODE_FLAGS, &flags, sizeof flags)) {
        ++pose.held_death;
    }
}

/* Puts the base track where the host's head is, once, when that lies past the window. The advance
 * is relative: it adds its frames to the time the track would advance from, which after a fresh
 * start is 0 and on a replica's first record is wherever its spawn clip had got to. The slot is
 * read now, after the start, because the player chose it. With a zero time step the advance moves
 * the head and arms the marker and parameter latches on the way, as a real one would, and fires
 * no event: the events it moves past are old news and are only counted. */
static void enter(uint32_t body, uint32_t wanted, bool first, bool restart)
{
    mp_enemy_pose_track_t track;
    float                 head = (float)wanted / 16.0f;
    float                 frames;

    if (!mp_enemy_pose_base_track(body, &track)) {
        ++pose.entry_unread;
        return;
    }
    if (!mp_clip_events_beyond_window(head, track.fps)) {
        ++pose.in_window;
        return;
    }
    frames = head - track.time_prev;
    if (!(frames > 0.0f)) {
        ++pose.ahead;
        return;
    }
    if (pose.update_track == NULL) {
        ++pose.no_advance;
        return;
    }
    pose.events_left += mp_clip_events_between(body, track.track, track.time_prev, head);
    (void)pose.update_track((void *)(uintptr_t)track.puppet, 0.0f, (uint32_t)track.slot, frames);
    ++pose.seeks;
    pose.seeks_first += first ? 1u : 0u;
    pose.seeks_restart += restart ? 1u : 0u;
}

/* How far the host's head lies from this side's clock, around the loop with the length of the
 * clip as this side has it. A measurement for the report; nothing acts on it. */
static void measure(uint32_t body, uint32_t wanted, bool entered)
{
    mp_enemy_pose_track_t track;
    float                 distance;

    if (!mp_enemy_pose_base_track(body, &track)) {
        return;
    }
    distance = mp_clip_head_distance((float)wanted / 16.0f, track.head, track.loops,
                                     track.num_frames);
    if (fabsf(distance) > HEAD_FAR_FRAMES) {
        ++pose.head_far;
    }
    if (!entered && distance < -HEAD_BEHIND_FRAMES) {
        ++pose.head_behind;
    }
}

static uint32_t starts_of(const mp_enemy_record_t *record)
{
    mp_enemy_body_state_t body;

    mp_enemy_body_unpack(record->value[MP_ENEMY_F_BODY], &body);
    return body.starts;
}

void mp_enemy_pose_apply(uint32_t body, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous)
{
    uint32_t asset   = 0;
    int32_t  limit   = 0;
    uint32_t current = 0;
    int32_t  clip;
    bool     first;
    bool     restarted;
    bool     started = false;

    if (record == NULL) {
        return;
    }
    apply_twists(body, record);

    if (pose.play_clip == NULL) {
        return;
    }
    clip = (int32_t)(record->value[MP_ENEMY_F_CLIP] & 0xFFu);

    /* Bounded against the model's own track count, because the engine's pose commit does that and
     * the play call does not. Out of range there reaches a stub that used to be an assert and is
     * now an empty function, so nothing would say the clip was wrong. */
    if (!memory_try_read_u32((uintptr_t)body + B_ACTOR, &asset) || asset == 0u ||
        !memory_try_read_u32((uintptr_t)asset + ACTOR_TRACKS, (uint32_t *)&limit)) {
        return;
    }
    if (clip < 0 || clip > limit) {
        ++pose.clip_refused;
        return;
    }
    if (!memory_try_read_u32((uintptr_t)body + B_CUR_CLIP, &current)) {
        return;
    }
    first = previous == NULL ||
            previous->value[MP_ENEMY_F_GENERATION] != record->value[MP_ENEMY_F_GENERATION];
    restarted = !first && starts_of(record) != starts_of(previous);

    if ((int32_t)current != clip || restarted) {
        int32_t mode = (first && (int32_t)current != clip) ? PLAY_CLIP_CUT : PLAY_CLIP_FADE;

        started = pose.play_clip((void *)(uintptr_t)body, clip, mode) != 0;
        ++pose.clip_starts;
        pose.cut_in += (mode == PLAY_CLIP_CUT) ? 1u : 0u;
        pose.restarts += ((int32_t)current == clip) ? 1u : 0u;
    }
    hold_death_track(body, record);

    /* No bit, no playhead. A field the sender never filled is not frame 0: a playhead of zero is a
     * real value and an absent one is not, and without the bit they are the same sixteen zero bits.
     * Seeking to such a field pinned the body to the first frame of its clip on every packet, and a
     * third of the seeks in the first field run were exactly this. */
    if ((record->value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_HEAD) == 0u) {
        return;
    }
    if (started || first) {
        enter(body, record->value[MP_ENEMY_F_HEAD] & 0xFFFFu, first,
              restarted && (int32_t)current == clip);
    }
    measure(body, record->value[MP_ENEMY_F_HEAD] & 0xFFFFu, started || first);
}

/* The first four numbers keep their words, so that a comparison of two runs' reports finds them.
 * The playheads are measured around the loop now, with the clip's length as this side has it, and
 * the last number is the host's head behind this side's clock: nothing is written back any more. */
void mp_enemy_pose_report(void)
{
    log_info("enemies, animation: %u clip start(s), %u substep(s) with the playheads far apart, "
             "%u clip(s) refused for naming a track the model has not got, %u node rotation(s) "
             "written, %u record(s) whose head lay more than a frame behind this side's on a clip "
             "already playing",
             (unsigned)pose.clip_starts, (unsigned)pose.head_far, (unsigned)pose.clip_refused,
             (unsigned)pose.rotations, (unsigned)pose.head_behind);
    log_info("enemies, the entry into a running clip: %u seek(s) to the host's head past the "
             "window of %u substeps (%u on a replica's first record, %u on a restart of the same "
             "clip), %u clip event(s) left behind by them; %u start(s) left to run from their "
             "first frame inside the window, %u where this side was as far already; %u first "
             "record(s) cut in rather than faded, %u restart(s) of a clip the host began again; "
             "%u death track(s) held at their end as the host's death arm holds them; %u with no "
             "base track to enter, %u past the window with no track advance",
             (unsigned)pose.seeks, (unsigned)MP_CLIP_ENTRY_WINDOW_SUBSTEPS,
             (unsigned)pose.seeks_first, (unsigned)pose.seeks_restart,
             (unsigned)pose.events_left, (unsigned)pose.in_window, (unsigned)pose.ahead,
             (unsigned)pose.cut_in, (unsigned)pose.restarts, (unsigned)pose.held_death,
             (unsigned)pose.entry_unread, (unsigned)pose.no_advance);
}
