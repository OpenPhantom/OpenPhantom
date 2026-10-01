/* mp_footstep.h: a far player's footfalls, made where they are seen.
 *
 * Layer 2, the engine's own footstep tick run over a body somebody else placed. The window
 * above decides when; this decides nothing but what the engine needs in order to answer.
 *
 * A footfall is not one effect but five: the sound, a sand kick, water rings, a landing spray and
 * a print. Only the first of them falls on every surface, and it is the one that makes a player
 * next to you audible at all. They are not separable without carrying the state machine that
 * chooses between them, so the engine's own tick runs whole and the cost of the other four is
 * counted rather than guessed.
 *
 * The foot does not travel. The receiver runs the same plant window the sender does, over the
 * same clip at the same frame, so it reaches the same foot on its own. What travels is the one
 * thing the engine never stores: which locomotion state the player is in. Zero means the engine
 * asked for no footfall in that substep, and zero is safe because the engine's own switch has
 * no arm for it.
 */
#ifndef MULTIPLAYER_MP_FOOTSTEP_H
#define MULTIPLAYER_MP_FOOTSTEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's locomotion states, as the four call sites pass them: 1 walking, 2 running, 3
 * landing, 4 walking backwards, 6 swimming forward, 7 slipping, 8 and 9 a stroke, 10 treading,
 * 11 pushing a block. 0 and 5 have no arm in the switch, which is what makes 0 usable as "none".
 */
#define MP_FOOTSTEP_NONE 0u

/* The queen is the only hero the engine marks female; the table it reads is {0,0,0,1} indexed by
 * hero. The rule is carried rather than the table, because one bit of shipped data behind its own
 * anchor would be a site for something a sentence says. */
#define MP_FOOTSTEP_HERO_QUEEN 3u

bool mp_footstep_install(void);

/* The state the engine last asked a footfall for on THIS machine's own player, and zero once it
 * has been read. Clearing on read is the whole of the timing: the byte means "the engine asked
 * for a footfall since you last looked", so a substep in which it did not ask reports none. */
uint8_t mp_footstep_take_local_state(void);

/* Run the engine's footstep tick over a far player's placed body. `loco` is what travelled,
 * `hero` picks the plant frames and the gender. Does nothing when `loco` is none, when the site
 * did not resolve, or when nothing under the reported position is floor. */
void mp_footstep_run(size_t bank, uint32_t object, uint8_t loco, uint8_t hero);

void mp_footstep_report(void);

#endif /* MULTIPLAYER_MP_FOOTSTEP_H */
