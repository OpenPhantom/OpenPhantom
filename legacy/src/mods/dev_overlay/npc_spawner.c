/* npc_spawner.c: see npc_spawner.h.
 *
 * ==============================================================================================
 * The record, and why a copy is spawned and never the level's own
 *
 * A placement is 0xD8 bytes of fields and the engine reads these of it (the activation scan's gate
 * chain, 0x004371B8..0x0043721C, and spawn_actor's copies): +0x00 flags, bit 0 the scan's own
 * filter and bit 0x2000 a parked record the player's body hosts, which never gets a body of its
 * own; +0x24 the starting yaw in degrees; +0x28 the activation radius and +0x2C the radius past
 * which the actor is removed; +0xA8 the model index; +0xAC the position; +0xB8 a name of up to
 * fifteen characters; +0xC8 the spawn state; +0xD0 the live actor's address, written by
 * spawn_actor and cleared by every delete; +0xD4 how many other placements a death reveals,
 * followed by their ids. Behind those the level's records carry a route, a count at +0xE8 and
 * sixteen bytes a node, at least one; so a level record is at least 0xFC bytes.
 *
 * The engine keeps one live actor per record and the death bookkeeping is written back into the
 * record: a second actor on the level's own record would take over its live word and its spawn
 * state, and the first one's death would reveal the placements the second one's should. So the
 * chosen record is copied into a record of this file's own, one per copy from a ring of them,
 * with the reveal count zeroed, the position and yaw replaced and a route of one node, the spot
 * it stands on; the engine writes its bookkeeping into the copy and the level's directory never
 * notices. No script of this project walks a route, and every one of the engine's route readers
 * stays inside a route of one.
 *
 * The copy is handed over under an index of its own, 256 + k, past every placement of every
 * level (the record itself is npc_spawn_record.c's). The engine stores that index in the actor
 * and reads it back in two places only: the
 * enemy block of a savegame, which npc_spawn_save.c keeps every copy out of, and the tripod gun's
 * mount. A copy under its source's index came back from a savegame as one more of that
 * placement, running the placement's own script, and a player mounted on a copied gun was saved
 * as mounted on the level's own. The copies are saved in the panel's own block instead
 * (npc_spawn_node.c) and raised again from it. With the hull on the enemy block missing no copy is
 * raised at all, because a save would then write 256 + k into that block and a load of it would
 * read the level's directory out of bounds.
 *
 * The ring holds one record per actor the engine's pool can hold, and a record is taken again
 * only when no actor names it: its live word is clear AND the last actor raised on it has let
 * go of it too. A corpse whose live word a delete cleared can stand in the pool a while longer
 * pointing at its record, and its final delete would write into the next copy's bookkeeping.
 * The live words are also what the cap counts. The name is copied by strcpy inside the engine
 * into a twelve byte field; a copied record carries its source's name, so the copy overflows only
 * where the level's own placement already did.
 *
 * Where the spawn routine and the delete are found, and the bytes that prove them, is
 * npc_spawner_sites.c's; what a level offers, and from which placement each kind is copied,
 * npc_census.c's; the archive's files beyond those, and the record written for one,
 * npc_foreign.c's; what a new copy is described as, npc_spawn_describe.c's; the scripts a copy
 * runs are spawn_scripts.c's; this file raises, keeps and removes.
 *
 * SIZE NOTE: over the 600 mark since a copy began carrying a script of its own, and the ring now
 * carries what a copy was raised from. The description of a copy went to npc_spawn_describe.c,
 * the record a copy is settled into to npc_spawn_record.c, and the finding of the two routines to
 * npc_spawner_sites.c when the placement mode's raise put this file eight lines under the hard
 * limit; the builder is the next seam, if this grows again.
 */
#include "npc_spawner.h"

#include "actor_loader.h"
#include "cheats_internal.h"
#include "npc_census.h"
#include "npc_foreign.h"
#include "npc_spawn_describe.h"
#include "npc_spawn_link.h"
#include "npc_spawn_list.h"
#include "npc_spawn_node.h"
#include "npc_spawn_record.h"
#include "npc_spawn_save.h"
#include "npc_spawner_sites.h"
#include "player_slot.h"
#include "spawn_ghost.h"
#include "spawn_scripts.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

_Static_assert(SPAWN_BEHAVIOUR_COUNT == NPC_SPAWN_BEHAVIOURS,
               "the panel's behaviours are the ones the note between the two DLLs carries");
_Static_assert(SPAWN_BEHAVIOUR_FOLLOW == NPC_SPAWN_BEHAVIOUR_FOLLOW &&
                   SPAWN_BEHAVIOUR_HELP == NPC_SPAWN_BEHAVIOUR_HELP,
               "the two behaviours a copy goes with a player in carry the contract's numbers");

#define ACTOR_RECORD       0x10u   /* the live actor's placement record */
#define ACTOR_KEY          0x18u   /* its index, which for a copy is 256 + k */
#define ACTOR_SCRIPT       0x28u   /* the live actor's script, pScript */
#define ACTOR_IP           0x2Cu   /* its instruction pointer; 0 restarts the script */
#define LINK_FREE          0xFFFFFFFFu   /* a pool node's link word when the node is free */
#define ACTOR_HEALTH       0x38u   /* the live actor's health, i32 */
#define DELETE_REASON_REMOVE 1     /* kDelete_Removed, a script's remove */

static struct {
    npc_spawn_fn_t   spawn;           /* NULL until both findings agree */
    npc_delete_fn_t  delete_actor;    /* NULL until its two findings agree; the row is then off */
    void *volatile  *level_pointer;   /* [g_level] */
    uint8_t          ring[NPC_SPAWNER_RING][NPC_SPAWN_RECORD_BYTES];
    uint8_t          script[NPC_SPAWNER_RING][SPAWN_SCRIPT_BYTES];   /* each copy's own script */
    npc_spawn_desc_t desc[NPC_SPAWNER_RING];     /* what each record's copy was raised from */
    uintptr_t        raised[NPC_SPAWNER_RING];   /* the last actor raised on each record */
    int32_t          health[NPC_SPAWNER_RING];   /* each copy's health as last seen, for the log */
    uint32_t         epoch;                      /* the world the ring belongs to */
    uint32_t         spawned;
    uint32_t         behaviour;
    bool             unkept_logged;              /* no hull on the enemy block: said once */
    bool             unsaved_logged;             /* no module node: said once */
} st;

uint32_t npc_spawner_behaviour(void)
{
    return st.behaviour;
}

void npc_spawner_set_behaviour(uint32_t behaviour)
{
    st.behaviour = (behaviour < SPAWN_BEHAVIOUR_COUNT) ? behaviour : 0u;
}

bool npc_spawner_install(void)
{
    npc_spawner_sites_t sites;

    if (!npc_spawner_sites_resolve(&sites)) {
        return false;
    }
    st.level_pointer = sites.level_pointer;
    st.spawn         = sites.spawn;
    st.delete_actor  = sites.delete_actor;   /* NULL when only the way back did not resolve */
    (void)actor_loader_install();   /* without it the list is the level's own files alone */
    (void)player_slot_resolve();
    (void)npc_spawn_save_install();
    (void)npc_spawn_node_install();
    log_info("npc spawner: spawn_actor at %08X, called from %08X, world pointer at %08X; the "
             "group lists the loaded level's actor files and raises a copy of a placement "
             "ahead of the player, under an index past the level's own, up to %u alive at once",
             (unsigned)(uintptr_t)sites.spawn, (unsigned)sites.caller,
             (unsigned)(uintptr_t)sites.level_pointer, NPC_SPAWNER_ALIVE_MAX);
    return true;
}

static const uint8_t *current_level(void)
{
    if (st.level_pointer == NULL) {
        return NULL;
    }
    return (const uint8_t *)*st.level_pointer;
}

bool npc_spawner_is_available(void)
{
    return st.spawn != NULL && current_level() != NULL && player_slot_current() != NULL;
}

/* The census and the ring follow the world. A world record is freed and replaced on every load,
 * so a pointer that moved is a new level; one rebuilt at the same address is caught by the
 * epoch the panel's module node counts, which changes with every world built and every level
 * ended. The level's close deletes every actor, each delete clears its record's live word, and
 * the ring is cleared here as well, so a level that went another way cannot leave a count. */
static void follow_level(void)
{
    const uint8_t *level = current_level();
    uint32_t       epoch = npc_spawn_node_epoch();

    if (level != npc_census()->level || epoch != st.epoch) {
        spawn_ghost_let_go();         /* first: its handle names a model released just below */
        memset(st.ring, 0, sizeof st.ring);
        memset(st.raised, 0, sizeof st.raised);
        st.epoch = epoch;
        actor_loader_release_all();   /* every body bound to one is gone with the old level */
        npc_census_count(level);
        npc_foreign_count();
    }
}

void npc_spawner_refresh(void)
{
    npc_census_count(current_level());
    npc_foreign_count();
}

uint32_t npc_spawner_kind_count(void)
{
    follow_level();
    return npc_census()->count + npc_foreign_kind_count();
}

bool npc_spawner_kind(uint32_t index, npc_spawner_kind_t *out)
{
    follow_level();
    if (out == NULL) {
        return false;
    }
    if (index < npc_census()->count) {
        *out = npc_census()->kind[index];
        return true;
    }
    return npc_foreign_kind(index - npc_census()->count, out);
}

int32_t npc_spawner_chosen(void)
{
    follow_level();
    return npc_census()->chosen;
}

void npc_spawner_choose(int32_t index)
{
    follow_level();
    npc_census()->chosen = (index >= 0 && (uint32_t)index < npc_spawner_kind_count()) ? index
                                                                                      : -1;
    /* The behaviour is left as it was: a default that followed the kind changed the choice
     * under the player's hands (played 2026-09-16, an Attack picked before the model went back
     * to Stand). */
}

/* A ring record's live actor, or NULL: the word spawn_actor stores and every delete clears. */
static const uint8_t *ring_live_actor(uint32_t i)
{
    uint32_t live;

    memcpy(&live, st.ring[i] + PLACE_LIVE_ACTOR, sizeof live);
    return (const uint8_t *)(uintptr_t)live;
}

const uint8_t *npc_spawner_ring_actor(uint32_t slot)
{
    return (slot < NPC_SPAWNER_RING) ? ring_live_actor(slot) : NULL;
}

/* Whether an actor names record `i`: its live word, or the last actor raised on it, whose live
 * word a delete may have cleared while its corpse stands in the pool pointing at the record. */
static bool record_is_named(uint32_t i)
{
    uintptr_t actor  = st.raised[i];
    uint32_t  link   = 0;
    uint32_t  record = 0;

    return ring_live_actor(i) != NULL ||
           (actor != 0 && memory_try_read_u32(actor - 4u, &link) && link != LINK_FREE &&
            memory_try_read_u32(actor + ACTOR_RECORD, &record) &&
            record == (uint32_t)(uintptr_t)st.ring[i]);
}

/* No actor names record `i` any more: see the ring above. */
static bool record_is_free(uint32_t i)
{
    if (record_is_named(i)) {
        return false;
    }
    st.raised[i] = 0;
    return true;
}

bool npc_spawner_names_any(void)
{
    uint32_t i;

    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        if (record_is_named(i)) {
            return true;
        }
    }
    return false;
}

bool npc_spawner_can_raise(void)
{
    return st.spawn != NULL && npc_spawn_save_keeps_copies_out() && npc_spawn_save_can_refuse() &&
           npc_spawn_node_standing();
}

bool npc_spawner_owns(uintptr_t actor)
{
    uint32_t  record = 0;
    uintptr_t first  = (uintptr_t)st.ring;

    return memory_try_read_u32(actor + ACTOR_RECORD, &record) && record >= first &&
           record < first + sizeof st.ring && (record - first) % NPC_SPAWN_RECORD_BYTES == 0u;
}

bool npc_spawner_description(uintptr_t actor, npc_spawn_desc_t *out)
{
    uint32_t record = 0;

    if (out == NULL || !npc_spawner_owns(actor) ||
        !memory_try_read_u32(actor + ACTOR_RECORD, &record)) {
        return false;
    }
    *out = st.desc[(record - (uint32_t)(uintptr_t)st.ring) / NPC_SPAWN_RECORD_BYTES];
    return true;
}

void npc_spawner_tick(void)
{
    const uint8_t *player = (const uint8_t *)player_slot_current();
    const float   *standing;
    uint32_t       i;

    if (st.spawn == NULL || current_level() == NULL || player == NULL ||
        current_level() != npc_census()->level || st.epoch != npc_spawn_node_epoch()) {
        return;
    }
    standing = (const float *)(player + PLAYER_POSITION_OFFSET);
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        const uint8_t *actor = ring_live_actor(i);
        int32_t        health;

        if (actor == NULL) {
            continue;
        }
        /* A flyer's scripts fly it to destination 0, the placement's authored position, which
         * for a copy is its ring record; kept over the feet of the player it belongs to, about
         * head height: the local player's, or in a session its owner's (npc_spawn_link.c). */
        if (npc_spawn_record_flies(st.ring[i])) {
            uint32_t key = 0;
            float    over[3];

            if (memory_try_read((uintptr_t)actor + ACTOR_KEY, &key, sizeof key) &&
                npc_spawn_link_flyer_point(key, standing, over)) {
                over[2] += NPC_SPAWN_FLYER_HEIGHT;
                memcpy(st.ring[i] + PLACE_POSITION, over, sizeof over);
            }
        }
        /* Every hit a copy takes goes to the log, since a fight between copies is otherwise
         * invisible until one falls: a helper's swings against a level's hit points take a few
         * points each. */
        if (memory_try_read((uintptr_t)actor + ACTOR_HEALTH, &health, sizeof health) &&
            health != st.health[i]) {
            log_info("npc spawner: %s (%08X) health %d -> %d", st.ring[i] + PLACE_NAME,
                     (unsigned)(uintptr_t)actor, st.health[i], health);
            st.health[i] = health;
        }
    }
}

uint32_t npc_spawner_alive(void)
{
    uint32_t alive = 0;
    uint32_t i;

    follow_level();
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        if (ring_live_actor(i) != NULL) {
            alive++;
        }
    }
    return alive;
}

uint32_t npc_spawner_remove_all(void)
{
    uint32_t removed = 0;
    uint32_t skipped = 0;
    uint32_t i;

    follow_level();
    if (st.delete_actor == NULL || current_level() == NULL || npc_spawn_save_window_open()) {
        return 0;
    }
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        const uint8_t *actor = ring_live_actor(i);
        uint32_t       key   = 0;
        char           where[160];

        if (actor == NULL) {
            continue;
        }
        /* A gun the player sits on stays: its delete frees the body the player's mode goes on
         * reading every tick. */
        if (memory_try_read_u32((uintptr_t)actor + ACTOR_KEY, &key) && npc_spawn_save_rides(key)) {
            skipped++;
            continue;
        }
        npc_spawn_describe_where(actor, where, sizeof where);
        log_info("npc spawner: removing %08X, %s", (unsigned)(uintptr_t)actor, where);
        st.delete_actor((void *)(uintptr_t)actor, DELETE_REASON_REMOVE);
        removed++;
    }
    if (removed != 0) {
        log_info("npc spawner: removed %u spawned actors through the engine's own delete",
                 removed);
    }
    if (skipped != 0) {
        log_info("npc spawner: %u copies skipped: the player rides one", skipped);
    }
    return removed;
}

/* ==============================================================================================
 * The builder: a copy from its description and its key, the one way a copy comes to be.
 * ============================================================================================ */

typedef struct used_keys {
    bool used[NPC_SPAWN_COPIES_MAX];
} used_keys_t;

static void note_key(uintptr_t actor, void *user)
{
    used_keys_t *keys = (used_keys_t *)user;
    uint32_t     key  = 0;

    if (memory_try_read_u32(actor + ACTOR_KEY, &key) && npc_spawn_key_is_copy(key)) {
        keys->used[key - NPC_SPAWN_KEY_FIRST] = true;
    }
}

/* The smallest key no actor in the pool carries: a copy keeps its key for its whole life, a
 * corpse included, and the ring's slot is not the key. 0 when all are taken or the pool cannot
 * be read. */
static uint32_t free_key(void)
{
    used_keys_t keys;
    uintptr_t   list = npc_spawn_save_pool_list();
    uint32_t    k;

    memset(&keys, 0, sizeof keys);
    if (list == 0 || !npc_spawn_list_each(list, NPC_SPAWN_POOL_CAPACITY, &note_key, &keys)) {
        return 0;
    }
    for (k = 0; k < NPC_SPAWN_COPIES_MAX; ++k) {
        if (!keys.used[k]) {
            return NPC_SPAWN_KEY_FIRST + k;
        }
    }
    return 0;
}

/* The record a copy is raised from, and its stem: an archive kind's is written here after the
 * file is brought in through the engine's loader and kept until the level changes, from the
 * everyday numbers of the donor the description names; a level kind's is its source placement
 * read afresh from the level, and it has to still be the file the description names. */
static bool copy_source(const uint8_t *level, const npc_spawn_desc_t *desc, uint8_t *record,
                        char *stem, uint32_t stem_size, void **foreign_model)
{
    char     file[ACTOR_FILE_NAME_LENGTH + 1u];
    uint32_t count = 0;

    *foreign_model = NULL;
    if (!memory_try_read((uintptr_t)level + WORLD_PLACEMENT_COUNT, &count, sizeof count)) {
        count = 0;
    }
    if (desc->archive) {
        *foreign_model = actor_loader_get(desc->file);
        if (*foreign_model == NULL) {
            log_warning("npc spawner: %s could not be loaded, nothing spawned", desc->file);
            return false;
        }
        npc_foreign_stem(desc->file, stem, stem_size);
        npc_foreign_write_record(level, record, stem, desc->file,
                                 desc->source < count ? desc->source
                                                      : (uint8_t)NPC_SPAWN_NO_SOURCE);
        return true;
    }
    if (desc->source >= count ||
        !npc_census_read_placement(level, desc->source, record, stem, stem_size, file,
                                   sizeof file) ||
        _stricmp(file, desc->file) != 0) {
        log_warning("npc spawner: placement %u of this level is not a %s that can be copied, "
                    "nothing spawned", (unsigned)desc->source, desc->file);
        return false;
    }
    return true;
}

/* The engine's own spawn, with the level's model table lent to a loaded file for the length of
 * the call: the spawn routine reads the table by the record's index three times, all inside
 * the one call, the eligibility test, the template, and the body's binding, and the slot is
 * put back after, so the level's table is as it was between any two frames. NULL when the
 * table could not be read or the engine spawned nothing. */
static void *raise_copy(const uint8_t *level, uint8_t *record, uint32_t key, void *foreign_model)
{
    uint32_t models = 0;
    void    *lent = NULL;
    void    *actor;

    if (foreign_model == NULL) {
        return st.spawn(record, (int32_t)key, -1);
    }
    if (!memory_try_read((uintptr_t)level + WORLD_MODELS, &models, sizeof models) ||
        !memory_try_read((uintptr_t)models + 4u * NPC_FOREIGN_MODEL_SLOT, &lent, sizeof lent)) {
        log_warning("npc spawner: the level's model table could not be read, nothing spawned");
        return NULL;
    }
    memcpy((void *)(uintptr_t)(models + 4u * NPC_FOREIGN_MODEL_SLOT), &foreign_model,
           sizeof foreign_model);
    actor = st.spawn(record, (int32_t)key, -1);
    memcpy((void *)(uintptr_t)(models + 4u * NPC_FOREIGN_MODEL_SLOT), &lent, sizeof lent);
    return actor;
}

/* A copy can be raised now: the routine, a level, and no savegame being written or read. */
static bool ready_to_raise(void)
{
    return st.spawn != NULL && current_level() != NULL && !npc_spawn_save_window_open() &&
           !npc_spawn_node_loading();
}

/* Raises `desc` under `key`. `saved`, when given, is how the copy stood in a savegame: its
 * position, the flyer's height already in it, its yaw and its health. NULL when nothing was
 * raised, and the log says why. The builder reads no player and checks no cap. */
static void *build(const npc_spawn_desc_t *desc, uint32_t key, const npc_spawn_saved_t *saved)
{
    const uint8_t *level  = current_level();
    uint8_t       *record = NULL;
    uint8_t        staged[NPC_SPAWN_RECORD_BYTES];
    uint32_t       slot   = 0;
    uint32_t       zero   = 0;
    uint32_t       i;
    char           stem[NPC_SPAWNER_NAME_MAX];
    float          position[3];
    float          facing;
    void          *actor;
    void          *script;
    void          *foreign_model;
    bool           shoots = false;
    bool           flies;
    int32_t        ceiling;
    char           clips[160];
    char           how[200];

    if (!npc_spawn_save_keeps_copies_out() || !npc_spawn_save_can_refuse()) {
        if (!st.unkept_logged) {
            st.unkept_logged = true;
            log_warning("npc spawner: no copy is raised: without the hulls on enemy_saveBlock and "
                        "save_saveGame a save could write the copy's index into the enemy block, "
                        "and loading that save would read the level's directory out of bounds");
        }
        return NULL;
    }
    if (!npc_spawn_node_standing()) {
        if (!st.unsaved_logged) {
            st.unsaved_logged = true;
            log_warning("npc spawner: no copy is raised without the panel's module node: every "
                        "save would leave it out without a word");
        }
        return NULL;
    }
    if (desc->behaviour >= SPAWN_BEHAVIOUR_COUNT || !npc_spawn_key_is_copy(key)) {
        log_warning("npc spawner: no copy of %s is raised: behaviour %u, key %u", desc->file,
                    (unsigned)desc->behaviour, key);
        return NULL;
    }
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        if (record_is_free(i)) {
            slot   = i;
            record = st.ring[i];
            break;
        }
    }
    if (record == NULL) {
        log_warning("npc spawner: every ring record is named by an actor, nothing spawned");
        return NULL;
    }
    /* The record is laid out here and goes into the ring only when nothing can refuse it any
     * more: the source placement's own record carries the live word of the level's actor, and
     * left in a ring record by a refusal it made "Remove spawned NPCs" delete that actor. */
    memset(staged, 0, sizeof staged);
    if (!copy_source(level, desc, staged, stem, sizeof stem, &foreign_model)) {
        return NULL;
    }
    flies = npc_spawn_record_flies(staged);
    if (saved != NULL) {
        memcpy(position, saved->position, sizeof position);
        facing = saved->yaw;
    } else {
        memcpy(position, desc->position, sizeof position);
        facing = desc->facing;
        if (flies) {
            position[2] += NPC_SPAWN_FLYER_HEIGHT;
        }
    }
    /* The copy's own script, laid out with this model's clips before anything is written, so
     * a file whose clips cannot be read is refused with the level untouched. */
    script = spawn_script_prepare((spawn_behaviour_t)desc->behaviour, desc->file, flies,
                                  st.script[slot], sizeof st.script[slot], clips, sizeof clips,
                                  &shoots);
    if (script == NULL) {
        log_warning("npc spawner: no %s script could be laid out for %s, nothing spawned",
                    spawn_behaviour_name((spawn_behaviour_t)desc->behaviour), stem);
        return NULL;
    }
    text_format(how, sizeof how, "%s, %s",
                spawn_behaviour_name((spawn_behaviour_t)desc->behaviour), clips);
    npc_spawn_record_settle(staged, desc, position, facing, shoots);
    memcpy(record, staged, sizeof staged);
    actor = raise_copy(level, record, key, foreign_model);
    if (actor == NULL) {
        log_warning("npc spawner: the engine spawned nothing for %s: its pool had no room even "
                    "after culling corpses, or no body could be allocated", stem);
        return NULL;
    }
    /* The swap: the engine bound the source's script and will begin it on the next tick from
     * its first entry, and it restarts from the first entry whenever the pointer is 0. The
     * copy's own script goes in its place before that tick. */
    memcpy((uint8_t *)actor + ACTOR_SCRIPT, &script, sizeof script);
    memcpy((uint8_t *)actor + ACTOR_IP, &zero, sizeof zero);
    st.desc[slot]   = *desc;
    st.raised[slot] = (uintptr_t)actor;
    /* A copy back from a savegame gets its health in the actor, never in the record: the
     * record's hit points are also the ceiling a heal clamps to and the value a reset restores. */
    memcpy(&ceiling, record + PLACE_HIT_POINTS, sizeof ceiling);
    if (saved != NULL && saved->health > 0) {
        int32_t health = (saved->health < ceiling) ? saved->health : ceiling;

        memcpy((uint8_t *)actor + ACTOR_HEALTH, &health, sizeof health);
    }
    memcpy(&st.health[slot], (uint8_t *)actor + ACTOR_HEALTH, sizeof st.health[slot]);
    st.spawned++;
    log_info("npc spawner: raised %s as %u from %s %u at %.1f %.1f %.1f facing %.0f%s, actor "
             "%08X, %s, %u alive, %u this session", stem, key,
             desc->archive ? "the donor" : "placement", (unsigned)desc->source,
             (double)position[0], (double)position[1], (double)position[2], (double)facing,
             saved != NULL ? " as saved" : "", (unsigned)(uintptr_t)actor, how,
             npc_spawner_alive(), st.spawned);
    return actor;
}

/* What every raise of the panel's own asks before it describes or builds: a player, and the cap. */
static bool may_raise_one_more(void)
{
    if (!ready_to_raise() || player_slot_current() == NULL) {
        return false;
    }
    if (npc_spawner_alive() >= NPC_SPAWNER_ALIVE_MAX) {
        log_info("npc spawner: %u spawned actors are alive in this level, the most this raises "
                 "at once; nothing spawned", NPC_SPAWNER_ALIVE_MAX);
        npc_spawn_link_refuse_here("16 are alive, the most at once");
        return false;
    }
    return true;
}

bool npc_spawner_raise_described(const npc_spawn_desc_t *desc)
{
    follow_level();
    return desc != NULL && may_raise_one_more() && build(desc, free_key(), NULL) != NULL;
}

bool npc_spawner_raise_saved(const npc_spawn_saved_t *saved)
{
    follow_level();
    return saved != NULL && ready_to_raise() && build(&saved->desc, free_key(), saved) != NULL;
}

bool npc_spawner_raise_granted(const npc_spawn_desc_t *desc, uint32_t key,
                               const npc_spawn_saved_t *saved)
{
    follow_level();
    return desc != NULL && ready_to_raise() && build(desc, key, saved) != NULL;
}

bool npc_spawner_delete(uintptr_t actor)
{
    if (st.delete_actor == NULL || npc_spawn_save_window_open() || !npc_spawner_owns(actor)) {
        return false;
    }
    st.delete_actor((void *)actor, DELETE_REASON_REMOVE);
    return true;
}
