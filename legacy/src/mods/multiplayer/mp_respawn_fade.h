/* mp_respawn_fade.h: the fade the engine's re-entry waits on, started again when a closing menu
 * has taken it away.
 *
 * Layer 2. It reads the player module's state and the tint's two cells and calls the engine's own
 * fade, through bindings the scene's module already holds.
 *
 * The engine's re-entry is two states of the player's task. State 4 starts a fade of one second to
 * black that holds, and becomes state 3. State 3 waits for the tint to say it has run its time,
 * and only then takes the body down and spawns it again. There is one tint in the whole engine,
 * and its draw reports the end only of a tint that is active. The menu system uses the same tint
 * as the backdrop of its screens and stops it when the last screen closes.
 *
 * In single player the two never meet: a menu halts the simulation, so the player's task does not
 * run under one. A session's pause menu stands over a running world. A player who dies under it is
 * asked back while it is open, the death closes it, and the close takes the fade away from under
 * state 3, which then waits for an end that is never reported. The body does not come back, the
 * engine opens no pause menu for a corpse, and the level had to be ended to let the player out.
 * The same holds for a living player the re-entry moves for a scene or a teleport, and for a
 * script's warp on the host: whoever asked, it is state 3 that waits.
 *
 * So once a frame one question is asked: does the player module wait in state 3 while the tint is
 * neither active nor run out. The engine's own sequence never produces that. State 4 starts the
 * tint in the tick it becomes 3; a tint that holds stays active past its end; one that does not
 * hold sets the end before it stops; and a menu that opens during the wait starts a backdrop that
 * runs out at once, which lets the re-entry go on. When it is so, the fade is started again with
 * the engine's own arguments, and state 3 ends a second later as it would have.
 */
#ifndef MULTIPLAYER_MP_RESPAWN_FADE_H
#define MULTIPLAYER_MP_RESPAWN_FADE_H

/* Once a frame of a session, after the re-entry's own tick. Does nothing where the tint's cells
 * did not resolve, and says so once. */
void mp_respawn_fade_tick(void);

void mp_respawn_fade_report(void);

#endif /* MULTIPLAYER_MP_RESPAWN_FADE_H */
