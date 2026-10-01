/* mp_npc_shot.c: an NPC's bolt on the wire, and the rules for it. See the header. */
#include "mp_npc_shot.h"

#include "mp_enemy_pack.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define KIND_ZAPPER  14u
#define KIND_SHATTER 16u
#define KIND_SPIN    19u
#define KIND_BLAST   0x23u
#define KIND_THERMAL     MP_NPC_SHOT_THERMAL      /* clears its own side at birth, see the header */
#define KIND_PLAYER_ZAP  MP_NPC_SHOT_PLAYER_ZAP   /* hurts whoever plays where it runs */

/* The class of the body the engine's arc tick hurts, and the most a world slot can be. */
#define CLASS_OF_THE_PLAYER 1
#define ZAP_SLOT_MAX        254u

/* A player's side, which the memory may not hold (see the header), and the widest side it can. */
#define PLAYER_SIDE   1u
#define SIDE_MAX      255u

size_t mp_npc_shot_encode(const mp_npc_shot_t *shot, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    int              axis;

    if (shot == NULL || buffer == NULL || capacity < MP_NPC_SHOT_BYTES) {
        return 0u;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_NPC_SHOT_TAG);
    mp_wire_put_u32(&w, shot->tick);
    mp_wire_put_u16(&w, shot->level);
    mp_wire_put_u8(&w, shot->kind);
    mp_wire_put_u8(&w, shot->shooter_class);
    for (axis = 0; axis < 3; ++axis) {
        mp_wire_put_position(&w, shot->muzzle[axis]);
    }
    mp_wire_put_angle(&w, shot->pitch);
    mp_wire_put_angle(&w, shot->yaw);
    return w.overflowed ? 0u : w.at;
}

bool mp_npc_shot_is(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes == MP_NPC_SHOT_BYTES && note[0] == (uint8_t)MP_NPC_SHOT_TAG;
}

bool mp_npc_shot_decode(const uint8_t *note, size_t bytes, mp_npc_shot_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    int              axis;

    if (out == NULL || !mp_npc_shot_is(note, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, note, bytes);
    if (!mp_wire_get_u8(&r, &tag) || !mp_wire_get_u32(&r, &out->tick) ||
        !mp_wire_get_u16(&r, &out->level) || !mp_wire_get_u8(&r, &out->kind) ||
        !mp_wire_get_u8(&r, &out->shooter_class)) {
        return false;
    }
    for (axis = 0; axis < 3; ++axis) {
        if (!mp_wire_get_position(&r, &out->muzzle[axis])) {
            return false;
        }
    }
    if (!mp_wire_get_angle(&r, &out->pitch) || !mp_wire_get_angle(&r, &out->yaw)) {
        return false;
    }
    return out->shooter_class >= 1u && out->kind < MP_NPC_SHOT_KINDS;
}

bool mp_npc_shot_kind_travels(uint32_t kind, bool has_handler, bool sub_shots_tied)
{
    if (kind >= MP_NPC_SHOT_KINDS || kind == KIND_BLAST) {
        return false;
    }
    if (!has_handler) {
        return true;
    }
    if (kind == KIND_THERMAL) {
        return sub_shots_tied;
    }
    return kind != KIND_PLAYER_ZAP;
}

uint16_t mp_npc_shot_zap_pack(uint8_t victim_slot)
{
    return (uint16_t)mp_enemy_pack_one(victim_slot, ZAP_SLOT_MAX);
}

bool mp_npc_shot_zap_unpack(uint16_t packed, uint8_t *victim_slot)
{
    uint32_t slot = 0;

    if (victim_slot == NULL || (packed & 0xFF00u) != 0u || !mp_enemy_unpack_one(packed, &slot)) {
        return false;
    }
    *victim_slot = (uint8_t)slot;
    return true;
}

mp_npc_shot_zap_ends_t mp_npc_shot_zap_ends(int32_t shooter_class, bool victim_here,
                                            int32_t victim_class)
{
    if (shooter_class == CLASS_OF_THE_PLAYER) {
        return MP_NPC_SHOT_ZAP_NONE;
    }
    if (!victim_here || victim_class == CLASS_OF_THE_PLAYER) {
        return MP_NPC_SHOT_ZAP_SHOOTER_ALONE;
    }
    return MP_NPC_SHOT_ZAP_BOTH;
}

mp_npc_shot_due_t mp_npc_shot_due(uint32_t shot_tick, uint32_t render_tick, bool render_known)
{
    int32_t ahead;

    if (!render_known) {
        return MP_NPC_SHOT_FIRE;   /* no replay to wait for yet */
    }
    ahead = (int32_t)(shot_tick - render_tick);
    if (ahead > (int32_t)MP_NPC_SHOT_AHEAD_TICKS) {
        return MP_NPC_SHOT_FIRE;   /* waiting would hold every later bolt behind a lost replay */
    }
    if (ahead > 0) {
        return MP_NPC_SHOT_WAIT;
    }
    if (ahead < -(int32_t)MP_NPC_SHOT_LATE_TICKS) {
        return MP_NPC_SHOT_STALE;
    }
    return MP_NPC_SHOT_FIRE;
}

void mp_npc_shot_memory_clear(mp_npc_shot_memory_t *memory)
{
    if (memory != NULL) {
        memset(memory, 0, sizeof *memory);
    }
}

bool mp_npc_shot_rememberable(uint32_t side)
{
    return side != PLAYER_SIDE && side <= SIDE_MAX;
}

static bool past_lifetime(const mp_npc_shot_memory_t *memory, size_t at, uint32_t tick_now)
{
    return (uint32_t)(tick_now - memory->tick[at]) > MP_NPC_SHOT_LIFETIME;
}

/* The slot `step` places back from the newest, 1 being the newest. */
static size_t back_from_newest(const mp_npc_shot_memory_t *memory, size_t step)
{
    return (memory->next + MP_NPC_SHOT_MEMORY - step) % MP_NPC_SHOT_MEMORY;
}

bool mp_npc_shot_remember(mp_npc_shot_memory_t *memory, uint32_t object, uint8_t side,
                          uint8_t kind, uint32_t tick)
{
    size_t at;
    bool   displaced;

    if (memory == NULL || object == 0u) {
        return false;
    }
    at        = memory->next % MP_NPC_SHOT_MEMORY;
    displaced = memory->object[at] != 0u && !past_lifetime(memory, at, tick);
    memory->object[at] = object;
    memory->tick[at]   = tick;
    memory->side[at]   = side;
    memory->kind[at]   = kind;
    memory->next       = (at + 1u) % MP_NPC_SHOT_MEMORY;
    return displaced;
}

size_t mp_npc_shot_forget(mp_npc_shot_memory_t *memory, uint32_t object, uint32_t tick_now)
{
    size_t step;
    size_t dropped = 0u;

    if (memory == NULL || object == 0u) {
        return 0u;
    }
    for (step = 1u; step <= MP_NPC_SHOT_MEMORY; ++step) {
        size_t at = back_from_newest(memory, step);

        if (past_lifetime(memory, at, tick_now)) {
            break;   /* every entry behind this one is older still */
        }
        if (memory->object[at] == object) {
            memory->object[at] = 0u;
            ++dropped;
        }
    }
    return dropped;
}

bool mp_npc_shot_still_npcs(const mp_npc_shot_memory_t *memory, uint32_t object,
                            uint32_t side_now, uint32_t kind_now, uint32_t tick_now)
{
    size_t step;

    if (memory == NULL || object == 0u) {
        return false;
    }
    /* Newest first: an object the engine has handed out again belongs to its latest bolt. The
     * first entry past the lifetime ends the search, because every one behind it is older. */
    for (step = 1u; step <= MP_NPC_SHOT_MEMORY; ++step) {
        size_t at = back_from_newest(memory, step);

        if (past_lifetime(memory, at, tick_now)) {
            return false;
        }
        if (memory->object[at] != object) {
            continue;
        }
        return side_now == (uint32_t)memory->side[at] && kind_now == (uint32_t)memory->kind[at];
    }
    return false;
}

mp_npc_shot_sub_t mp_npc_shot_sub_verdict(bool in_impact, bool parent_is_npcs,
                                          int32_t shooter_class, uint32_t kind)
{
    if (shooter_class != 0) {
        return MP_NPC_SHOT_SUB_NONE;
    }
    if (in_impact) {
        return parent_is_npcs ? MP_NPC_SHOT_SUB_TIED : MP_NPC_SHOT_SUB_OF_ANOTHER;
    }
    if (kind == MP_NPC_SHOT_EXPLOSION_RING || kind == MP_NPC_SHOT_FIREBALL) {
        return MP_NPC_SHOT_SUB_ORPHAN;
    }
    return MP_NPC_SHOT_SUB_NONE;
}
