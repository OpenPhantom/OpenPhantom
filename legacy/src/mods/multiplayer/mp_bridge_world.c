/* mp_bridge_world.c: the world's two ends on the bridge; the header carries the cadence and why.
 *
 * SIZE NOTE: over 600 lines. Four things share the file because they share the two histories and
 * the acknowledged baseline: the build of a body out of a block, the host's send, the client's
 * send, and both receives. The codec check of the loopback, which shared none of that, left for
 * mp_bridge_world_check.c when the host's send took its measurements. The next seam is the object's
 * animation read, read_channel and mp_bridge_world_read_object, which reads nothing of the
 * histories; the appearance sampler is deliberately NOT a seam, because it exists to run inside the
 * one read of the hero block this file already makes.
 *
 * Left mp_bridge.c on 2026-09-04, when that file stood at the size limit with the event and body
 * work still to come. The snapshot ends were the seam that took the most shared state with them,
 * both histories and the acknowledged baseline, and left nothing of theirs behind: the bridge
 * hands a session in and takes a decoded snapshot out. Everything sized like a history lives in
 * static storage; two of them are close to a megabyte, far past any stack.
 */
#include "mp_bridge_world.h"

#include "mp_enemy_sync.h"

#include "mp_bank.h"
#include "mp_bridge_far.h"
#include "mp_body.h"
#include "mp_budget_rule.h"
#include "mp_cadence.h"
#include "mp_cells.h"
#include "mp_channel.h"
#include "mp_footstep.h"
#include "mp_level_state_rule.h"
#include "mp_own_body.h"
#include "mp_snapshot_history.h"
#include "mp_stopwatch.h"
#include "mp_twist.h"
#include "mp_world.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hero block fields, the offsets the body module stands on. curClip and curOverlayClip are the
 * block's mirrors of the object's clips, refreshed by the engine's phase one, so a body can be
 * built out of a banked block without touching the object; the live read below prefers the
 * object's own, which the mirrors trail by a substep. */
#define HERO_BLOCK_OBJECT      0x0Cu
#define HERO_BLOCK_HERO_INDEX  0x6Cu
#define HERO_BLOCK_WEAPON      0x84u
#define HERO_BLOCK_POS         0x118u
#define HERO_BLOCK_HEADING     0x2A0u
#define HERO_BLOCK_CUR_CLIP    0x36Cu
#define HERO_BLOCK_CUR_OVERLAY 0x370u
#define HERO_BLOCK_DEAD        0x394u

/* The object, and where its animation lives. The object holds the clip ordinal and the track
 * slot of each channel, and a note the clip player leaves when the base clip changed by a
 * crossfade, which nothing in the engine reads back. The tracks hang off the render thing's
 * puppet, four records of 0x14C bytes; a track's time and its carried time are both playheads
 * in frames and the larger is the current one, because the carry over a weapon change writes
 * only the second. A track belongs to the ordinal the object names when its keyframe pointer is
 * the clip's own, which is the test the engine's overlay player runs before it retires a slot. */
#define BAP_OBJ_ACTOR          0x14u
#define BAP_OBJ_THING          0x9Cu
#define BAP_OBJ_ANIM           0xE8u    /* five dwords: clip, slot0, fading, overlay clip, slot1 */
/* Written by the crossfading clip player (the slot that is fading out) and by the stopping fade,
 * initialised to -1, and read by nothing in the image: a census over every read of this offset
 * found only other structures. So it is a write only breadcrumb and this reader consumes it. */
#define BAP_OBJ_FADING         0xF0u
#define THING_PUPPET           0x18u
#define PUPPET_TRACKS          0x08u
#define TRACK_STRIDE           0x14Cu
#define TRACK_COUNT            4
#define TRACK_FLAGS            0x00u
#define TRACK_TIME             0x120u
#define TRACK_TIME_CARRIED     0x124u
#define TRACK_KEYFRAMES        0x128u
#define ACTOR_NUM_CLIPS        0xC8u
#define ACTOR_CLIP_TABLE       0xE4u
#define CLIP_KEYFRAMES         0x3Cu
#define FADING_NONE            0xFFFFFFFFu

typedef struct mp_bridge_world_state {
    uint32_t client_acked[MP_SESSION_MAX_PEERS];   /* newest tick the host believes each holds */

    uint32_t own_sent;
    uint32_t snapshots_sent;
    uint32_t payloads_lost;   /* the whole payload was refused, so the enemies were abandoned */
    uint32_t snapshots_decoded;
    uint32_t decode_refusals;
    uint32_t enemy_refusals;   /* the snapshot was good and the enemy block was not, so neither
                                * half was taken and the payload goes unacknowledged */
    uint32_t object_refusals;    /* live objects that did not read whole */
    uint32_t bodies_off_the_model;   /* substeps this side's own body was read off its model,
                                      * because a script had stopped the player module */
    uint32_t off_the_model[MP_HERO_MODULE_STATES];   /* the state the stopped module was seen in */
    uint32_t health_refusals;    /* status records that did not read */
    uint32_t fade_reset_refusals;
    bool     health_logged;
    bool     fade_logged;

    /* The client's acknowledgement prefixes and what their bits said. */
    uint32_t acks_sent;
    uint32_t acks_whole;         /* every bit of the window set */
    uint32_t acks_gapped;        /* a tick missing between two held ones */
    uint32_t acks_widest_gap;

    /* THE LOCAL APPEARANCE, sampled where the block is read anyway.
     *
     * The name is not in the hero block; the block holds a pointer to the mounted actor and the
     * actor's own header holds the name. That is the only identity of an appearance that means
     * the same on two machines: the hero index has four values and every added character rides
     * one of them, so two different characters report the same number.
     *
     * Sampling here covers both roles, because the host and the client both build their own body
     * through the same function. What is reported is only that it CHANGED; who sends it is not
     * this file's business and the change is consumed by whoever asks first. */
    char     local_asset[MP_ACTOR_NAME_BYTES];
    bool     local_asset_seen;
    bool     local_asset_changed;
    uint32_t local_asset_changes;
    uint32_t local_asset_refusals;
    bool     local_asset_logged;
} mp_bridge_world_state_t;

static mp_bridge_world_state_t world;

/* The two bytes that say how long the enemy block in front of the snapshot is. */
#define ENEMY_LENGTH_BYTES MP_PAYLOAD_ENEMY_LENGTH_BYTES

/* What is kept back for the bodies, worked out per payload from the bodies in it, because the
 * enemies can wait a substep and the bodies cannot; the fixed 192 it replaces held two bodies and
 * not three. */
#define SNAPSHOT_HEADER_BYTES MP_PAYLOAD_SNAPSHOT_HEADER_BYTES

/* One enemy view per peer a listen host seats, and it seats one per far bank. */
_Static_assert(MP_ENEMY_SYNC_VIEWS == MP_BANK_FAR_MAX,
               "the enemy views are not one per peer a listen host seats");

/* The budget's fixed floor for the enemies, the least a raised one can be, holds what a block
 * cannot be built without: its head with the presence bitmap, and the copies' part head at its
 * largest. */
_Static_assert(MP_BUDGET_ENEMY_FLOOR_BYTES >=
                   MP_ENEMY_SYNC_HEADER_BYTES + 2u + MP_ENEMY_SYNC_COPY_BITMAP_BYTES,
               "the enemy floor is smaller than a block's own head");

/* With four players a view carries three bodies, and their reserve, the enemy floor and one
 * message header still fit one packet beside the block's length. */
_Static_assert(SNAPSHOT_HEADER_BYTES + MP_BANK_FAR_MAX * MP_WIRE_BODY_MAX_BYTES +
                   MP_BUDGET_ENEMY_FLOOR_BYTES + MP_CHANNEL_MESSAGE_HEADER_BYTES <=
                   MP_CHANNEL_PAYLOAD_BYTES - ENEMY_LENGTH_BYTES,
               "four players' bodies and the enemy floor leave no seat for a message");

/* And the largest state note, the map digest of the largest level, fits the messages' share at
 * four players, so no state waits for a packet without a payload to be seated at all. So does the
 * level's state with every part it can carry. Both are measured against the fixed floor: a floor
 * raised for the world events leaves them to the next packet, which is how a raised floor has
 * always given way. */
_Static_assert(MP_WORLD_DIGEST_MAX_BYTES + MP_CHANNEL_MESSAGE_HEADER_BYTES <=
                   MP_CHANNEL_PAYLOAD_BYTES - ENEMY_LENGTH_BYTES - SNAPSHOT_HEADER_BYTES -
                       MP_BANK_FAR_MAX * MP_WIRE_BODY_MAX_BYTES - MP_BUDGET_ENEMY_FLOOR_BYTES,
               "the largest map digest does not fit beside four players' payload");
_Static_assert(MP_LEVEL_STATE_MAX_BYTES + MP_CHANNEL_MESSAGE_HEADER_BYTES <=
                   MP_CHANNEL_PAYLOAD_BYTES - ENEMY_LENGTH_BYTES - SNAPSHOT_HEADER_BYTES -
                       MP_BANK_FAR_MAX * MP_WIRE_BODY_MAX_BYTES - MP_BUDGET_ENEMY_FLOOR_BYTES,
               "the largest level state does not fit beside four players' payload");

/* And a block with a full part of world events still leaves one message a seat beside three
 * bodies. This one is against the buffer the payload is built in, which is the tighter of the two
 * limits: its head, the copies' head at its largest, every event the part can carry and a message
 * header fit beside the reserve for the bodies. */
_Static_assert(SNAPSHOT_HEADER_BYTES + MP_BANK_FAR_MAX * MP_WIRE_BODY_MAX_BYTES +
                   MP_ENEMY_SYNC_HEADER_BYTES + 2u + MP_ENEMY_SYNC_COPY_BITMAP_BYTES +
                   MP_WORLD_EVENT_PART_MAX_BYTES + MP_CHANNEL_MESSAGE_HEADER_BYTES <=
                   MP_SESSION_PAYLOAD_BYTES - ENEMY_LENGTH_BYTES,
               "four players' bodies and a full part of world events leave no seat for a message");

static mp_snapshot_history_t host_history;
static mp_snapshot_history_t client_history;

/* Block relative on purpose. One builder serves the live block through the resolved hero block
 * cell, the bank's persistent block through the bank's reader, and the unit test's synthetic
 * block where the field offsets are pinned; a draft that had the builder reach through the block
 * into the object and the status record was refused, because the test block has neither. What
 * the block does not hold comes from the two readers below, and this leaves those fields at
 * zero. What crosses is a clip ordinal and its play state, never bone matrices, which is what
 * the original networked build of this engine sent as well: every machine solves the skeleton
 * itself. */
bool mp_bridge_world_build_body(mp_wire_body_t *body, mp_bridge_world_reader_t read_bytes)
{
    uint32_t raw = 0;
    int      axis;

    memset(body, 0, sizeof *body);
    for (axis = 0; axis < 3; ++axis) {
        if (!read_bytes(HERO_BLOCK_POS + (size_t)axis * 4u, &body->position[axis],
                        sizeof(float))) {
            return false;
        }
    }
    if (!read_bytes(HERO_BLOCK_HEADING, &body->orientation[1], sizeof(float))) {
        return false;
    }
    if (!read_bytes(HERO_BLOCK_HERO_INDEX, &raw, sizeof raw)) {
        return false;
    }
    body->hero = (uint8_t)raw;
    if (!read_bytes(HERO_BLOCK_DEAD, &raw, sizeof raw)) {
        return false;
    }
    body->dead  = raw != 0u;
    body->alive = raw == 0u;

    /* The block's own clip mirrors and the weapon slot, so a running, jumping, dying,
     * weapon-switching player crosses the wire as more than a sliding capsule. The clips are
     * small ordinals; the wire carries them as u16. The playheads and the live bits are not in
     * the block; the object read fills them, and without it they stay zero. */
    if (read_bytes(HERO_BLOCK_CUR_CLIP, &raw, sizeof raw)) {
        body->anim.clip[0] = (uint16_t)raw;
    }
    if (read_bytes(HERO_BLOCK_CUR_OVERLAY, &raw, sizeof raw)) {
        body->anim.clip[1] = (uint16_t)raw;
    }
    if (read_bytes(HERO_BLOCK_WEAPON, &raw, sizeof raw)) {
        body->weapon = (uint8_t)raw;
    }
    /* Cleared as it is read, so it means "the engine asked for a footfall since you last
     * looked". A substep in which it did not ask reports none, which is the truth. */
    body->loco = mp_footstep_take_local_state();
    if (read_bytes(HERO_BLOCK_OBJECT, &raw, sizeof raw) && raw != 0u) {
        body->twist_count = (uint8_t)mp_twist_read(raw, body->twist);
    }
    return true;
}

/* One channel of the object's animation: the playhead of its track, and whether the track still
 * belongs to the clip the object names. A channel with no track is silent, not a failure. */
static bool read_channel(uint32_t puppet, uint32_t clip_table, uint32_t num_clips, int32_t slot,
                         int32_t ordinal, uint16_t *head, bool *live)
{
    uint32_t track;
    uint32_t flags = 0;
    uint32_t keyframes = 0;
    uint32_t clip = 0;
    uint32_t clip_keyframes = 0;
    float    time = 0.0f;
    float    carried = 0.0f;

    *head = 0u;
    *live = false;
    if (slot < 0 || slot >= TRACK_COUNT) {
        return true;
    }
    track = puppet + PUPPET_TRACKS + (uint32_t)slot * TRACK_STRIDE;
    if (!memory_try_read(track + TRACK_FLAGS, &flags, sizeof flags) ||
        !memory_try_read(track + TRACK_TIME, &time, sizeof time) ||
        !memory_try_read(track + TRACK_TIME_CARRIED, &carried, sizeof carried) ||
        !memory_try_read(track + TRACK_KEYFRAMES, &keyframes, sizeof keyframes)) {
        return false;
    }
    *head = mp_wire_track_from_frames(carried > time ? carried : time);
    if (flags == 0u || ordinal < 0 || (uint32_t)ordinal >= num_clips) {
        return true;   /* a reset track, or an ordinal outside the actor's table: not live */
    }
    if (!memory_try_read(clip_table + (uint32_t)ordinal * 4u, &clip, sizeof clip) || clip == 0u ||
        !memory_try_read(clip + CLIP_KEYFRAMES, &clip_keyframes, sizeof clip_keyframes)) {
        return false;
    }
    *live = keyframes == clip_keyframes;
    return true;
}

bool mp_bridge_world_read_object(mp_wire_body_t *body, uint32_t object)
{
    uint32_t actor = 0;
    uint32_t thing = 0;
    uint32_t puppet = 0;
    uint32_t num_clips = 0;
    uint32_t clip_table = 0;
    int32_t  anim[5];   /* clip, slot0, fading, overlay clip, slot1 */
    uint16_t head = 0;
    bool     live = false;

    body->anim.track[0]    = 0u;
    body->anim.track[1]    = 0u;
    body->anim.channel_mask = 0u;
    if (object == 0u) {
        return false;
    }
    if (!memory_try_read(object + BAP_OBJ_ACTOR, &actor, sizeof actor) ||
        !memory_try_read(object + BAP_OBJ_THING, &thing, sizeof thing) ||
        !memory_try_read(object + BAP_OBJ_ANIM, anim, sizeof anim) ||
        actor == 0u || thing == 0u ||
        !memory_try_read(thing + THING_PUPPET, &puppet, sizeof puppet) || puppet == 0u ||
        !memory_try_read(actor + ACTOR_NUM_CLIPS, &num_clips, sizeof num_clips) ||
        !memory_try_read(actor + ACTOR_CLIP_TABLE, &clip_table, sizeof clip_table)) {
        ++world.object_refusals;
        return false;
    }

    /* The object's own ordinals, which the block's mirrors trail by a substep. */
    body->anim.clip[0] = (uint16_t)anim[0];
    body->anim.clip[1] = (uint16_t)anim[3];

    if (!read_channel(puppet, clip_table, num_clips, anim[1], anim[0], &head, &live)) {
        ++world.object_refusals;
        return false;
    }
    body->anim.track[0] = head;
    if (live) {
        body->anim.channel_mask |= MP_WIRE_ANIM_BASE_LIVE;
    }
    if (!read_channel(puppet, clip_table, num_clips, anim[4], anim[3], &head, &live)) {
        ++world.object_refusals;
        return false;
    }
    body->anim.track[1] = head;
    if (live) {
        body->anim.channel_mask |= MP_WIRE_ANIM_OVERLAY_LIVE;
    }

    /* The fade note is a slot index or -1, left by the crossfading clip player and read by
     * nothing in the engine. Reporting it once and putting -1 back is what makes it a bit per
     * substep rather than a level that stays set forever after the first crossfade. */
    if (anim[2] >= 0) {
        body->anim.channel_mask |= MP_WIRE_ANIM_BASE_FADED;
        if (patch_write_u32(object + BAP_OBJ_FADING, FADING_NONE) != PATCH_RESULT_OK) {
            ++world.fade_reset_refusals;
            if (!world.fade_logged) {
                world.fade_logged = true;
                log_warning("the fade note on the object could not be reset, so the far side "
                            "will see every base clip change as a crossfade; later refusals "
                            "are counted");
            }
        }
    }
    return true;
}

static bool read_player_block(size_t offset, void *out, size_t size)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);

    return block != 0 && memory_try_read(block + offset, out, size);
}

static uint32_t object_of(mp_bridge_world_reader_t read_bytes)
{
    uint32_t object = 0;

    return read_bytes(HERO_BLOCK_OBJECT, &object, sizeof object) ? object : 0u;
}

/* The local player's health is the first dword of the active status record, which the engine
 * addresses through one pointer cell, the one the cells module reads out of the status
 * functions' operands. The engine's own setter does not clamp, so the value goes through the
 * wire's clamp to 0..255 rather than wrapping, and the receiver treats anything over 100 as 100.
 * A record that does not read leaves the wire's byte at zero, counted and said once, rather than
 * a guess. */
static bool read_local_health(uint8_t *health)
{
    uintptr_t cell = mp_cells_address(MP_CELL_PLR_STATUS_POINTER);
    uint32_t  record = 0;
    int32_t   raw = 0;

    if (cell == 0 || !memory_try_read_u32(cell, &record) || record == 0u ||
        !memory_try_read(record, &raw, sizeof raw)) {
        ++world.health_refusals;
        if (!world.health_logged) {
            world.health_logged = true;
            log_warning("the local player's status record did not read, so its health goes "
                        "out as zero; later refusals are counted");
        }
        return false;
    }
    *health = mp_wire_clamp_health(raw);
    return true;
}

/* The name of the actor the local player is wearing, out of the mounted asset's own header.
 *
 * Two rules, both from a census of the shipped actors and neither optional. The field is 32 bytes
 * and is cut at its FIRST zero byte, because two of the 265 carry bytes after it that a raw
 * comparison of all 32 reports as a difference every substep; in all 265 the field equals the
 * file name. And it is lower cased, because nothing guarantees the case a name was authored in.
 * The hero index at +0x6C cannot serve instead: the character feature puts all 164 of its
 * profiles on hero slot 3, so two different characters report the same number.
 *
 * The bank must be at 0. Inside a window the player pointer names a far body's block, and the
 * name read there would be that body's, which would report the local player as having changed
 * into the puppet and back again once per substep. */
static void sample_local_asset(void)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  actor = 0;
    char      raw[MP_ACTOR_NAME_BYTES];
    char      name[MP_ACTOR_NAME_BYTES];
    size_t    index;

    if (mp_bank_active() != 0u) {
        return;
    }
    if (block == 0 || !memory_try_read(block + MP_HERO_BLOCK_HERO_ACTOR, &actor, sizeof actor) ||
        actor == 0u || !memory_try_read((uintptr_t)actor + MP_ACTOR_NAME, raw, sizeof raw)) {
        ++world.local_asset_refusals;
        if (!world.local_asset_logged) {
            world.local_asset_logged = true;
            log_warning("the local player's actor name did not read, so a change of appearance "
                        "cannot be noticed here; later refusals are counted");
        }
        return;
    }

    name[0] = '\0';
    for (index = 0; index + 1u < sizeof name && index < sizeof raw && raw[index] != '\0';
         ++index) {
        char c = raw[index];

        name[index] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    name[index] = '\0';
    if (name[0] == '\0') {
        return;
    }
    if (world.local_asset_seen && strcmp(world.local_asset, name) == 0) {
        return;
    }

    /* The first sample of a session is a change as well. Nothing on the far side knows what this
     * machine is wearing until it is told once, and a first sample treated as "no change" is the
     * case where two players who never switch never learn each other's hero. */
    memcpy(world.local_asset, name, sizeof world.local_asset);
    world.local_asset_seen    = true;
    world.local_asset_changed = true;
    ++world.local_asset_changes;
    log_info("the local player now wears %s", world.local_asset);
}

bool mp_bridge_world_take_local_asset_change(char *out, size_t bytes)
{
    if (!world.local_asset_changed) {
        return false;
    }
    world.local_asset_changed = false;
    if (out != NULL && bytes != 0u) {
        (void)text_format(out, bytes, "%s", world.local_asset);
    }
    return true;
}

uint32_t mp_bridge_world_off_the_model_in(uint32_t state)
{
    return (state < MP_HERO_MODULE_STATES) ? world.off_the_model[state] : 0u;
}

uint32_t mp_bridge_world_bodies_off_the_model(void)
{
    return world.bodies_off_the_model;
}

const char *mp_bridge_world_local_asset(void)
{
    return world.local_asset_seen ? world.local_asset : "";
}

/* Where this side's own position and heading come from while a script is driving the body.
 *
 * A scene that wants the hero as an actor stops the player module (`player_suspend`), and the
 * actor then drives the OBJECT. The block the module normally writes stands still for as long as
 * that lasts, so a host in a scene goes on sending the position it had when the scene began: the
 * far side sees its body standing in the wrong place for the whole scene and jumping at the end of
 * it, which is `player_resume` putting the block back from the model.
 *
 * So while the module is stopped the sample is taken from the model too, which is the same place
 * the engine's own resume reads. The read itself is mp_own_body's, because the judgement of a
 * spoken line asks where this body stands as well and has to be told what the others are sent. */
static bool read_from_the_model(mp_wire_body_t *body)
{
    uint32_t       state = 1u;
    float          position[3];
    float          heading = body->orientation[1];
    mp_own_model_t read  = mp_own_body_model(position, &heading, &state);

    if (read == MP_OWN_MODEL_NOT_HELD) {
        return false;
    }
    /* WHICH state, and not just "not one". The module has five (0 idle, 1 running, 2 quitting,
     * 3 dying, 4 respawning), and a sum over four of them cannot answer the one question that
     * matters after a scene: was the module PARKED, or was it dying and respawning like any
     * other death. A field run of 2026-09-20 was read as the first and the log could not tell
     * the two apart, because a client with no scene traffic at all showed the same number. */
    if (state < MP_HERO_MODULE_STATES) {
        ++world.off_the_model[state];
    }
    /* Only a whole read is the model; a body held for a death is at the block, and the reading
     * says so, for this send and for every other reader of this body alike. */
    if (read != MP_OWN_MODEL_READ) {
        return false;
    }
    memcpy(body->position, position, sizeof position);
    body->orientation[1] = heading;
    ++world.bodies_off_the_model;
    return true;
}

/* The live player's body: the block, then the object's animation and the status record's
 * health, which the block does not hold, and the appearance the block points at. */
static bool build_local_player(mp_wire_body_t *body)
{
    if (!mp_bridge_world_build_body(body, &read_player_block)) {
        return false;
    }
    body->world = mp_bridge_far_world();   /* the world this body stands in, for every reader */
    (void)read_from_the_model(body);
    (void)mp_bridge_world_read_object(body, object_of(&read_player_block));
    (void)read_local_health(&body->health);
    sample_local_asset();
    return true;
}

void mp_bridge_world_reset(void)
{
    mp_snapshot_history_init(&host_history);
    mp_snapshot_history_init(&client_history);
    memset(world.client_acked, 0, sizeof world.client_acked);
}

static size_t bodies_in(const mp_snapshot_t *snapshot)
{
    size_t slot;
    size_t count = 0;

    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        count += mp_snapshot_has_body(snapshot, slot) ? 1u : 0u;
    }
    return count;
}

/* One peer's payload: the enemies as that peer is believed to hold them, then the world without
 * the peer's own body, which it skips anyway and which cost it a full record every substep,
 * delta encoded against the world that peer last acknowledged. The peer's own slot is absent
 * from every world it is sent, so its history and this side's agree about every slot the delta
 * names. */
static void send_to_peer(mp_session_t *session, size_t peer, const mp_snapshot_t *world_now,
                         bool enemies)
{
    uint8_t              payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t        view;
    const mp_snapshot_t *baseline;
    size_t               encoded = 0;
    size_t               enemy_bytes = 0;
    size_t               reserve;
    size_t               enemy_floor;
    size_t               room;
    size_t               at = 0;

    view = *world_now;
    mp_snapshot_clear_body(&view, mp_session_slot_of_peer(peer));
    baseline = (world.client_acked[peer] != 0u)
                   ? mp_snapshot_history_get(&host_history, world.client_acked[peer])
                   : NULL;
    reserve  = mp_payload_world_reserve(bodies_in(&view));

    /* The enemies ride in FRONT of the snapshot, behind a length, and that order is the reason the
     * snapshot codec did not have to change: it still gets a buffer that begins with its own
     * header and ends where its own bytes end. A block appended after it would instead have needed
     * the decoder to report how far it read, which is a change to a layer this had no business
     * touching.
     *
     * They ride the unreliable payload with the bodies rather than the reliable channel because an
     * enemy's position is superseded by the next one; a resend would hold every later state behind
     * a packet nobody wants any more. */
    /* The enemies do not get the whole buffer. The bodies are the thing a player watches and they
     * go in the same payload, so their room is taken out first; an enemy block that filled the
     * buffer would push the snapshot out and the substep would carry no bodies at all.
     *
     * And the steering goes before the mass: what is due on this peer's reliable channel, asked
     * for by the same walk the build seats by, is taken out of the enemy block next, down to its
     * floor (mp_budget_rule.h). A full block used to leave a digest or a roster no seat, packet
     * after packet, with the ordered queue behind it; now the enemies give way and heal in the
     * next substep, which is what Quake 3 and Tribes do with the state of the world.
     *
     * The floor is what this view's block may not leave out this substep, a death or an event in
     * its window, and never less than the fixed one. It is asked after the census and before the
     * block, which asks the same question of itself. */
    enemy_floor = mp_enemy_sync_floor_bytes(peer);
    room = mp_budget_enemy_room(sizeof payload, reserve,
                                mp_session_due_bytes(session, peer,
                                                     mp_budget_message_limit(reserve,
                                                                             enemy_floor)));
    if (!enemies || room == 0u ||
        !mp_enemy_sync_encode_for(peer, payload + ENEMY_LENGTH_BYTES, room, &enemy_bytes)) {
        enemy_bytes = 0;
    }
    at = ENEMY_LENGTH_BYTES + enemy_bytes;

    if (mp_payload_put_enemy_length(payload, sizeof payload, enemy_bytes) &&
        mp_snapshot_encode(&view, baseline, payload + at, sizeof payload - at, &encoded) &&
        mp_session_set_payload(session, peer, payload, at + encoded)) {
        ++world.snapshots_sent;
        mp_session_note_payload_parts(session, peer, encoded, enemy_bytes);
        /* Out, not held. The peer is believed to hold it when it says so, in
         * mp_bridge_world_acknowledged, and not a substep earlier. */
        mp_enemy_sync_sent_for(peer, world_now->tick);
    } else {
        /* Either half failing loses the whole payload, and the enemies must then be told that
         * what they described did not go out. Without this the next commit adopts it and the
         * view describes a body the peer never received. */
        ++world.payloads_lost;
        mp_enemy_sync_abandon_for(peer);
    }
}

/* The world as this machine has it at the end of the substep: its own player live out of the
 * engine, and every far player in the slot its bank shows, as that player's own machine last sent
 * it. The newest sample and not the far body's block: the block is where this machine placed the
 * puppet, a render lag behind the sample, and it was refreshed before the collision; relaying it
 * would show every other player the far body a lag and a substep later than its owner had it.
 *
 * Then one payload per connected peer. It went to peer 0 alone, so a second and a third client
 * saw neither the host nor a single enemy. */
void mp_bridge_world_send(mp_session_t *session, uint32_t tick)
{
    mp_snapshot_t  current;
    mp_wire_body_t body;
    uint8_t        slot = 0;
    size_t         bank;
    size_t         peer;
    bool           enemies;

    /* A body that cannot be read is left out of the mask rather than sent as garbage. */
    mp_snapshot_clear(&current);
    current.tick = tick;
    if (build_local_player(&body)) {
        mp_snapshot_set_body(&current, 0, &body);
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (mp_bridge_far_slot_of(bank, &slot) && slot != 0u &&
            mp_interp_newest(mp_bridge_far_interp(bank), &body, NULL)) {
            mp_snapshot_set_body(&current, slot, &body);
        }
    }
    mp_snapshot_history_store(&host_history, &current);

    /* The census and every peer's block, each measured at its call so that no early return inside
     * them can leave a stage open: on the wall clock for the cadence of the sends, and by stage
     * and by the census's size for the stopwatch. */
    mp_cadence_send_begins();
    mp_stopwatch_enter(MP_WATCH_CENSUS);
    enemies = mp_enemy_sync_begin_send();
    mp_stopwatch_leave(MP_WATCH_CENSUS);
    mp_stopwatch_note_census(enemies, mp_enemy_sync_census_actors());
    for (peer = 0; peer < MP_ENEMY_SYNC_VIEWS; ++peer) {
        const mp_peer_t *seat = mp_session_peer(session, peer);

        if (seat != NULL && seat->state == MP_PEER_CONNECTED) {
            mp_stopwatch_enter(MP_WATCH_PEER_SEND);
            send_to_peer(session, peer, &current, enemies);
            mp_stopwatch_leave(MP_WATCH_PEER_SEND);
        }
    }
    mp_cadence_send_ends();
    mp_enemy_sync_trace_host();
}

void mp_bridge_world_acknowledged(size_t peer, const mp_payload_ack_t *ack)
{
    if (peer < MP_SESSION_MAX_PEERS && ack != NULL) {
        world.client_acked[peer] = ack->newest;
        /* And the enemies ride the same payload, so the same word answers for them: the newest
         * payload it holds, and by its bits which of the ones before it arrived as well. */
        mp_enemy_sync_acked_for(peer, ack->newest, ack->bits);
    }
}

void mp_bridge_world_forget_peer(size_t peer)
{
    static const mp_payload_ack_t nothing = { 0u, 0u };

    mp_bridge_world_acknowledged(peer, &nothing);
    mp_enemy_sync_forget_view(peer);   /* and it holds no enemy either */
}

/* The acknowledgement used to be a four byte reliable message per decoded snapshot, thirty two a
 * second in the same ordered stream as the events. A lost packet carrying one blocked every
 * event behind it for a throttle plus a round trip, and a stall of the host over two seconds
 * filled the sixty four send slots with acknowledgements so that events were refused. In the
 * unreliable payload it costs MP_PAYLOAD_ACK_BYTES per packet, needs no retransmission because the
 * next packet carries a newer one, and the reliable stream carries events alone. The loopback
 * client sends a command stream rather than a state and had no carrier for it, so it prefixes the
 * same bytes in front of its commands and the loopback host reads them before the command
 * decoder; without that the host would have fallen back to full snapshots forever with the
 * verification still green. */
bool mp_bridge_world_put_ack(uint8_t *buffer, size_t capacity)
{
    mp_payload_ack_t ack;
    uint32_t         gap;

    mp_payload_ack_from(&client_history, &ack);
    if (!mp_payload_put_ack(buffer, capacity, &ack)) {
        return false;
    }
    gap = mp_payload_ack_widest_gap(&ack);
    ++world.acks_sent;
    world.acks_whole  += ack.bits == ~(uint32_t)MP_PAYLOAD_ACK_UNUSED ? 1u : 0u;
    world.acks_gapped += gap != 0u ? 1u : 0u;
    if (gap > world.acks_widest_gap) {
        world.acks_widest_gap = gap;
    }
    return true;
}

/* State rather than commands, because the sampled command carries only the digital turn: a
 * player turning with the mouse, which the input fixes make the normal way to play, would turn on
 * the far side not at all, and without a correction the two views drift apart besides. The
 * command stream stays built and proven for the day the host simulates remote bodies with
 * prediction; this exchange is the honest topology, both bodies placed from the owner's state. */
void mp_bridge_world_send_own(mp_session_t *session, size_t slot, uint32_t tick)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  own;
    mp_wire_body_t body;
    size_t         bytes = 0;

    if (slot >= MP_SNAPSHOT_MAX_BODIES || !build_local_player(&body)) {
        return;
    }
    mp_snapshot_clear(&own);
    own.tick = tick;
    mp_snapshot_set_body(&own, slot, &body);
    if (mp_bridge_world_put_ack(payload, sizeof payload) &&
        mp_snapshot_encode(&own, NULL, payload + MP_PAYLOAD_ACK_BYTES,
                           sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes) &&
        mp_session_set_payload(session, 0, payload, bytes + MP_PAYLOAD_ACK_BYTES)) {
        ++world.own_sent;
    }
}

/* Both reads answer three ways rather than two. The first build answered a bool and the bridge's
 * drains looped on it, so a refused payload ended the drain and the payloads behind it in the
 * ring of four waited for the next one while further arrivals pushed the oldest out unread; a
 * payload too short for a world's header, which the bool read passed over without counting, is
 * now a counted refusal like any other. */
mp_bridge_world_read_t mp_bridge_world_receive_full(mp_session_t *session, size_t peer,
                                                    size_t slot, mp_snapshot_t *out,
                                                    mp_payload_ack_t *ack)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    const uint8_t *snapshot = payload + MP_PAYLOAD_ACK_BYTES;
    size_t         bytes = 0;
    uint32_t       baseline_tick = 0;

    if (!mp_session_read_payload(session, peer, payload, sizeof payload, &bytes)) {
        return MP_BRIDGE_WORLD_NOTHING;
    }
    if (bytes > MP_PAYLOAD_ACK_BYTES && mp_payload_get_ack(payload, bytes, ack) &&
        mp_snapshot_baseline_tick(snapshot, bytes - MP_PAYLOAD_ACK_BYTES, &baseline_tick) &&
        baseline_tick == 0u &&
        mp_snapshot_decode(snapshot, bytes - MP_PAYLOAD_ACK_BYTES, NULL, out) &&
        mp_snapshot_has_body(out, slot)) {
        return MP_BRIDGE_WORLD_DECODED;
    }
    ++world.decode_refusals;
    return MP_BRIDGE_WORLD_REFUSED;
}

mp_bridge_world_read_t mp_bridge_world_receive(mp_session_t *session, mp_snapshot_t *out)
{
    uint8_t              payload[MP_SESSION_PAYLOAD_BYTES];
    size_t               bytes = 0;
    uint32_t             baseline_tick = 0;
    const mp_snapshot_t *baseline = NULL;
    size_t               enemy_bytes = 0;
    size_t               at = 0;

    if (!mp_session_read_payload(session, 0, payload, sizeof payload, &bytes)) {
        return MP_BRIDGE_WORLD_NOTHING;
    }

    /* The enemy block first, then the snapshot behind it. A length that does not fit inside the
     * payload is a torn or foreign packet and nothing in it is trusted, the snapshot included. */
    if (!mp_payload_split_world(payload, bytes, &enemy_bytes, &at)) {
        ++world.decode_refusals;
        return MP_BRIDGE_WORLD_REFUSED;
    }
    if (!mp_snapshot_baseline_tick(payload + at, bytes - at, &baseline_tick)) {
        ++world.decode_refusals;   /* too short for a header: a torn or foreign packet */
        return MP_BRIDGE_WORLD_REFUSED;
    }
    if (baseline_tick != 0u) {
        baseline = mp_snapshot_history_get(&client_history, baseline_tick);
    }
    if (!mp_snapshot_decode(payload + at, bytes - at, baseline, out)) {
        /* A delta whose baseline was dropped cannot be applied, and the next full or applicable
         * one heals it. */
        ++world.decode_refusals;
        return MP_BRIDGE_WORLD_REFUSED;
    }

    /* The enemies ride in front of the snapshot but they are taken here, AFTER it decoded and
     * BEFORE its tick is stored, and both halves of that matter.
     *
     * After, because the block is ordered by the substep that carried it and the snapshot is what
     * names that substep. Before, because storing the tick is what this side later reports as the
     * newest it holds, and that report is the far side's licence to stop describing what it sent.
     * A payload whose enemy half this side will not take must therefore not be acknowledged at
     * all: the far side is then still owing those bytes and sends them again. That is what turns
     * a lost or ill timed block from a permanent hole into a repeat.
     *
     * A module that is off is asked nothing. It takes no enemies at any time, so a refusal from it
     * would stop this side acknowledging anything for ever. */
    if (enemy_bytes != 0u && mp_enemy_sync_is_enabled() &&
        !mp_enemy_sync_apply(payload + ENEMY_LENGTH_BYTES, enemy_bytes, out->tick)) {
        ++world.enemy_refusals;
        return MP_BRIDGE_WORLD_REFUSED;
    }
    ++world.snapshots_decoded;
    mp_snapshot_history_store(&client_history, out);
    return MP_BRIDGE_WORLD_DECODED;
}

const mp_snapshot_t *mp_bridge_world_built(uint32_t tick)
{
    return mp_snapshot_history_get(&host_history, tick);
}

void mp_bridge_world_report(uint32_t host_tick)
{
    const mp_snapshot_t *newest = mp_snapshot_history_newest(&client_history);
    uint32_t             checks = 0u;
    uint32_t             faults = 0u;
    float                worst  = 0.0f;

    mp_bridge_world_check_counts(&checks, &faults, &worst);
    log_info("  the world: %u own state(s) and %u snapshot(s) sent, %u decoded with %u "
             "refusal(s), %u state check(s) with %u beyond the wire's error (worst %f), %u "
             "payload(s) left unacknowledged for their enemy block",
             (unsigned)world.own_sent, (unsigned)world.snapshots_sent,
             (unsigned)world.snapshots_decoded, (unsigned)world.decode_refusals,
             (unsigned)checks, (unsigned)faults, worst, (unsigned)world.enemy_refusals);
    log_info("  the acknowledgement bits (client): %u sent, %u with every bit of the window set, "
             "%u with a gap, the widest gap %u substep(s)", (unsigned)world.acks_sent,
             (unsigned)world.acks_whole, (unsigned)world.acks_gapped,
             (unsigned)world.acks_widest_gap);
    mp_bridge_far_report_worlds();
    log_info("  the live reads: %u object refusal(s), %u health refusal(s), %u fade reset "
             "refusal(s)",
             (unsigned)world.object_refusals, (unsigned)world.health_refusals,
             (unsigned)world.fade_reset_refusals);
    log_info("  the local appearance: %s, changed %u time(s), %u sample refusal(s)%s",
             world.local_asset_seen ? world.local_asset : "never sampled",
             (unsigned)world.local_asset_changes, (unsigned)world.local_asset_refusals,
             world.local_asset_changed ? ", and one change is still waiting to be taken" : "");
    if (newest != NULL) {
        log_info("  the client's newest world is tick %u against the host's %u, with %u "
                 "body(ies) in it, this player's own left out by the host",
                 (unsigned)newest->tick, (unsigned)host_tick, (unsigned)bodies_in(newest));
    }
}
