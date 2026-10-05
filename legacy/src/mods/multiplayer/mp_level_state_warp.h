/* mp_level_state_warp.h: a warp of the host's in the level's journal, said by the host and heard
 * by a client.
 *
 * Layer 2. A script's warp sends the player somewhere else in the level, often as another hero,
 * and it is the host's alone: a client's scripts are parked and its own warp is refused. What a
 * client has to learn is that the host was sent away and where to, so that its player can follow
 * him. That is a moment of the host's scripts, like a line of text, and it travels the same way:
 * as an entry of the level's journal, which every note of the level's state carries until sixteen
 * later changes have pushed it out.
 *
 * The entry carries the hero the host became and the target the engine was handed, in the enemy
 * record's quantisation. The target and not the host's pose, because the entry is written as the
 * engine takes the warp, a fade of a second before the host stands there. The respawn the warp
 * opcode calls, at 0x00447C90, only stores the hero, the place and the heading and sets the
 * player module's state to 4, and it does nothing at all unless that state is 1. The player's
 * task starts a fade out of one second in state 4 and spawns the hero at the stored place from
 * state 3, once the tint has run out. A field run on the assault level measured 34 substeps from
 * the warp's door to the landing.
 *
 * This file is the two ends of the entry and nothing of what a client does about it: the host
 * says a warp, a client keeps the newest one it heard and how many it heard, and whoever follows
 * the host asks for that.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_WARP_H
#define MULTIPLAYER_MP_LEVEL_STATE_WARP_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* The hero byte of an entry whose hero does not fit one. */
#define MP_LEVEL_WARP_HERO_UNKNOWN 0xFFu

/* The entry's three fields for a warp to `at` as `hero`. False when a coordinate is not finite or
 * lies outside what the quantisation carries, and then nothing is written. */
bool mp_level_state_warp_encode(int32_t hero, const float at[3], uint8_t *a, uint16_t *b,
                                uint32_t *c);

/* The other way. A hero the entry does not know comes back as -1. */
void mp_level_state_warp_decode(const mp_level_journal_entry_t *entry, int32_t *hero,
                                float at[3]);

/* The host: a script's warp the engine is taking. Returns the number the journal gave it, 0 when
 * the journal took no entry, and then no client hears of it. */
uint16_t mp_level_state_warp_say(int32_t hero, const float at[3]);

/* A client: one entry of the journal, played in the host's order. */
void mp_level_state_warp_play(const mp_level_journal_entry_t *entry);

/* A client: the warps heard in this process so far, and where the newest one of this level sent
 * the host. False while none was heard in this level. The count never goes back, so a reader that
 * remembers it sees every new warp once. */
bool mp_level_state_warp_heard(uint32_t *count, float at[3], int32_t *hero);

/* Forgets the warp of a level or a session that ended. The counts stay for the report. */
void mp_level_state_warp_reset(void);

void mp_level_state_warp_report(bool host);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_WARP_H */
