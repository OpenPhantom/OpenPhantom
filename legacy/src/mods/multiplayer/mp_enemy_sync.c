/* mp_enemy_sync.c: which enemies changed, and what a receiver does with them.
 *
 * SIZE NOTE: over 600 lines. The receiving half grew three decisions in one day, each of which is a
 * field run's lesson written where it applies: the mirror that follows the sender, the wishes
 * granted from the substep, and the level a block is about; then the two passes over a block and
 * the copies' part; then the corpse the host still lists, which the flush goes on writing. The
 * sending half, which shares nothing with the receiving one but the table and the census, is
 * mp_enemy_sync_send.c; the report and the deaths it follows are mp_enemy_sync_report.c; the flush
 * and the handing back, which share only the placement row with the rest, are
 * mp_enemy_sync_flush.c. How a replica's body ends travels in its record and is written by
 * mp_enemy_body.c, so nothing here lays a body down or stands it up. The next seam is the wishes
 * and the spawning from the substep.
 */
#include "mp_enemy_sync.h"

#include "mp_enemy_sync_internal.h"
#include "mp_cadence.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_body.h"
#include "mp_enemy_burst.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_queue_rule.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_wire.h"
#include "mp_level_state.h"
#include "mp_scene_exit.h"
#include "mp_world_event.h"

#include "mp_events.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static enemy_sync_state_t sync;

enemy_sync_state_t *mp_enemy_sync_state(void)
{
    return &sync;
}

void mp_enemy_sync_set_enabled(bool enabled)
{
    if (!enabled && sync.enabled) {
        mp_enemy_sync_release_all();   /* never leave an actor parked by a module going quiet */
    }
    sync.enabled = enabled;
}

static void forget_wanted(void)
{
    size_t i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        sync.placement[i].wanted = false;
    }
}

void mp_enemy_sync_set_level(bool known, uint16_t identity)
{
    if (known != sync.level_known || identity != sync.level) {
        forget_wanted();   /* a wish was about the level before */
    }
    sync.level_known = known;
    sync.level       = identity;
}

void mp_enemy_sync_set_copies(const mp_npc_copies_t *host, mp_npc_copies_client_t *client)
{
    sync.copies_host   = host;
    sync.copies_client = client;
}

/* The counters are NOT cleared here, and that is the whole point of doing this by hand rather
 * than with one memset.
 *
 * This runs on a level change, and the report is written at a level END. A memset therefore zeroed
 * every number in the report a moment before it was printed, so a field run that had walked the
 * pool two thousand times and written fifty thousand actors said "sent 0 blocks carrying 0
 * records" and looked like a feature that had never run at all. That cost a whole field run to
 * notice, and it was only noticed because the BINDING's counters, which nothing resets, disagreed
 * with it. */
void mp_enemy_sync_reset(void)
{
    uint8_t forgot[MP_ENEMY_SYNC_KEYS];
    size_t  i;

    /* The actors are let go BEFORE the table is cleared, or their addresses are gone and nothing
     * can unpark them. A record a key kept in front of its mirror goes with its row. What last made
     * this side forget each key outlives the row: a key known now was forgotten by this reset, and
     * one not known keeps the reason it had, so a second reset before any block does not turn a
     * reset into "never known". */
    mp_enemy_sync_release_all();
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        forgot[i] = sync.placement[i].known ? (uint8_t)MP_ENEMY_FORGOT_RESET
                                            : sync.placement[i].forgot_by;
    }
    memset(sync.placement, 0, sizeof sync.placement);
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        sync.placement[i].forgot_by = forgot[i];
    }
    /* Every world says its first refused blocks and its first view lines again. */
    sync.lines_baseless = 0u;
    sync.lines_loading  = 0u;
    sync.lines_later    = 0u;
    memset(sync.lines_view, 0, sizeof sync.lines_view);
    memset(sync.view, 0, sizeof sync.view);
    memset(sync.bitmap, 0, sizeof sync.bitmap);
    memset(sync.copy_bitmap, 0, sizeof sync.copy_bitmap);
    sync.copy_bitmap_bytes = 0;
    sync.send_ready = false;
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        sync.placement[i].generation = 0u;
    }
    /* The level is forgotten with the table: the next substep tells it again, and a block arriving
     * in between, which is a block arriving during a load, is refused rather than applied to a
     * world that is being built. */
    sync.level_known        = false;
    sync.level              = 0u;
    sync.applied_level      = 0u;
    sync.census_level_known = false;   /* the next census has no level before it to differ from */
    /* And the stream starts over with the table: the ordering test is about blocks against THIS
     * mirror, and a reset is a new mirror. Keeping the old mark would refuse every block of a
     * session whose substeps begin below where the last one left off. */
    sync.applied_tick       = 0u;
    sync.applied_tick_known = false;
    mp_enemy_nodes_reset();   /* a new level counts its keys again */
    mp_enemy_body_reset();   /* and a savegame another body at the address of the last */
    /* The events and the level's state go with the table, from here, because every way out of a
     * level or a session already takes this reset. */
    mp_world_event_reset();
    mp_enemy_burst_reset();
    mp_level_state_reset();
    mp_scene_exit_run();   /* a scene gathered in this world ends with it */
    memset(sync.let_go, 0, sizeof sync.let_go);
    ++sync.resets;
}

/* ==============================================================================================
 * The walk, which is the same one for both sides: it says who is alive and where.
 * ============================================================================================ */

static void note_live(uintptr_t actor, void *user)
{
    uint32_t index = 0;

    (void)user;
    mp_world_event_census_saw(actor);   /* in the pool, whichever actor its key ends up read on */
    if (!mp_enemy_bind_index(actor, &index)) {
        return;
    }
    if (index >= MP_ENEMY_SYNC_KEYS) {
        ++sync.index_too_large;
        if (!sync.index_logged) {
            sync.index_logged = true;
            log_error("a live actor carries key %u and this build names %u keys, so it is left "
                      "out rather than sent as somebody else: no shipped level holds more than "
                      "255 placements and the actor pool no more than 128 copies",
                      (unsigned)index, (unsigned)MP_ENEMY_SYNC_KEYS);
        }
        return;
    }
    sync.placement[index].actor = actor;
    sync.placement[index].live  = true;
}

/* One pass over the pool, leaving `live` and `actor` current and `was_live` as it was. */
void mp_enemy_sync_take_census(void)
{
    size_t i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        sync.placement[i].was_live = sync.placement[i].live;
        sync.placement[i].live     = false;
        sync.placement[i].actor    = 0;
    }
    (void)mp_enemy_bind_walk(&note_live, NULL);
}

static bool get_bit(const uint8_t *map, size_t index)
{
    return (map[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u;
}

/* ==============================================================================================
 * The receiving side.
 * ============================================================================================ */

/* A block is read in two passes. The first decodes every record into the stage and changes
 * nothing here; only a block read to its end is applied. One pass had applied every record in
 * front of a torn one, so "refused whole" held only when the first record was the torn one. */
typedef struct stage {
    mp_enemy_record_t record[MP_ENEMY_SYNC_KEYS];
    uint8_t           generation[MP_ENEMY_SYNC_KEYS];
    bool              named[MP_ENEMY_SYNC_KEYS];
    uint16_t          order[MP_ENEMY_SYNC_KEYS];   /* the keys, in the block's order */
    size_t            count;
    size_t            placements;                  /* how many of them come first */
    uint8_t           copy_bitmap[MP_ENEMY_SYNC_COPY_BITMAP_BYTES];
    size_t            copy_bitmap_bytes;           /* 0 for a block without the copies' part */
    bool              baseless;                    /* a record with no base was not whole */
    bool              baseless_positioned;         /* and it named a position */
    bool              baseless_names_life;         /* and whether it carried the life field */
    uint16_t          baseless_key;
    uint8_t           baseless_generation;         /* the life its identity named */
} stage_t;

static stage_t stage;

/* One record into the stage, decoded against the mirror of the life it names. A generation the
 * receiver has not seen is a new life, and it must not be decoded against the previous one's
 * mirror: the mask would leave out every field the two lives happen to share and the body would
 * inherit the dead one's pose. A key the block names twice makes it torn, because the sender's
 * second record would be a delta against its first. */
static bool stage_record(const uint8_t *block, size_t bytes, size_t *at, size_t key,
                         uint8_t generation)
{
    const placement_t       *p    = &sync.placement[key];
    const mp_enemy_record_t *base = (p->known && p->generation == generation) ? &p->mirror : NULL;
    size_t                   read = 0;

    if (stage.named[key]) {
        return false;
    }
    if (!mp_enemy_wire_decode(block + *at, bytes - *at, base, &stage.record[key], &read)) {
        return false;
    }
    /* Read against nothing, a field the mask leaves out is zero. A sender leaves a field out only
     * when it believes this side holds it already, so a record that is not whole while this side
     * holds nothing for its life is a delta against a base that never arrived. It is not taken,
     * and its block goes with it: unacknowledged, the payload is one the sender finds missing, and
     * it describes those keys again, whole. Taken, the replica would carry zero in every field the
     * sender left out, the life among them, until each next changed. The rule used to ask only for
     * the position, and a delta of a body that moved on all three axes was taken with the rest at
     * zero; whether a refused record named a position is still counted, to say how often it was. */
    if (base == NULL && !mp_enemy_wire_is_whole(block + *at, bytes - *at)) {
        stage.baseless            = true;
        stage.baseless_positioned = mp_enemy_wire_names_position(block + *at, bytes - *at);
        stage.baseless_names_life = false;
        (void)mp_fieldset_changed(mp_enemy_wire_set(), block + *at, bytes - *at,
                                  MP_ENEMY_F_GENERATION, &stage.baseless_names_life);
        stage.baseless_key        = (uint16_t)key;
        stage.baseless_generation = generation;
        return false;
    }
    *at += read;
    stage.generation[key]      = generation;
    stage.named[key]           = true;
    stage.order[stage.count++] = (uint16_t)key;
    return true;
}

static bool stage_placements(const uint8_t *block, size_t bytes, size_t *at, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        size_t  index;
        uint8_t generation;

        if (*at + MP_ENEMY_SYNC_IDENTITY_BYTES > bytes) {
            return false;
        }
        index      = block[*at];
        generation = block[*at + 1u];
        *at += MP_ENEMY_SYNC_IDENTITY_BYTES;
        if (!stage_record(block, bytes, at, index, generation)) {
            return false;
        }
    }
    return true;
}

/* The copies' part, when the block has one (mp_enemy_sync.h). A bitmap longer than this build
 * names, a k past the bitmap, or a byte behind the part makes the block torn. */
static bool stage_copies(const uint8_t *block, size_t bytes, size_t *at, bool *present)
{
    size_t length;
    size_t count;
    size_t i;

    *present                = (*at < bytes);
    stage.copy_bitmap_bytes = 0u;
    if (!*present) {
        return true;
    }
    length = block[*at];
    if (length == 0u || length > MP_ENEMY_SYNC_COPY_BITMAP_BYTES || *at + 2u + length > bytes) {
        return false;
    }
    memcpy(stage.copy_bitmap, block + *at + 1u, length);
    stage.copy_bitmap_bytes = length;
    count = block[*at + 1u + length];
    *at += 2u + length;
    for (i = 0; i < count; ++i) {
        size_t  k;
        uint8_t generation;

        if (*at + MP_ENEMY_SYNC_COPY_IDENTITY_BYTES > bytes) {
            return false;
        }
        k          = (size_t)block[*at] | ((size_t)block[*at + 1u] << 8);
        generation = block[*at + 2u];
        if (k >= length * 8u) {
            return false;
        }
        *at += MP_ENEMY_SYNC_COPY_IDENTITY_BYTES;
        if (!stage_record(block, bytes, at, MP_WIRE_KEY_COPY_BASE + k, generation)) {
            return false;
        }
    }
    return *at == bytes;
}

/* The second pass, for one staged record. A refusal to create the old life says nothing about
 * the new one. */
static void take_staged(placement_t *p, size_t key)
{
    if (!p->known || p->generation != stage.generation[key]) {
        p->generation  = stage.generation[key];
        p->unspawnable = false;
        p->health_up   = false;
    }
    /* The mirror follows the sender whether or not a body exists here: the sender's belief
     * advanced when this payload went out, and the next delta is against THIS record. */
    p->mirror = stage.record[key];
    p->known  = true;
    if (mp_enemy_wire_health_known(&p->mirror) && mp_enemy_wire_health(&p->mirror) > 0) {
        p->health_up = true;   /* a later fall to zero is a death this life can have */
    }
}

/* Whether a copy's staged record of life `generation` lands on a replica: the table says the life
 * is built here and its replica is the actor the census found under the key. A record of another
 * life gives the held one up in the table. */
static bool copy_record_lands(const placement_t *p, size_t key, uint8_t generation)
{
    uint32_t k = (uint32_t)(key - MP_WIRE_KEY_COPY_BASE);

    return sync.copies_client != NULL &&
           mp_npc_copies_client_record(sync.copies_client, k, generation) && p->actor != 0 &&
           p->actor == mp_npc_copies_client_replica(sync.copies_client, k, generation);
}

/* A staged record about to take the mirror's place, asked before take_staged overwrites it: the
 * queue keeps the mirror in front of the new record while the body has not been given it and both
 * are of one life, and counts what that leaves to fall (mp_enemy_queue_rule.h). `lands` says the
 * new record goes to a body here. */
static void keep_behind(size_t key, placement_t *p, bool lands)
{
    uint32_t          fell = 0;
    mp_cadence_fall_t why  = MP_CADENCE_FELL_LET_GO;

    switch (mp_enemy_queue_take(&p->queue, &p->mirror, p->dirty && p->known,
                                p->known && p->generation == stage.generation[key], lands,
                                &fell)) {
    case MP_ENEMY_QUEUE_THIRD:
        why = MP_CADENCE_FELL_THIRD;
        break;
    case MP_ENEMY_QUEUE_NEW_LIFE:
        why = MP_CADENCE_FELL_NEW_LIFE;
        break;
    default:
        break;
    }
    mp_cadence_records_fell(key, why, fell);
}

/* Whether lhs is the more recent of two wrapping substep numbers. The same test the channel and
 * the snapshot history use, and it has to be this one rather than `<`: the counter is 32 bits at
 * 32 a second, so it wraps, and a plain comparison calls the first block after the wrap old. */
static bool tick_after(uint32_t lhs, uint32_t rhs)
{
    return (lhs != rhs) && ((uint32_t)(lhs - rhs) < 0x80000000u);
}

bool mp_enemy_sync_is_enabled(void)
{
    return sync.enabled;
}

/* A refused block, counted by why. A record with nothing here to be read against is not a
 * malformed block and is not counted as one; it is a sender whose belief about this side was wrong,
 * and the count says how often. */
static void count_refusal(mp_enemy_refusal_t why)
{
    switch (why) {
    case MP_ENEMY_REFUSAL_TORN:
        ++sync.refused;
        break;
    case MP_ENEMY_REFUSAL_TORN_COPIES:
        ++sync.refused;
        ++sync.copy_refused;
        break;
    case MP_ENEMY_REFUSAL_OLDER:
        ++sync.out_of_order;
        break;
    case MP_ENEMY_REFUSAL_NO_LEVEL:
        ++sync.no_level;
        break;
    case MP_ENEMY_REFUSAL_OTHER_LEVEL:
        ++sync.other_level;
        break;
    case MP_ENEMY_REFUSAL_BASELESS:
        ++sync.baseless_refused;
        sync.baseless_positioned += stage.baseless_positioned ? 1u : 0u;
        break;
    default:
        break;
    }
}

/* The first pass: every check a block has to pass before anything here changes, and every record
 * decoded into the stage. Answers why it may not be taken, or MP_ENEMY_REFUSAL_NONE. */
static mp_enemy_refusal_t stage_block(const uint8_t *block, size_t bytes, uint32_t tick,
                                      uint16_t *level, bool *copies)
{
    size_t at;
    size_t count;

    if (block == NULL || bytes < MP_ENEMY_SYNC_HEADER_BYTES) {
        return MP_ENEMY_REFUSAL_TORN;
    }
    count  = block[0];
    *level = (uint16_t)(block[1] | ((uint16_t)block[2] << 8));
    at     = 1u + MP_ENEMY_SYNC_LEVEL_BYTES + MP_ENEMY_SYNC_BITMAP_BYTES;

    /* The stream is ordered by the substep that carried it. The channel takes a reordered packet
     * back up to a window of 32, and an older block would write fields the newer one has already
     * moved on from. It is refused rather than ignored, so that the caller refuses the payload
     * with it and the far side hears no acknowledgement for a block this side did not take. */
    if (sync.applied_tick_known && !tick_after(tick, sync.applied_tick)) {
        return MP_ENEMY_REFUSAL_OLDER;
    }

    /* A block is about ONE level and this machine is in one level, and the two have to be the
     * same. With none open here there is nothing to apply to and nothing worth wishing for. */
    if (!sync.level_known) {
        return MP_ENEMY_REFUSAL_NO_LEVEL;
    }
    if (*level != sync.level) {
        return MP_ENEMY_REFUSAL_OTHER_LEVEL;
    }
    stage.count               = 0;
    stage.baseless            = false;
    stage.baseless_positioned = false;
    memset(stage.named, 0, sizeof stage.named);
    if (!mp_world_event_stage(block, bytes, &at) || !stage_placements(block, bytes, &at, count)) {
        return stage.baseless ? MP_ENEMY_REFUSAL_BASELESS : MP_ENEMY_REFUSAL_TORN;
    }
    stage.placements = stage.count;
    if (!stage_copies(block, bytes, &at, copies)) {
        return stage.baseless ? MP_ENEMY_REFUSAL_BASELESS : MP_ENEMY_REFUSAL_TORN_COPIES;
    }
    return MP_ENEMY_REFUSAL_NONE;
}

/* The one way out for a block this side will not take: counted, and said within its budget. */
static void refuse_block(mp_enemy_refusal_t why, uint32_t tick, uint16_t level)
{
    mp_enemy_refusal_note_t note;

    count_refusal(why);
    memset(&note, 0, sizeof note);
    note.why         = why;
    note.tick        = tick;
    note.block_level = level;
    if (why == MP_ENEMY_REFUSAL_BASELESS) {
        note.key            = stage.baseless_key;
        note.life           = stage.baseless_generation;
        note.names_life     = stage.baseless_names_life;
        note.names_position = stage.baseless_positioned;
    }
    mp_enemy_sync_note_refusal(&note);
}

bool mp_enemy_sync_apply(const uint8_t *block, size_t bytes, uint32_t tick)
{
    uint8_t            bitmap[MP_ENEMY_SYNC_BITMAP_BYTES];
    size_t             i;
    uint16_t           level  = 0;
    bool               copies = false;
    mp_enemy_refusal_t why;

    if (!sync.enabled) {
        return false;   /* a module that is off counts nothing and says nothing */
    }
    why = stage_block(block, bytes, tick, &level, &copies);
    if (why != MP_ENEMY_REFUSAL_NONE) {
        refuse_block(why, tick, level);
        return false;
    }
    memcpy(bitmap, block + 1u + MP_ENEMY_SYNC_LEVEL_BYTES, sizeof bitmap);

    if (sync.applied_level != level) {
        forget_wanted();
        sync.applied_level = level;
    }
    mp_enemy_sync_take_census();

    /* Which copies the host holds, for the client's table: one it stops naming is an orphan. */
    if (sync.copies_client != NULL) {
        mp_npc_copies_client_block(sync.copies_client, copies ? stage.copy_bitmap : NULL,
                                   copies ? stage.copy_bitmap_bytes : 0u);
    }
    for (i = 0; i < stage.count; ++i) {
        size_t       key   = stage.order[i];
        placement_t *p     = &sync.placement[key];
        bool         copy  = i >= stage.placements;
        bool         lands = copy ? copy_record_lands(p, key, stage.generation[key])
                                  : p->actor != 0;

        keep_behind(key, p, lands);
        take_staged(p, key);
        if (copy) {
            /* A copy: its mirror follows, and its record goes to the replica this machine's
             * overlay built for the host's grant of that same life, once there is one. */
            ++sync.copy_records_applied;
            if (lands) {
                p->dirty = true;
                ++sync.copy_written;
            } else {
                ++sync.copy_unbuilt;
            }
            continue;
        }
        if (!lands) {
            ++sync.missing_actor;
            if (!sync.missing_logged) {
                sync.missing_logged = true;
                log_info("placement %u is alive on the host and has no actor on this machine, "
                         "so it is remembered and created from the next substep", (unsigned)key);
            }
            if (!p->unspawnable) {
                p->wanted = true;
            }
            continue;
        }
        /* The BODY is written from the substep and not from here. Four fifths of the blocks
         * a client applies arrive between substeps, out of the idle pump, and writing the
         * pose pair there lands it at a moment the draw knows nothing about: the engine
         * interpolates from the pair it finds, so a pair replaced mid-frame makes the replica
         * jump back by whatever was left of the step. The fifth field run measured it: 5028
         * of 6279 blocks drained between substeps, and every NPC on that client jumped.
         *
         * So the mirror moves here, where it is safe, and the flush writes it where the
         * engine expects a new pose: once, at the start of a substep. A second record before
         * that flush does not overwrite the first unseen; the queue keeps it for this flush and
         * the flush after gets the newer. */
        p->dirty = true;
    }
    if (copies) {
        ++sync.copy_blocks_applied;
    }
    mp_enemy_sync_note_gaps(bitmap, stage.named, tick);

    /* Anything alive here that the host does not list is gone there. It is let go rather than
     * removed, in the state the host last reported: this machine's own simulation then decides
     * what happens to it, which for a corpse is the fade and for a fighter the same thing that
     * happens to an enemy nobody replicated in the first place. */
    for (i = 0; i < 8u * sizeof bitmap; ++i) {
        placement_t *p = &sync.placement[i];

        if (p->actor != 0 && !get_bit(bitmap, i) && mp_enemy_bind_is_parked(p->actor)) {
            p->let_go = true;   /* handed back from the substep, like every other write */
            p->dirty  = false;
            mp_enemy_sync_queue_drop(i, p);
        }
    }
    mp_world_event_take_staged(tick);
    sync.applied_tick       = tick;
    sync.applied_tick_known = true;
    ++sync.blocks_applied;
    return true;
}

/* ==============================================================================================
 * The wishes, granted from the substep. The flush and the handing back are in
 * mp_enemy_sync_flush.c.
 * ============================================================================================ */

uint32_t mp_enemy_sync_spawn_pending(void)
{
    uint32_t made = 0;
    size_t   i;

    if (!sync.enabled) {
        return 0;
    }
    /* The wishes are about `applied_level`; the world here is `level`. Both are told from inside
     * a substep, so they disagree only across a level change, and then the wishes are stale. */
    if (!sync.level_known || sync.level != sync.applied_level || !mp_enemy_spawn_installed()) {
        forget_wanted();
        return 0;
    }

    for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
        placement_t *p = &sync.placement[i];
        uintptr_t    actor = 0;

        if (!p->wanted) {
            continue;
        }
        p->wanted = false;
        if (!p->known || p->actor != 0) {
            continue;   /* nothing to put on it, or a body turned up by itself */
        }
        switch (mp_enemy_spawn_create((uint8_t)i, &actor)) {
        case MP_ENEMY_SPAWN_CREATED:
            /* Parked before anything else in the engine can see it, then dressed with what the
             * host last said, a death included. No previous pose: the body is new and starts
             * where it lands. */
            p->actor = actor;
            p->live  = true;
            (void)mp_enemy_sync_dress(i, p, &p->mirror, NULL);
            ++sync.spawned;
            ++made;
            if (!sync.spawned_logged) {
                sync.spawned_logged = true;
                log_info("placement %u was created here for the host, parked, and put where the "
                         "host has it", (unsigned)i);
            }
            break;
        case MP_ENEMY_SPAWN_LATER:
            ++sync.spawn_later;
            break;
        case MP_ENEMY_SPAWN_NEVER:
        default:
            ++sync.spawn_never;
            p->unspawnable = true;
            break;
        }
    }
    return made;
}

uint32_t mp_enemy_sync_pending(void)
{
    uint32_t n = 0;
    size_t   i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        n += sync.placement[i].wanted ? 1u : 0u;
    }
    return n;
}

uintptr_t mp_enemy_sync_actor_for(uint32_t key)
{
    placement_t *p;

    if (key >= MP_ENEMY_SYNC_KEYS) {
        return 0;
    }
    p = &sync.placement[key];
    return (p->live && p->actor != 0) ? p->actor : 0;
}

uintptr_t mp_enemy_sync_replica_for(uint32_t key)
{
    if (mp_wire_key_is_placement(key)) {
        return mp_enemy_sync_actor_for(key);
    }
    if (mp_wire_key_is_copy(key) && sync.copies_client != NULL && sync.placement[key].known) {
        return mp_npc_copies_client_replica(sync.copies_client, key - MP_WIRE_KEY_COPY_BASE,
                                            sync.placement[key].generation);
    }
    return 0u;
}

bool mp_enemy_sync_block_applied(void)
{
    return sync.applied_tick_known;
}

bool mp_enemy_sync_let_go_before(uintptr_t actor)
{
    size_t i;

    for (i = 0; actor != 0u && i < MP_ENEMY_SYNC_LET_GO; ++i) {
        if (sync.let_go[i] == actor) {
            return true;
        }
    }
    return false;
}

bool mp_enemy_sync_mirror(uint32_t key, mp_enemy_record_t *out)
{
    placement_t *p;

    if (key >= MP_ENEMY_SYNC_KEYS) {
        return false;
    }
    p = &sync.placement[key];
    if (out == NULL || !p->known) {
        return false;
    }
    *out = p->mirror;
    return true;
}

bool mp_enemy_sync_generation(uint32_t key, uint8_t *out)
{
    placement_t *p;

    if (key >= MP_ENEMY_SYNC_KEYS) {
        return false;
    }
    /* A host's copy is the life its grant made, dead or alive, one source with the block. */
    if (mp_wire_key_is_copy(key) && sync.copies_host != NULL) {
        return out != NULL &&
               mp_npc_copies_generation(sync.copies_host, key - MP_WIRE_KEY_COPY_BASE, out);
    }
    p = &sync.placement[key];
    if (out == NULL || (!p->known && p->generation == 0u)) {
        return false;
    }
    *out = p->generation;
    return true;
}

mp_enemy_slot_t mp_enemy_sync_slot(uint32_t key, uintptr_t actor, uint8_t generation)
{
    const placement_t *p;

    if (key >= MP_ENEMY_SYNC_KEYS || actor == 0) {
        return MP_ENEMY_SLOT_UNREAD;
    }
    p = &sync.placement[key];
    return mp_enemy_slot_for_life(mp_enemy_spawn_actor_slot(actor, key), actor, generation,
                                  p->kept_actor, p->kept_generation);
}

void mp_enemy_sync_performed(uint32_t key, uintptr_t actor, uint8_t generation, uint8_t reason)
{
    if (key >= MP_ENEMY_SYNC_KEYS) {
        return;
    }
    if (reason == MP_EVENT_REMOVE_LEAVE_CORPSE) {
        sync.placement[key].kept_actor      = actor;
        sync.placement[key].kept_generation = generation;
        return;
    }
    mp_enemy_sync_forget(key, generation);   /* the slot is free and the pointer is not ours */
}

void mp_enemy_sync_forget(uint32_t key, uint8_t generation)
{
    placement_t *p;

    if (key >= MP_ENEMY_SYNC_KEYS) {
        return;
    }
    p = &sync.placement[key];
    p->actor      = 0;
    p->live       = false;
    p->kept_actor = 0;
    /* The mirror is the life's, and it goes only with the life the removal named. A newer life the
     * host is already describing keeps its mirror, or its next delta would be read against
     * nothing. A record kept in front of the mirror is the life's as well and goes with it; the
     * newer life's, with no actor to write it to now, drops at the next flush. */
    if (!p->known || p->generation == generation) {
        if (p->known) {
            p->forgot_by = (uint8_t)MP_ENEMY_FORGOT_REMOVAL;
        }
        p->known  = false;
        p->wanted = false;
        mp_enemy_sync_queue_drop(key, p);
    }
}
