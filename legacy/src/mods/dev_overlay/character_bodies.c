/* character_bodies.c: the two tables of the swap and the decisions over them. See the header. */
#include "character_bodies.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The player's own body and every bank the note describes, each with a pair of its own at worst.
 * A note that grew past this table would leave a far body with nowhere to be filed. */
_Static_assert(BODY_MAX >= 1u + MODEL_WEAR_BANKS,
               "the body table must hold the player's own body and every bank of the note");
_Static_assert(PAIR_MAX >= BODY_MAX, "every body must be able to hold a pair of its own");
_Static_assert(MODEL_WEAR_BANKS < 0xFFu, "a far bank is filed in one byte");

uint32_t character_bodies_find(const character_bodies_t *t, uintptr_t thing)
{
    uint32_t i;

    if (t == NULL || thing == 0u) {
        return BODY_NONE;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (t->body[i].used && t->body[i].thing == thing) {
            return i;
        }
    }
    return BODY_NONE;
}

uint32_t character_bodies_find_bank(const character_bodies_t *t, uint32_t bank)
{
    uint32_t i;

    if (t == NULL || bank > MODEL_WEAR_BANKS) {
        return BODY_NONE;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (t->body[i].used && t->body[i].bank == bank) {
            return i;
        }
    }
    return BODY_NONE;
}

bool character_bodies_any(const character_bodies_t *t)
{
    uint32_t i;

    if (t == NULL) {
        return false;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (t->body[i].used) {
            return true;
        }
    }
    return false;
}

bool character_bodies_holds(const body_entry_t *body, const body_reads_t *reads)
{
    if (body == NULL || reads == NULL || !body->used || body->thing == 0u) {
        return false;
    }
    if (!body->local && (body->obj == 0u || reads->block_body != body->obj)) {
        return false;
    }
    return reads->obj_thing == body->thing && reads->worn == body->target &&
           reads->worn_nodes == body->target_nodes && reads->actor_model == body->reference;
}

/* ============================================================================================ */

static bool pair_has_live_wearer(const character_bodies_t *t, uint32_t pair, uint32_t pass)
{
    uint32_t i;

    for (i = 0; i < BODY_MAX; ++i) {
        if (t->body[i].used && t->body[i].pair == pair && t->body[i].checked == pass) {
            return true;
        }
    }
    return false;
}

uint32_t character_bodies_pair_take(character_bodies_t *t, uintptr_t reference, uintptr_t target,
                                    uint32_t target_nodes, uint32_t share_pass, bool *fresh)
{
    uint32_t p;

    if (fresh != NULL) {
        *fresh = false;
    }
    if (t == NULL || fresh == NULL || reference == 0u || target == 0u || target_nodes == 0u) {
        return BODY_NONE;
    }
    if (share_pass != 0u) {
        for (p = 0; p < PAIR_MAX; ++p) {
            const body_pair_t *pair = &t->pair[p];

            if (pair->used && pair->reference == reference && pair->target == target &&
                pair->target_nodes == target_nodes && pair_has_live_wearer(t, p, share_pass)) {
                return p;
            }
        }
    }
    for (p = 0; p < PAIR_MAX; ++p) {
        body_pair_t *pair = &t->pair[p];

        if (!pair->used) {
            memset(pair, 0, sizeof *pair);
            pair->used = true;
            pair->reference = reference;
            pair->target = target;
            pair->target_nodes = target_nodes;
            pair->made = ++t->made;
            *fresh = true;
            return p;
        }
    }
    return BODY_NONE;
}

void character_bodies_pair_drop(character_bodies_t *t, uint32_t pair)
{
    if (t != NULL && pair < PAIR_MAX && t->pair[pair].used && t->pair[pair].wearers == 0u) {
        t->pair[pair].used = false;
    }
}

uint32_t character_bodies_add(character_bodies_t *t, const body_entry_t *body)
{
    uint32_t i;

    if (t == NULL || body == NULL || body->thing == 0u || body->pair >= PAIR_MAX ||
        !t->pair[body->pair].used || body->bank > MODEL_WEAR_BANKS ||
        body->local != (body->bank == 0u)) {
        return BODY_NONE;
    }
    if (character_bodies_find(t, body->thing) != BODY_NONE ||
        character_bodies_find_bank(t, body->bank) != BODY_NONE) {
        return BODY_NONE;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (!t->body[i].used) {
            t->body[i] = *body;
            t->body[i].used = true;
            t->pair[body->pair].wearers++;
            return i;
        }
    }
    return BODY_NONE;
}

uint32_t character_bodies_remove(character_bodies_t *t, uint32_t index)
{
    uint32_t pair;

    if (t == NULL || index >= BODY_MAX || !t->body[index].used) {
        return BODY_NONE;
    }
    pair = t->body[index].pair;
    memset(&t->body[index], 0, sizeof t->body[index]);
    if (pair >= PAIR_MAX || !t->pair[pair].used) {
        return BODY_NONE;
    }
    if (t->pair[pair].wearers != 0u) {
        t->pair[pair].wearers--;
    }
    if (t->pair[pair].wearers != 0u) {
        return BODY_NONE;
    }
    t->pair[pair].used = false;
    return pair;
}

uint32_t character_bodies_begin_pass(character_bodies_t *t)
{
    if (t == NULL) {
        return 0u;
    }
    t->pass++;
    if (t->pass == 0u) {
        t->pass = 1u;
    }
    return t->pass;
}

void character_bodies_mark(character_bodies_t *t, uint32_t index, uint32_t pass)
{
    if (t != NULL && index < BODY_MAX && t->body[index].used) {
        t->body[index].checked = pass;
    }
}

body_plan_t character_bodies_on_arm(const body_entry_t *body)
{
    body_plan_t plan;

    plan.clear_cursors = body != NULL && body->used;
    plan.waterline = body != NULL && body->used && body->local;
    return plan;
}

body_plan_t character_bodies_on_disarm(const body_entry_t *body, const body_reads_t *reads)
{
    body_plan_t plan;

    plan.clear_cursors = character_bodies_holds(body, reads);
    plan.waterline = body != NULL && body->used && body->local;
    return plan;
}

/* ============================================================================================ */

body_pose_t character_bodies_pose(const body_entry_t *body, bool holds, uintptr_t worn)
{
    if (body == NULL || !body->used) {
        return BODY_POSE_ENGINE;
    }
    if (holds) {
        return BODY_POSE_TRANSLATED;
    }
    return (worn != 0u && worn == body->target) ? BODY_POSE_FROZEN : BODY_POSE_ENGINE;
}

body_guard_t character_bodies_guard(bool borrowed, bool body_read, uintptr_t body_in_block,
                                    uintptr_t swapped_body, bool swap_live)
{
    if (borrowed && body_read && body_in_block != swapped_body) {
        return BODY_GUARD_ENGINE;
    }
    if (swap_live) {
        return BODY_GUARD_DECLINE;
    }
    return borrowed ? BODY_GUARD_TAKE_DOWN : BODY_GUARD_ENGINE;
}

/* ============================================================================================ */

static far_step_t step(far_verdict_t verdict, uint8_t reason)
{
    far_step_t answer;

    answer.verdict = verdict;
    answer.reason = reason;
    return answer;
}

far_step_t character_bodies_far_step(const far_facts_t *facts)
{
    if (facts == NULL) {
        return step(FAR_WAIT, MODEL_WEAR_REASON_NONE);
    }
    /* A body that wears a model keeps it until the multiplayer builds it again. Whatever the bank
     * asks for now, the answer names what is on the body, and the row stays: taking the
     * translation off a body that still wears the rig would leave the hero's clips driving it by
     * ordinal. */
    if (facts->entry) {
        if (facts->wants && facts->entry_is_wish) {
            return step(FAR_WORN, MODEL_WEAR_REASON_NONE);
        }
        return step(FAR_REFUSE, MODEL_WEAR_REASON_WEARS_OTHER);
    }
    if (!facts->asked || !facts->wants) {
        return step(FAR_NONE, MODEL_WEAR_REASON_NONE);
    }
    if (facts->answered) {
        return step(FAR_KEEP, MODEL_WEAR_REASON_NONE);
    }
    if (!facts->block_sound) {
        return step(FAR_REFUSE, MODEL_WEAR_REASON_BAD_BLOCK);
    }
    if (!facts->able) {
        return step(FAR_REFUSE, MODEL_WEAR_REASON_NOT_ABLE);
    }
    /* A far Jedi's own spawn already went through the blade guard, so for him the proof that the
     * guard is reached is there to be had; his take down is a resize the guard has to decline. */
    if (facts->jedi && !facts->blade_seen) {
        return step(FAR_REFUSE, MODEL_WEAR_REASON_BLADE);
    }
    /* Neither of these is an answer. The first is a broken invariant, the second a note older
     * than the body, which the multiplayer replaces within a substep. */
    if (!facts->window_shut || !facts->block_names_body) {
        return step(FAR_WAIT, MODEL_WEAR_REASON_NONE);
    }
    if (!facts->fresh) {
        return step(FAR_REFUSE, MODEL_WEAR_REASON_NOT_FRESH);
    }
    return step(FAR_LOAD, MODEL_WEAR_REASON_NONE);
}

bool character_bodies_far_final(uint8_t state, uint8_t reason)
{
    if (state == (uint8_t)MODEL_WEAR_STATE_WORN) {
        return true;
    }
    if (state != (uint8_t)MODEL_WEAR_STATE_REFUSED) {
        return false;
    }
    return reason != (uint8_t)MODEL_WEAR_REASON_BLADE &&
           reason != (uint8_t)MODEL_WEAR_REASON_NO_ROOM &&
           reason != (uint8_t)MODEL_WEAR_REASON_WEARS_OTHER;
}

const char *character_bodies_reason_text(uint8_t reason)
{
    switch (reason) {
    case MODEL_WEAR_REASON_NO_ROW:      return "the model is no row of the model roster";
    case MODEL_WEAR_REASON_NO_ASSET:    return "the asset or its model would not load";
    case MODEL_WEAR_REASON_FIT:         return "too few of the hero's node names are on the rig";
    case MODEL_WEAR_REASON_NOT_ABLE:    return "the swap's sites, guard or translation are missing";
    case MODEL_WEAR_REASON_BLADE:       return "a Jedi body, and the blade guard has not asked yet";
    case MODEL_WEAR_REASON_NOT_FRESH:   return "the body does not wear its own actor's model";
    case MODEL_WEAR_REASON_WEARS_OTHER: return "the body still wears another model";
    case MODEL_WEAR_REASON_NO_ROOM:     return "no body or pair is left in the translation";
    case MODEL_WEAR_REASON_BIND_FAILED: return "the rebind failed and the body is its hero again";
    case MODEL_WEAR_REASON_NO_HAND:     return "the rig has no node a push could leave from";
    case MODEL_WEAR_REASON_BAD_BLOCK:   return "the block the wish names is not one to write";
    case MODEL_WEAR_REASON_BROKEN:      return "the rebind and the way back both failed";
    default:                            return "no reason";
    }
}
