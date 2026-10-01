/* mp_body_death.h: who died here, who did it, and whether that pair was allowed to hurt at all.
 *
 * The seam mp_body.c named for itself and measured. The dispatcher stays there, because it is the
 * contact path; the attribution behind it is this file, and the friendly fire gate comes with it
 * because it is the same subject and reads the same table of bank slots.
 *
 * ============================== Why the answer only exists here ===============================
 *
 * A contact delivery is the one moment at which this machine can say both that its player died
 * and who killed him. The player record's dead flag is 0 in front of the engine's own handler and
 * 1 behind it, and the sender of the message carries the collision class of the bank it came out
 * of. A substep later the message globals hold whatever happened next, so the question has to be
 * asked around that one call and nowhere else.
 *
 * ================================= What is public and what is not =============================
 *
 * The names a caller outside the module uses stay in mp_body.h: setting the world slot a bank
 * stands for, the death listener, the damage gate and the two death counters are all declared
 * there and defined here. This header is only the contract between the two translation units.
 */
#ifndef MULTIPLAYER_MP_BODY_DEATH_H
#define MULTIPLAYER_MP_BODY_DEATH_H

#include "mp_contact_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Resolves the player record pointer and the contact sender. False when either did not, in which
 * case a death of this player cannot be seen at all and nothing here reports one; the dispatcher
 * goes on doing everything else it did. */
bool mp_body_death_install(void);
bool mp_body_death_ready(void);

/* The two halves of one delivery to the local player's own body. Begin is called in front of the
 * engine's own handler and end behind it; a dead flag that was 0 and is now 1 is that contact
 * having killed him, and it is attributed and reported from end. Calling end without begin is a
 * no-op, so a path that decided not to watch does not have to say so twice. */
void mp_body_death_watch_begin(void);
void mp_body_death_watch_end(void);

/* What the contact being delivered is to this machine: allowed, refused by the rule set, or not
 * this machine's to judge. `victim_bank` is the bank of the body that was TOUCHED: 0 for this
 * machine's own player, i for a far one. The sender's bank is read off the message, so one
 * function answers both directions, and it answers the same either way: whichever of the pair is
 * the far player, the gate is asked about that one's world slot, and the rule behind it gives the
 * same verdict whoever of the two swung. A refusal drops the whole contact rather than softening
 * it, so no health goes, nothing is shoved and no limb comes off. What each delivery path does
 * with the answer is mp_contact_rule's.
 *
 * While it took the bank of a FAR body only, it judged the half in which this machine's player is
 * the attacker. The other half, a far player's bolt, blade, push or blast arriving on this
 * player's own body, is the half a client is hurt through, and that half was never asked. */
mp_contact_verdict_t mp_body_death_contact_verdict(size_t victim_bank);

/* The same ordering with the sender's bank passed in rather than read off the message, so it can
 * be driven in a process with no game. Negative is a sender that stands for no bank at all. */
mp_contact_verdict_t mp_body_death_judge_contact(int32_t from, size_t victim_bank);

/* Whether an object is a projectile's body, asked of the hit relay, which owns the test; unset,
 * every sender is read as a body. */
typedef bool (*mp_body_death_shot_test_fn_t)(uint32_t object);
void mp_body_death_set_shot_test(mp_body_death_shot_test_fn_t test);

/* Whether the contact being delivered was sent by an ally, asked of the hit relay; unset, nobody
 * is an ally. An ally reads as the local player's side here, and is refused before the rule set
 * is asked: it hurts no player, whatever friendly fire says. */
typedef bool (*mp_body_death_ally_test_fn_t)(void);
void mp_body_death_set_ally_test(mp_body_death_ally_test_fn_t test);

/* Which far player's bank fired the shot flying as `object`, for a shot the engine left
 * with no side of its own. Without it such a contact names no bank, and a contact that
 * names no bank never reaches the friendly fire gate at all. */
typedef bool (*mp_body_death_far_shot_fn_t)(uint32_t object, size_t *bank);
void mp_body_death_set_far_shot_test(mp_body_death_far_shot_fn_t test);

/* The attribution's own lines of the body module's report. */
void mp_body_death_report(void);

#endif /* MULTIPLAYER_MP_BODY_DEATH_H */
