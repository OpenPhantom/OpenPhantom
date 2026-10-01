/* mp_enemy_spawn.h: a placement the host has alive becomes a body here.
 *
 * Layer 2, and the one piece of the enemy binding that CREATES an actor. The rest of it walks,
 * reads, writes and parks actors that already exist.
 *
 * ============================== Why a receiver has to spawn at all =============================
 *
 * An actor exists only once the activation scan has woken its placement, and the scan measures
 * the distance to the LOCAL player. Two machines in the same level therefore hold different actor
 * sets whenever their players stand apart: the host has bodies near its player that the client
 * never woke, and their state arrives here every substep and lands on nothing. A field run counted
 * 1038 such records in one session, and that was the one thing the player could see.
 *
 * ========================== What the engine's spawner does, and does not =======================
 *
 * `spawn_actor(rec, index, -1)` is the one place a placement becomes an actor. It is called here
 * through its resolved site with the same three arguments the engine's own scan passes, and four
 * facts about it decide the shape of this file, all read off the bytes:
 *
 *   It asserts on NO failure path. Five silent nulls: a placement without a model, an actor pool
 *   that is full even after the corpses were culled, and an object pool that is full. So the return
 *   is checked, and a null is a refusal rather than a body.
 *
 *   It does NOT set the placement's spawn state. The scan does that on success, and the scan is
 *   what this replaces, so the state is written here. Without it this machine's own scan would
 *   wake the same placement a substep later and put a second actor on it.
 *
 *   The script override is not bounds checked. It is always -1 here, which means the placement's
 *   own script; a value off the wire has no business in it.
 *
 *   When the actor pool is full it culls every corpse with reason 1, which marks their placements
 *   dead for good. A spawn on this machine can therefore bury placements the host still has, and
 *   the two machines then differ about what can come back. Known, and the cost of a body rather
 *   than none.
 *
 * ================================== What is refused here ======================================
 *
 * A placement that hosts the player, flag 0x2000: the spawner builds no body for it and leaves it
 * in a state past the seventeen the reaction layer has, and a replica of that is a script anchor
 * nobody asked for.
 *
 * An index past this level's placement table: the block was about another level.
 *
 * A placement with a live actor already on it, which the census should have found. Refused rather
 * than doubled; the next census finds the actor.
 *
 * A spawn that would eat into the object pool reserve, through the same latch every other slot
 * this feature takes goes through. The spawner allocates the body itself, so the latch is asked in
 * front of the call rather than inside it.
 *
 * =================================== When it may be called =====================================
 *
 * Inside a substep only. The block that names a missing actor may arrive from the idle pump,
 * which runs from the message loop and can find the engine halfway through a level load; the sync
 * therefore remembers what is missing and asks here from the task half. Nothing in this file
 * enforces that, because nothing here can tell; the caller is the one that knows.
 */
#ifndef MULTIPLAYER_MP_ENEMY_SPAWN_H
#define MULTIPLAYER_MP_ENEMY_SPAWN_H

#include "mp_enemy_bind.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many actor slots a spawn made here leaves free for the engine's own scan. The shipped pool
 * holds 128, the field runs measured a peak of 37 alive around one player, and the pool being full
 * is the one case in which the spawner culls every corpse for good rather than refusing; so a
 * spawn made for another player stops short of the last sixteen, and the engine's own pass, which
 * asks nothing, keeps them. A client of a started session runs no pass of its own any more
 * (mp_world_anchor), so there the sixteen are only a margin. The number is a reserve and not a
 * proof, like the object pool's. */
#define MP_ENEMY_SPAWN_ACTOR_RESERVE 16u

/* Resolves the spawner and the world cell. False when either is missing, and then no body is ever
 * created here; the sync counts what it could not place and says so. */
bool mp_enemy_spawn_install(void);
bool mp_enemy_spawn_installed(void);

/* WHICH LEVEL this machine is in, as the number of movers its world holds: the identity the map's
 * digest already uses, different in all eleven shipped levels. False when no level is open, which
 * is a menu or a load and not a fault. */
bool mp_enemy_spawn_level_identity(uint16_t *out);

/* What a request came back with. A LATER is worth asking again on the next block; a NEVER is a
 * placement no receiver creates, and asking again would only count the same refusal twice. */
typedef enum mp_enemy_spawn_outcome {
    MP_ENEMY_SPAWN_CREATED,
    MP_ENEMY_SPAWN_LATER,
    MP_ENEMY_SPAWN_NEVER
} mp_enemy_spawn_outcome_t;

/* Creates the actor for one placement index through the engine's own spawner and writes the
 * placement's spawn state, which the spawner leaves alone. The actor comes back parked by nobody:
 * the caller parks it before the next arm of the engine can see it. */
mp_enemy_spawn_outcome_t mp_enemy_spawn_create(uint8_t index, uintptr_t *actor);

/* The engine's own activation test, as a pure decision: whether a placement with these fields
 * wakes for a player standing `dx, dy, dz` away, under this machine's difficulty and detail.
 *
 * Read off the scan's bytes: bit 0 of the flags is scan eligibility; a spawn state other than 0
 * is live or buried; a range of EXACTLY zero skips the distance test and any other value applies
 * it as a strict comparison of squares; a difficulty below the placement's minimum skips it; a
 * detail gate above this machine's detail level skips it. */

/* The activation scan for a SECOND body: the same placements, the same gates, the same spawner,
 * measured from `pos` instead of from the local player. Placements hosting the player are left
 * alone, because the engine's own scan already handles that one for the one player it knows.
 * Returns false when there is no level to scan. Inside a substep only. */

/* Whether the engine still has a live actor on placement `index`, read from the placement
 * itself at the moment of the call.
 *
 * `enmyRecord+0xd0` is where the engine stores the live character, and `enemy_delete` at
 * 0x00437850 clears it on every path before it frees the body and the pool slot. It is
 * therefore the one answer to 'is this one still there' that cannot be stale, which a
 * remembered pointer always can be: a freed pool slot keeps its bytes, so it still reports its
 * old placement index and still looks like the actor it was.
 *
 * It no longer decides anything. mp_enemy_spawn_actor_is_live asks the actor's own record and
 * keeps this answer beside it only to count where the two disagree.
 *
 * Read as a yes or no and nothing more. The reconstruction has the field holding the actor's
 * address and one reconstructed writer putting a 1 there, and this needs neither to be right. */
bool mp_enemy_spawn_placement_is_live(uint8_t index);

/* What an actor's slot holds for `key`, asked before an actor is written, released, removed or
 * named as an attacker: the rule in mp_enemy_bind_slot, with the directory's answer above
 * computed beside the liveness it implies and counted where the two differ, so that a field run
 * shows whether moving the question changed anything for the placements. Writing, letting go
 * and a removal ask whether the slot is still the actor; naming an attacker asks whether it is
 * alive, through the call below. */
mp_enemy_slot_t mp_enemy_spawn_actor_slot(uintptr_t actor, uint32_t key);

/* The same question, answered as "alive": the slot is MP_ENEMY_SLOT_LIVE. */
bool mp_enemy_spawn_actor_is_live(uintptr_t actor, uint32_t key);

typedef struct mp_enemy_liveness_counters {
    uint32_t asked;
    uint32_t live;
    uint32_t gone;
    uint32_t directory_live;   /* the record said gone and the directory said live */
    uint32_t directory_gone;   /* the record said live and the directory said gone */
} mp_enemy_liveness_counters_t;

void mp_enemy_spawn_liveness_counters(mp_enemy_liveness_counters_t *out);

/* For the report. */
typedef struct mp_enemy_spawn_counters {
    uint32_t created;
    uint32_t revived;         /* created on a placement this machine had marked dead for good */
    uint32_t engine_refused;  /* the spawner answered null */
    uint32_t reserve_kept;    /* the object pool latch said no */
    uint32_t actor_reserve_kept; /* the actor pool was inside its reserve */
    uint32_t player_host;
    uint32_t past_table;
    uint32_t occupied;
    uint32_t no_level;
    uint32_t unreadable;
    uint32_t state_faults;    /* the actor exists and the spawn state could not be written */
    uint32_t scan_created;    /* woken by the far body's pass */
    uint32_t scan_refused;    /* the spawner or the reserve said no to one of those */
} mp_enemy_spawn_counters_t;

void mp_enemy_spawn_counters(mp_enemy_spawn_counters_t *out);

#endif /* MULTIPLAYER_MP_ENEMY_SPAWN_H */
