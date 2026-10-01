/* mp_enemy_sync_flush.c: what a client's enemy table owes the engine once a substep, and the one
 * way a replica is handed back.
 *
 * Its own file because the receiving half in mp_enemy_sync.c had reached the size limit, and the
 * seam its size note named is this one: the flush and the handing back share nothing with the
 * decode but the placement row. Everything here runs from inside a substep, never from the pump,
 * and in this order:
 *
 *   the head closes the pose pairs of every parked replica of this side's, before a record of this
 *   substep parts them again;
 *   a replica the host stopped listing is handed back;
 *   a replica with a record its body has not been given is written, the older of two first.
 *
 * Why the head closes. The engine interpolates a body between the pose pair of its last substep and
 * the pose it has, and its own pose commit, which rolls the pair every substep, does not run for a
 * parked actor. A substep that brings a replica no record therefore kept the pair the last write
 * had opened, and every frame of it drew that turn and that step again from their start: a saw
 * tooth, the one the far players' node rotations have and close the same way. Closing every pair
 * at the head and letting only a record that is really written open it again is what makes it
 * impossible to forget: no way through the loop below, and no write the body refuses, can leave a
 * pair open behind it.
 */
#include "mp_enemy_sync.h"

#include "mp_enemy_sync_internal.h"
#include "mp_cadence.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_body.h"
#include "mp_enemy_queue_rule.h"
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What the head of a flush found for one key: the slot the census's actor holds for the table's
 * life, whether it is a replica parked here, and what closing its pairs did. The write reads the
 * slot again from here, so a flush asks the engine about each key once. */
typedef struct flush_row {
    mp_enemy_slot_t       slot;
    bool                  parked;
    mp_enemy_pair_close_t close;
} flush_row_t;

static flush_row_t rows[MP_ENEMY_SYNC_KEYS];

/* ==============================================================================================
 * Handing a replica back.
 * ============================================================================================ */

void mp_enemy_sync_queue_drop(size_t key, placement_t *p)
{
    mp_cadence_records_fell(key, MP_CADENCE_FELL_LET_GO, mp_enemy_queue_clear(&p->queue));
}

/* The one exit from being held here, which every way out takes: unparked in the state the host
 * last reported, or as a corpse when a removal kept it (the binding decides that). Its body is
 * as the host's last record left it, lying, fading or standing, and this machine's own arms go
 * on from there. A record kept for it goes with it, and so does the word that one is owed: a
 * body the engine has back is no body the next flush writes into. */
static void hand_back(size_t key, placement_t *p)
{
    enemy_sync_state_t      *s    = mp_enemy_sync_state();
    mp_enemy_slot_t          slot = mp_enemy_bind_slot(p->actor, (uint32_t)key, NULL);
    const mp_enemy_record_t *last = p->known ? &p->mirror : NULL;

    (void)mp_enemy_bind_release(p->actor, last);
    mp_enemy_sync_queue_drop(key, p);
    p->dirty = false;
    s->let_go[s->let_go_next % MP_ENEMY_SYNC_LET_GO] = p->actor;
    ++s->let_go_next;
    ++s->released;
    s->released_corpses += (slot == MP_ENEMY_SLOT_KEPT) ? 1u : 0u;
}

/* Lets go of every replica this side is holding, each in the state the host last reported.
 *
 * Without this, leaving a session left every parked actor parked, and a parked actor is stepped
 * over before its tick AND before the removal decision. The enemies of that level would then have
 * stood still for the rest of the run, and nothing would have said why: the wire that was holding
 * them had gone, which is exactly when the local simulation should have taken them back. */
void mp_enemy_sync_release_all(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              i;

    for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
        placement_t *p = &s->placement[i];

        if (p->actor != 0 && mp_enemy_bind_is_parked(p->actor)) {
            hand_back(i, p);
        }
    }
    if (s->copies_client != NULL) {
        mp_npc_copies_client_release_all(s->copies_client);
    }
}

/* ==============================================================================================
 * Writing a replica.
 * ============================================================================================ */

/* Every replica is parked, so the local simulation steps over it entirely: no pre-tick, no state
 * arm, and no removal decision either, which is what stops this machine's own deactivation radius
 * from throwing away an enemy the host still has.
 *
 * Every actor the host lists, none excepted. An earlier form left actors with bit 0x80 in their
 * placement's flag word running here as walk plates. The bit marks an ESCORT BODY: enemy_preTick
 * fires the walk plate under the feet of an actor that has it (bapmap_firePlate 0x408e3a), and in
 * the field such a body walked on here while the host's copy stood blocked by the far player,
 * 1101 blocks, one actor each, with a truth of its own. The host's copy fires the plates, and
 * what they open reaches here through the world scratch. */
static bool park_and_apply(size_t key, placement_t *p, const mp_enemy_record_t *record,
                           const mp_enemy_record_t *previous)
{
    if (!mp_enemy_bind_is_parked(p->actor)) {
        (void)mp_enemy_bind_park(p->actor, true);
        mp_enemy_body_forget((uint32_t)key);   /* bound again: its next write is a first */
    }
    if (!mp_enemy_bind_write(p->actor, (uint32_t)key, record, previous)) {
        return false;
    }
    ++mp_enemy_sync_state()->records_applied;
    return true;
}

/* The record carries the host's body with it, so a newer life written onto a body that lay dead
 * stands it up by the same write that lays a dying one down. */
bool mp_enemy_sync_dress(size_t key, placement_t *p, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous)
{
    const enemy_sync_state_t *s = mp_enemy_sync_state();

    if (s->flush_pass != 0u && p->dressed_pass == s->flush_pass) {
        mp_cadence_flush_fault(MP_CADENCE_FAULT_SECOND_WRITE);
    }
    p->dressed_pass = s->flush_pass;
    if ((record->value[MP_ENEMY_F_GENERATION] & 0xFFu) != p->generation) {
        mp_cadence_flush_fault(MP_CADENCE_FAULT_OTHER_LIFE);
    }
    return park_and_apply(key, p, record, previous);
}

/* A write that did not happen, counted by why. A corpse kept for an older life waits here for
 * the host's removal of it. */
static void count_gone(placement_t *p, mp_enemy_slot_t slot)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ++s->flush_gone;
    if (slot == MP_ENEMY_SLOT_OTHER) {
        ++s->gone_other;
        s->gone_old_corpse += (p->kept_actor != 0 && p->kept_actor == p->actor) ? 1u : 0u;
    } else {
        ++s->gone_freed;
    }
}

/* ==============================================================================================
 * The flush.
 * ============================================================================================ */

/* The head: the slot of every key's actor, asked once, and both pose pairs of each replica that is
 * parked here brought to one value. The binding leaves a body alone that carries this player or
 * none, the same refusal its write makes. A key let go in this flush is skipped: it goes back to
 * the engine, whose own pose commit rolls the pair from the next substep on. An actor with no
 * record to write that is not parked is no replica held here, and one word says so before the
 * slot is asked: the census finds every actor of the pool, not only the replicas. */
static void close_every_pair(const enemy_sync_state_t *s)
{
    size_t i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        const placement_t *p   = &s->placement[i];
        flush_row_t       *row = &rows[i];

        row->slot   = MP_ENEMY_SLOT_FREED;
        row->parked = false;
        memset(&row->close, 0, sizeof row->close);
        if (p->actor == 0 || p->let_go || (!p->dirty && !mp_enemy_bind_is_parked(p->actor))) {
            continue;
        }
        row->slot   = mp_enemy_sync_slot((uint32_t)i, p->actor, p->generation);
        row->parked = mp_enemy_slot_is_actor(row->slot) && mp_enemy_bind_is_parked(p->actor);
        if (row->parked) {
            (void)mp_enemy_bind_close_pair(p->actor, &row->close);
        }
    }
}

/* A parked replica that got no write in this substep, told to the cadence with what the head's
 * close did for it. */
static void note_held(size_t key, const flush_row_t *row)
{
    mp_cadence_flush_held(key, row->close.ours, row->close.rotation, row->close.position);
}

/* A replica the host stopped listing, handed back from the substep like every other write. */
static void let_go_one(size_t key, placement_t *p)
{
    p->let_go = false;
    if (p->actor != 0 &&
        mp_enemy_slot_is_actor(mp_enemy_sync_slot((uint32_t)key, p->actor, p->generation)) &&
        mp_enemy_bind_is_parked(p->actor)) {
        hand_back(key, p);
        mp_enemy_sync_count_unlisted(p);
    }
    if (p->known) {
        p->forgot_by = (uint8_t)MP_ENEMY_FORGOT_LET_GO;
    }
    p->known         = false;
    p->written_known = false;
}

/* A key whose body has not been given its newest record: the one the queue names goes, the older
 * of two first. Returns whether a write was tried, which is what the enemies line counts.
 *
 * `written` takes the record handed over whether the body took it or not, as it always has: the
 * next write steps from it. That is also why `written_known` is no proof that a body was written,
 * and why the close at the head asks the binding rather than it. */
static bool write_one(size_t key, placement_t *p, const flush_row_t *row)
{
    enemy_sync_state_t      *s    = mp_enemy_sync_state();
    mp_enemy_slot_t          slot = (p->actor != 0) ? row->slot : MP_ENEMY_SLOT_FREED;
    const mp_enemy_record_t *previous;
    const mp_enemy_record_t *record;
    bool                     late = false;
    bool                     wrote;

    if (!mp_enemy_slot_is_actor(slot)) {
        count_gone(p, slot);
        mp_enemy_sync_watch_death(key, p, slot, false, &p->mirror);
        mp_enemy_sync_queue_drop(key, p);
        p->dirty         = false;
        p->written_known = false;
        return false;
    }
    previous = p->written_known ? &p->written : NULL;
    record   = mp_enemy_queue_next(&p->queue, &p->mirror, &late);
    wrote    = mp_enemy_sync_dress(key, p, record, previous);
    s->corpse_written += (wrote && slot == MP_ENEMY_SLOT_KEPT) ? 1u : 0u;
    mp_enemy_sync_watch_death(key, p, slot, wrote, record);
    if (wrote) {
        mp_cadence_flush_written(key, previous, record, late);
    } else if (row->parked) {
        note_held(key, row);
    }
    p->written       = *record;
    p->written_known = true;
    p->dirty         = mp_enemy_queue_handled(&p->queue);
    mp_cadence_flush_behind(mp_enemy_queue_flushed(&p->queue, p->dirty));
    return true;
}

/* Everything this module owes the engine, paid once at the start of a substep.
 *
 * Two writes live here and both used to happen wherever the block arrived. The pose pair is
 * one: the engine interpolates a body between the pose it had and the pose it has, and a
 * pair installed at a random moment inside a drawn frame is a jump. The handing back of an
 * actor the host no longer lists is the other, and it unparks a body, which is the last
 * thing to do from a message pump halfway through a level load.
 *
 * Whether the census's actor is still there is asked of the ENGINE here, not of the census:
 * the census can be a substep old by now, and a freed pool slot still answers with the index
 * it had. What is asked is whether the slot is still that actor for the life the host describes,
 * not whether it is alive: a corpse this side kept for that life is the host's corpse too, the
 * host goes on describing it, and its death clip may arrive only after the removal did. */
uint32_t mp_enemy_sync_flush(void)
{
    enemy_sync_state_t *s       = mp_enemy_sync_state();
    uint32_t            written = 0;
    size_t              i;

    if (!s->enabled) {
        return 0;
    }
    ++s->flush_pass;
    close_every_pair(s);
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        placement_t *p = &s->placement[i];

        if (p->let_go) {
            let_go_one(i, p);
        } else if (p->dirty) {
            written += write_one(i, p, &rows[i]) ? 1u : 0u;
        } else if (rows[i].parked) {
            note_held(i, &rows[i]);
        }
    }
    mp_cadence_flush_done();
    s->flushes += written;
    return written;
}
