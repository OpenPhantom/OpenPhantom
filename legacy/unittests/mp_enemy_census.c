/* mp_enemy_census.c: the host's walk of the enemy pool and its read of one actor, against the walk
 * and the read they were before the swap.
 *
 * The census walks the pool by hand once a substep and reads every actor it finds, and both moved
 * from the asking form of common/memory to the trying form. The walk and the actor's own half of
 * the read are kept below word for word as they were, and both are run over one pool made up in
 * this process's memory: 22 actors chained as the engine chains them, the size the host's census
 * averaged in a field run. Then the pool is broken the two ways the walk has an
 * answer for: a link into a page nobody may read, which ends the walk with what it saw, and a
 * chain that runs round past its own capacity, which reports nothing.
 *
 * The pool: a cell holding the list, the list's head at +0x04 and its capacity at +0x10, and each
 * slot a link word followed by the actor. The actor's fields are the module's: placement index
 * +0x18, state +0x20, flags +0x14, body +0x34, health +0x38, heading +0xAC, pitch +0xB8, roll
 * +0xC0, position +0xD0. The engine side of the multiplayer's cell table is played by this file:
 * the enemy pool is the one cell anybody asks for here, and every other answers nothing.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_body.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_shield.h"
#include "mp_enemy_wire.h"

#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine's cell table, played by the test.
 * ============================================================================================ */

static uint32_t pool_cell;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_ENEMY_POOL ? (uintptr_t)&pool_cell : 0u;
}

size_t mp_cells_resolve(bool log_every_cell)
{
    (void)log_every_cell;
    return 0u;
}

const char *mp_cells_name(mp_cell_t cell)
{
    (void)cell;
    return "cell";
}

const mp_operand_t *mp_cells_operands(size_t *count)
{
    if (count != NULL) {
        *count = 0u;
    }
    return NULL;
}

uint32_t mp_cells_damage_table_hash(const uint32_t impacts[MP_CELLS_SHOT_ROWS])
{
    (void)impacts;
    return 0u;
}

bool mp_cells_hero_position(float out[3])
{
    (void)out;
    return false;
}

/* ==============================================================================================
 * The reference: the walk and the actor's own read as mp_enemy_bind.c had them, word for word.
 * ============================================================================================ */

#define LIST_HEAD_OFFSET     0x04u
#define LIST_CAPACITY_OFFSET 0x10u
#define NODE_TO_ACTOR 4u
#define WALK_SLACK 16u
#define A_STATE_FLAGS   0x14u
#define A_INDEX         0x18u
#define A_STATE         0x20u
#define A_BODY          0x34u
#define A_HEALTH        0x38u
#define A_HEADING       0xACu
#define A_PITCH         0xB8u
#define A_ROLL          0xC0u
#define A_POS           0xD0u
#define A_VELOCITY      0xDCu
#define B_PREV_ROT      0x60u
#define VOLATILE_FLAGS 0x06180000u

typedef struct reference_bind {
    bool      installed;
    uintptr_t pool;
    bool      broken_chain_logged;
} reference_bind_t;

static reference_bind_t bind;

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
    if (!memory_read_u32(bind.pool, &list) || list == 0) {
        return 0;   /* no level open */
    }
    if (!memory_read_u32((uintptr_t)list + LIST_CAPACITY_OFFSET, capacity) ||
        !memory_read_u32((uintptr_t)list + LIST_HEAD_OFFSET, &node)) {
        return 0;
    }

    while (node != 0u) {
        if (seen > *capacity + WALK_SLACK) {
            if (!bind.broken_chain_logged) {
                bind.broken_chain_logged = true;
            }
            return 0;
        }
        ++seen;
        visit((uintptr_t)node + NODE_TO_ACTOR, user);
        if (!memory_try_readable((uintptr_t)node, 4u) ||
            !memory_read_u32((uintptr_t)node, &node)) {
            return seen;
        }
    }
    *whole = true;
    return seen;
}

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

static bool reference_read(uintptr_t actor, mp_enemy_record_t *out)
{
    mp_enemy_record_t record;
    uint32_t          body  = 0;
    uint32_t          raw   = 0;
    uint32_t          index = 0;
    uint32_t          state_flags = 0;
    int32_t           value = 0;
    float             angle = 0.0f;

    if (!bind.installed || out == NULL) {
        return false;
    }
    memset(&record, 0, sizeof record);

    if (!memory_try_readable(actor, A_VELOCITY + 12u) ||
        !memory_read_u32(actor + A_BODY, &body) || body == 0u) {
        return false;
    }
    if (!memory_try_readable((uintptr_t)body, B_PREV_ROT + 12u)) {
        return false;
    }

    if (!memory_read_u32(actor + A_INDEX, &index)) {
        return false;
    }
    record.value[MP_ENEMY_F_INDEX] = index & 0xFFu;

    if (!read_position_triple(actor + A_POS, &record, MP_ENEMY_F_POS_X)) {
        return false;
    }

    if (!read_f32(actor + A_HEADING, &angle)) {
        return false;
    }
    record.value[MP_ENEMY_F_HEADING] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);

    if (!memory_read_u32(actor + A_STATE, &raw)) {
        return false;
    }
    record.value[MP_ENEMY_F_STATE] = raw & MP_ENEMY_STATE_MASK;
    if (memory_read_u32(actor + A_STATE_FLAGS, &state_flags)) {
        uint32_t flags = state_flags & VOLATILE_FLAGS;

        record.value[MP_ENEMY_F_STATE] |= (flags != 0u) ? MP_ENEMY_FLAG_IMPULSE : 0u;
    }

    if (!memory_read_u32(actor + A_HEALTH, &raw)) {
        return false;
    }
    value = (int32_t)raw;
    record.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(value);

    mp_enemy_pose_read(body, &record);
    mp_enemy_shield_read(body, &record);
    mp_enemy_body_read(actor, body, index, &record);
    mp_enemy_nodes_read(actor, body, index, &record);

    if (read_f32(actor + A_PITCH, &angle) && angle != 0.0f) {
        record.value[MP_ENEMY_F_PITCH] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);
        record.value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_FLYER_POSE;
        if (read_f32(actor + A_ROLL, &angle)) {
            record.value[MP_ENEMY_F_ROLL] = (uint32_t)((int32_t)(angle * 182.044444f) & 0xFFFF);
        }
    }

    *out = record;
    return true;
}

/* ==============================================================================================
 * The pool, in pages of its own, the last of which may not be read.
 * ============================================================================================ */

#define ACTORS     22u
#define SLOT_BYTES 0x100u
#define BODY_BYTES 0x180u

typedef struct pool {
    uint8_t *pages;
    size_t   bytes;       /* the readable part; one no access page follows */
    uint8_t *list;
    uint8_t *slot[ACTORS];
    uint8_t *body[ACTORS];
    uint8_t *forbidden;   /* the first byte of the page nobody may read */
} pool_t;

static pool_t pool;

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void putf(uint8_t *at, float value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t addr(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

/* 22 actors in slot order, each on its own body, standing at different places with different
 * headings, states and health, every third one flying with a pitch and a roll. */
static void build(void)
{
    uint32_t i;

    memset(pool.pages, 0, pool.bytes);
    pool_cell = addr(pool.list);
    put32(pool.list + LIST_CAPACITY_OFFSET, ACTORS);
    put32(pool.list + LIST_HEAD_OFFSET, addr(pool.slot[0]));
    for (i = 0; i < ACTORS; ++i) {
        uint8_t *actor = pool.slot[i] + NODE_TO_ACTOR;
        uint8_t *body  = pool.body[i];

        put32(pool.slot[i], i + 1u < ACTORS ? addr(pool.slot[i + 1u]) : 0u);
        put32(actor + A_INDEX, 10u + i);
        put32(actor + A_STATE, i % 14u);
        put32(actor + A_STATE_FLAGS, (i % 5u == 0u) ? 0x00080000u : 0x00000001u);
        put32(actor + A_BODY, addr(body));
        put32(actor + A_HEALTH, 100u - i * 3u);
        putf(actor + A_HEADING, 12.5f * (float)i);
        putf(actor + A_POS, 100.0f + (float)i);
        putf(actor + A_POS + 4u, -50.0f + 2.0f * (float)i);
        putf(actor + A_POS + 8u, 7.25f);
        if (i % 3u == 0u) {
            putf(actor + A_PITCH, 4.0f + (float)i);
            putf(actor + A_ROLL, -3.0f);
        }
        put32(body + 0x00u, 0x3u);           /* drawn, with a shadow */
        put32(body + 0x04u, 5u);             /* a class: solid */
        put32(body + 0xECu, 0xFFFFFFFFu);    /* no base track */
        put32(body + 0xF8u, 0xFFFFFFFFu);    /* no overlay */
        put32(body + 0xE8u, i);              /* the clip */
    }
}

typedef struct visits {
    uint32_t  count;
    uintptr_t actor[64];
} visits_t;

static void note_visit(uintptr_t actor, void *user)
{
    visits_t *v = (visits_t *)user;

    if (v->count < sizeof v->actor / sizeof v->actor[0]) {
        v->actor[v->count] = actor;
    }
    ++v->count;
}

/* Both walks over the pool as it stands: the same count, the same verdict, the same capacity and
 * the same actors in the same order. */
static void same_walk(const char *what, uint32_t seen_wanted, bool whole_wanted)
{
    visits_t swapped;
    visits_t reference;
    bool     whole_swapped = false;
    bool     whole_reference = false;
    uint32_t cap_swapped = 0;
    uint32_t cap_reference = 0;
    uint32_t seen_swapped;
    uint32_t seen_reference;

    memset(&swapped, 0, sizeof swapped);
    memset(&reference, 0, sizeof reference);
    seen_swapped   = mp_enemy_bind_walk_whole(&note_visit, &swapped, &whole_swapped, &cap_swapped);
    seen_reference = walk_chain(&note_visit, &reference, &whole_reference, &cap_reference);

    ut_checkf(seen_reference == seen_wanted && whole_reference == whole_wanted,
              "%s: the reference walk sees %u and is %s", what, (unsigned)seen_wanted,
              whole_wanted ? "whole" : "not whole");
    ut_checkf(seen_swapped == seen_reference && whole_swapped == whole_reference &&
                  cap_swapped == cap_reference,
              "%s: the walk answers as the reference does", what);
    ut_checkf(swapped.count == reference.count &&
                  memcmp(swapped.actor, reference.actor, sizeof swapped.actor) == 0,
              "%s: and visits the same actors in the same order", what);
}

static void same_reads(const char *what)
{
    uint32_t i;
    uint32_t agreed = 0;
    uint32_t read = 0;

    for (i = 0; i < ACTORS; ++i) {
        uintptr_t         actor = (uintptr_t)(pool.slot[i] + NODE_TO_ACTOR);
        mp_enemy_record_t swapped;
        mp_enemy_record_t reference;
        bool              ok_reference;
        bool              ok_swapped;

        memset(&swapped, 0, sizeof swapped);
        memset(&reference, 0, sizeof reference);
        ok_reference = reference_read(actor, &reference);
        ok_swapped   = mp_enemy_bind_read(actor, &swapped);
        read += ok_swapped ? 1u : 0u;
        if (ok_swapped == ok_reference &&
            memcmp(&swapped, &reference, sizeof swapped) == 0) {
            ++agreed;
        }
    }
    ut_checkf(agreed == ACTORS, "%s: all %u records are the reference's, field for field (%u read)",
              what, (unsigned)ACTORS, (unsigned)read);
}

static void check_the_whole_pool(void)
{
    ut_section("22 actors, chained as the engine chains them");
    build();
    same_walk("the whole pool", ACTORS, true);
    same_reads("the whole pool");
    same_reads("the whole pool read a second time, as the next substep would");

    build();
    put32(pool.slot[4] + NODE_TO_ACTOR + A_BODY, 0u);
    putf(pool.slot[7] + NODE_TO_ACTOR + A_POS, 1.0e9f);
    put32(pool.body[9] + 0x9Cu, addr(pool.forbidden));
    put32(pool.slot[11] + NODE_TO_ACTOR + A_BODY, addr(pool.forbidden - 0x10u));
    same_reads("an actor with no body, one off every level, a thing and a body nobody may read");
}

static void check_the_broken_pools(void)
{
    ut_section("a pool the walk cannot finish");
    build();
    put32(pool.slot[10], addr(pool.forbidden));
    same_walk("a link into a page nobody may read", 12u, false);

    build();
    put32(pool.slot[ACTORS - 1u], addr(pool.slot[0]));
    same_walk("a chain that runs round past its capacity", 0u, false);

    build();
    pool_cell = 0u;
    same_walk("no level open", 0u, false);
}

int main(void)
{
    SYSTEM_INFO info;
    DWORD       previous;
    uint32_t    i;
    size_t      used;

    GetSystemInfo(&info);
    used       = 0x40u + ACTORS * (SLOT_BYTES + BODY_BYTES);
    pool.bytes = (used + info.dwPageSize - 1u) / info.dwPageSize * info.dwPageSize;
    pool.pages = (uint8_t *)VirtualAlloc(NULL, pool.bytes + info.dwPageSize,
                                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (pool.pages == NULL ||
        !VirtualProtect(pool.pages + pool.bytes, info.dwPageSize, PAGE_NOACCESS, &previous)) {
        ut_check(0, "the pool's pages, the last no access (prerequisite)");
        return ut_summary("the census's walk and read of an actor");
    }
    pool.forbidden = pool.pages + pool.bytes;
    pool.list      = pool.pages;
    for (i = 0; i < ACTORS; ++i) {
        pool.slot[i] = pool.pages + 0x40u + i * SLOT_BYTES;
        pool.body[i] = pool.pages + 0x40u + ACTORS * SLOT_BYTES + i * BODY_BYTES;
    }

    build();
    bind.installed = true;
    bind.pool      = (uintptr_t)&pool_cell;
    ut_check(mp_enemy_bind_install(), "the binding installs on the pool made up here");

    check_the_whole_pool();
    check_the_broken_pools();

    (void)VirtualFree(pool.pages, 0, MEM_RELEASE);
    return ut_summary("the census's walk and read of an actor");
}
