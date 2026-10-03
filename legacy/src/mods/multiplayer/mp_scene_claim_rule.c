/* mp_scene_claim_rule.c: whose a script's doors are on the host, pure. See the header. */
#include "mp_scene_claim_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The run.
 * ============================================================================================ */

/* The first three rules stand in front of the actor's own answer on purpose. The answer is the
 * nearest player or the last attacker and changes from tick to tick, and a take and its release
 * are ticks apart: judged by it alone, an actor that took the host's camera could not give it back
 * once a far player stood nearer. */
mp_scene_run_rule_t mp_scene_run(const mp_scene_run_evidence_t *evidence, uint8_t *bank)
{
    mp_scene_run_rule_t rule   = MP_SCENE_RUN_BY_HOST_ANCHOR;
    uint8_t             chosen = 0u;

    if (evidence == NULL || !evidence->joined) {
        rule = MP_SCENE_RUN_ALONE;
    } else if (evidence->of_the_scene) {
        rule = MP_SCENE_RUN_OF_THE_SCENE;
    } else if (evidence->taker) {
        rule = MP_SCENE_RUN_OF_A_TAKER;
    } else {
        switch (mp_scene_trigger(&evidence->actor, &chosen)) {
        case MP_SCENE_BY_OWN_ANSWER:
            rule = MP_SCENE_RUN_BY_OWN_ANSWER;
            break;
        case MP_SCENE_BY_LAST_ATTACKER:
            rule = MP_SCENE_RUN_BY_LAST_ATTACKER;
            break;
        case MP_SCENE_BY_HOST_ANCHOR:
        default:
            /* An answer that is fresh and names no player is an answer: the waking speaks only
             * for an actor that has asked nothing lately. */
            if ((evidence->actor.own == MP_SCENE_ANSWER_NONE ||
                 evidence->actor.own == MP_SCENE_ANSWER_STALE) &&
                evidence->woke_for_far && evidence->woke_bank != 0u) {
                rule   = MP_SCENE_RUN_BY_THE_WAKING;
                chosen = evidence->woke_bank;
            } else {
                chosen = 0u;
            }
            break;
        }
    }
    if (bank != NULL) {
        *bank = chosen;
    }
    return rule;
}

const char *mp_scene_run_text(mp_scene_run_rule_t rule)
{
    switch (rule) {
    case MP_SCENE_RUN_ALONE:
        return "nobody else being in the session";
    case MP_SCENE_RUN_OF_THE_SCENE:
        return "being an actor of the host's scene";
    case MP_SCENE_RUN_OF_A_TAKER:
        return "having taken here and not given back";
    case MP_SCENE_RUN_BY_OWN_ANSWER:
        return "the actor's own last answer";
    case MP_SCENE_RUN_BY_LAST_ATTACKER:
        return "the last attacker";
    case MP_SCENE_RUN_BY_THE_WAKING:
        return "the player its placement woke for";
    case MP_SCENE_RUN_BY_HOST_ANCHOR:
    case MP_SCENE_RUN_RULES:
    default:
        return "the host as the anchor";
    }
}

/* ==============================================================================================
 * The marks.
 * ============================================================================================ */

uint8_t mp_scene_mark_taken(uint8_t marks, uint8_t bit, bool spoken)
{
    unsigned after = marks;

    if (bit == MP_SCENE_MARK_CAMERA) {
        if (!spoken) {
            after &= ~MP_SCENE_MARK_SPOKEN;
        } else if ((after & MP_SCENE_MARK_CAMERA) == 0u) {
            after |= MP_SCENE_MARK_SPOKEN;
        }
    }
    return (uint8_t)(after | bit);
}

bool mp_scene_mark_given_back(uint8_t *marks, uint8_t bit, bool hosts_run)
{
    unsigned gone = bit;

    if (marks == NULL) {
        return hosts_run;
    }
    if (!hosts_run && (*marks & bit) == 0u) {
        return false;
    }
    if (bit == MP_SCENE_MARK_CAMERA) {
        gone |= MP_SCENE_MARK_SPOKEN;   /* the flag is the camera bit's and goes with it */
    }
    *marks = (uint8_t)(*marks & ~gone);
    return true;
}

bool mp_scene_mark_keeps_the_host(uint8_t marks)
{
    if ((marks & (MP_SCENE_MARK_LOCK | MP_SCENE_MARK_BARS)) != 0u) {
        return true;
    }
    return (marks & MP_SCENE_MARK_CAMERA) != 0u && (marks & MP_SCENE_MARK_SPOKEN) == 0u;
}

/* ==============================================================================================
 * The doors of a run.
 * ============================================================================================ */

void mp_scene_run_doors_forget(mp_scene_run_doors_t *doors)
{
    if (doors != NULL) {
        doors->count = 0u;
    }
}

bool mp_scene_run_doors_keep(mp_scene_run_doors_t *doors, uint8_t bit, int32_t argument)
{
    if (doors == NULL) {
        return false;
    }
    if (doors->count >= MP_SCENE_RUN_DOORS) {
        ++doors->left_out;
        return false;
    }
    doors->door[doors->count].bit      = bit;
    doors->door[doors->count].argument = argument;
    ++doors->count;
    return true;
}

/* ==============================================================================================
 * The latch.
 * ============================================================================================ */

void mp_scene_latch_open(mp_scene_latch_t *latch, uint32_t now)
{
    if (latch == NULL) {
        return;
    }
    latch->open  = true;
    latch->since = now;
}

/* The row of `key`, or the count when it is not written down. */
static size_t latch_row(const mp_scene_latch_t *latch, uint32_t key)
{
    size_t index;

    for (index = 0u; index < latch->count && index < MP_SCENE_LATCH_KEYS; ++index) {
        if (latch->key[index] == key) {
            return index;
        }
    }
    return latch->count;
}

mp_scene_refusal_t mp_scene_latch_holds(const mp_scene_latch_t *latch, uint32_t key)
{
    size_t row;

    if (latch == NULL) {
        return MP_SCENE_REFUSAL_NONE;
    }
    row = latch_row(latch, key);
    return row < latch->count ? latch->why[row] : MP_SCENE_REFUSAL_NONE;
}

static void latch_drop_row(mp_scene_latch_t *latch, size_t row)
{
    for (; row + 1u < latch->count; ++row) {
        latch->key[row] = latch->key[row + 1u];
        latch->why[row] = latch->why[row + 1u];
    }
    --latch->count;
}

static bool latch_write(mp_scene_latch_t *latch, uint32_t key, mp_scene_refusal_t why)
{
    if (latch->count >= MP_SCENE_LATCH_KEYS) {
        ++latch->left_out;
        return false;
    }
    latch->key[latch->count] = key;
    latch->why[latch->count] = why;
    ++latch->count;
    return true;
}

/* The age is taken in unsigned arithmetic, so a window opened after `now`, which only a counter
 * started over produces, reads as run out and not as open for four thousand million substeps. */
bool mp_scene_latch_door(mp_scene_latch_t *latch, uint32_t key, uint32_t now, bool hosts_run)
{
    size_t row;

    if (latch == NULL) {
        return false;
    }
    row = latch_row(latch, key);
    if (row < latch->count && latch->why[row] == MP_SCENE_REFUSAL_FOREIGN && hosts_run) {
        latch_drop_row(latch, row);
        ++latch->taken_back;
        row = latch->count;
    }
    if (row < latch->count) {
        ++latch->refused;
        return true;
    }
    if (latch->open && now - latch->since >= MP_SCENE_LATCH_SUBSTEPS) {
        latch->open = false;
    }
    if (!latch->open) {
        return false;
    }
    /* Refused while the window stands whether there was room to write it down or not; a door
     * past the room is counted there and not here. */
    if (latch_write(latch, key, MP_SCENE_REFUSAL_REPAIRED)) {
        ++latch->refused;
    }
    return true;
}

bool mp_scene_latch_foreign(mp_scene_latch_t *latch, uint32_t key)
{
    if (latch == NULL) {
        return false;
    }
    if (latch_row(latch, key) < latch->count) {
        return true;   /* written down already, for either reason, and that reason stays */
    }
    if (!latch_write(latch, key, MP_SCENE_REFUSAL_FOREIGN)) {
        return false;
    }
    ++latch->foreign;
    return true;
}

void mp_scene_latch_forget_foreign(mp_scene_latch_t *latch, uint32_t key)
{
    size_t row;

    if (latch == NULL) {
        return;
    }
    row = latch_row(latch, key);
    if (row < latch->count && latch->why[row] == MP_SCENE_REFUSAL_FOREIGN) {
        latch_drop_row(latch, row);
    }
}

bool mp_scene_latch_repaired(mp_scene_latch_t *latch, uint32_t key)
{
    size_t row;

    if (latch == NULL) {
        return false;
    }
    row = latch_row(latch, key);
    if (row < latch->count) {
        latch->why[row] = MP_SCENE_REFUSAL_REPAIRED;
        return true;
    }
    return latch_write(latch, key, MP_SCENE_REFUSAL_REPAIRED);
}

size_t mp_scene_latch_keys(const mp_scene_latch_t *latch, uint32_t *keys, size_t max)
{
    size_t index;
    size_t written = 0u;

    if (latch == NULL || keys == NULL) {
        return 0u;
    }
    for (index = 0u; index < latch->count && index < MP_SCENE_LATCH_KEYS && written < max;
         ++index) {
        if (latch->why[index] == MP_SCENE_REFUSAL_REPAIRED) {
            keys[written++] = latch->key[index];
        }
    }
    return written;
}

void mp_scene_latch_clear(mp_scene_latch_t *latch)
{
    if (latch == NULL) {
        return;
    }
    latch->open  = false;
    latch->since = 0u;
    latch->count = 0u;
}

/* ==============================================================================================
 * The waking.
 * ============================================================================================ */

void mp_scene_woke_note(mp_scene_woke_t *woke, uintptr_t record, uint8_t bank, uint32_t now)
{
    mp_scene_woke_row_t *row = NULL;
    size_t               index;

    if (woke == NULL || record == 0u || bank == 0u) {
        return;
    }
    for (index = 0u; index < MP_SCENE_WOKE_ROWS; ++index) {
        if (woke->row[index].record == record) {
            row = &woke->row[index];
            break;
        }
    }
    if (row == NULL) {
        row        = &woke->row[woke->next % MP_SCENE_WOKE_ROWS];
        woke->next = (woke->next + 1u) % MP_SCENE_WOKE_ROWS;
        if (row->record != 0u && now - row->at <= MP_SCENE_WOKE_SUBSTEPS) {
            ++woke->replaced;
        }
    }
    row->record = record;
    row->at     = now;
    row->bank   = bank;
    ++woke->noted;
}

bool mp_scene_woke_for(const mp_scene_woke_t *woke, uintptr_t record, uint32_t now, uint8_t *bank)
{
    size_t index;

    if (woke == NULL || record == 0u) {
        return false;
    }
    for (index = 0u; index < MP_SCENE_WOKE_ROWS; ++index) {
        const mp_scene_woke_row_t *row = &woke->row[index];

        if (row->record != record) {
            continue;
        }
        if (now - row->at > MP_SCENE_WOKE_SUBSTEPS) {
            return false;
        }
        if (bank != NULL) {
            *bank = row->bank;
        }
        return true;
    }
    return false;
}

/* ==============================================================================================
 * A release owed.
 * ============================================================================================ */

void mp_scene_owed_note(mp_scene_owed_t *owed, uint32_t key, uintptr_t actor, uint32_t now)
{
    mp_scene_owed_row_t *free_row = NULL;
    size_t               index;

    if (owed == NULL || actor == 0u) {
        return;
    }
    for (index = 0u; index < MP_SCENE_OWED_ROWS; ++index) {
        mp_scene_owed_row_t *row = &owed->row[index];

        if (row->have && row->actor == actor && row->key == key) {
            row->at = now;
            return;
        }
        if (!row->have && free_row == NULL) {
            free_row = row;
        }
    }
    if (free_row == NULL) {
        ++owed->left_out;
        return;
    }
    free_row->have  = true;
    free_row->key   = key;
    free_row->actor = actor;
    free_row->at    = now;
    ++owed->noted;
}

mp_scene_owed_look_t mp_scene_owed_step(mp_scene_owed_t *owed, size_t row, uint32_t now,
                                        bool actor_lives, bool scene_stands)
{
    mp_scene_owed_row_t *kept;

    if (owed == NULL || row >= MP_SCENE_OWED_ROWS || !owed->row[row].have) {
        return MP_SCENE_OWED_NOTHING;
    }
    kept = &owed->row[row];
    if (!scene_stands) {
        kept->have = false;
        ++owed->forgotten;
        return MP_SCENE_OWED_FORGOTTEN;
    }
    if (!actor_lives) {
        kept->have = false;
        ++owed->due;
        return MP_SCENE_OWED_DUE;
    }
    if (now - kept->at >= MP_SCENE_OWED_SUBSTEPS) {
        kept->have = false;
        ++owed->forgotten;
        return MP_SCENE_OWED_FORGOTTEN;
    }
    return MP_SCENE_OWED_WAITS;
}

bool mp_scene_owed_again(uint32_t last, uint32_t now)
{
    return last != 0u && last != now + 1u && now + 1u - last <= MP_SCENE_OWED_SUBSTEPS;
}

void mp_scene_owed_drop(mp_scene_owed_t *owed, uint32_t key)
{
    size_t index;

    if (owed == NULL) {
        return;
    }
    for (index = 0u; index < MP_SCENE_OWED_ROWS; ++index) {
        if (owed->row[index].have && owed->row[index].key == key) {
            owed->row[index].have = false;
            ++owed->habitual;
        }
    }
}

void mp_scene_owed_clear(mp_scene_owed_t *owed)
{
    if (owed != NULL) {
        memset(owed->row, 0, sizeof owed->row);
    }
}
