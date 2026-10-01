/* mp_effects.h: what the actor's own handler would have done, for a body that does not act here.
 *
 * The engine splits one blade contact into two messages with two different receivers. The victim
 * gets the one that carries the damage; the actor gets the one that carries the feeling of having
 * connected, the spark, the flash, the impact voice and the shove on himself. A puppet is the
 * actor of every action the far player took, and its contacts are answered rather than run,
 * because what touches it is the far machine's to judge. The effect message is answered along
 * with the rest, so a replicated sabre reads as an animation with no consequence.
 *
 * There is a second lock behind the first. The armed arm demands the sabre attack descriptor on
 * the record or midair attack phase 1, and a puppet's plan hangs no mode descriptor at all, so
 * even an unsuppressed contact would have reached that arm and returned. Both locks are why this
 * is a layer of its own rather than a condition removed from the dispatcher.
 *
 * This module puts the effects back, and nothing else. No damage, no mode descriptor, no HUD
 * cell, no knockback. It is the smallest thing that makes a replicated action look like one, and
 * it is written so that a replicated enemy or a mover can be given the same treatment later
 * without the puppet module growing another special case.
 *
 * Two moments, and they cannot be one. The contact arrives inside the pair pass, at bank 0, where
 * the player record names the LOCAL player: the impact voice reads and writes a cooldown in that
 * record, so playing it there would spend the local player's voice. So the contact is only
 * judged there and the verdict is held; the effects run in the next puppet window, where the
 * record and the object are the puppet's own.
 */
#ifndef MULTIPLAYER_MP_EFFECTS_H
#define MULTIPLAYER_MP_EFFECTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The impact voice groups, as the armed contact's third arm picks them: the body group for a
 * blade that met flesh, the blade group for a blade that met another blade. */
#define MP_EFFECTS_VOICE_BODY  2
#define MP_EFFECTS_VOICE_BLADE 3

/* The contact codes each voice group answers, and the code that answers none.
 *
 * This is the engine's own rule and not a wider one, because the point of the module is that a
 * far body's hit looks like a near body's. The arm this stands in for plays the flash and the
 * spark for EVERY code it reaches; only the voice is chosen by the code, 2 and 3 for a body and
 * 0x21 or 0x25 for a blade, the two contact codes a sabre carries for a Jedi and for anyone else.
 * A code outside both, which is what the pair pass sends when the victim is another player,
 * leaves the swing silent and still lit: that is what the local player's own swing does, so it is
 * what the puppet's has to do. */
#define MP_EFFECTS_CODE_BODY_FIRST  2u
#define MP_EFFECTS_CODE_BODY_LAST   3u
#define MP_EFFECTS_CODE_BLADE_PLAIN 0x21u
#define MP_EFFECTS_CODE_BLADE_JEDI  0x25u
#define MP_EFFECTS_VOICE_NONE       (-1)

/* The engine reseeds a melee cooldown of 0.3 s, at pr+0x2BC, in the arm this module stands in
 * for, which is what stops one connecting swing from firing an effect on every substep it
 * overlaps. The substeps run at 32 a second, so the same span is 0.3 * 32 = 9.6 of them, rounded
 * up so the module is never faster than the engine. It is counted here rather than written into
 * the record, because a puppet's record is not this module's to write. */
#define MP_EFFECTS_COOLDOWN_SUBSTEPS 10u

/* What one contact is answered with. The two refusals are kept apart because they are two
 * different things to find in a log: a contact that was never the actor's message at all, and one
 * the module's own cooldown swallowed. */
typedef enum mp_effects_verdict {
    MP_EFFECTS_NOTHING,
    MP_EFFECTS_LOCKED,
    MP_EFFECTS_DUE
} mp_effects_verdict_t;

/* The decision, pure. `message_b` is the contact message's b flag, `armed` says the receiving
 * body still carries a live contact sphere, and `substeps_since_effect` is how many substeps have
 * passed since this module last played anything. `voice_group` is written only on DUE, and is
 * MP_EFFECTS_VOICE_NONE for a code that names no group, which is played without a voice. */
mp_effects_verdict_t mp_effects_decide(uint32_t contact_code, uint32_t message_b, bool armed,
                                       uint32_t substeps_since_effect, int32_t *voice_group);

/* WHETHER THE BLADE is swept against the level for a far body this substep, and behind what.
 *
 * A body in its hero's own model is swept as the engine sweeps its own player's: the contact node
 * the swing starter parked in the record is one the engine answers for itself. A body wearing a
 * borrowed model is swept only while it carries its player's own weapon, because the node that
 * swing named is missing, hidden or without a mesh on such a rig otherwise; and even then the
 * sphere is asked for first, since the answer comes from the DLL that draws that weapon and can
 * be no for a substep, and the query leaves the centre where it found it when it is. */
typedef enum mp_effects_sweep {
    MP_EFFECTS_SWEEP_NONE,    /* a worn body with no weapon at the borrowed rig's hand */
    MP_EFFECTS_SWEEP_PROBE,   /* swept only behind a sphere this module asked for */
    MP_EFFECTS_SWEEP_RUN      /* swept as the engine's own tick would */
} mp_effects_sweep_t;

mp_effects_sweep_t mp_effects_sweep_verdict(bool worn, bool weapon);

/* ==============================================================================================
 * The engine half: what the decision is carried out with.
 * ============================================================================================ */

/* Resolve the impact voice, the flash, the spark and the node sphere. A site that did not resolve
 * leaves the module inert and says so once. */
void mp_effects_resolve(void);

/* Forget far bank `bank`'s verdict that has not been played yet. Every entry below takes the bank
 * whose body it is about, because each far body has its own blade, cooldown and sweep. Run on
 * every arrival of a peer in front of that bank, because the body the contact was meant for is
 * gone. The counters are the report's and survive. */
void mp_effects_reset(size_t bank);

/* One contact the dispatcher answered for the puppet, offered here before it is dropped.
 * `receiver_object` is the puppet's body object, the contact message's own receiver, and its
 * contact code is what says the blade is armed. Reads the message globals through their cells;
 * plays nothing. */
void mp_effects_note_contact(size_t bank, uint32_t receiver_object);

/* Once per substep inside the puppet's bank window, after everything else has been placed: play a
 * verdict that is due at the puppet's own sabre node, and advance the cooldown. */
void mp_effects_run_in_window(size_t bank, uint32_t record, uint32_t object);

/* The blade against the LEVEL, for a substep in which the puppet's blade is armed. This is not a
 * reproduction: it is the engine's own swept test, called with the puppet's record in place, so
 * the wall gets the same scorch, spark, flash and voice a local swing leaves. The engine calls it
 * from the swing mode's tick, which a puppet does not run, which is why a far player's blade
 * marked bodies and left walls clean.
 *
 * One guard the engine has no need of: the test sweeps from where the node stood LAST substep,
 * and a puppet is placed from the wire rather than moved, so a resync or a respawn can put it
 * across the level between two substeps. A sweep across that line would strike a wall nobody
 * swung at. The previous point is therefore seeded rather than swept on the first armed substep
 * and after any jump no player could have walked. */
void mp_effects_sweep_blade(size_t bank, uint32_t record, uint32_t object);

/* ==============================================================================================
 * The second reason: a far body was HURT.
 *
 * The engine plays two things for a player who takes damage, and a puppet gets neither, because
 * its contact is answered rather than performed. The pain voice belongs to every damage contact;
 * the blood belongs only to the melee code and only at the node that was struck.
 *
 * Why it is not the same pending slot as the blade above: a blade contact and a damage contact can
 * arrive in the same substep, they stand at DIFFERENT nodes (the sabre node against the struck
 * node), and one field would let one of them silently take the other's place.
 *
 * Why the cooldown is not the same number: the engine rations the pain voice through
 * `pr->dropTimer = 0.25f` at the head of its damage receiver, not through the 0.3 s melee cooldown
 * the blade effects count. A puppet has no dropTimer this file may write, so the span is counted
 * here in substeps, and it is eight, not ten.
 * ============================================================================================ */

/* 0.25 s of substeps at 1/32, the engine's own drop timer, rounded down to whole substeps. */
#define MP_EFFECTS_HURT_COOLDOWN_SUBSTEPS 8u

/* The band of contact codes that reach the engine's damage receiver at all, and the one inside it
 * that also draws blood. Named apart from MP_EFFECTS_CODE_BLADE_PLAIN even though the number is
 * the same: that one names the ATTACKER's message, this one the VICTIM's, and a single constant
 * for both would be one name for two states. */
#define MP_EFFECTS_HURT_CODE_FIRST 0x20u
#define MP_EFFECTS_HURT_CODE_LAST  0x27u
#define MP_EFFECTS_HURT_CODE_BLOOD 0x21u

/* The builtin particle template the engine spawns at a struck node. Case 7 of the engine's own
 * table is "Splurt". */
#define MP_EFFECTS_BLOOD_TEMPLATE 7

/* A damage contact on a far body, judged where it arrives. The contact code and the struck node
 * are read from their own cells here rather than handed in: the caller is the dispatcher, which
 * knows nothing about cells, and both stand valid only until the next contact is published. */
void mp_effects_note_hurt(size_t bank);

/* Plays what the notice above earned, inside the far body's own window and inside the sound
 * anchor it holds open, so the voice is placed at the body rather than in the listener's
 * head. Runs the cooldown even when nothing is pending. */
void mp_effects_run_hurt(size_t bank, uint32_t object);

/* Offered, in the code band, voice played, swallowed by the cooldown, blood drawn, and the three
 * ways blood can fail: no node in the cell, the node has no sphere, the site did not resolve. */
void mp_effects_hurt_counters(uint32_t *offered, uint32_t *in_band, uint32_t *voices,
                              uint32_t *swallowed, uint32_t *blood, uint32_t *no_node,
                              uint32_t *no_sphere, uint32_t *refused);

/* The report's numbers. `contacts` counts the armed contacts offered by the dispatcher, and
 * `played` and `suppressed` divide it up: the effect sets actually run in a window and the ones
 * the module's own cooldown swallowed. A run where `contacts` stays at zero means the dispatcher
 * is offering nothing, which is a different fault from a run where it climbs and `played` does
 * not. `without_voice` counts the played sets whose contact code named no voice group, which is
 * the ordinary case for one player body hitting another and is not a fault.
 *
 * `blade_contacts` splits the first number again: those that arrived with the message's a flag set
 * are the pair pass's blade against blade message rather than blade against body.
 *
 * `refused` stands beside all of them rather than inside them: an address that did not resolve, a
 * read that failed, a window with no body, a sabre node with no sphere. */
uint32_t mp_effects_contacts(void);
uint32_t mp_effects_blade_contacts(void);
uint32_t mp_effects_played(void);
uint32_t mp_effects_suppressed(void);
uint32_t mp_effects_without_voice(void);

/* The two reasons a suppressed contact was none of this module's business. A run where the blade
 * connected but nothing sparked shows them apart: only body messages means the blades never met,
 * and armed messages on an unarmed blade would mean the arming is not where this module reads it.
 * */
uint32_t mp_effects_body_messages(void);
uint32_t mp_effects_not_armed(void);

/* The swept blade against the level: substeps it ran, how many of those were on a body wearing a
 * borrowed model, the ones withheld because the swing's contact node answered no sphere on such a
 * body, and the ones it seeded instead because the puppet had just been armed or had just been
 * placed somewhere else. A run where the worn count stays at 0 while far players swing in models
 * is a run where no weapon hung; one where the withheld count climbs is a run where the weapon
 * hung and its sphere was not answered for. */
uint32_t mp_effects_sweeps(void);
uint32_t mp_effects_sweeps_worn(void);
uint32_t mp_effects_sweeps_unprobed(void);
uint32_t mp_effects_sweeps_seeded(void);
uint32_t mp_effects_refused(void);

#endif /* MULTIPLAYER_MP_EFFECTS_H */
