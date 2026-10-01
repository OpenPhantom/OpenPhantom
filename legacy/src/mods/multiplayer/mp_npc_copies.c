/* mp_npc_copies.c: the host's table of the NPC copies. See the header.
 *
 * SIZE NOTE: over 600 lines. One state machine: the census, the answers, the removals and the
 * owners all move the same rows, and a reader checks a transition against the others in one place.
 * The messages already live apart, in mp_npc_copy_wire, and the pool's rule in mp_pool_rule.
 */
#include "mp_npc_copies.h"

#include "mp_npc_copy_wire.h"
#include "mp_pool_rule.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_NPC_COPIES_REASONS > NPC_SPAWN_REFUSED_NO_KEY, "a counter for every reason");

/* What the throttle lets through at once beyond the one due now. */
#define BUCKET_ALLOWANCE_MS ((MP_NPC_COPIES_BUCKET_BURST - 1u) * MP_NPC_COPIES_BUCKET_MS)

/* Duties nobody needs once a copy is on its way out. */
#define NOT_FOR_AN_ENDING (MP_NPC_COPY_DUTY_ANNOUNCE | MP_NPC_COPY_DUTY_OWNER_NOTE)

static void enter(mp_npc_copies_t *copies, mp_npc_copy_row_t *row, uint8_t state)
{
    row->state = state;
    row->since = copies->censuses;
}

void mp_npc_copies_init(mp_npc_copies_t *copies, uint32_t corpse_censuses)
{
    if (copies != NULL) {
        memset(copies, 0, sizeof *copies);
        copies->corpse_censuses = corpse_censuses;
    }
}

void mp_npc_copies_clear(mp_npc_copies_t *copies)
{
    size_t k;

    if (copies == NULL) {
        return;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copy_row_t *row        = &copies->row[k];
        uint8_t            generation = row->generation;

        memset(row, 0, sizeof *row);
        row->generation = generation;
    }
    memset(copies->bucket, 0, sizeof copies->bucket);
    copies->pool_known = false;
}

static bool assignable(const mp_npc_copy_row_t *row)
{
    return row->state == MP_NPC_COPY_FREE && !row->present;
}

void mp_npc_copies_census_begin(mp_npc_copies_t *copies)
{
    size_t k;

    if (copies == NULL) {
        return;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        copies->row[k].seen      = false;
        copies->row[k].seen_live = false;
    }
}

void mp_npc_copies_census_saw(mp_npc_copies_t *copies, uint32_t k, bool live)
{
    mp_npc_copy_row_t *row;

    if (copies == NULL || k >= MP_WIRE_COPY_MAX) {
        return;
    }
    row = &copies->row[k];
    if (row->seen) {
        ++copies->counters.doubles;
    }
    row->seen      = true;
    row->seen_live = row->seen_live || live;
}

/* A life ends. A client that asked for it and was never told of it is owed an answer; the host's
 * own overlay heard of it through its grant, or hears through owe_cancel below. */
static void end_life(mp_npc_copies_t *copies, mp_npc_copy_row_t *row)
{
    enter(copies, row, MP_NPC_COPY_ENDING);
    row->duties = (uint8_t)(row->duties & ~NOT_FOR_AN_ENDING);
    if (!row->announced && !row->who.asker_own) {
        row->duties |= MP_NPC_COPY_DUTY_REFUSE_ASKER;
    }
}

/* A cancel is owed, unless the overlay never got the grant: then there is nothing to cancel, and
 * the host's own overlay, whose wish that grant was to answer, is told the wish was refused. */
static void owe_cancel(mp_npc_copy_row_t *row)
{
    if ((row->duties & MP_NPC_COPY_DUTY_GRANT_NOTE) != 0u) {
        row->duties = (uint8_t)(row->duties & ~MP_NPC_COPY_DUTY_GRANT_NOTE);
        if (row->who.asker_own) {
            row->duties |= MP_NPC_COPY_DUTY_REFUSE_OWN;
        }
        return;
    }
    row->duties |= MP_NPC_COPY_DUTY_CANCEL_NOTE;
}

static bool nothing_open(const mp_npc_copy_row_t *row)
{
    return row->waiting == 0u && row->duties == 0u;
}

static void census_ending(mp_npc_copies_t *copies, mp_npc_copy_row_t *row, bool complete)
{
    uint32_t age         = copies->censuses - row->since;
    bool     cancel_owed = (row->duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u;

    if (row->seen_live && row->waiting == 0u && !cancel_owed) {
        if (!row->recancelled) {
            row->duties |= MP_NPC_COPY_DUTY_CANCEL_NOTE;   /* alive, and nothing asked it away */
            row->recancelled = true;
            ++copies->counters.recancelled;
        } else if (!row->stuck) {
            row->stuck = true;
            ++copies->counters.stuck;
        }
    } else if (row->seen && !row->seen_live && copies->corpse_censuses != 0u && !row->swept &&
               age >= copies->corpse_censuses && nothing_open(row)) {
        row->duties |= MP_NPC_COPY_DUTY_CANCEL_NOTE;
        row->swept = true;
        ++copies->counters.corpses_swept;
    } else if (complete && !row->seen && nothing_open(row)) {
        enter(copies, row, MP_NPC_COPY_RESTING);
    }
}

static void census_row(mp_npc_copies_t *copies, mp_npc_copy_row_t *row, bool complete)
{
    uint32_t age = copies->censuses - row->since;

    switch (row->state) {
    case MP_NPC_COPY_FREE:
    case MP_NPC_COPY_RESTING:
        if (row->seen) {
            enter(copies, row, MP_NPC_COPY_FOREIGN);
            ++copies->counters.foreign;
        } else if (row->state == MP_NPC_COPY_RESTING && age >= MP_NPC_COPIES_REST) {
            enter(copies, row, MP_NPC_COPY_FREE);
        }
        break;
    case MP_NPC_COPY_FOREIGN:
        if (complete && !row->seen) {
            enter(copies, row, MP_NPC_COPY_RESTING);
        }
        break;
    case MP_NPC_COPY_GRANTED:
        /* Only alive counts as built. An actor that is there but not alive gets the same deadline
         * as one that is not there: a copy spawned this substep may not read alive yet. */
        if (row->seen_live) {
            enter(copies, row, MP_NPC_COPY_LIVE);
            row->duties |= MP_NPC_COPY_DUTY_ANNOUNCE;
            ++copies->counters.first_seen;
        } else if (complete && age > MP_NPC_COPIES_SEEN_DEADLINE) {
            if (row->seen) {
                ++copies->counters.built_dead;
            } else {
                ++copies->counters.never_seen;
            }
            end_life(copies, row);
            owe_cancel(row);
        }
        break;
    case MP_NPC_COPY_LIVE:
        /* Dead or away: seen dead by any walk, or not seen by a complete one. An announcement it
         * still owed would build a live copy of a corpse. */
        if (row->seen ? !row->seen_live : complete) {
            if (!row->announced) {
                ++copies->counters.died_unannounced;
            }
            end_life(copies, row);
            ++copies->counters.gone;
        }
        break;
    case MP_NPC_COPY_ENDING:
        census_ending(copies, row, complete);
        break;
    default:
        break;
    }
}

void mp_npc_copies_census_end(mp_npc_copies_t *copies, bool complete, uint32_t chain_length,
                              uint32_t capacity)
{
    size_t k;

    if (copies == NULL) {
        return;
    }
    ++copies->censuses;
    if (!complete) {
        ++copies->counters.incomplete;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copy_row_t *row = &copies->row[k];

        census_row(copies, row, complete);
        /* What a walk saw is so, whether it walked the whole pool or not; what it did not see is
         * gone only when it did. */
        if (row->seen || complete) {
            row->present = row->seen;
            row->live    = row->seen_live;
        }
    }
    if (complete) {
        copies->chain_length = chain_length;
        copies->capacity     = capacity;
        copies->pool_known   = true;
    }
}

uint32_t mp_npc_copies_count(const mp_npc_copies_t *copies)
{
    uint32_t count = 0;
    size_t   k;

    if (copies == NULL) {
        return 0u;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        const mp_npc_copy_row_t *row = &copies->row[k];

        if (row->state == MP_NPC_COPY_GRANTED || row->state == MP_NPC_COPY_LIVE ||
            (row->state == MP_NPC_COPY_ENDING && row->live)) {
            ++count;
        }
    }
    return count;
}

/* Grants the overlay may still be building: they take a pool slot the census has not counted. */
static uint32_t open_grants(const mp_npc_copies_t *copies)
{
    uint32_t count = 0;
    size_t   k;

    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        if (copies->row[k].state == MP_NPC_COPY_GRANTED && !copies->row[k].present) {
            ++count;
        }
    }
    return count;
}

static bool first_assignable(const mp_npc_copies_t *copies, uint32_t *k)
{
    uint32_t i;

    for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
        if (assignable(&copies->row[i])) {
            *k = i;
            return true;
        }
    }
    return false;
}

static void refuse(mp_npc_copies_t *copies, mp_npc_copy_verdict_t *verdict, uint8_t reason)
{
    verdict->outcome = (uint8_t)MP_NPC_COPY_REFUSE;
    verdict->reason  = reason;
    ++copies->counters.refused[reason < MP_NPC_COPIES_REASONS ? reason : 0u];
}

/* The part every kind of wish goes through: which world it belongs to, and whether its asker may
 * ask now. False when it was decided here. */
static bool passes_the_common_checks(mp_npc_copies_t *copies, const mp_npc_copy_ask_t *ask,
                                     mp_npc_copy_verdict_t *verdict)
{
    if (!ask->from_wire && ask->wish_epoch != ask->epoch) {
        verdict->outcome = (uint8_t)MP_NPC_COPY_STALE;
        ++copies->counters.stale;
        return false;
    }
    if (ask->from_wire && !ask->bucket_ok) {
        ++copies->counters.throttled;
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_TOO_FAST);
        return false;
    }
    if (!ask->in_level ||
        (ask->from_wire && (ask->wish_level != ask->level || ask->wish_world != ask->world))) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_NO_LEVEL);
        return false;
    }
    return true;
}

void mp_npc_copies_decide(mp_npc_copies_t *copies, const mp_npc_copy_ask_t *ask,
                          mp_npc_copy_verdict_t *verdict)
{
    uint32_t k = 0;

    if (copies == NULL || ask == NULL || verdict == NULL) {
        return;
    }
    memset(verdict, 0, sizeof *verdict);
    ++copies->counters.taken;
    if (ask->from_wire) {
        ++copies->counters.from_wire;
    }
    if (!passes_the_common_checks(copies, ask, verdict)) {
        return;
    }
    if (ask->kind == NPC_SPAWN_WISH_REMOVE_OWN || ask->kind == NPC_SPAWN_WISH_REMOVE_ALL) {
        verdict->outcome = (uint8_t)MP_NPC_COPY_REMOVE;
        ++copies->counters.removals;
        return;
    }
    if (!ask->has_builder) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_NO_BUILDER);
    } else if (!ask->description_sound) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
    } else if (mp_npc_copies_count(copies) >= ask->cap) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_CAP);
    } else if (!copies->pool_known ||
               !mp_pool_may_take(copies->chain_length + open_grants(copies), copies->capacity,
                                 NPC_SPAWN_POOL_RESERVE)) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_POOL);
    } else if (!first_assignable(copies, &k)) {
        refuse(copies, verdict, (uint8_t)NPC_SPAWN_REFUSED_NO_KEY);
    } else {
        verdict->outcome = (uint8_t)MP_NPC_COPY_GRANT;
        verdict->k       = k;
    }
}

uint8_t mp_npc_copies_grant(mp_npc_copies_t *copies, uint32_t k, const mp_npc_copy_who_t *who,
                            const uint8_t *desc)
{
    mp_npc_copy_row_t    *row;
    npc_spawn_note_desc_t check;
    uint8_t               generation;

    if (copies == NULL || who == NULL || desc == NULL || k >= MP_WIRE_COPY_MAX ||
        who->owner >= NPC_SPAWN_WORLD_SLOTS || who->asker >= NPC_SPAWN_WORLD_SLOTS ||
        !mp_npc_copy_desc_get(desc, &check)) {
        return 0u;
    }
    row = &copies->row[k];
    if (!assignable(row)) {
        return 0u;
    }
    generation = (uint8_t)(row->generation == 255u ? 1u : row->generation + 1u);
    memset(row, 0, sizeof *row);
    row->generation = generation;
    row->who        = *who;
    row->duties     = MP_NPC_COPY_DUTY_GRANT_NOTE;
    memcpy(row->desc, desc, MP_NPC_COPY_DESC_BYTES);
    enter(copies, row, MP_NPC_COPY_GRANTED);
    ++copies->counters.granted;
    return row->generation;
}

static uint32_t cancel_where(mp_npc_copies_t *copies, bool all, uint8_t slot, uint64_t connection)
{
    uint32_t count = 0;
    size_t   k;

    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copy_row_t *row = &copies->row[k];

        if ((row->state == MP_NPC_COPY_GRANTED || row->state == MP_NPC_COPY_LIVE) &&
            (all || (row->who.owner == slot && row->who.owner_connection == connection))) {
            end_life(copies, row);
            owe_cancel(row);
            ++count;
        }
    }
    if (count == 0u) {
        ++copies->counters.removals_empty;
    }
    return count;
}

uint32_t mp_npc_copies_cancel_owned(mp_npc_copies_t *copies, uint8_t slot, uint64_t connection)
{
    return copies != NULL ? cancel_where(copies, false, slot, connection) : 0u;
}

uint32_t mp_npc_copies_cancel_all(mp_npc_copies_t *copies)
{
    return copies != NULL ? cancel_where(copies, true, 0u, 0u) : 0u;
}

void mp_npc_copies_owners(mp_npc_copies_t *copies, const uint64_t *connection_of)
{
    size_t k;

    if (copies == NULL || connection_of == NULL) {
        return;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copy_row_t *row = &copies->row[k];

        if (row->state != MP_NPC_COPY_GRANTED && row->state != MP_NPC_COPY_LIVE &&
            row->state != MP_NPC_COPY_ENDING) {
            continue;
        }
        if (row->who.owner == 0u || connection_of[row->who.owner] == row->who.owner_connection) {
            continue;
        }
        row->who.owner            = 0u;
        row->who.owner_connection = 0u;
        ++copies->counters.owners_changed;
        if (row->state == MP_NPC_COPY_LIVE) {
            row->duties |= MP_NPC_COPY_DUTY_OWNER_NOTE | MP_NPC_COPY_DUTY_ANNOUNCE;
        } else if (row->state == MP_NPC_COPY_GRANTED) {
            row->duties |= MP_NPC_COPY_DUTY_OWNER_NOTE;
        }
    }
}

/* The order a row's duties are done in: the overlay hears of a copy before anybody is told of it,
 * and a cancel before an owner change nobody needs any more. */
static const uint8_t DUTY_ORDER[] = {
    MP_NPC_COPY_DUTY_GRANT_NOTE,   MP_NPC_COPY_DUTY_CANCEL_NOTE, MP_NPC_COPY_DUTY_REFUSE_OWN,
    MP_NPC_COPY_DUTY_REFUSE_ASKER, MP_NPC_COPY_DUTY_OWNER_NOTE,  MP_NPC_COPY_DUTY_ANNOUNCE,
};

bool mp_npc_copies_next_duty(const mp_npc_copies_t *copies, uint8_t mask, uint32_t *k,
                             uint8_t *duty)
{
    uint32_t i;
    size_t   d;

    if (copies == NULL || k == NULL || duty == NULL) {
        return false;
    }
    for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
        uint8_t owed = (uint8_t)(copies->row[i].duties & mask);

        for (d = 0; owed != 0u && d < sizeof DUTY_ORDER / sizeof DUTY_ORDER[0]; ++d) {
            if ((owed & DUTY_ORDER[d]) != 0u) {
                *k    = i;
                *duty = DUTY_ORDER[d];
                return true;
            }
        }
    }
    return false;
}

bool mp_npc_copies_note_for(const mp_npc_copies_t *copies, uint32_t k, uint8_t duty,
                            npc_spawn_grant_t *out)
{
    const mp_npc_copy_row_t *row;

    if (copies == NULL || out == NULL || k >= MP_WIRE_COPY_MAX ||
        (copies->row[k].duties & duty) == 0u) {
        return false;
    }
    row = &copies->row[k];
    memset(out, 0, sizeof *out);
    if (duty == MP_NPC_COPY_DUTY_REFUSE_OWN) {
        out->kind   = (uint8_t)NPC_SPAWN_GRANT_REFUSED;
        out->wish   = row->who.wish;
        out->reason = (uint8_t)NPC_SPAWN_REFUSED_NOT_BUILT;
        return true;
    }
    out->generation = row->generation;
    out->key        = (uint16_t)(NPC_SPAWN_KEY_FIRST + k);
    out->owner      = row->who.owner;
    switch (duty) {
    case MP_NPC_COPY_DUTY_GRANT_NOTE:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_BUILD;
        out->wish = row->who.asker_own ? row->who.wish : 0u;
        return mp_npc_copy_desc_get(row->desc, &out->desc);
    case MP_NPC_COPY_DUTY_CANCEL_NOTE:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_CANCEL;
        return true;
    case MP_NPC_COPY_DUTY_OWNER_NOTE:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_OWNER;
        return true;
    default:
        return false;
    }
}

bool mp_npc_copies_entry_for(const mp_npc_copies_t *copies, uint32_t k, uint8_t duty,
                             uint16_t level, uint8_t world, mp_npc_copy_entry_t *out)
{
    const mp_npc_copy_row_t *row;

    if (copies == NULL || out == NULL || k >= MP_WIRE_COPY_MAX) {
        return false;
    }
    row = &copies->row[k];
    memset(out, 0, sizeof *out);
    out->level = level;
    out->world = world;
    if (duty == MP_NPC_COPY_DUTY_ANNOUNCE && row->state == MP_NPC_COPY_LIVE && row->live) {
        out->kind       = (uint8_t)NPC_SPAWN_GRANT_BUILD;
        out->k          = (uint16_t)k;
        out->generation = row->generation;
        out->owner      = row->who.owner;
        return mp_npc_copy_desc_get(row->desc, &out->desc);
    }
    if (duty == MP_NPC_COPY_DUTY_REFUSE_ASKER && (row->duties & duty) != 0u) {
        out->kind   = (uint8_t)NPC_SPAWN_GRANT_REFUSED;
        out->serial = (uint16_t)row->who.wish;
        out->reason = (uint8_t)NPC_SPAWN_REFUSED_NOT_BUILT;
        out->owner  = row->who.asker;
        return true;
    }
    return false;
}

bool mp_npc_copies_refusal_for(const mp_npc_copy_wish_t *wish, uint8_t asker, uint8_t reason,
                               mp_npc_copy_entry_t *out)
{
    if (wish == NULL || out == NULL || reason == 0u || asker >= NPC_SPAWN_WORLD_SLOTS) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->kind   = (uint8_t)NPC_SPAWN_GRANT_REFUSED;
    out->serial = wish->serial;
    out->reason = reason;
    out->owner  = asker;
    out->level  = wish->level;
    out->world  = wish->world;
    return true;
}

bool mp_npc_copies_asker_present(const mp_npc_copies_t *copies, uint32_t k,
                                 const uint64_t *connection_of)
{
    const mp_npc_copy_row_t *row;

    if (copies == NULL || connection_of == NULL || k >= MP_WIRE_COPY_MAX) {
        return false;
    }
    row = &copies->row[k];
    return row->who.asker < NPC_SPAWN_WORLD_SLOTS &&
           connection_of[row->who.asker] == row->who.asker_connection;
}

void mp_npc_copies_duty_done(mp_npc_copies_t *copies, uint32_t k, uint8_t duty, uint32_t serial)
{
    mp_npc_copy_row_t *row;

    if (copies == NULL || k >= MP_WIRE_COPY_MAX) {
        return;
    }
    row = &copies->row[k];
    if ((row->duties & duty) == 0u) {
        ++copies->counters.duties_unowed;
        return;
    }
    row->duties = (uint8_t)(row->duties & ~duty);
    switch (duty) {
    case MP_NPC_COPY_DUTY_GRANT_NOTE:
        row->waiting     = serial;
        row->waiting_for = (uint8_t)MP_NPC_COPY_WAIT_GRANT;
        break;
    case MP_NPC_COPY_DUTY_CANCEL_NOTE:
        row->waiting     = serial;
        row->waiting_for = (uint8_t)MP_NPC_COPY_WAIT_CANCEL;
        break;
    case MP_NPC_COPY_DUTY_OWNER_NOTE:
    case MP_NPC_COPY_DUTY_REFUSE_OWN:
        row->owner_waiting = serial;   /* answered, and nothing follows from the answer */
        break;
    case MP_NPC_COPY_DUTY_ANNOUNCE:
        row->announced = true;
        break;
    default:
        break;
    }
}

void mp_npc_copies_duty_drop(mp_npc_copies_t *copies, uint32_t k, uint8_t duty)
{
    if (copies == NULL || k >= MP_WIRE_COPY_MAX || (copies->row[k].duties & duty) == 0u) {
        return;
    }
    copies->row[k].duties = (uint8_t)(copies->row[k].duties & ~duty);
    ++copies->counters.refusals_dropped;
}

/* The answer to a row's open serial, read against what that serial was. An answer to a grant never
 * brings back a life that is ending; only a refused cancel does, and only while the copy is seen
 * alive. */
static void answer_row(mp_npc_copies_t *copies, mp_npc_copy_row_t *row, npc_spawn_answer_t answer)
{
    uint8_t wait = row->waiting_for;

    row->waiting     = 0u;
    row->waiting_for = (uint8_t)MP_NPC_COPY_WAIT_NONE;
    if (answer == NPC_SPAWN_ANSWER_DONE) {
        return;
    }
    if (wait == MP_NPC_COPY_WAIT_GRANT) {
        ++copies->counters.not_built;
        if (row->state == MP_NPC_COPY_GRANTED) {
            end_life(copies, row);
        }
    } else if (wait == MP_NPC_COPY_WAIT_CANCEL && row->state == MP_NPC_COPY_ENDING && row->live) {
        /* The local player rides it. It stays, and it stays a copy that counts, is repeated to
         * latecomers and keeps an owner the overlay agrees on. */
        enter(copies, row, MP_NPC_COPY_LIVE);
        row->recancelled = false;   /* a later ending of this life is one of its own */
        row->swept       = false;
        row->stuck       = false;
        row->duties |= MP_NPC_COPY_DUTY_OWNER_NOTE;
        if (!row->announced) {
            /* Its announcement is the asker's answer; a refusal still owed would contradict it. */
            row->duties = (uint8_t)((row->duties & ~MP_NPC_COPY_DUTY_REFUSE_ASKER) |
                                    MP_NPC_COPY_DUTY_ANNOUNCE);
        }
        ++copies->counters.ridden;
    }
}

void mp_npc_copies_take_answer(mp_npc_copies_t *copies, uint32_t serial,
                               npc_spawn_answer_t answer)
{
    size_t k;

    if (copies == NULL || serial == 0u) {
        return;
    }
    if (answer == NPC_SPAWN_ANSWER_OPEN) {
        ++copies->counters.answers_open;
        return;
    }
    if (answer == NPC_SPAWN_ANSWER_LOST) {
        ++copies->counters.answers_lost;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copy_row_t *row = &copies->row[k];

        if (row->owner_waiting == serial) {
            row->owner_waiting = 0u;
            return;
        }
        if (row->waiting == serial) {
            answer_row(copies, row, answer);
            return;
        }
    }
    ++copies->counters.answers_unmatched;
}

static bool holds_a_life(const mp_npc_copy_row_t *row)
{
    return row->state == MP_NPC_COPY_GRANTED || row->state == MP_NPC_COPY_LIVE ||
           row->state == MP_NPC_COPY_ENDING;
}

bool mp_npc_copies_generation(const mp_npc_copies_t *copies, uint32_t k, uint8_t *out)
{
    if (copies == NULL || out == NULL || k >= MP_WIRE_COPY_MAX ||
        !holds_a_life(&copies->row[k])) {
        return false;
    }
    *out = copies->row[k].generation;
    return true;
}

bool mp_npc_copies_owner(const mp_npc_copies_t *copies, uint32_t k, uint8_t *out)
{
    if (copies == NULL || out == NULL || k >= MP_WIRE_COPY_MAX ||
        !holds_a_life(&copies->row[k])) {
        return false;
    }
    *out = copies->row[k].who.owner;
    return true;
}

bool mp_npc_copies_follows(const mp_npc_copies_t *copies, uint32_t k, uint8_t *owner)
{
    npc_spawn_note_desc_t desc;

    if (copies == NULL || owner == NULL || k >= MP_WIRE_COPY_MAX ||
        !holds_a_life(&copies->row[k]) || !mp_npc_copy_desc_get(copies->row[k].desc, &desc) ||
        (desc.behaviour != NPC_SPAWN_BEHAVIOUR_FOLLOW &&
         desc.behaviour != NPC_SPAWN_BEHAVIOUR_HELP)) {
        return false;
    }
    *owner = copies->row[k].who.owner;
    return true;
}

uint8_t mp_npc_copies_goes_with(uint8_t owner, bool owner_stands)
{
    return (owner != 0u && owner < NPC_SPAWN_WORLD_SLOTS && owner_stands) ? owner : 0u;
}

void mp_npc_copies_anchors(const float point[NPC_SPAWN_WORLD_SLOTS][3], uint16_t stands,
                           uint8_t epoch, npc_spawn_anchor_record_t *out)
{
    uint8_t s;

    if (point == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->epoch = epoch;
    for (s = 0; s < NPC_SPAWN_WORLD_SLOTS; ++s) {
        uint8_t with = s == 0u ? 0u : mp_npc_copies_goes_with(s, (stands & (1u << s)) != 0u);

        if ((stands & (1u << with)) == 0u) {
            continue;   /* nobody to fly to: the overlay leaves the record where it is */
        }
        out->valid = (uint16_t)(out->valid | (1u << s));
        out->point[s][0] = point[with][0];
        out->point[s][1] = point[with][1];
        out->point[s][2] = point[with][2];
    }
}

bool mp_npc_copies_describable(const mp_npc_copies_t *copies, uint32_t k)
{
    return copies != NULL && k < MP_WIRE_COPY_MAX && holds_a_life(&copies->row[k]);
}

bool mp_npc_copies_next_live(const mp_npc_copies_t *copies, uint32_t *cursor, uint32_t *k)
{
    uint32_t step;

    if (copies == NULL || cursor == NULL || k == NULL) {
        return false;
    }
    for (step = 1; step <= MP_WIRE_COPY_MAX; ++step) {
        uint32_t i = (*cursor + step) % MP_WIRE_COPY_MAX;

        if (copies->row[i].state == MP_NPC_COPY_LIVE && copies->row[i].live) {
            *cursor = i;
            *k      = i;
            return true;
        }
    }
    return false;
}

/* The generic cell rate: a wish is due no earlier than its theoretical arrival time less the
 * burst's allowance, and each one taken pushes that time on by an interval. Compared by distance,
 * so the millisecond counter may wrap. */
bool mp_npc_copies_throttle(mp_npc_copies_t *copies, uint8_t slot, uint64_t connection,
                            uint32_t now_ms)
{
    mp_npc_copies_bucket_t *bucket;

    if (copies == NULL || slot >= NPC_SPAWN_WORLD_SLOTS) {
        return false;
    }
    bucket = &copies->bucket[slot];
    if (!bucket->used || bucket->connection != connection) {
        memset(bucket, 0, sizeof *bucket);
        bucket->used       = true;
        bucket->connection = connection;
        bucket->due_ms     = now_ms;
    }
    if ((int32_t)(now_ms - (bucket->due_ms - BUCKET_ALLOWANCE_MS)) < 0) {
        return false;
    }
    if ((int32_t)(now_ms - bucket->due_ms) > 0) {
        bucket->due_ms = now_ms;
    }
    bucket->due_ms += MP_NPC_COPIES_BUCKET_MS;
    return true;
}

bool mp_npc_copies_throttle_answer(mp_npc_copies_t *copies, uint8_t slot, uint32_t now_ms)
{
    mp_npc_copies_bucket_t *bucket;

    if (copies == NULL || slot >= NPC_SPAWN_WORLD_SLOTS) {
        return false;
    }
    bucket = &copies->bucket[slot];
    if (bucket->answered &&
        (int32_t)(now_ms - bucket->answered_ms) < (int32_t)MP_NPC_COPIES_BUCKET_MS) {
        return false;
    }
    bucket->answered    = true;
    bucket->answered_ms = now_ms;
    return true;
}
