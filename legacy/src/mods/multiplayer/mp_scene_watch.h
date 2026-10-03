/* mp_scene_watch.h: the doors of a script on the host, heard and judged.
 *
 * A scene on the host begins inside the script of one actor: the lock opcode, a warp, or a spawn
 * that puts the hero on a placement carrying the handover flag. And every script takes and gives
 * back through a few calls of the engine: the bars, the camera, the lock, and their releases. A
 * scene is the host's alone, the far players go on playing through it, and their scripts run on
 * the host all the same. So each of those calls is judged by whose the run is.
 *
 * Three instruments. A hull on the head of ai_run tells whose script is running. The doors are
 * heard through listeners of the modules that already hull them: the lock, the bars, the camera
 * and their releases in mp_cutscene, the respawn in mp_lifecycle, the spawner in mp_arena. And at
 * each door mp_scene_claim says whose the run is: the host's, or a far player's by the actor's
 * own fresh answer, its last attacker or the player its placement woke for.
 *
 * What follows from the answer:
 *
 *   A run of the host's takes and gives back as the engine does with nobody else in the world,
 *   and a lock or a hero of it begins a scene of the host's that runs at once.
 *
 *   A far player's run takes nothing of the host's: its bars and its camera are refused and
 *   remembered for the run, and it gives back only what its own actor took. Its lock, or its
 *   hero, is the door of a scene. With no scene of the host's standing that scene is the host's
 *   from the door on: what the run was refused before it is made up, the lock goes through, and
 *   the host's scene (mp_scene_host) holds the actor and brings the host to that player's place.
 *   With a scene of the host's standing nothing of it is the host's: the lock is refused and that
 *   scene plays without him, a hero is left to the engine, and both are said with a warning.
 *
 *   A warp is the engine's on a host, whoever it meant, and is refused on a client.
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
