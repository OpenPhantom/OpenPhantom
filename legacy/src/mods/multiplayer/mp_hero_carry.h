/* mp_hero_carry.h: the player's things go with him when the hero under him changes.
 *
 * Layer 3, the engine half of mp_hero_carry_rule, which says what travels and why. It runs in the
 * player spawn's hull: before the original it reads the outgoing hero's record, the live six
 * inventory and key bytes and the incoming hero's record, has the rule write the incoming record,
 * and writes that record back in one write. After the original it reads the six bytes again. The
 * cells are the ones mp_bank_spawn has proven for the far spawn's window, taken from there.
 *
 * Every change of the player's own hero passes the spawn, except the savegame's restore, which
 * calls status_setActivePlayer itself: the level's begin, the lobby's choice, the developer menu's
 * "play as", the console's hero codes, the script's change of player on the host, and the re-entry
 * after a death, which keeps the hero and so carries nothing. The restore is left alone on
 * purpose: the savegame is the truth, and the lobby's change to this player's own hero after it
 * carries.
 *
 * It does nothing unless this machine plays in a running session (mp_session_now), so the single
 * player keeps the engine's records per hero. It does nothing on a lent call or while a bank
 * window is open, which is a far body's spawn and the window's business. And it does nothing when
 * the current player index and the status pointer do not name the same record, because the
 * engine's swap would then store the live bytes into another record than the one the carry read.
 */
#ifndef MULTIPLAYER_MP_HERO_CARRY_H
#define MULTIPLAYER_MP_HERO_CARRY_H

#include <stdbool.h>
#include <stdint.h>

/* The hull's, before the original. `lent` is the block loan's answer for this call. */
void mp_hero_carry_before(int32_t hero_index, bool lent);

/* The hull's, after an unlent original. Checks the carry made just before, if one was made:
 * the six bytes must read as they did, and the current player must be the incoming hero. */
void mp_hero_carry_after(void);

void mp_hero_carry_report(void);

#endif /* MULTIPLAYER_MP_HERO_CARRY_H */
