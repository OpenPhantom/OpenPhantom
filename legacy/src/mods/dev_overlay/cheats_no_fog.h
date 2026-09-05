/* cheats_no_fog.h: push the loaded level's fog band out past anything visible, every frame, for as
 * long as the cheat is on, and hand it back, exactly as authored, the moment it is switched off.
 *
 * A different shape from the ammunition and health cheats beside it in the panel, because fog is
 * not spent. It is two world-unit floats in the loaded level's own record (world+0x218/0x21C),
 * read fresh by the renderer every frame; the same fields view_distance_fix's fog_regime.c already
 * documents in full, reusing byte evidence already proven there rather than re-deriving it, though
 * this is a separate DLL and resolves its own copy of the site independently. There is no single
 * function call to decline the way ammunition and damage have, so this holds the band out itself,
 * once per frame, through common/frame_hook.h.
 *
 * It is also the one cheat here that is remembered between runs, under [dev_overlay] NoFog.
 * Fog is a matter of taste rather than an advantage, and somebody who does not want it does not
 * want it again tomorrow; the rest of this panel is deliberately session-only because a cheat left
 * on by accident should not follow a player into a fresh game.
 *
 * UNLIKE the ammunition and health cheats, this DOES restore a value on the way back off, the
 * band it found the level holding the first frame it saw that level's record, captured before ever
 * writing to it. A first version declined here too, the same "never invent a value" rule the other
 * two follow, and field testing found that the wrong call for this specific case: ammunition and
 * health decline a SUBTRACTION, so their own "off" is simply the game's other systems carrying on
 * from wherever they already were, but nothing else in the engine ever moves this band, so nothing
 * else was ever going to hand it back. Declining left the player able to turn fog off and never on
 * again short of a level reload. See cheats_no_fog.c for how the remembered band survives the
 * record being freed and reallocated on a level change without ever being read as a value FROM the
 * previous level.
 */
#ifndef CHEATS_NO_FOG_H
#define CHEATS_NO_FOG_H

#include <stdbool.h>

/* Resolves the level pointer and arms the per-frame hook. Answers false when either could not be
 * reached, in which case the cheat is offered as unavailable rather than as a row that ticks and
 * does nothing. Idempotent. */
bool cheats_no_fog_install(void);

bool cheats_no_fog_is_available(void);

/* The cheat's own intent, not a read of the live band: while it is on this forces the band out
 * every frame, so the live value is never a trustworthy answer to "did the player ask for this" on
 * its own. */
bool cheats_no_fog_is_on(void);

/* Flips it and answers the new state. Refused, answering false, when the site never resolved. */
bool cheats_no_fog_toggle(void);

#endif /* CHEATS_NO_FOG_H */
