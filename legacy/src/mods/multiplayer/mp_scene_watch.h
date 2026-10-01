/* mp_scene_watch.h: which player a scene's script meant, on the host, and the doors a scene's
 * gathering begins at.
 *
 * A scene on the host begins inside the script of one actor: the lock opcode, the camera dolly, a
 * warp, or a spawn that puts the hero on a placement carrying the handover flag. Which player that
 * script meant, nothing could say until the watch was built. The engine asks for "the player", the
 * target resolver answers with whichever player it chose, and the cache the engine keeps of that
 * answer is written again by the actor's next question of any kind.
 *
 * Two instruments. A hull on the head of ai_run knows whose script is running, and the target
 * resolver keeps each actor's last answer about a player. The doors are heard through listeners of
 * the modules that already hull them: the lock and the camera dolly in mp_cutscene, the respawn in
 * mp_lifecycle, the spawner in mp_arena. At each door the rule in mp_scene_rule names the player:
 * the actor's own fresh answer, the last attacker of an actor that died, or the host as the anchor.
 *
 * What the watch does with that answer is the gathering's (mp_scene_host): a lock, a hero and a
 * warp begin one, and the hull on ai_run holds the scene's actor while it gathers. A camera alone
 * that a far player's scene asked for is refused, so the host's view is not swung to where that
 * player stands.
 *
 * Installed on the session's way in and never when the DLL loads, so single player runs none of
 * it. The hull and every listener ask one predicate first, the target resolver's own: hosting,
 * with a transport up. A client installs the hull and never counts anything.
 */
#ifndef MULTIPLAYER_MP_SCENE_WATCH_H
#define MULTIPLAYER_MP_SCENE_WATCH_H

#include <stdbool.h>

/* Hulls ai_run, finds the warp's call and hangs the listeners on the doors. Idempotent. False with
 * a line when ai_run cannot be hulled, and then no listener is set either. A door that cannot be
 * heard is named in the line, and the others are heard without it. */
bool mp_scene_watch_install(void);

void mp_scene_watch_report(void);

#endif /* MULTIPLAYER_MP_SCENE_WATCH_H */
