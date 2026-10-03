/* mp_range_gate.h: the site. An enemy's range is measured against the nearest player.
 *
 * Layer 2. The rule is in mp_range_gate_rule.h; this is where it meets the engine.
 *
 * The engine has one range test and two callers. `within_range` squares the distance in three
 * axes and compares it against a squared radius. The activation scan asks it what wakes; the
 * entity loop asks it what is removed again. Both hand it the LOCAL player's body. In a session
 * that is the wrong question, and answering it around the outside is what this file replaces:
 *
 *   - the host used to run a SECOND pass over the whole placement table for every far body,
 *     once a substep, to wake what the engine's own scan had not;
 *   - and it used to REFUSE the removal afterwards when a far body stood near. The engine asks
 *     again on the next substep, so the refusal was paid for every substep the body stood there,
 *     and every ask that was not refused put another despawn on the wire. In one field run that
 *     was 49733 refusals, and 551 of 652 despawns were for a life already removed.
 *
 * Widening the one test at its source makes both of those unnecessary rather than cheaper.
 *
 * Why it can be widened at all: two independent censuses say the function has exactly two
 * callers: the decompiled image lists two call sites, and the four bytes of its address occur
 * nowhere in the file, so it sits in no pointer table, is no message handler and is no task
 * coroutine. The install repeats the caller census on the running image and REFUSES to widen
 * anything if the count is not two, because a caller this build has not seen is a caller whose
 * question may not be "is a player near".
 *
 * The answer is only ever widened, never narrowed. The engine's own call is made first and its
 * true is returned untouched, so single player, a session with no far body and every caller
 * outside the two are the engine's own behaviour to the bit.
 *
 * One thing is remembered beside the answer, and it changes no answer: which far player the
 * activation scan was widened for, by placement, for a short while. The two callers hand the test
 * different places, the placement's own for the scan and the actor's for the removal, so the
 * placement is known without a read. Whoever asks whose a scene is reads it
 * (mp_range_gate_woke_for_far).
 *
 * Host only. A client may not widen its own activation: it would wake enemies for itself AND be
 * sent the host's, and the level belongs to the host. On a client this installs nothing.
 *
 * The positions are read once a substep, not once a call. The activation scan runs once a FRAME
 * and asks for every eligible placement, so at the host's frame rate this hook is called tens of
 * thousands of times a second. Reading three far bodies inside it would be the same mistake one
 * layer down. A far body's position changes once a substep and no faster, so that is when it is
 * copied.
 */
#ifndef MULTIPLAYER_MP_RANGE_GATE_H
#define MULTIPLAYER_MP_RANGE_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A far body's position, bank by bank. Answers false for a bank with no body. */
typedef bool (*mp_range_gate_far_body_fn_t)(size_t bank, float out[3]);

/* Resolves the range test, repeats the caller census and detours it. False when anything about
 * that is not exactly as described above, and then nothing is installed and nothing is widened. */
bool mp_range_gate_install(void);

/* Only a host widens. */
void mp_range_gate_set_host(bool host);

/* Armed while a session can have far bodies at all. Unarmed, the hook is the engine's own call
 * and one comparison. */
void mp_range_gate_set_armed(bool armed);

void mp_range_gate_set_far_body(mp_range_gate_far_body_fn_t far_body);

/* Whether the gate measures far bodies at all right now: installed, on a host, and armed. The
 * world anchor asks this exact question before it names a far body, so the anchor and the gate
 * cannot answer "who is near" from two different readings of the role. */
bool mp_range_gate_widens(void);

/* Once a substep, from inside the substep: copies the far bodies the gate will be asked about
 * until the next one. */
void mp_range_gate_refresh(void);

/* Where the far player of bank `bank` stands for the gate this substep, out of the table the last
 * refresh filled, which is before the puppet of this substep is placed. False with no row for that
 * bank: the gate is not installed or not armed, the bank has no body, or it is no far bank.
 *
 * It is here so that "where does player X stand on the host" has one answer. Anything else that
 * measures an enemy against a far player asks this rather than reading the body again, because
 * two readings a substep apart put the two answers on different sides of a radius. */
bool mp_range_gate_player(size_t bank, float out[3]);

/* Whether the gate measures at all this session: installed and armed. With it off the table is
 * empty and every question above answers false. */
bool mp_range_gate_measuring(void);

/* Whether the placement whose record lies at `record` woke for a far player lately: within the
 * last MP_SCENE_WOKE_SUBSTEPS substeps the activation scan was answered yes for it only because
 * a far player stood in its range, the engine's own player not. `bank` names him. An actor that
 * opens a door of a scene in its first run has asked for no player yet, and this is then all
 * that says whose the scene is. The answer the gate gives the engine is not changed by it.
 *
 * Not written down: a waking the engine answered by itself. With the host dead the world anchor
 * runs the scan on a far player's body, the engine says yes without this gate, and such a
 * placement reads here as woken for nobody. */
bool mp_range_gate_woke_for_far(uintptr_t record, uint8_t *bank);

/* Nothing is remembered as woken: at the end of a world, because the next one's records may lie
 * at the same addresses. */
void mp_range_gate_forget_woken(void);

/* Whether the activation scan's call into the range test no longer lands on the test itself, which
 * is what a module that scales the wake radius at that call does. False when it does, and when the
 * gate is not installed. Read from the live call operand, once per question. */
bool mp_range_gate_wake_redirected(void);

void mp_range_gate_report(void);

#endif /* MULTIPLAYER_MP_RANGE_GATE_H */
