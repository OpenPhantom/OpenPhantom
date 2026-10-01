/* character_prop_body.c: the rows of the bodies a weapon is drawn on, and nothing that reads a
 * model.
 *
 * This file was cut out of character_prop.c along its lifetime: what a body IS, how it is found
 * and how long it lasts lives here, and what a rig SAYS lives there. An earlier reading called
 * that cut impossible, and it was looking at a different seam: the visibility half and the draw
 * half share the shown parts array, so a boundary between THEM would have put nine fields either
 * side of a translation unit that are written and read inside one frame. The boundary that works
 * runs the other way, and the reason it works is that the state is a row a caller hands over
 * rather than a global either file can reach.
 *
 * The table is sized for the player's own body and one per far bank. The player's own row is armed
 * out of the swap next door; a far row is armed out of the far path, at the end of a dressing, and
 * the two differ in three ways and no more: a far row carries a block of its own, it is lent the
 * words that hide its rig's own weapon instead of collecting them, and its hand is resolved when
 * it is armed rather than at its first draw, because the swing starter asks for that hand inside a
 * bank window that can open before the body has ever been drawn.
 */
#include "character_prop_body.h"

#include "character_prop.h"
#include "character_rebind.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stddef.h>
#include <string.h>

static struct {
    prop_body_t body[BODY_MAX];
    uint32_t    pass;
    bool        frame_ended;
    bool        drawing;
    uint32_t    tally[PROP_TALLY_MAX];
} table;

/* ============================================================================================ */

prop_body_t *character_prop_body_of_bank(uint8_t bank)
{
    uint32_t i;

    for (i = 0; i < BODY_MAX; ++i) {
        if (table.body[i].used && table.body[i].bank == bank) {
            return &table.body[i];
        }
    }
    return NULL;
}

prop_body_t *character_prop_body_hold(uint8_t bank)
{
    prop_body_t *body = character_prop_body_of_bank(bank);
    uint32_t     i;

    if (body != NULL) {
        return body;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (!table.body[i].used) {
            memset(&table.body[i], 0, sizeof table.body[i]);
            table.body[i].used = true;
            table.body[i].bank = bank;
            return &table.body[i];
        }
    }
    return NULL;
}

prop_body_t *character_prop_body_for_thing(const void *thing)
{
    uint32_t i;

    if (thing == NULL) {
        return NULL;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (table.body[i].used && table.body[i].armed &&
            table.body[i].thing == (uintptr_t)thing) {
            return &table.body[i];
        }
    }
    return NULL;
}

prop_body_t *character_prop_body_for_obj(const void *obj)
{
    uint32_t i;

    if (obj == NULL) {
        return NULL;
    }
    for (i = 0; i < BODY_MAX; ++i) {
        if (table.body[i].used && table.body[i].armed && table.body[i].obj == (uintptr_t)obj) {
            return &table.body[i];
        }
    }
    return NULL;
}

uint32_t character_prop_body_armed(void)
{
    uint32_t armed = 0;
    uint32_t i;

    for (i = 0; i < BODY_MAX; ++i) {
        if (table.body[i].used && table.body[i].armed) {
            armed++;
        }
    }
    return armed;
}

/* ============================================================================================ */

/* Whether `block` is a player block naming this row's body. */
static bool block_names(uintptr_t block, uintptr_t obj)
{
    uint32_t named = 0;

    return block != 0 && memory_try_read(block + PROP_BLOCK_ACTOR, &named, sizeof named) &&
           (uintptr_t)named == obj;
}

bool character_prop_block_of(const prop_body_t *body, uintptr_t *out_block)
{
    uint32_t cell = 0;

    if (body == NULL || out_block == NULL || body->record == 0) {
        return false;
    }
    if (memory_try_read(body->record, &cell, sizeof cell) && cell != 0u) {
        /* A row with no block of its own is read through the cell and through nothing else, and
         * the cell is read again every time, so a level change cannot leave this module driving a
         * body that no longer exists. */
        if (body->block == 0 || block_names((uintptr_t)cell, body->obj)) {
            *out_block = (uintptr_t)cell;
            return true;
        }
    }
    if (body->block != 0 && block_names(body->block, body->obj)) {
        *out_block = body->block;
        return true;
    }
    return false;
}

bool character_prop_body_still_ours(const prop_body_t *body, uintptr_t *out_block)
{
    uint32_t thing = 0;

    if (!character_prop_block_of(body, out_block) || !block_names(*out_block, body->obj)) {
        return false;
    }
    return memory_try_read(body->obj + PROP_OBJ_THING, &thing, sizeof thing) &&
           (uintptr_t)thing == body->thing;
}

/* ============================================================================================ */

void character_prop_body_frame_ended(void)
{
    table.frame_ended = true;
}

void character_prop_body_begin_pass(void)
{
    if (table.frame_ended) {
        table.frame_ended = false;
        table.pass++;
    }
}

void character_prop_body_place(prop_body_t *body, bool placed)
{
    if (body == NULL) {
        return;
    }
    /* The stamp is written for a refusal as well, so a row that was drawn and measured nothing
     * carries the current pass rather than the one it last succeeded in. Without that a stamp
     * could only go stale by the counter running all the way round. */
    body->placed = placed;
    body->placed_pass = table.pass;
}

bool character_prop_body_is_placed(const prop_body_t *body)
{
    return body != NULL && body->placed && body->placed_pass == table.pass;
}

bool character_prop_body_is_drawing(void)
{
    return table.drawing;
}

void character_prop_body_drawing(bool drawing)
{
    table.drawing = drawing;
}

/* ============================================================================================ */

void character_prop_body_forget(prop_body_t *body)
{
    if (body == NULL) {
        return;
    }
    character_prop_write_own_weapons(body, 0u);
    body->target_model = 0;
    body->hidden_count = 0;
    body->hidden_from = PROP_HIDDEN_COLLECTED;
    body->target_hand_slot = 0;
    body->target_hand_node = 0;
    character_prop_body_place(body, false);
    body->found_said = false;
    body->older_said = false;
}

void character_prop_body_model_changing(void)
{
    character_prop_body_forget(character_prop_body_of_bank(PROP_BANK_LOCAL));
}

bool character_prop_arm(uintptr_t player_record, uintptr_t player_obj, uintptr_t reference_model)
{
    prop_body_t *body;
    uint32_t     thing = 0;

    if (!character_prop_sites_ready() || player_record == 0 || player_obj == 0 ||
        reference_model == 0) {
        return false;
    }
    if (!memory_try_read(player_obj + PROP_OBJ_THING, &thing, sizeof thing) || thing == 0u) {
        return false;
    }
    body = character_prop_body_hold(PROP_BANK_LOCAL);
    if (body == NULL) {
        return false;
    }
    /* Whatever the last body wore, its own weapon words go back to it. The guard inside declines
     * when the handle no longer wears the rig those slots were resolved against, which is the case
     * on every path that reaches here from a swap: the caller rebinds the handle first, and the
     * table rdThing_SetModel allocates for the new rig is zeroed by the engine anyway. */
    if (body->armed) {
        if (body->thing != (uintptr_t)thing) {
            body->target_model = 0;   /* another body: its predecessor's words are not ours */
        }
        character_prop_body_forget(body);
    }

    if (body->reference_model != reference_model) {
        body->armed = false;
        if (!character_prop_bind_reference(body, reference_model)) {
            return false;
        }
    }

    body->record = player_record;
    body->block = 0;              /* the cell is the player's own record and nothing else is */
    body->obj = player_obj;
    body->thing = (uintptr_t)thing;
    body->target_model = 0;
    body->target_hand_slot = 0;
    body->hidden_count = 0;
    body->shown_name_id = PROP_NAME_ID_NONE;
    body->shown_part_count = 0;
    character_prop_body_place(body, false);
    table.drawing = false;
    body->armed = true;
    log_info("the player's own weapon meshes are mounted on the borrowed body: reference model "
             "%08X, %u nodes, right hand at slot %u", (unsigned)body->reference_model,
             (unsigned)body->reference_nodes, (unsigned)body->hand_slot);
    return true;
}

void character_prop_body_disarm(prop_body_t *body)
{
    if (body == NULL || !body->armed) {
        return;
    }
    /* The borrowed rig's own weapon words are given back first, so a body that goes on living
     * after this module lets go is not left with part of itself invisible. Zero is what a fresh
     * bind leaves there and what the engine's next weapon change would write anyway, and the guard
     * inside declines when the body no longer wears the rig those words belong to. */
    character_prop_body_forget(body);
    body->armed = false;
    table.drawing = false;
    body->shown_name_id = PROP_NAME_ID_NONE;
    if (body->bank == PROP_BANK_LOCAL) {
        log_info("the borrowed weapon is put away");
        return;
    }
    log_info("the far body in bank %u no longer carries its player's weapon", (unsigned)body->bank);
}

/* ============================================================================================ */

/* A far player's body, arming step by step. The answer is the reason it did not, or NULL. */
static const char *far_arm(const prop_far_t *far, prop_body_t **out_body,
                           char weapon[PROP_NAME_BYTES])
{
    prop_body_t *body;
    uint32_t     thing = 0;

    if (!character_prop_sites_ready()) {
        return "the places a weapon is drawn and shot from did not resolve";
    }
    if (far->record == 0 || far->block == 0 || far->obj == 0 || far->thing == 0 ||
        far->reference == 0 || far->hidden_count > PROP_HIDE_MAX ||
        (far->hidden == NULL && far->hidden_count != 0u)) {
        return "the far path named no body to hang one on";
    }
    if (!memory_try_read(far->obj + PROP_OBJ_THING, &thing, sizeof thing) ||
        (uintptr_t)thing != far->thing) {
        return "the body does not own the render handle it was dressed on";
    }
    body = character_prop_body_hold(far->bank);
    if (body == NULL) {
        return "every row of the table is taken";
    }
    character_prop_body_disarm(body);
    if (body->reference_model != far->reference &&
        !character_prop_bind_reference(body, far->reference)) {
        return "a second render handle on its hero could not be built";
    }
    body->record = far->record;
    body->block = far->block;
    body->obj = far->obj;
    body->thing = far->thing;
    body->target_model = 0;
    body->target_hand_slot = 0;
    body->target_hand_node = 0;
    /* LENT, not collected. The far path writes these words per frame and gives them back; this
     * row only reads them, to tell a weapon name landing on the rig's own hidden blade apart from
     * one landing on something that is drawn. */
    body->hidden_from = PROP_HIDDEN_LENT;
    body->hidden_count = far->hidden_count;
    if (far->hidden_count != 0u) {
        memcpy(body->hidden, far->hidden, far->hidden_count * sizeof body->hidden[0]);
    }
    body->shown_name_id = PROP_NAME_ID_NONE;
    body->shown_part_count = 0;
    body->found_said = false;
    body->older_said = false;
    character_prop_body_place(body, false);
    if (!character_prop_resolve_hand(body, weapon)) {
        return "the borrowed rig states no hand to hang one on";
    }
    body->armed = true;
    *out_body = body;
    return NULL;
}

bool character_prop_far_arm(const prop_far_t *far)
{
    prop_body_t *body = NULL;
    char         weapon[PROP_NAME_BYTES];
    char         hand[PROP_NAME_BYTES];
    const char  *refused;

    if (far == NULL || far->bank == PROP_BANK_LOCAL || far->bank > MODEL_WEAR_BANKS) {
        return false;
    }
    memcpy(weapon, "empty hand", 11u);
    refused = far_arm(far, &body, weapon);
    if (refused != NULL) {
        table.tally[PROP_TALLY_REFUSED]++;
        log_info("the far body in bank %u carries no weapon: %s (body %u)", (unsigned)far->bank,
                 refused, (unsigned)far->serial);
        return false;
    }
    /* The node the rig states, by the name it states it under. The slot is the array position on
     * every shipped asset, which is the same reading the far path's own log line takes. */
    if (!character_rebind_node_name(body->target_model, (int32_t)body->target_hand_slot, hand,
                                    sizeof hand)) {
        memcpy(hand, "?", 2u);
    }
    table.tally[PROP_TALLY_ARMED]++;
    log_info("the far body in bank %u carries its player's %s: reference model %08X, on the "
             "borrowed rig's %s at slot %u (body %u)", (unsigned)far->bank, weapon,
             (unsigned)body->reference_model, hand, (unsigned)body->target_hand_slot,
             (unsigned)far->serial);
    return true;
}

bool character_prop_far_carries(uint8_t bank)
{
    const prop_body_t *body = character_prop_body_of_bank(bank);

    return bank != PROP_BANK_LOCAL && bank <= MODEL_WEAR_BANKS && body != NULL && body->armed;
}

void character_prop_far_disarm(uint8_t bank)
{
    prop_body_t *body = character_prop_body_of_bank(bank);

    if (body == NULL || bank == PROP_BANK_LOCAL) {
        return;
    }
    character_prop_body_disarm(body);
    character_prop_release_reference(body);
    memset(body, 0, sizeof *body);
}

/* ============================================================================================ */

void character_prop_body_tally(prop_tally_t what)
{
    if (what < PROP_TALLY_MAX) {
        table.tally[what]++;
    }
}

void character_prop_body_report(void)
{
    uint32_t i;

    for (i = 0; i < PROP_TALLY_MAX; ++i) {
        if (table.tally[i] != 0u) {
            break;
        }
    }
    if (i == PROP_TALLY_MAX) {
        return;
    }
    log_info("far weapons: armed %u, refused %u; spheres from the draw %u, measured without a "
             "draw %u, from an older pass %u, never answered %u",
             table.tally[PROP_TALLY_ARMED], table.tally[PROP_TALLY_REFUSED],
             table.tally[PROP_TALLY_FROM_DRAW], table.tally[PROP_TALLY_MEASURED],
             table.tally[PROP_TALLY_OLDER], table.tally[PROP_TALLY_UNANSWERED]);
    memset(table.tally, 0, sizeof table.tally);
}

void character_prop_disarm(void)
{
    character_prop_body_disarm(character_prop_body_of_bank(PROP_BANK_LOCAL));
}

bool character_prop_is_armed(void)
{
    return character_prop_body_armed() != 0u;
}
