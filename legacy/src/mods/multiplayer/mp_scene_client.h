/* mp_scene_client.h: a client is in no scene.
 *
 * Layer 3. A scene is the host's alone, and a client goes on playing through it. Nothing of a
 * scene takes a client:
 *
 *   not a script of its own machine, which the scene gates refuse at their doors (mp_cutscene);
 *
 *   not the host, whose scene is not mirrored here: no lock is raised for it, no bars are drawn
 *   and nobody is moved;
 *
 *   and not a savegame saved in the middle of a scene, which restores the lock with its input
 *   mode, the bars, the camera and the actor that drives the player's body past every door. On
 *   the host the scene's own script runs on and lets go of all of it at its end; on a client
 *   that script never runs, because the host plays the world there.
 *
 * So what stands on a client is given back on the first substep it runs and on every one after
 * it, with no wait for anything. The look at the engine, the rule and the calls are
 * mp_scene_free's; what is here is a client's asking, once a substep, and its count. It reads
 * this machine's own cells and nothing off the wire, so it runs whether or not the handshake
 * with the host stands.
 *
 * The one thing that waits is a lock under an open menu of the engine: the menu's close puts
 * back the input mode its open found, so the lock goes on the first substep after the close.
 *
 * Nothing is told to a client about the host's scene: no note of it is sent.
 */
#ifndef MULTIPLAYER_MP_SCENE_CLIENT_H
#define MULTIPLAYER_MP_SCENE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Binds the release a client asks through (mp_scene_free_install). Idempotent; on both roles,
 * because the role is decided by the session and not at the install. False where the lock's
 * release did not resolve, and then a lock a savegame left on a client stays. */
bool mp_scene_client_install(void);

/* One substep on a client of a started session, from the bridge's post-tick half, ahead of the
 * handshake's question: whatever a scene left holding this player is given back. Nothing on a
 * host and nothing outside a started session. */
void mp_scene_client_tick(uint32_t substep);

/* The one exit, at every end of a world: a wait for a menu that stood in that world is no wait
 * in the next one. Nothing is given back here, because nothing is held. */
void mp_scene_client_leave(void);

/* A client's line where this machine was one, and the line of the release itself on any role. */
void mp_scene_client_report(void);

#endif /* MULTIPLAYER_MP_SCENE_CLIENT_H */
