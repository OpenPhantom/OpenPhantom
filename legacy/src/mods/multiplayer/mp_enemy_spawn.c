/* mp_enemy_spawn.c: the engine's spawner, called for a placement the host has alive. */
#include "mp_enemy_spawn.h"

#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_pool.h"
#include "mp_signatures.h"
#include "mp_world.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The placement record, as far as a receiver reads and writes it. The spawn state is the cell the
 * save game persists; the live word holds the actor's address and is nulled on every delete path,
 * so a non zero one is an actor the census should already have reported. The four in the middle
 * are the activation scan's gates, in the order it tests them. */
#define PLACEMENT_FLAGS          MP_PLACEMENT_FLAGS
#define PLACEMENT_ACTIVE_RANGE   0x28u
#define PLACEMENT_MIN_DIFFICULTY 0x3Cu
#define PLACEMENT_DETAIL_GATE    0x40u
#define PLACEMENT_POSITION       0xACu
#define PLACEMENT_SPAWN_STATE    MP_PLACEMENT_SPAWN_STATE
#define PLACEMENT_LIVE_WORD      0xD0u
#define PLACEMENT_READ_BYTES     (PLACEMENT_LIVE_WORD + 4u)

/* Bit 0 of the flags: the scan may find this placement at all. Cleared placements are invisible
 * until a reveal list sets it. */
#define PLACEMENT_SCAN_ELIGIBLE  0x0001u

/* Bit 13 of the flags word marks the placement that HOSTS THE PLAYER. The spawner tests it on the
 * line after it copies the word, builds no body, and parks the actor in state 16. */
#define PLACEMENT_HOSTS_PLAYER 0x2000u

/* No exception for bit 7, 0x80. The first form refused placements carrying it as walk plates and
 * left their actors running on the receiver; the bit marks an ESCORT body, whose pre-tick fires
 * the walk plate under its feet through the plate trigger at 00408E3A. Such an actor is created
 * and parked like every other, and the plates it fires are fired on the host, whose copy runs. */

#define SPAWN_STATE_LIVE       1u
#define SPAWN_STATE_DEAD       2u

/* "Take the placement's own script." The spawner tests for exactly this value. */
#define SCRIPT_OVERRIDE_NONE   (-1)

/* A placement count past this is a pointer that is not a world. The largest shipped level has
 * 255, and the engine's own assert bound on a placement id is 0xff. */
#define PLACEMENT_COUNT_LIMIT  0x10000u

/* __cdecl, proven at all three callers: push, push, push, call, add esp 0xC; the actor in EAX.
 * The spawner sits at 00437250 in the retail image and returns through the `ret` at 00437652.
 * Its callers are 004326A6 (push edx, the record), 00435169 (push eax) and 00437224 (push -1),
 * each followed by `call 0x437250; add esp,0xC` and a test of EAX against zero. The third is
 * the activation scan itself, and its arguments are the ones passed here: the placement, its
 * index, and -1 for the placement's own script, which 0043730F tests for exactly; any other
 * value is indexed into the script table unchecked, so nothing off the wire goes in there. */
typedef uintptr_t(__cdecl *spawn_actor_fn_t)(uintptr_t placement, int32_t index,
                                             int32_t script_override);

typedef struct enemy_spawn_state {
    bool                      installed;
    spawn_actor_fn_t          call;
    uintptr_t                 level_cell;
    mp_enemy_spawn_counters_t c;
    mp_enemy_liveness_counters_t live;
    uint32_t                  live_logged;   /* disagreements written out so far */
    bool                      reserve_logged;
    bool                      engine_logged;
    bool                      state_logged;
} enemy_spawn_state_t;

static enemy_spawn_state_t spawn;

bool mp_enemy_spawn_install(void)
{
    uintptr_t call;

    if (spawn.installed) {
        return true;
    }
    memset(&spawn, 0, sizeof spawn);

    call             = mp_signatures_address(MP_SITE_SPAWN_ACTOR);
    spawn.level_cell = mp_cells_address(MP_CELL_LEVEL);
    if (call == 0 || spawn.level_cell == 0) {
        log_error("the spawner or the world cell did not resolve, so an enemy the host wakes and "
                  "this machine's own scan never does stays missing here");
        return false;
    }
    spawn.call      = (spawn_actor_fn_t)call;
    spawn.installed = true;
    log_info("the receiver spawn is bound: spawner %08X, world cell %08X; a placement the host "
             "has alive and this machine has not is created through the engine's own spawner",
             (unsigned)call, (unsigned)spawn.level_cell);
    return true;
}

bool mp_enemy_spawn_installed(void)
{
    return spawn.installed;
}

static bool world_pointer(uint32_t *world)
{
    return memory_try_read_u32(spawn.level_cell, world) && *world != 0u;
}

bool mp_enemy_spawn_level_identity(uint16_t *out)
{
    uint32_t world = 0;
    uint32_t count = 0;

    if (out == NULL || !spawn.installed || !world_pointer(&world)) {
        return false;
    }
    if (!memory_try_read(world + MP_LEVEL_MOVER_COUNT, &count, sizeof count) ||
        count > MP_WORLD_MOVER_LIMIT) {
        return false;
    }
    *out = (uint16_t)count;
    return true;
}

/* The two pool latches, asked in front of the spawner because the spawner allocates the body and
 * the actor itself. With nothing to measure against there is nothing to refuse on.
 *
 * The object pool is the wall every body, bolt and corpse shares, and its reserve is the one the
 * whole feature uses. The actor pool is the enemy's own, 128 slots, and it has a reserve of its
 * own here because a full one does not refuse: the spawner culls every corpse for good and tries
 * again, which buries placements the far side still has. Both answer LATER, so the next block
 * asks again once something has freed up. */
static bool reserve_allows(void)
{
    const mp_capacity_reading_t *reading;
    uint32_t actors_live = 0;
    uint32_t actors_capacity = 0;

    if (mp_enemy_bind_occupancy(&actors_live, &actors_capacity) &&
        !mp_pool_may_take(actors_live, actors_capacity, MP_ENEMY_SPAWN_ACTOR_RESERVE)) {
        ++spawn.c.actor_reserve_kept;
        return false;
    }
    mp_capacity_sample();
    reading = mp_capacity_last();
    if (reading == NULL || !reading->objects_readable) {
        return true;
    }
    return mp_pool_may_take(reading->objects_live, reading->objects_capacity, mp_pool_reserve());
}

mp_enemy_spawn_outcome_t mp_enemy_spawn_create(uint8_t index, uintptr_t *actor)
{
    uint32_t  world     = 0;
    uint32_t  count     = 0;
    uint32_t  table     = 0;
    uint32_t  placement = 0;
    uint32_t  flags     = 0;
    uint32_t  state     = 0;
    uint32_t  live      = 0;
    uint32_t  one       = SPAWN_STATE_LIVE;
    uintptr_t made;

    if (actor == NULL) {
        return MP_ENEMY_SPAWN_NEVER;
    }
    *actor = 0;
    if (!spawn.installed) {
        return MP_ENEMY_SPAWN_NEVER;
    }
    if (!world_pointer(&world)) {
        ++spawn.c.no_level;
        return MP_ENEMY_SPAWN_LATER;
    }
    /* The count at world+0x204 and the table of record pointers at world+0x20C are what the
     * engine's scan reads at 00437195 and 004371A6. */
    if (!memory_try_read(world + MP_LEVEL_PLACEMENT_COUNT, &count, sizeof count) ||
        count > PLACEMENT_COUNT_LIMIT) {
        ++spawn.c.unreadable;
        return MP_ENEMY_SPAWN_LATER;
    }
    if ((uint32_t)index >= count) {
        ++spawn.c.past_table;
        return MP_ENEMY_SPAWN_NEVER;
    }
    if (!memory_try_read(world + MP_LEVEL_PLACEMENTS, &table, sizeof table) || table == 0u ||
        !memory_try_read((uintptr_t)table + (uintptr_t)index * 4u, &placement,
                         sizeof placement) ||
        placement == 0u ||
        !memory_try_readable((uintptr_t)placement, PLACEMENT_READ_BYTES) ||
        !memory_try_read((uintptr_t)placement + PLACEMENT_FLAGS, &flags, sizeof flags) ||
        !memory_try_read((uintptr_t)placement + PLACEMENT_SPAWN_STATE, &state, sizeof state) ||
        !memory_try_read((uintptr_t)placement + PLACEMENT_LIVE_WORD, &live, sizeof live)) {
        ++spawn.c.unreadable;
        return MP_ENEMY_SPAWN_NEVER;
    }

    if ((flags & PLACEMENT_HOSTS_PLAYER) != 0u) {
        ++spawn.c.player_host;
        return MP_ENEMY_SPAWN_NEVER;
    }
    if (live != 0u) {
        ++spawn.c.occupied;
        return MP_ENEMY_SPAWN_LATER;
    }
    if (!reserve_allows()) {
        ++spawn.c.reserve_kept;
        if (!spawn.reserve_logged) {
            spawn.reserve_logged = true;
            log_warning("a replica was not created because the object pool is inside its "
                        "reserve; the host has an enemy this machine does not show until a slot "
                        "frees up");
        }
        return MP_ENEMY_SPAWN_LATER;
    }

    /* Five silent nulls inside: no model, a full actor pool even after the corpses were culled,
     * a full object pool. The one assert in reach, the actor bind at 00412458 on a null actor,
     * guards an expression the spawner tested in its first condition, so the return is the only
     * answer. A full actor pool makes it call the corpse cull at 004376CD first, which deletes
     * every actor in state 0xE with reason 1 and writes spawn state 2 into their placements. */
    made = spawn.call((uintptr_t)placement, (int32_t)index, SCRIPT_OVERRIDE_NONE);
    if (made == 0) {
        ++spawn.c.engine_refused;
        if (!spawn.engine_logged) {
            spawn.engine_logged = true;
            log_warning("the engine's spawner refused placement %u: no model, or a full actor or "
                        "object pool. It says nothing itself, so this is the only line about it",
                        (unsigned)index);
        }
        return MP_ENEMY_SPAWN_LATER;
    }

    /* The spawner leaves this to its caller: the scan's own success path at 0043723A writes the 1
     * after the call, and the spawner itself writes only the live word. Without it this machine's
     * own scan wakes the same placement on its next pass and a second actor lands on it. */
    if (!memory_try_write((uintptr_t)placement + PLACEMENT_SPAWN_STATE, &one, sizeof one)) {
        ++spawn.c.state_faults;
        if (!spawn.state_logged) {
            spawn.state_logged = true;
            log_warning("placement %u was created here but its spawn state could not be "
                        "written, so this machine's own scan may create it a second time",
                        (unsigned)index);
        }
    }
    ++spawn.c.created;
    if (state == SPAWN_STATE_DEAD) {
        ++spawn.c.revived;
    }
    /* Not measured: whether a body created here and dressed with the host's record smears across
     * the level for one frame. The write passes no previous pose, so the body's pair is seeded
     * from where it lands and the draw should interpolate nothing. */
    *actor = made;
    return MP_ENEMY_SPAWN_CREATED;
}

void mp_enemy_spawn_counters(mp_enemy_spawn_counters_t *out)
{
    if (out != NULL) {
        *out = spawn.c;
    }
}

bool mp_enemy_spawn_placement_is_live(uint8_t index)
{
    uint32_t world = 0;
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t placement = 0;
    uint32_t live = 0;

    if (!spawn.installed || !world_pointer(&world)) {
        return false;
    }
    if (!memory_try_read(world + MP_LEVEL_PLACEMENT_COUNT, &count, sizeof count) ||
        count > PLACEMENT_COUNT_LIMIT || (uint32_t)index >= count) {
        return false;
    }
    if (!memory_try_read(world + MP_LEVEL_PLACEMENTS, &table, sizeof table) || table == 0u ||
        !memory_try_read((uintptr_t)table + (uintptr_t)index * 4u, &placement,
                         sizeof placement) ||
        placement == 0u ||
        !memory_try_readable((uintptr_t)placement, PLACEMENT_READ_BYTES) ||
        !memory_try_read((uintptr_t)placement + PLACEMENT_LIVE_WORD, &live, sizeof live)) {
        return false;
    }
    return live != 0u;
}

/* The directory's index is a byte; a key past it has no directory entry to ask. */
#define DIRECTORY_KEYS     256u
#define LIVENESS_LOG_LINES 8u   /* disagreements written out one by one before only counting */

mp_enemy_slot_t mp_enemy_spawn_actor_slot(uintptr_t actor, uint32_t key)
{
    mp_enemy_liveness_t seen;
    mp_enemy_slot_t     slot           = mp_enemy_bind_slot(actor, key, &seen);
    bool                record_says    = (slot == MP_ENEMY_SLOT_LIVE);
    bool                directory_says = key < DIRECTORY_KEYS &&
                                         mp_enemy_spawn_placement_is_live((uint8_t)key);

    ++spawn.live.asked;
    if (record_says) {
        ++spawn.live.live;
    } else {
        ++spawn.live.gone;
    }
    /* A copy's key is past the directory's byte: there is nothing to compare it with. */
    if (key >= DIRECTORY_KEYS || record_says == directory_says) {
        return slot;
    }
    if (directory_says) {
        ++spawn.live.directory_live;
    } else {
        ++spawn.live.directory_gone;
    }
    if (spawn.live_logged < LIVENESS_LOG_LINES) {
        ++spawn.live_logged;
        log_info("the liveness of %u: the record of actor %08X says %s and the level's "
                 "directory says %s (the actor carries index %u and record %08X, whose live "
                 "word is %08X, %s)", (unsigned)key, (unsigned)actor,
                 record_says ? "live" : "gone", directory_says ? "live" : "gone",
                 (unsigned)seen.index, (unsigned)seen.record, (unsigned)seen.live_word,
                 seen.read ? "all read" : "not all readable");
    }
    return slot;
}

bool mp_enemy_spawn_actor_is_live(uintptr_t actor, uint32_t key)
{
    return mp_enemy_spawn_actor_slot(actor, key) == MP_ENEMY_SLOT_LIVE;
}

void mp_enemy_spawn_liveness_counters(mp_enemy_liveness_counters_t *out)
{
    if (out != NULL) {
        *out = spawn.live;
    }
}
