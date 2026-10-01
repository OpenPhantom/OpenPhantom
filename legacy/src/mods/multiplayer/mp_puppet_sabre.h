/* mp_puppet_sabre.h: the far player's sabre actions performed on the puppet.
 *
 * A swing, a block, a parry and the disarm that ends each of them arrive as events, because none
 * of them can be read off state: the row of the swing table decides the contact node, radius and
 * impact of a swing, the block and parry clips were chosen against a bolt or an attacker this
 * machine never saw, and the disarm happens at a marker inside a clip. Each is performed inside
 * the puppet's bank window, where the player pointer names the puppet's record, through the
 * engine's own starters where one exists (the swing starter, the swing end) and by the same
 * stores the engine's own deflect and parry make where the engine's function would scan for a
 * bolt this machine does not have.
 *
 * The decisions are plain functions over plain numbers and are pinned in a unit test: which
 * stores an action needs, whether a block waits for the aux slot or takes it over, when the
 * fallback disarm is due, and which operands are refused. The engine reads and calls that carry
 * them out follow in the second half of the module.
 *
 * The blade is armed by an event and disarmed by the disarm event, which the sender sends from
 * the one function every sabre action ends through. Should that event be lost, a fallback
 * disarms the blade when the clip the action plays has ended or its track is gone, or after a
 * second and a half, which is longer than any swing clip; a peer change, which empties the rings
 * the event was in, makes the fallback due in the first window after it. The engine's
 * return-to-stand entry at 0x0044CD1C is never called on a puppet, because it hangs the stand
 * descriptor on +0x60 and writes the speed caps, and the puppet's plan reads no descriptor.
 */
#ifndef MULTIPLAYER_MP_PUPPET_SABRE_H
#define MULTIPLAYER_MP_PUPPET_SABRE_H

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The swing table has 28 rows. Which of them plays its clip on the overlay channel is read out of
 * the table itself, by mp_puppet_starter_swing_overlay. */
#define MP_PUPPET_SABRE_SWING_ROWS 28u

/* The first six of those rows swing a fist or a foot: their node column names the rig's own hand
 * or foot and not the blade. A weapon hung on a borrowed rig is answered for under the blade's
 * name alone, so a swing from one of these rows has no measured sphere on such a body however
 * well the weapon hangs, and its contact is put back whatever the body carries. */
#define MP_PUPPET_SABRE_UNARMED_ROWS 6u

/* The overlay clips a block and a parry may name, as the engine's own deflect and parry choose
 * them. An operand outside these is refused: the state path treats every other overlay as its
 * own to start, and performing it here as well would start it twice. */
#define MP_PUPPET_SABRE_BLOCK_FIRST 0x60u
#define MP_PUPPET_SABRE_BLOCK_LAST  0x69u
#define MP_PUPPET_SABRE_PARRY_FIRST 0x5Du
#define MP_PUPPET_SABRE_PARRY_LAST  0x5Fu

/* The fallback disarm: a second and a half at 32 substeps a second, longer than the longest
 * swing clip, which is 1.189 seconds for rows 20 and 21 of the table; a design value over that
 * length, not a measurement against the field. And how long a block or parry may wait for the
 * aux slot, or the midair swing for a free track, before it is dropped: the same two seconds a
 * push waits. */
#define MP_PUPPET_SABRE_FALLBACK_SUBSTEPS 48u
#define MP_PUPPET_SABRE_AUX_WAIT_LIMIT    64u

/* The stores an action makes on the puppet before its clip plays, as a mask: the blade's contact
 * node from the record's sabre node, the contact radius, the reflect contact code, the deflect
 * success latch cleared, and the parry's absorb count cleared. A swing makes none of them (the
 * engine's own starter makes its own), and a disarm makes none (the engine's own end clears). */
#define MP_PUPPET_SABRE_STORE_NODE   0x01u
#define MP_PUPPET_SABRE_STORE_RADIUS 0x02u
#define MP_PUPPET_SABRE_STORE_CODE   0x04u
#define MP_PUPPET_SABRE_STORE_LATCH  0x08u
#define MP_PUPPET_SABRE_STORE_ABSORB 0x10u

uint32_t mp_puppet_sabre_stores(uint8_t action);

/* Whether an event's operand is one this module performs: a swing row inside the table, a block
 * or parry clip inside its range, a disarm carrying nothing. */
bool mp_puppet_sabre_row_ok(uint8_t row);
bool mp_puppet_sabre_operand_ok(uint8_t action, uint8_t operand);

/* What a block or parry does about the aux slot it needs: an empty slot is taken, a slot holding
 * the deflect's or the parry's own continuation is taken over (the engine lets a new block
 * replace a running one), and any other continuation (a weapon change, a push) is waited for. */
typedef enum mp_puppet_sabre_aux_verdict {
    MP_PUPPET_SABRE_AUX_FREE,
    MP_PUPPET_SABRE_AUX_OVERWRITE,
    MP_PUPPET_SABRE_AUX_WAIT
} mp_puppet_sabre_aux_verdict_t;

mp_puppet_sabre_aux_verdict_t mp_puppet_sabre_aux_verdict(uint32_t current_aux,
                                                          uint32_t block_aux, uint32_t parry_aux);

/* Whether a continuation is one of the two this module installs, which the disarm clears. An
 * unknown continuation (0) matches nothing. */
bool mp_puppet_sabre_aux_is_ours(uint32_t aux, uint32_t block_aux, uint32_t parry_aux);

/* When the fallback disarms an armed blade nobody has disarmed: the track the action plays on
 * is gone or has completed, or the action has been armed for the fallback's span. */
bool mp_puppet_sabre_fallback_due(bool track_gone, bool track_complete, uint32_t armed_substeps);

/* ==============================================================================================
 * The engine half: what the decisions are carried out with.
 * ============================================================================================ */

typedef struct mp_puppet_sabre_counters {
    uint32_t swings;
    uint32_t blocks;
    uint32_t parries;
    uint32_t disarms;            /* the disarm event performed */
    uint32_t fallback_disarms;   /* disarmed by the fallback, the event never came */
    uint32_t refused;            /* an operand out of range, a site missing, an object unread */
    uint32_t held_dropped;       /* a block or parry that waited two seconds for the aux slot */
    uint32_t track_dropped;      /* a midair swing that waited two seconds for a free track */
    uint32_t overlays_unplayed;  /* a block or parry armed whose clip did not play */
    uint32_t write_faults;       /* a store the patch layer refused */
    uint32_t worn_swings;               /* a worn body's swing, whatever node it had */
    uint32_t swing_contacts_kept;       /* of those, the ones that swung a weapon that hangs */
    uint32_t swing_contacts_withheld;   /* and the ones whose contact node was put back to 0 */
} mp_puppet_sabre_counters_t;

/* Resolve the swing starter, the swing end, the overlay player and the two continuation cells.
 * A site that did not resolve leaves its action unperformed and says so once. */
void mp_puppet_sabre_resolve(void);

/* On every arrival of a peer in front of far bank `bank`: forget that blade's waits for the aux
 * slot and for a free track, and make a
 * blade that is still armed due for the fallback in the next window, because the disarm event was
 * in the rings the arrival emptied. Never disarms by itself: the swing end may only be called
 * inside the window. The counters are the report's and survive. */
void mp_puppet_sabre_reset(size_t bank);

/* What performing one event answers: consumed (performed or refused), or held for a later
 * window because the aux slot is busy, in which case the caller keeps the event in order. */
typedef enum mp_puppet_sabre_result {
    MP_PUPPET_SABRE_DONE,
    MP_PUPPET_SABRE_HOLD
} mp_puppet_sabre_result_t;

/* Perform one sabre event inside bank `bank`'s window, over that puppet's record and object, on
 * that far body's own blade. `aux_taken`
 * is the window's own note that an earlier event of this window took the aux slot; a block that
 * installs its continuation sets it. */
mp_puppet_sabre_result_t mp_puppet_sabre_perform(size_t bank, uint32_t record, uint32_t object,
                                                 const mp_event_t *event, bool *aux_taken);

/* A swing just started on a far body that wears a borrowed model: its contact node is put back to
 * 0, which the pair pass reads as no sphere, because the node the starter found by the hero's
 * blade name is missing, hidden or without a mesh on a borrowed rig.
 *
 * Unless the body carries its player's own weapon and `row` is a row that swings one. The blade
 * name then answers the hand that weapon is drawn at, whose sphere is the drawn weapon's, and the
 * swing keeps the contact the starter armed.
 *
 * False when nothing was written: the body wears its hero, the swing kept its contact, the node
 * was 0 already, or the write was refused, which is counted with the other write faults. Every
 * call for a worn body with an object counts as a worn swing, whatever the node. */
bool mp_puppet_sabre_withhold_contact(uint32_t object, bool worn, bool weapon, uint8_t row);

/* Once per substep inside the window, after the events: the fallback disarm. */
void mp_puppet_sabre_tick(size_t bank, uint32_t record, uint32_t object);

/* Whether the blade of bank `bank`'s puppet is still armed, for the report and the caller. */
bool mp_puppet_sabre_armed(size_t bank);

void mp_puppet_sabre_counters(mp_puppet_sabre_counters_t *out);

#endif /* MULTIPLAYER_MP_PUPPET_SABRE_H */
