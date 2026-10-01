/* mp_enemy_sync_report.c: what the enemy block module says in the run report, and the deaths it
 * follows one by one for it.
 *
 * Its own file because it reads the table and writes nothing into the engine; the receiving half
 * in mp_enemy_sync.c had reached the size limit with it inside.
 */
#include "mp_enemy_sync.h"

#include "mp_enemy_sync_internal.h"
#include "mp_budget_rule.h"
#include "mp_cadence.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_body.h"
#include "mp_enemy_dead_watch.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_wire.h"
#include "mp_knockback_rule.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The deaths.
 * ============================================================================================ */

/* What the first record of a death met on this side. */
enum { DEATH_WRITTEN, DEATH_REFUSED, DEATH_FREED, DEATH_OTHER };

static death_watch_t *death_watch_for(enemy_sync_state_t *s, size_t key, const placement_t *p,
                                      const mp_enemy_record_t *now, bool may_begin)
{
    death_watch_t *d;
    uint32_t       i;

    for (i = 0; i < s->death_count; ++i) {
        if (s->deaths[i].level == s->level && s->deaths[i].key == key &&
            s->deaths[i].generation == p->generation) {
            return &s->deaths[i];
        }
    }
    if (!may_begin || s->death_count >= MP_ENEMY_SYNC_DEATHS) {
        return NULL;
    }
    d = &s->deaths[s->death_count++];
    memset(d, 0, sizeof *d);
    d->level      = s->level;
    d->key        = (uint16_t)key;
    d->generation = p->generation;
    d->state      = (uint8_t)(now->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK);
    d->clip       = (uint8_t)(now->value[MP_ENEMY_F_CLIP] & 0xFFu);
    d->health     = mp_enemy_wire_health(now);
    d->outcome    = DEATH_REFUSED;
    if (p->written_known) {
        d->last_clip = p->written.value[MP_ENEMY_F_CLIP] & 0xFFu;
        d->last_head = p->written.value[MP_ENEMY_F_HEAD] & 0xFFFFu;
    }
    return d;
}

/* A death begins where mp_enemy_wire_death_begins says, which is a death state, or a health that
 * falls to zero in a life seen above it. A placement authored with no hit points never begins
 * one and takes no place here. Once begun, every later record of the same life updates it.
 *
 * Everything here reads `now`, the record the flush gave the body, and never the mirror: while an
 * older record is written first the mirror is a record the body has not been shown, and the
 * watches compare what the host said with what the replica does. */
void mp_enemy_sync_watch_death(size_t key, placement_t *p, mp_enemy_slot_t slot, bool wrote,
                               const mp_enemy_record_t *now)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    death_watch_t      *d;
    uint32_t            before;

    if (now == NULL) {
        return;
    }
    /* The throw is counted for every record, before anything below turns back for a copy or a
     * record that begins no death. What was written before is only known while `written` still
     * holds the previous record, which is until the flush copies this one over it, and only a
     * record of the same life says anything about this one. */
    mp_knockback_edges_count(&s->knockback,
                             p->written_known && p->written.value[MP_ENEMY_F_GENERATION] ==
                                                     now->value[MP_ENEMY_F_GENERATION],
                             p->written.value[MP_ENEMY_F_STATE], p->written.value[MP_ENEMY_F_CLIP],
                             now->value[MP_ENEMY_F_STATE], now->value[MP_ENEMY_F_CLIP], wrote);
    mp_enemy_dead_watch_replica((uint32_t)key, p->generation,
                                mp_enemy_slot_is_actor(slot) ? (uint32_t)p->actor : 0u, now);
    if (!mp_wire_key_is_placement((uint32_t)key)) {
        return;
    }
    before = s->death_count;
    d      = death_watch_for(s, key, p, now, mp_enemy_wire_death_begins(now, p->health_up));
    if (d == NULL) {
        return;
    }
    if (before != s->death_count) {   /* this record is the first of that death */
        if (wrote) {
            d->outcome = DEATH_WRITTEN;
        } else if (mp_enemy_slot_is_actor(slot)) {
            d->outcome = DEATH_REFUSED;
        } else {
            d->outcome = (slot == MP_ENEMY_SLOT_OTHER) ? DEATH_OTHER : DEATH_FREED;
        }
    }
    if (mp_enemy_wire_reports_death(now)) {
        d->death_state = 1u;
    }
    if (!wrote) {
        return;
    }
    d->last_clip = now->value[MP_ENEMY_F_CLIP] & 0xFFu;
    if ((now->value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_HEAD) != 0u) {
        d->last_head = now->value[MP_ENEMY_F_HEAD] & 0xFFFFu;
    }
    d->after_removal += (slot == MP_ENEMY_SLOT_KEPT) ? 1u : 0u;
    {
        mp_enemy_marks_t marks;

        /* Laid down is what the host's body field made of the replica: no class, as the host's
         * body has none once it lies. */
        if (mp_enemy_bind_marks(p->actor, &marks) && marks.objclass == 0) {
            d->laid = 1u;
        }
    }
}

/* Whether the replica lay down with its host, which is its class written away from the host's
 * body. A script whose death branch goes on living falls to zero and never reports a death state,
 * and that is not a failure here; a death state the host reported and this side did not lay down
 * is one. The two must not read alike. */
static const char *death_laid(const death_watch_t *d)
{
    if (d->laid != 0u) {
        return "laid down";
    }
    return d->death_state != 0u ? "not laid down although the host reported a death state"
                                : "not laid down: the host reported no death state";
}

static const char *death_outcome(uint8_t outcome)
{
    switch (outcome) {
    case DEATH_WRITTEN: return "its replica took the write";
    case DEATH_REFUSED: return "its replica did not take the write";
    case DEATH_FREED:   return "its slot had been freed before the write";
    default:            return "another life held its slot before the write";
    }
}

/* ==============================================================================================
 * The interest rule, as each peer saw it, and as a client sees it.
 * ============================================================================================ */

/* Two records of one replica further apart than this, in the host's substeps, are further apart
 * than the rule lets a far key wait while there is room for it. */
#define GAP_LIMIT MP_ENEMY_INTEREST_LIMIT_FAR

void mp_enemy_sync_note_gaps(const uint8_t *bitmap, const bool *named, uint32_t tick)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              i;

    if (bitmap == NULL || named == NULL) {
        return;
    }
    /* A reset is another table, and a record of the last one says nothing about a gap in this, nor
     * about how many records a key has taken since the flush. */
    if (s->gap_resets != s->resets) {
        s->gap_resets = s->resets;
        memset(s->gap_known, 0, sizeof s->gap_known);
        mp_cadence_forget_records();
    }
    mp_cadence_records_taken(named, MP_ENEMY_SYNC_KEYS);
    for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
        uint32_t gap;

        if ((bitmap[i >> 3] & (uint8_t)(1u << (i & 7u))) == 0u) {
            s->gap_known[i] = false;   /* no longer listed: the next record starts a new run */
            continue;
        }
        if (!named[i]) {
            continue;
        }
        if (s->gap_known[i]) {
            gap = tick - s->gap_tick[i];
            ++s->gaps_measured;
            s->gaps_over += gap > GAP_LIMIT ? 1u : 0u;
            if (gap > s->gap_longest) {
                s->gap_longest = gap;
            }
            mp_cadence_record_gap(gap);
        }
        s->gap_tick[i]  = tick;
        s->gap_known[i] = true;
    }
}

static unsigned bits_in(uint8_t byte)
{
    unsigned count = 0u;

    for (; byte != 0u; byte = (uint8_t)(byte & (byte - 1u))) {
        ++count;
    }
    return count;
}

/* The live rows of this substep's census, placements and copies, out of the two bitmaps it left;
 * nought when no census ran in this substep. */
uint32_t mp_enemy_sync_census_actors(void)
{
    const enemy_sync_state_t *s     = mp_enemy_sync_state();
    uint32_t                  count = 0u;
    size_t                    i;

    if (!s->send_ready) {
        return 0u;
    }
    for (i = 0; i < sizeof s->bitmap; ++i) {
        count += (uint32_t)bits_in(s->bitmap[i]);
    }
    for (i = 0; i < s->copy_bitmap_bytes && i < sizeof s->copy_bitmap; ++i) {
        count += (uint32_t)bits_in(s->copy_bitmap[i]);
    }
    return count;
}

/* The views a host keeps are the views the trace counts. */
_Static_assert(MP_CADENCE_VIEWS == MP_ENEMY_SYNC_VIEWS, "a trace row for every view");

void mp_enemy_sync_trace_host(void)
{
    const enemy_sync_state_t *s   = mp_enemy_sync_state();
    uint32_t                  key = mp_cadence_traced();
    size_t                    view;

    if (key == 0u || key >= MP_ENEMY_SYNC_MAX_PLACEMENTS) {
        return;
    }
    for (view = 0; view < MP_ENEMY_SYNC_VIEWS; ++view) {
        const mp_enemy_interest_row_t *row = &s->view[view].interest.row[key];

        /* A record that left in this substep's payload wrote the row and put its age back to
         * nought; a key the view held back aged by one. */
        mp_cadence_host_trace(view, s->counts[view].blocks, s->placement[key].live, row->reach,
                              row->seen && row->age == 0u);
    }
}

/* One line per view on a host, the counters of the rule for that peer, and on a client the one
 * number that says whether the rule held on the far side of the wire. Printed whether or not they
 * are nought, because a nought is what most of them owe. */
static void report_interest(const enemy_sync_state_t *s)
{
    size_t view;

    for (view = 0; s->blocks_sent != 0u && view < MP_ENEMY_SYNC_VIEWS; ++view) {
        const interest_counts_t *c = &s->counts[view];

        log_info("the enemies to one peer: peer %u (slot %u), %u block(s), %u record(s) (%u near, "
                 "%u middle, %u far, %u with no position to measure against); %u must-send (%u "
                 "state changes, %u new lives), %u of them did not fit (must be 0); %u first "
                 "description(s) near, %u for a world event on a key this peer did not hold; %u "
                 "deferred, the oldest deferral near %u, middle %u, far %u substep(s); %u whole; "
                 "%u substep(s) throttled while its acknowledgements were silent",
                 (unsigned)view, (unsigned)s->viewer[view].slot, (unsigned)c->blocks,
                 (unsigned)c->records, (unsigned)c->by_reach[MP_ENEMY_REACH_NEAR],
                 (unsigned)c->by_reach[MP_ENEMY_REACH_MIDDLE],
                 (unsigned)c->by_reach[MP_ENEMY_REACH_FAR],
                 (unsigned)c->by_reach[MP_ENEMY_REACH_NONE], (unsigned)c->must,
                 (unsigned)c->must_state, (unsigned)c->must_life, (unsigned)c->must_unfit,
                 (unsigned)c->first_near, (unsigned)c->first_event, (unsigned)c->deferred,
                 (unsigned)c->oldest[MP_ENEMY_REACH_NEAR],
                 (unsigned)c->oldest[MP_ENEMY_REACH_MIDDLE],
                 (unsigned)c->oldest[MP_ENEMY_REACH_FAR], (unsigned)c->whole,
                 (unsigned)c->throttled);
        log_info("the must-send to one peer: peer %u, at the most %u byte(s) in one block, %u "
                 "block(s) whose must-send the question for the floor named otherwise (must be "
                 "0); %u far key(s) a substep held back because this peer was never told of them",
                 (unsigned)view, (unsigned)c->must_bytes_most, (unsigned)c->must_bytes_apart,
                 (unsigned)c->far_withheld);
        log_info("the floor under the enemies to one peer: peer %u, %u block(s) with the floor "
                 "raised above %u byte(s) for the world events and the records that may not wait, "
                 "to %u byte(s) at the most; %u of them were given all the room the floor asked, "
                 "%u were not (must be 0)",
                 (unsigned)view, (unsigned)c->floor_raised, (unsigned)MP_BUDGET_ENEMY_FLOOR_BYTES,
                 (unsigned)c->floor_most, (unsigned)(c->floor_raised - c->floor_short),
                 (unsigned)c->floor_short);
        log_info("the hits that raise an enemy for one peer: peer %u, %u hit(s) on this peer's "
                 "player noted for the enemy that made them; %u substep(s) an enemy's priority was "
                 "doubled by such a hit of the last second where its target alone would not have "
                 "doubled it",
                 (unsigned)view, (unsigned)c->struck_noted, (unsigned)c->struck_boosted);
    }
    if (s->blocks_applied != 0u) {
        log_info("the enemies from the host: %u gap(s) of more than %u substeps between two "
                 "records of one replica the host went on listing, the longest %u substep(s), "
                 "over %u pair(s) of records measured",
                 (unsigned)s->gaps_over, (unsigned)GAP_LIMIT, (unsigned)s->gap_longest,
                 (unsigned)s->gaps_measured);
    }
    if (s->interest.report != NULL) {
        s->interest.report();
    }
}

/* ==============================================================================================
 * A refused block, said as it happens.
 * ============================================================================================ */

static const char *refusal_name(mp_enemy_refusal_t why)
{
    switch (why) {
    case MP_ENEMY_REFUSAL_TORN:
        return "torn";
    case MP_ENEMY_REFUSAL_TORN_COPIES:
        return "torn in the copies' part";
    case MP_ENEMY_REFUSAL_OLDER:
        return "older than a block already taken";
    case MP_ENEMY_REFUSAL_NO_LEVEL:
        return "no level open here";
    case MP_ENEMY_REFUSAL_OTHER_LEVEL:
        return "about another level";
    case MP_ENEMY_REFUSAL_BASELESS:
        return "a record not whole with nothing to be read against";
    default:
        return "taken";
    }
}

static const char *forgot_name(uint8_t forgot_by)
{
    switch (forgot_by) {
    case MP_ENEMY_FORGOT_RESET:
        return "a reset";
    case MP_ENEMY_FORGOT_REMOVAL:
        return "a removal";
    case MP_ENEMY_FORGOT_LET_GO:
        return "being let go";
    case MP_ENEMY_FORGOT_NEVER_KNOWN:
        return "never known";
    default:
        return "unknown";
    }
}

/* Whether this refusal still has a line of its budget. A record with nothing to be read against
 * has its own, and every other kind has one before the first block this side takes after a reset,
 * which is a load, and one after it, which is what a level has in it: the refusals of one load
 * alone can spend a shared budget before the level has begun. */
static bool take_a_line(enemy_sync_state_t *s, mp_enemy_refusal_t why)
{
    uint8_t *used  = &s->lines_later;
    uint8_t  limit = (uint8_t)MP_ENEMY_SYNC_LINES_LATER;

    if (why == MP_ENEMY_REFUSAL_BASELESS) {
        used  = &s->lines_baseless;
        limit = (uint8_t)MP_ENEMY_SYNC_LINES_BASELESS;
    } else if (!s->applied_tick_known) {
        used  = &s->lines_loading;
        limit = (uint8_t)MP_ENEMY_SYNC_LINES_LOADING;
    }
    if (*used >= limit) {
        ++s->lines_left_out;
        return false;
    }
    ++*used;
    return true;
}

void mp_enemy_sync_note_refusal(const mp_enemy_refusal_note_t *note)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    const placement_t  *p;

    if (note == NULL || !take_a_line(s, note->why)) {
        return;
    }
    if (note->why != MP_ENEMY_REFUSAL_BASELESS || note->key >= MP_ENEMY_SYNC_KEYS) {
        log_info("enemies, a block refused here: substep %u, %s; the block's level %u, this "
                 "side's %u (%s); the last block taken substep %u%s",
                 (unsigned)note->tick, refusal_name(note->why), (unsigned)note->block_level,
                 (unsigned)s->level, s->level_known ? "open" : "none open",
                 (unsigned)s->applied_tick,
                 s->applied_tick_known ? "" : " (none taken since the reset)");
        return;
    }
    p = &s->placement[note->key];
    log_info("enemies, a block refused here: substep %u, %s; the block's level %u, this side's %u; "
             "the last block taken substep %u%s; key %u of life %u, whose mask %s the life "
             "and %s a position; this side %s it, life %u, last forgotten by %s",
             (unsigned)note->tick, refusal_name(note->why), (unsigned)note->block_level,
             (unsigned)s->level, (unsigned)s->applied_tick,
             s->applied_tick_known ? "" : " (none taken since the reset)",
             (unsigned)note->key, (unsigned)note->life,
             note->names_life ? "names" : "leaves out",
             note->names_position ? "names" : "leaves out",
             p->known ? "knows" : "does not know", (unsigned)p->generation,
             forgot_name(p->forgot_by));
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

void mp_enemy_sync_report(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    uint32_t walks = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;
    uint32_t refused = 0;

    mp_enemy_spawn_counters_t    made;
    mp_enemy_liveness_counters_t alive;
    uint32_t                     i;

    mp_enemy_bind_counters(&walks, &reads, &writes, &refused);
    mp_enemy_spawn_counters(&made);
    /* Several sentences where there was one. With its values the one came to about 1180 characters
     * and the log keeps a line of 1021, so every field log lost the counters at its end. The words
     * around each number are the ones the one sentence used, which is what a comparison of two
     * runs' reports finds a counter by; the binding, the spawner and the animation now open
     * sentences of their own. */
    log_info("enemies: sent %u block(s) carrying %u record(s), %u not built with no level open | "
             "applied %u block(s) carrying %u record(s) | %u had no local actor, %u created here, "
             "%u put off, %u not for a receiver to create, %u block(s) about another level, "
             "%u arrived with no level open here, %u block(s) refused, %u index(es) past the "
             "placement limit, %u reset(s), %u let go, %u written from a substep, %u gone "
             "before the write was due, %u block(s) abandoned unsent, %u block(s) older than one "
             "already taken, %u acknowledgement(s) that moved a view on, %u key(s) opened again "
             "for a payload nobody named, %u view(s) given up whole",
             (unsigned)s->blocks_sent, (unsigned)s->records_sent, (unsigned)s->unbuilt,
             (unsigned)s->blocks_applied, (unsigned)s->records_applied,
             (unsigned)s->missing_actor, (unsigned)s->spawned, (unsigned)s->spawn_later,
             (unsigned)s->spawn_never, (unsigned)s->other_level, (unsigned)s->no_level,
             (unsigned)s->refused, (unsigned)s->index_too_large, (unsigned)s->resets,
             (unsigned)s->released, (unsigned)s->flushes, (unsigned)s->flush_gone,
             (unsigned)s->abandoned, (unsigned)s->out_of_order, (unsigned)s->acked,
             (unsigned)s->reopened, (unsigned)s->view_given_up);
    log_info("enemies, the base a record is read against: %u record(s) sent whole to a view no "
             "acknowledgement had confirmed yet, %u record(s) sent whole to a confirmed view for a "
             "key it had opened again or a life it had not described, %u view(s) confirmed by an "
             "acknowledgement, %u of the views given up whole given up at a send, no "
             "acknowledgement having come for a whole ring | %u payload(s) refused for a record "
             "that was not whole and had nothing to be read against, %u of them naming a position",
             (unsigned)s->sent_whole, (unsigned)s->opened_whole, (unsigned)s->views_confirmed,
             (unsigned)s->given_up_unheard, (unsigned)s->baseless_refused,
             (unsigned)s->baseless_positioned);
    log_info("enemies, the whole records a delta waits for: %u record(s) sent whole to a confirmed "
             "view for a key it knew whose whole record was not acknowledged yet, %u key(s) an "
             "acknowledgement proved held whole there; %u census(es) found a level other than the "
             "last one's and started every placement over as a new life; %u line(s) of refused "
             "blocks or of views held back past their budget",
             (unsigned)s->whole_unbased, (unsigned)s->based_acknowledged,
             (unsigned)s->census_new_level, (unsigned)s->lines_left_out);
    mp_enemy_sync_report_acknowledgement_bits();
    /* The laying down and the getting up are the body line's now, where the host's body field is
     * written; this line keeps the corpses and the gone. */
    log_info("enemies, the dead: %u write(s) to a corpse the host still lists, %u let go as a "
             "corpse | of the gone, %u on a freed or unreadable slot, %u on a slot another life "
             "holds, %u of those a corpse kept for an older life that waits for the host's "
             "removal of it",
             (unsigned)s->corpse_written, (unsigned)s->released_corpses, (unsigned)s->gone_freed,
             (unsigned)s->gone_other, (unsigned)s->gone_old_corpse);
    log_info("enemies, binding: %u walk(s), %u read(s), %u write(s), %u refused | spawner: %u "
             "created (%u of them on a placement this side had buried), %u refused by the "
             "engine, %u kept back for the object pool reserve, %u for the actor pool reserve, "
             "%u hosting the player, %u past the table, %u occupied, %u with no level, %u "
             "unreadable, %u state write fault(s), %u woken for the far body, %u of those "
             "refused",
             (unsigned)walks, (unsigned)reads, (unsigned)writes, (unsigned)refused,
             (unsigned)made.created, (unsigned)made.revived, (unsigned)made.engine_refused,
             (unsigned)made.reserve_kept, (unsigned)made.actor_reserve_kept,
             (unsigned)made.player_host, (unsigned)made.past_table,
             (unsigned)made.occupied, (unsigned)made.no_level, (unsigned)made.unreadable,
             (unsigned)made.state_faults, (unsigned)made.scan_created,
             (unsigned)made.scan_refused);
    mp_enemy_bind_report_refusals();
    mp_enemy_pose_report();
    mp_enemy_body_report(s->blocks_sent != 0u, s->blocks_applied != 0u);
    /* Whether a throw the host made reached the replica, and with its clip. The host starts the
     * throw's clip one substep after the state, so the record after the one that enters state 6
     * is the one that brings it, and each throw that plays here is followed by one. */
    log_info("enemies, the knockback: %u throw(s) the host reported, written here (a record "
             "entering state 6), %u record(s) in state 6 that brought a clip other than the one "
             "last written here (one follows each throw) | %u record(s) in state 6 with nothing "
             "written here before them in the same life; %u record(s) in states 6 to 9 written",
             (unsigned)s->knockback.throws, (unsigned)s->knockback.clip_moved,
             (unsigned)s->knockback.no_before, (unsigned)s->knockback.in_throw);
    mp_enemy_nodes_report();

    log_info("enemies, the copies' part: sent in %u block(s) carrying %u record(s), %u block(s) "
             "not built for want of room for its head, %u live copy row(s) the host's table did "
             "not hand out | taken in %u block(s) carrying %u record(s), %u written to a replica "
             "built here, %u waiting for the overlay, %u block(s) refused for it",
             (unsigned)s->copy_blocks_sent, (unsigned)s->copy_records_sent,
             (unsigned)s->copy_crowded, (unsigned)s->copy_undescribed,
             (unsigned)s->copy_blocks_applied, (unsigned)s->copy_records_applied,
             (unsigned)s->copy_written, (unsigned)s->copy_unbuilt,
             (unsigned)s->copy_refused);

    mp_enemy_spawn_liveness_counters(&alive);
    log_info("liveness: %u asked of the actor's own record, %u live, %u gone, %u where the "
             "level's directory answered otherwise (%u it called live, %u it called gone)",
             (unsigned)alive.asked, (unsigned)alive.live, (unsigned)alive.gone,
             (unsigned)(alive.directory_live + alive.directory_gone),
             (unsigned)alive.directory_live, (unsigned)alive.directory_gone);

    /* One line per followed death. Written, laid down, the death clip last written and writes
     * after the removal is a replica that died with its host; the clip from before the death
     * with no writes after the removal is the replica that went on standing. */
    for (i = 0; i < s->death_count; ++i) {
        const death_watch_t *d = &s->deaths[i];

        log_info("placement %u died on the host (health %d, state %u, clip %u); here %s and it "
                 "was %s, last written clip %u at frame %u, %u write(s) after a removal kept its "
                 "body",
                 (unsigned)d->key, (int)d->health, (unsigned)d->state, (unsigned)d->clip,
                 death_outcome(d->outcome), death_laid(d),
                 (unsigned)d->last_clip, (unsigned)(d->last_head / 16u),
                 (unsigned)d->after_removal);
    }
    mp_enemy_dead_watch_report(s->blocks_sent != 0u);
    mp_enemy_dead_watch_client_report(s->blocks_applied != 0u);
    report_interest(s);
}

/* ==============================================================================================
 * The let go, split by why.
 * ============================================================================================ */

void mp_enemy_sync_count_unlisted(const placement_t *p)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    if (p == NULL) {
        return;
    }
    ++s->released_unlisted;
    if (p->known && mp_enemy_wire_reports_death(&p->mirror)) {
        ++s->released_unlisted_dead;
    }
}

/* The `let go` of the enemies line split by why. The first number is the rest, the resets and
 * session ends, so the three always add up to that line's count. */
void mp_enemy_sync_report_let_go(void)
{
    const enemy_sync_state_t *s = mp_enemy_sync_state();

    log_info("enemies, let go: %u at a reset or a session end, %u because the host stopped listing "
             "them, %u of those dead in the host's last word",
             (unsigned)(s->released - s->released_unlisted), (unsigned)s->released_unlisted,
             (unsigned)s->released_unlisted_dead);
}

uint32_t mp_enemy_sync_resets(void)
{
    return mp_enemy_sync_state()->resets;
}
