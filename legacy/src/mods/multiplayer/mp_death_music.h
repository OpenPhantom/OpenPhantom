/* mp_death_music.h: the music of this player across his own death in a session, bound to the
 * engine.
 *
 * Layer 2. It reads the engine's two music latches and calls the engine's own state setter; what
 * is owed and when is the rule of mp_death_music_rule.h, and this file only carries it out.
 *
 * The world the memory belongs to and the two readings of the player come in as arguments, so the
 * file names no module above its layer and a test can stand in front of the rule alone.
 */
#ifndef MULTIPLAYER_MP_DEATH_MUSIC_H
#define MULTIPLAYER_MP_DEATH_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

/* Once a frame of a session. `lives` and `corpse` are this machine's own player, `world` a count
 * that changes whenever the level, the savegame or the session does, `now_ms` the session's
 * millisecond clock. Does nothing where the setters or their latches did not resolve, and says so
 * once. */
void mp_death_music_tick(bool lives, bool corpse, uint32_t world, uint32_t now_ms);

void mp_death_music_report(void);

#endif /* MULTIPLAYER_MP_DEATH_MUSIC_H */
