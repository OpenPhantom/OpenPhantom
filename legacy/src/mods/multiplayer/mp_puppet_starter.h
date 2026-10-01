/* mp_puppet_starter.h: the guard in front of the three engine starters the puppet calls.
 *
 * The weapon setter, the force push starter and the swing starter each play a clip of their own
 * choosing on the overlay channel, and the overlay player ends the process when the body's actor
 * does not carry it. That is not a case of a broken asset: a far body built as the local hero
 * because the far player's hero or asset was not known here yet carries another actor than the
 * far player's own, and Panaka and the Queen carry neither the push clip nor the midair clip. So
 * the puppet asks here before every call. The answer comes from the same two words the overlay
 * player reads, the actor at +0x14 of the object and its clip count.
 *
 * A change, push or swing the actor cannot play is not started at all and is not tried again: the
 * starter is never entered, so the weapon slots and the aux slot stay as they were, and a clip that
 * is missing now is missing a second later as well. Each kind is said once per process with the
 * actor's name and count, and counted after that. A verdict that could not read the actor is a
 * wait, never a call.
 *
 * One body cannot be left there. A far body that wears a borrowed model draws every clip out of
 * the lent rig, and of the three hundred actors in the game six carry the setter's draw clips, so
 * refusing the change would freeze that body's weapon for the rest of the level. For that body
 * the answer is MP_STARTER_SET_ONLY: the caller writes the two weapon words itself and the change
 * takes effect without the draw animation and without the sabre's ignition sound. A body without
 * a model, and the local player, are answered exactly as before.
 */
#ifndef MULTIPLAYER_MP_PUPPET_STARTER_H
#define MULTIPLAYER_MP_PUPPET_STARTER_H

#include "mp_starter_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The weapon setter asked for `wanted` on bank `bank`'s body `object`, which holds `equipped`;
 * `worn` is whether that body wears a borrowed model, which the caller knows and this module does
 * not ask. A slot past the weapon table is never, worn or not: that bound goes first. A missing
 * draw clip is MP_STARTER_SET_ONLY for a worn body and MP_STARTER_NEVER for every other, and an
 * object without an actor stays never, because it is no body that wears anything. The same
 * refused change asked again for the same body, by the state after the event or the other way
 * round, is never again without a second count; a set is not remembered, because the written slot
 * answers the second asker by itself. */
mp_starter_verdict_t mp_puppet_starter_weapon(size_t bank, uint32_t object, uint32_t equipped,
                                              uint32_t wanted, bool worn);

/* The equipped slot and the request of the record `record` the window installed, both set to
 * `slot`: what the setter and its equip commit would have written between them for a change whose
 * draw clip cannot be played. Answers whether both words read back, and counts. Only for a body
 * the verdict above sent here. */
bool mp_puppet_starter_set_slot(size_t bank, uint32_t record, uint32_t slot);

/* The force push starter on bank `bank`'s body `object`. */
mp_starter_verdict_t mp_puppet_starter_push(size_t bank, uint32_t object);

/* Whether row `row` of the swing table at `table` plays its clip on the overlay channel, read the
 * way the swing starter reads it: the row's first word against the midair clip. A table of 0, or a
 * word that does not read, answers what the shipped table says, which is the midair row alone.
 * This one reading decides both the guard and how the puppet waits for the swing. */
bool mp_puppet_starter_swing_overlay(uintptr_t table, uint8_t row);

/* The swing starter on bank `bank`'s body `object` for a row whose clip plays on the overlay
 * channel when `overlay` is set; a row on the base channel is always a go. */
mp_starter_verdict_t mp_puppet_starter_swing(size_t bank, uint32_t object, bool overlay);

/* A new body in front of bank `bank`: the refused change it remembers is forgotten, because the
 * new body's actor decides again. The counters are the report's and survive. */
void mp_puppet_starter_reset(size_t bank);

typedef struct mp_puppet_starter_counters {
    uint32_t weapon_missing;       /* weapon changes not started: the actor lacks the draw clip */
    uint32_t push_missing;         /* pushes dropped: the actor lacks the push clip */
    uint32_t midair_missing;       /* midair swings dropped: the actor lacks the midair clip */
    uint32_t slot_past_table;      /* weapon changes to a slot the weapon table has no row for */
    uint32_t unread;               /* verdicts that waited because the actor or its count failed */
    uint32_t fewest_clips;         /* the fewest clips an actor judged here carried; 0 for none */
    uint32_t weapon_set_worn;      /* weapon changes written into a worn body's block instead */
    uint32_t set_writes_refused;   /* those writes whose two words did not read back */
} mp_puppet_starter_counters_t;

void mp_puppet_starter_counters(mp_puppet_starter_counters_t *out);

#endif /* MULTIPLAYER_MP_PUPPET_STARTER_H */
