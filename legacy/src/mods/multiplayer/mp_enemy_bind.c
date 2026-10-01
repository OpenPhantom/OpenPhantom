/* mp_enemy_bind.c: the enemy pool against the running engine.
 *
 * SIZE NOTE: over 600 lines, and the reason is that three quarters of it is the byte level contract
 * of a structure this file writes into a running game. Every offset here has to say what it is and
 * why it may be touched, because the ones that may not be touched sit between them and look
 * identical. The next seam is the log of the first writes and the refusals by placement, which
 * share only the counters with the rest.
 */
#include "mp_enemy_bind.h"

#include "mp_cells.h"
#include "mp_enemy_body.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_shield.h"
#include "mp_enemy_wire.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The list header in front of the slab. Both offsets are already used by the object pool walk in
 * this feature and are the same header type: id at +0x00, the chain head at +0x04, ONE shared
 * iteration cursor at +0x08, the element size at +0x0C, the capacity at +0x10, a lock at +0x14
 * the shipped build never sets, then the slab, 0x18 bytes in all. Each slot is a link word and
 * its payload, stride element size plus four, and a free slot carries -1 in the link word. */
#define LIST_HEAD_OFFSET     0x04u
#define LIST_CAPACITY_OFFSET 0x10u

/* A slot begins with its link word; the actor is what follows it. */
#define NODE_TO_ACTOR 4u

/* How far past the pool's own capacity a chain may run before it is called broken. A walk inside a
 * substep must end, and a chain longer than the pool cannot be a chain. */
#define WALK_SLACK 16u

/* The actor. Every offset here is read only unless the write list in the header names it. */
#define A_STATE_FLAGS   0x14u
#define A_INDEX         0x18u   /* the placement index; one writer in the engine */
#define A_STATE         0x20u
#define A_PARK_SHADOW   0x24u   /* where parking stows the state it took away */
#define A_PLACEMENT     0x10u   /* the authored record this actor was made from */
#define A_BODY          0x34u
#define A_HEALTH        0x38u
#define A_HEADING       0xACu
#define A_COMMANDED_YAW 0xB0u
#define A_PITCH         0xB8u
#define A_ACTUAL_YAW    0xBCu
#define A_ROLL          0xC0u
#define A_POS           0xD0u   /* three floats */

/* The authored record the actor points at. The spawner writes its live word with the actor's
 * address and every delete clears it. */
#define R_LIVE_WORD     0xD0u
#define A_VELOCITY      0xDCu   /* three floats */

/* The body. The pose pair is what buys the free interpolation between substeps. */
#define B_POSE_POS      0x18u
#define B_POSE_ROT      0x3Cu
#define B_PREV_POS      0x54u
#define B_PREV_ROT      0x60u

/* The state the engine gives a parked actor, which its tick steps over before anything else. */
#define STATE_PARKED 3u

/* The state the spawner gives a placement that hosts the player, which is the one state past the
 * seventeen the reaction layer has. Never written here. */
#define STATE_NOT_TICKED 16u

/* Only these flag bits are volatile enough to travel. The rest are authored in the placement and
 * both machines build the actor from the same level file, so sending them would be sending
 * something the receiver already has.
 *
 * The read happens at the end of the substep, after the collision pass, and not in task slot 4
 * where it first sat: health, the reaction state, the hit latch, the last attacker, velocity,
 * the removal reason and the completion flag are all written by the enemy contact handler at
 * 00436A68, which runs in that pass, so from slot 4 every one of them was a substep late. Bit
 * 0x2000000 was not merely late but unreachable: the contact handler sets it and the NEXT
 * pre-tick clears it, so the end of the substep is the only moment it is visible at all. */
#define VOLATILE_FLAGS 0x06180000u

/* How many replicas the log describes at their first write, and after how many writes it looks at
 * the same ones again: where the actor, its body and the host's record put each. */
#define REPLICA_SAMPLES    6u
#define SECOND_LOOK_WRITES 3000u

/* The body's flag word, whose bit 0 the spawner sets for a drawn body and whose bit 1 gives it
 * a ground shadow, and the class beside it, which is what makes it collide and a target. */
#define B_FLAGS         0x00u
#define B_CLASS         0x04u

/* The bit in an actor's state flags that says it carries the player's body. */
#define A_FLAG_HOSTS_PLAYER 0x2000u

/* How many placements whose actor carries the player's body the report names one by one. */
#define HOSTING_ROWS 8u

/* A pool slot's link word once the engine's list_free has taken the slot back. */
#define LINK_WORD_FREE  0xFFFFFFFFu

typedef struct enemy_bind_state {
    bool      installed;
    uintptr_t pool;
    uint32_t  walks;
    uint32_t  reads;
    uint32_t  writes;
    uint32_t  refused;
    uint32_t  refused_hosting;     /* the actor carries the player's body */
    uint32_t  refused_no_body;
    uint32_t  refused_unwritable;  /* the position would not write */
    uint32_t  hosting_index[HOSTING_ROWS];
    uint32_t  hosting_count[HOSTING_ROWS];
    uint32_t  hosting_rows;
    uint32_t  hosting_unlisted;    /* skips of a placement past the rows */
    bool      broken_chain_logged;
    bool      broken_link_logged;
    uint32_t  broken_links;        /* walks that stopped at a link that would not read */

    /* The replicas this side has written, as the log saw them: which actors were sampled at their
     * first write, and whether the second look at the same ones has been taken. */
    uintptr_t sampled[REPLICA_SAMPLES];
    bool      looked_again[REPLICA_SAMPLES];
    uint32_t  sampled_count;
} enemy_bind_state_t;

static enemy_bind_state_t bind;

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

bool mp_enemy_bind_install(void)
{
    uintptr_t cell;
    uint32_t  list     = 0;
    uint32_t  capacity = 0;

    if (bind.installed) {
        return true;
    }
    memset(&bind, 0, sizeof bind);

    /* The pool cell rests on two data anchors, the creation site at 00431F45 and the walk at
     * 00432C47, and both resolve exactly once in every shipped image. The first names the list
     * root, the task record and the tick wait in one run of code; the second names the root
     * twice more, so the cell rests on two compilands. The retail links put the three cells at
     * 006C4D98, 006C4DA4 and 006C4DA8 and the recompile at 006C4D48, 006C4D54 and 006C4D58: same
     * code, same pattern, three different addresses, which is the whole argument for reading a
     * cell out of an operand. The creation site's two pushed immediates are required bytes, so
     * `list_new(0x204, 0x80)`, 516 bytes an element and 128 of them, is pinned: a pool of another
     * shape matches nothing rather than resolving to a list of the wrong stride. */
    cell = mp_cells_address(MP_CELL_ENEMY_POOL);
    if (cell == 0) {
        log_error("the enemy pool cell did not resolve, so no actor can be found and nothing "
                  "about enemies will travel");
        return false;
    }
    bind.pool = cell;

    /* The pool is built when a level opens, so an empty cell here is a menu and not a fault. What
     * is checked is that when it IS filled the header has the shape these patterns describe. */
    if (memory_read_u32(cell, &list) && list != 0) {
        if (!memory_try_readable((uintptr_t)list, LIST_CAPACITY_OFFSET + 4u) ||
            !memory_read_u32((uintptr_t)list + LIST_CAPACITY_OFFSET, &capacity)) {
            log_error("the enemy pool at %08X is not readable as a list header", (unsigned)list);
            return false;
        }
        if (capacity == 0u || capacity > 0x10000u) {
            log_error("the enemy pool reports a capacity of %u, which is not a list this build "
                      "describes", (unsigned)capacity);
            return false;
        }
    }
    bind.installed = true;
    log_info("the enemy pool is bound at cell %08X", (unsigned)cell);
    return true;
}

bool mp_enemy_bind_installed(void)
{
    return bind.installed;
}

/* ==============================================================================================
 * The walk.
 * ============================================================================================ */

/* The walk itself. `whole` says whether it reached the end of the chain: a chain that runs past
 * its capacity reports nothing, and a link that cannot be read ends it with what it saw. */
static uint32_t walk_chain(mp_enemy_bind_visit_fn_t visit, void *user, bool *whole,
                           uint32_t *capacity)
{
    uint32_t list = 0;
    uint32_t node = 0;
    uint32_t seen = 0;

    *whole    = false;
    *capacity = 0u;
    if (!bind.installed || visit == NULL) {
        return 0;
    }
    if (!memory_try_read_u32(bind.pool, &list) || list == 0) {
        return 0;   /* no level open */
    }
    if (!memory_try_read_u32((uintptr_t)list + LIST_CAPACITY_OFFSET, capacity) ||
        !memory_try_read_u32((uintptr_t)list + LIST_HEAD_OFFSET, &node)) {
        return 0;
    }

    /* By hand through the link word, never through the engine's own iterator: the cursor that
     * `list_next` at 0046EABF reads and writes lives in the list at +0x08 and is shared, so a
     * walk started here would derail one the engine has open. The actor is the node plus four. */
    while (node != 0u) {
        if (seen > *capacity + WALK_SLACK) {
            if (!bind.broken_chain_logged) {
                bind.broken_chain_logged = true;
                log_error("the enemy chain does not end within its own capacity of %u, so it is "
                          "not walkable and no actor is reported this substep",
                          (unsigned)*capacity);
            }
            return 0;
        }
        ++seen;
        visit((uintptr_t)node + NODE_TO_ACTOR, user);
        if (!memory_try_readable((uintptr_t)node, 4u) ||
            !memory_try_read_u32((uintptr_t)node, &node)) {
            /* Up to three walks a substep share this link, so the line is written once. */
            ++bind.broken_links;
            if (!bind.broken_link_logged) {
                bind.broken_link_logged = true;
                log_error("the enemy chain is not readable %u actors in; later walks that stop at "
                          "a link are counted, not logged", (unsigned)seen);
            }
            return seen;
        }
    }
    *whole = true;
    return seen;
}

uint32_t mp_enemy_bind_walk(mp_enemy_bind_visit_fn_t visit, void *user)
{
    bool     whole    = false;
    uint32_t capacity = 0;
    uint32_t seen     = walk_chain(visit, user, &whole, &capacity);

    if (whole) {
        ++bind.walks;
    }
    return seen;
}

uint32_t mp_enemy_bind_walk_whole(mp_enemy_bind_visit_fn_t visit, void *user, bool *complete,
                                  uint32_t *capacity)
{
    bool     whole = false;
    uint32_t cap   = 0;
    uint32_t seen  = walk_chain(visit, user, &whole, &cap);

    if (complete != NULL) {
        *complete = whole;
    }
    if (capacity != NULL) {
        *capacity = cap;
    }
    return seen;
}

/* ==============================================================================================
 * Reading one actor.
 * ============================================================================================ */

static bool read_f32(uintptr_t at, float *out)
{
    return memory_try_read(at, out, sizeof *out);
}

static bool read_position_triple(uintptr_t at, mp_enemy_record_t *out, size_t first)
{
    float    v[3];
    uint32_t packed;
    size_t   i;

    if (!memory_try_read(at, v, sizeof v)) {
        return false;
    }
    for (i = 0; i < 3u; ++i) {
        if (!mp_enemy_wire_put_position(v[i], &packed)) {
            return false;   /* a coordinate outside every shipped level: refused, never clamped */
        }
        out->value[first + i] = packed;
    }
    return true;
}

/* The actor's own fields, which is all the census reads that it keeps no memory of: no counter
 * moves and nothing of an earlier substep is asked. The census reads through here, and so does the
 * double probe, which is what lets the probe time the census's own reading code. */
static bool read_actor(uintptr_t actor, mp_enemy_record_t *record, uint32_t *body,
                       uint32_t *index)
{
    uint32_t raw   = 0;
    uint32_t state_flags = 0;
    int32_t  value = 0;
    float    angle = 0.0f;

    memset(record, 0, sizeof *record);
    if (!memory_try_readable(actor, A_VELOCITY + 12u) ||
        !memory_try_read_u32(actor + A_BODY, body) || *body == 0u) {
        return false;
    }
    /* The body is read as far as its pose pair, which is what this half writes; the puppet fields
     * past it are the pose module's business and it checks them itself. */
    if (!memory_try_readable((uintptr_t)*body, B_PREV_ROT + 12u)) {
        return false;
    }

    if (!memory_try_read_u32(actor + A_INDEX, index)) {
        return false;
    }
    record->value[MP_ENEMY_F_INDEX] = *index & 0xFFu;

    if (!read_position_triple(actor + A_POS, record, MP_ENEMY_F_POS_X)) {
        return false;
    }

    if (!read_f32(actor + A_HEADING, &angle)) {
        return false;
    }
    record->value[MP_ENEMY_F_HEADING] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);

    if (!memory_try_read_u32(actor + A_STATE, &raw)) {
        return false;
    }
    record->value[MP_ENEMY_F_STATE] = raw & MP_ENEMY_STATE_MASK;
    if (memory_try_read_u32(actor + A_STATE_FLAGS, &state_flags)) {
        /* The five volatile bits, moved down into the wire's own flag positions. The rest of the
         * word is authored in the placement and both machines read it from the same file. */
        uint32_t flags = state_flags & VOLATILE_FLAGS;

        record->value[MP_ENEMY_F_STATE] |= (flags != 0u) ? MP_ENEMY_FLAG_IMPULSE : 0u;
    }

    if (!memory_try_read_u32(actor + A_HEALTH, &raw)) {
        return false;
    }
    value = (int32_t)raw;
    record->value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(value);
    return true;
}

/* Last, after the puppet's modules: its bit joins the state they have added to. */
static void read_flyer_pose(uintptr_t actor, mp_enemy_record_t *record)
{
    float angle = 0.0f;

    if (read_f32(actor + A_PITCH, &angle) && angle != 0.0f) {
        record->value[MP_ENEMY_F_PITCH] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_FLYER_POSE;
        if (read_f32(actor + A_ROLL, &angle)) {
            record->value[MP_ENEMY_F_ROLL] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);
        }
    }
}

bool mp_enemy_bind_read(uintptr_t actor, mp_enemy_record_t *out)
{
    mp_enemy_record_t record;
    uint32_t          body  = 0;
    uint32_t          index = 0;

    if (!bind.installed || out == NULL || !read_actor(actor, &record, &body, &index)) {
        return false;
    }

    /* The clip, both playheads and the node rotations are the puppet's, and they live next door;
     * so do the hidden nodes, a sabre put away among them. */
    mp_enemy_pose_read(body, &record);
    mp_enemy_shield_read(body, &record);
    mp_enemy_body_read(actor, body, index, &record);
    mp_enemy_nodes_read(actor, body, index, &record);
    read_flyer_pose(actor, &record);

    *out = record;
    ++bind.reads;
    return true;
}

bool mp_enemy_bind_peek(uintptr_t actor, mp_enemy_record_t *out, uint32_t *body)
{
    uint32_t index = 0;

    if (!bind.installed || out == NULL || body == NULL ||
        !read_actor(actor, out, body, &index)) {
        return false;
    }
    mp_enemy_pose_read(*body, out);
    read_flyer_pose(actor, out);
    return true;
}

/* ==============================================================================================
 * Writing one actor.
 * ============================================================================================ */

static bool write_f32(uintptr_t at, float value)
{
    return memory_try_write(at, &value, sizeof value);
}

/* One replica as the engine now holds it, beside what the host's record said. The hidden nodes
 * are named as this body had them before the write and as the host has them, because the write is
 * what makes the two agree. */
static void describe_replica(const char *when, uintptr_t actor, uint32_t body, const float wire[3],
                             const char *nodes_here, const mp_enemy_record_t *record)
{
    uint32_t index = 0;
    uint32_t state = 0;
    uint32_t flags = 0;
    float    at[3] = { 0.0f, 0.0f, 0.0f };
    float    drawn[3] = { 0.0f, 0.0f, 0.0f };

    (void)memory_try_read_u32(actor + A_INDEX, &index);
    (void)memory_try_read_u32(actor + A_STATE, &state);
    (void)memory_try_read_u32((uintptr_t)body + B_FLAGS, &flags);
    (void)memory_try_read(actor + A_POS, at, sizeof at);
    (void)memory_try_read((uintptr_t)body + B_POSE_POS, drawn, sizeof drawn);
    log_info("a replica %s: placement %u, state %u, body flags %08X; the host says %.2f %.2f %.2f, "
             "the actor stands at %.2f %.2f %.2f, the body is drawn at %.2f %.2f %.2f; its "
             "hidden %s, %s",
             when, (unsigned)index, (unsigned)state, (unsigned)flags, (double)wire[0],
             (double)wire[1], (double)wire[2], (double)at[0], (double)at[1], (double)at[2],
             (double)drawn[0], (double)drawn[1], (double)drawn[2], nodes_here,
             mp_enemy_nodes_said(record));
}

/* Which line this write of `actor` owes the log: 1 for its first write, 2 for the second look,
 * 0 for none. The sample is taken here, so a write asks once. */
static int replica_line_due(uintptr_t actor)
{
    uint32_t i;

    for (i = 0; i < bind.sampled_count; ++i) {
        if (bind.sampled[i] == actor) {
            break;
        }
    }
    if (i == bind.sampled_count) {
        if (bind.sampled_count >= REPLICA_SAMPLES) {
            return 0;
        }
        bind.sampled[bind.sampled_count++] = actor;
        return 1;
    }
    if (!bind.looked_again[i] && bind.writes + 1u >= SECOND_LOOK_WRITES) {
        bind.looked_again[i] = true;
        return 2;
    }
    return 0;
}

/* Which placement carried the player's body, counted per placement for the report. */
static void note_hosting(uint32_t index)
{
    uint32_t row;

    for (row = 0; row < bind.hosting_rows; ++row) {
        if (bind.hosting_index[row] == index) {
            ++bind.hosting_count[row];
            return;
        }
    }
    if (bind.hosting_rows < HOSTING_ROWS) {
        bind.hosting_index[bind.hosting_rows] = index;
        bind.hosting_count[bind.hosting_rows] = 1u;
        ++bind.hosting_rows;
        return;
    }
    ++bind.hosting_unlisted;
}

/* Whether the body an actor carries is this module's to write or to close, one question for both.
 * An actor with the hosting flag carries this machine's own hero, or none while the scene gate
 * refuses the hosting on a client. It stays parked, because the tick skips a parked actor before
 * it asks to host anybody, so a close asked by "parked" alone would hold this player still. */
static bool body_is_ours(uintptr_t actor, uint32_t *body, bool *hosts_player)
{
    uint32_t flags = 0;

    *body         = 0u;
    *hosts_player = memory_try_read_u32(actor + A_STATE_FLAGS, &flags) &&
                    (flags & A_FLAG_HOSTS_PLAYER) != 0u;
    return !*hosts_player && memory_try_read_u32(actor + A_BODY, body) && *body != 0u;
}

bool mp_enemy_bind_write(uintptr_t actor, uint32_t key, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous)
{
    uint32_t    body  = 0;
    uint32_t    index = 0;
    bool        hosting = false;
    int         due;
    const char *nodes_here = NULL;
    float       pos[3];
    float       prev[3];
    float       heading;
    size_t      i;

    if (!bind.installed || record == NULL) {
        return false;
    }
    (void)memory_try_read_u32(actor + A_INDEX, &index);   /* names the placement in a log line */

    /* An actor that carries the player's body is never written, whatever the host says of it: a
     * write would put the host's position on this player. */
    if (!body_is_ours(actor, &body, &hosting)) {
        ++bind.refused;
        if (hosting) {
            ++bind.refused_hosting;
            note_hosting(index);
        } else {
            ++bind.refused_no_body;
        }
        return false;
    }

    for (i = 0; i < 3u; ++i) {
        pos[i] = mp_enemy_wire_get_position(record->value[MP_ENEMY_F_POS_X + i]);
        prev[i] = (previous != NULL)
                      ? mp_enemy_wire_get_position(previous->value[MP_ENEMY_F_POS_X + i])
                      : pos[i];
    }
    heading = (float)(record->value[MP_ENEMY_F_HEADING] & 0xFFFFu) / 182.044444f;

    if (!memory_try_write(actor + A_POS, pos, sizeof pos)) {
        ++bind.refused;
        ++bind.refused_unwritable;
        return false;
    }
    due = replica_line_due(actor);
    if (due != 0) {
        nodes_here = mp_enemy_nodes_describe(body);
    }

    /* The heading travels once and lands three times. The commanded yaw has to follow it or the
     * engine turns the body back towards the old one the moment anything reads it, and the actual
     * yaw is the heading by definition and is what the body's rotation is built from. */
    (void)write_f32(actor + A_HEADING, heading);
    (void)write_f32(actor + A_COMMANDED_YAW, heading);
    (void)write_f32(actor + A_ACTUAL_YAW, heading);

    if ((record->value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_FLYER_POSE) != 0u) {
        (void)write_f32(actor + A_PITCH,
                        (float)(record->value[MP_ENEMY_F_PITCH] & 0xFFFFu) / 182.044444f);
        (void)write_f32(actor + A_ROLL,
                        (float)(record->value[MP_ENEMY_F_ROLL] & 0xFFFFu) / 182.044444f);
    }

    mp_enemy_pose_apply(body, record, previous);
    mp_enemy_shield_apply(actor, body, index, record);
    mp_enemy_body_apply(actor, body, key, record, previous);
    mp_enemy_nodes_apply(actor, body, key, record, previous);

    /* The pose pair, written as a pair and never singly. The engine's own pose commit at 004333E1
     * does this at the end of its post tick and does not run for a parked or suspended replica, so
     * a receiver that skips it leaves the body wherever the slot last was and the draw
     * interpolates a smear across the level from there. Calling that commit here is not the
     * answer, although it is callable: it turns the heading itself against the commanded yaw,
     * advances the turn ramp, and in the equal case drops a running turn clip back to clip 0. Nor
     * is the mover's pose and push at 0042AAFE: it builds the matrix, but alongside it runs corpse
     * physics, node sphere queries and floor correction, and it ADDS velocity from every polygon
     * it touches, which on a replica is a second simulator. */
    (void)memory_try_write((uintptr_t)body + B_PREV_POS, prev, sizeof prev);
    (void)memory_try_write((uintptr_t)body + B_POSE_POS, pos, sizeof pos);

    /* The rotation half, a PAIR like the position and written into the body, which is what gets
     * drawn: the draw interpolates between the two once per frame, and writing only the new one
     * makes every frame a fresh interpolation from a rotation the body never had. */
    {
        float rot[3];
        float prev_rot[3];

        if (memory_try_read((uintptr_t)body + B_POSE_ROT, rot, sizeof rot)) {
            memcpy(prev_rot, rot, sizeof prev_rot);
            rot[1] = heading;
            if ((record->value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_FLYER_POSE) != 0u) {
                rot[0] = (float)(record->value[MP_ENEMY_F_PITCH] & 0xFFFFu) / 182.044444f;
                rot[2] = (float)(record->value[MP_ENEMY_F_ROLL] & 0xFFFFu) / 182.044444f;
            }
            (void)memory_try_write((uintptr_t)body + B_PREV_ROT, prev_rot, sizeof prev_rot);
            (void)memory_try_write((uintptr_t)body + B_POSE_ROT, rot, sizeof rot);
        }
    }

    ++bind.writes;
    if (due != 0) {
        describe_replica(due == 1 ? "at its first write" : "3000 writes later", actor, body, pos,
                         nodes_here, record);
    }
    return true;
}

/* The engine's pose commit for an actor that did not move: each previous half takes the current
 * one, all twelve bytes read out of the body. The actor's own heading fields are left alone. */
bool mp_enemy_bind_close_pair(uintptr_t actor, mp_enemy_pair_close_t *out)
{
    uint32_t body    = 0;
    bool     hosting = false;
    float    rot[3];
    float    pos[3];

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->ours = bind.installed && body_is_ours(actor, &body, &hosting);
    if (!out->ours) {
        return false;
    }
    out->rotation = memory_try_read((uintptr_t)body + B_POSE_ROT, rot, sizeof rot) &&
                    memory_try_write((uintptr_t)body + B_PREV_ROT, rot, sizeof rot);
    out->position = memory_try_read((uintptr_t)body + B_POSE_POS, pos, sizeof pos) &&
                    memory_try_write((uintptr_t)body + B_PREV_POS, pos, sizeof pos);
    return out->rotation && out->position;
}

/* ==============================================================================================
 * The two marks a burst takes off a replica's body.
 * ============================================================================================ */

static bool body_of(uintptr_t actor, uint32_t *body)
{
    return bind.installed && memory_try_read_u32(actor + A_BODY, body) && *body != 0u;
}

bool mp_enemy_bind_marks(uintptr_t actor, mp_enemy_marks_t *out)
{
    uint32_t body = 0;

    return out != NULL && body_of(actor, &body) &&
           memory_try_read((uintptr_t)body + B_CLASS, &out->objclass, sizeof out->objclass) &&
           memory_try_read((uintptr_t)body + B_FLAGS, &out->flags, sizeof out->flags);
}

bool mp_enemy_bind_set_marks(uintptr_t actor, const mp_enemy_marks_t *marks)
{
    uint32_t body = 0;

    return marks != NULL && body_of(actor, &body) &&
           memory_try_write((uintptr_t)body + B_CLASS, &marks->objclass,
                            sizeof marks->objclass) &&
           memory_try_write((uintptr_t)body + B_FLAGS, &marks->flags, sizeof marks->flags);
}

/* ==============================================================================================
 * Parking, the index, and the counters.
 * ============================================================================================ */

bool mp_enemy_bind_park(uintptr_t actor, bool parked)
{
    uint32_t state  = 0;
    uint32_t shadow = 0;

    if (!bind.installed ||
        !memory_try_read_u32(actor + A_STATE, &state) ||
        !memory_try_read_u32(actor + A_PARK_SHADOW, &shadow)) {
        return false;
    }
    if (parked) {
        if (state == STATE_PARKED) {
            return true;   /* idempotent, and doing it twice would lose the real state */
        }
        return memory_try_write(actor + A_PARK_SHADOW, &state, sizeof state) &&
               memory_try_write(actor + A_STATE, &(uint32_t){ STATE_PARKED }, sizeof(uint32_t));
    }
    if (state != STATE_PARKED) {
        return true;
    }
    return memory_try_write(actor + A_STATE, &shadow, sizeof shadow) &&
           memory_try_write(actor + A_PARK_SHADOW, &(uint32_t){ 0u }, sizeof(uint32_t));
}

bool mp_enemy_bind_is_parked(uintptr_t actor)
{
    uint32_t state = 0;

    return bind.installed && memory_try_read_u32(actor + A_STATE, &state) && state == STATE_PARKED;
}

static bool has_a_body(uintptr_t actor)
{
    uint32_t body = 0;

    return memory_try_read_u32(actor + A_BODY, &body) && body != 0u;
}

/* A body a removal kept is a corpse, whatever the last record said: the removal that keeps a
 * body is the one that makes a corpse, and on a receiver it ran because the host ran it. The
 * record can lag behind it by a substep, and a corpse let go as active would get up. */
static bool is_kept_corpse(uintptr_t actor)
{
    uint32_t index = 0;

    return memory_try_read_u32(actor + A_INDEX, &index) &&
           mp_enemy_bind_slot(actor, index, NULL) == MP_ENEMY_SLOT_KEPT;
}

bool mp_enemy_bind_release(uintptr_t actor, const mp_enemy_record_t *last)
{
    uint32_t state;

    if (!mp_enemy_bind_park(actor, false)) {
        return false;
    }
    if (is_kept_corpse(actor)) {
        state = MP_ENEMY_STATE_CORPSE;
        return memory_try_write(actor + A_STATE, &state, sizeof state);
    }
    if (last == NULL) {
        return true;
    }
    /* The state the parking restored would be 1 on a body playing its death clip, and the script
     * would stand it up; the reported one is 0xE for a corpse, and the engine's corpse arms run
     * instead. Writing 3 would leave a shadow slot of zero, and the engine would then leave
     * standby into state 0. */
    state = last->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK;
    if (state == STATE_PARKED || state >= STATE_NOT_TICKED) {
        return true;   /* the parking itself, or a state only a player-hosting placement has */
    }
    /* And never into a ticking state with nothing to tick. A placement that hosts the player's own
     * body has no body of its own until a script gives it one, and on a client that script is
     * refused: releasing such an actor into a state the AI runs would have it reach for a body
     * that is not there on the very next tick. Parked is where it stays until the host's record
     * says otherwise. */
    if (!has_a_body(actor)) {
        return true;
    }
    return memory_try_write(actor + A_STATE, &state, sizeof state);
}

bool mp_enemy_bind_index(uintptr_t actor, uint32_t *out)
{
    return bind.installed && out != NULL && memory_try_read_u32(actor + A_INDEX, out);
}

mp_enemy_slot_t mp_enemy_slot_of(const mp_enemy_liveness_t *seen, uintptr_t actor, uint32_t key)
{
    if (seen == NULL || !seen->read || actor == 0) {
        return MP_ENEMY_SLOT_UNREAD;
    }
    if (seen->link == LINK_WORD_FREE) {
        return MP_ENEMY_SLOT_FREED;
    }
    if (seen->index != key || seen->record == 0u) {
        return MP_ENEMY_SLOT_OTHER;
    }
    if ((uintptr_t)seen->live_word == actor) {
        return MP_ENEMY_SLOT_LIVE;
    }
    /* Zero is what every removal leaves in the word, and the only one that leaves the slot
     * linked is the corpse that stays. Any other actor in the word means the record has moved on
     * to another body, and this one is not its placement's any more. */
    return seen->live_word == 0u ? MP_ENEMY_SLOT_KEPT : MP_ENEMY_SLOT_OTHER;
}

bool mp_enemy_slot_is_actor(mp_enemy_slot_t slot)
{
    return slot == MP_ENEMY_SLOT_LIVE || slot == MP_ENEMY_SLOT_KEPT;
}

mp_enemy_slot_t mp_enemy_slot_for_life(mp_enemy_slot_t slot, uintptr_t actor, uint8_t generation,
                                       uintptr_t kept_actor, uint8_t kept_generation)
{
    if (slot != MP_ENEMY_SLOT_KEPT) {
        return slot;
    }
    return (kept_actor != 0 && kept_actor == actor && kept_generation == generation)
               ? MP_ENEMY_SLOT_KEPT
               : MP_ENEMY_SLOT_OTHER;
}

bool mp_enemy_liveness_holds(const mp_enemy_liveness_t *seen, uintptr_t actor, uint32_t key)
{
    return mp_enemy_slot_of(seen, actor, key) == MP_ENEMY_SLOT_LIVE;
}

/* The record is read with the faulting read: a freed pool slot keeps its bytes, so its record
 * pointer is still the old one, and a record is only ever freed with its whole level. */
mp_enemy_slot_t mp_enemy_bind_slot(uintptr_t actor, uint32_t key, mp_enemy_liveness_t *seen)
{
    mp_enemy_liveness_t  local;
    mp_enemy_liveness_t *s = (seen != NULL) ? seen : &local;

    memset(s, 0, sizeof *s);
    s->read = bind.installed && actor != 0 &&
              memory_try_read(actor - NODE_TO_ACTOR, &s->link, sizeof s->link) &&
              memory_try_read_u32(actor + A_INDEX, &s->index) &&
              memory_try_read_u32(actor + A_PLACEMENT, &s->record) && s->record != 0u &&
              memory_try_read((uintptr_t)s->record + R_LIVE_WORD, &s->live_word,
                              sizeof s->live_word);
    return mp_enemy_slot_of(s, actor, key);
}

bool mp_enemy_bind_is_live(uintptr_t actor, uint32_t key, mp_enemy_liveness_t *seen)
{
    mp_enemy_liveness_t  local;
    mp_enemy_liveness_t *s = (seen != NULL) ? seen : &local;

    (void)mp_enemy_bind_slot(actor, key, s);
    return mp_enemy_liveness_holds(s, actor, key);
}

static void count_one(uintptr_t actor, void *user)
{
    (void)actor;
    ++*(uint32_t *)user;
}

bool mp_enemy_bind_occupancy(uint32_t *live, uint32_t *capacity)
{
    uint32_t list = 0;
    uint32_t seen = 0;

    if (live == NULL || capacity == NULL || !bind.installed ||
        !memory_try_read_u32(bind.pool, &list) || list == 0u ||
        !memory_try_read_u32((uintptr_t)list + LIST_CAPACITY_OFFSET, capacity) || *capacity == 0u) {
        return false;
    }
    (void)mp_enemy_bind_walk(&count_one, &seen);
    *live = seen;
    return true;
}

/* The refusals of the binding line, by reason. A count of them in one number could not tell a
 * replica written on the player's own body from one with nothing to write on. */
void mp_enemy_bind_report_refusals(void)
{
    char     rows[160];
    size_t   at = 0;
    uint32_t row;

    rows[0] = '\0';
    for (row = 0; row < bind.hosting_rows && at + 1u < sizeof rows; ++row) {
        at += text_format(rows + at, sizeof rows - at, "%s%u x%u", row == 0u ? "" : ", ",
                          (unsigned)bind.hosting_index[row], (unsigned)bind.hosting_count[row]);
    }
    log_info("enemies, binding, what was refused: %u record(s) for an actor that carries the "
             "player's body (placement x records: %s; %u more past the first %u), %u for an "
             "actor with no body, %u whose position would not write",
             (unsigned)bind.refused_hosting, rows[0] != '\0' ? rows : "none",
             (unsigned)bind.hosting_unlisted, (unsigned)HOSTING_ROWS,
             (unsigned)bind.refused_no_body, (unsigned)bind.refused_unwritable);
}

uint32_t mp_enemy_bind_broken_links(void)
{
    return bind.broken_links;
}

void mp_enemy_bind_counters(uint32_t *walks, uint32_t *reads, uint32_t *writes, uint32_t *refused)
{
    if (walks != NULL) {
        *walks = bind.walks;
    }
    if (reads != NULL) {
        *reads = bind.reads;
    }
    if (writes != NULL) {
        *writes = bind.writes;
    }
    if (refused != NULL) {
        *refused = bind.refused;
    }
}
