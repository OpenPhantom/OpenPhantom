/* mp_scene_host.h: a scene the host plays gathers every player first.
 *
 * Layer 3. The rule: when any player, a far client as well, sets off a scene, a scene with the hero
 * as an actor or a warp, it starts for everybody, and EVERY player is brought to the place first;
 * the host is the actor. The decisions are mp_scene_flow's, the engine is mp_scene_bind's, and this
 * is the host's half that runs them:
 *
 *   The scene watch hears a scene begin at one of its doors, inside the engine, and says which
 *   player the script meant. Here the scene's state begins at once, and with a lock or a hero scene
 *   so do the two holds: the actor whose script began it is skipped in the script runner, and the
 *   hero's grab answers "not yet" through the one predicate the put-back asks as well.
 *
 *   In the substep that follows, every player is handed a seat through the one seat search the
 *   re-entry and the arrival use, around the player the script meant: the host beside him, unless
 *   the host is the one, and every standing far player but him. A warp's target is the place, and
 *   every far player is seated around it while the engine sends the host there.
 *
 *   The host moves itself as a client does, under a short fade, through the placement; the far
 *   players move themselves on the note. The host counts a far player arrived when its body here
 *   stands within a unit of its seat. When everybody has, or after a second and a half of the host
 *   standing, the holds fall and the scene runs; never with the host dead, who is waited for, and
 *   a hero scene waits as well for a host its grab cannot take. At twenty seconds from the
 *   beginning the scene is given up for everybody and plays here as it would alone. With nobody
 *   else in the session nothing is held at all.
 *
 *   The note, 0xA4, tells every client the scene's state, on every change and once a second, until
 *   two seconds after it is over, and for a scene given up until the engine has played it here.
 *
 * Every way out of a scene's world takes the one exit: the holds fall, a held fade is given back,
 * the named anchor of the host's re-entry is taken away. A second scene while one gathers or runs
 * is counted and not gathered; a warp takes over, because the engine moves the host whatever this
 * does.
 *
 * Installed on the session's way in, never when the DLL loads; everything here asks first whether
 * this machine hosts.
 */
#ifndef MULTIPLAYER_MP_SCENE_HOST_H
#define MULTIPLAYER_MP_SCENE_HOST_H

#include "mp_scene_flow.h"
#include "mp_scene_rule.h"
#include "mp_scene_send.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Binds the gathering and the mirror together, and puts their one exit where every way out of a
 * level and a session runs. Idempotent. */
bool mp_scene_install(void);

/* From the scene watch, at a door, inside the engine. `bank` is the player the script meant, 0 the
 * host; a warp names its hero, target and heading, which the engine is about to take. */
void mp_scene_host_began(mp_scene_kind_t kind, uintptr_t actor, uint8_t bank, int32_t hero,
                         const float *at, float heading);

/* From the scene watch's hull on the script runner: whether this actor's script is held for a
 * gathering. Answered for the actor whose script began the scene, while its hold stands. */
bool mp_scene_host_holds(uintptr_t actor);

/* From the scene watch, for every respawn the engine is asked for on the host, sorted by caller. */
void mp_scene_host_note_respawn(mp_scene_respawn_caller_t caller);

/* One substep of the host's scene, from the bridge's post-tick half, with the substep count. */
void mp_scene_host_tick(uint32_t substep);

/* The note, from the bridge's substep end, into the send it hands every reliable message to; when
 * it goes is mp_scene_send's. */
void mp_scene_host_send(uint32_t substep, mp_scene_send_fn_t send);

/* The one question for both roles: whether a scene runs for everybody now. On the host its own
 * scene, gathering or running a lock or a hero scene; on a client the host's scene as its newest
 * note says, while the host is heard. A spoken line of such a scene is heard by every player it
 * gathered, and `known`, when given, says what the scene knows to tell its lines by, the same on
 * both roles: its place, its number and whether it gathered this machine's player. */
bool mp_scene_for_all(mp_scene_known_t *known);

/* The one exit of both halves, for every way out of a scene's world. The judgement of spoken
 * lines and the seat search are told as well, because their level ends there too: the lines named
 * each level and the seats that ended a life belong to the world they were counted in. */
void mp_scene_reset(void);

/* The host's lines where this side hosted, a client's where it is one, and the one question. */
void mp_scene_report(void);

#endif /* MULTIPLAYER_MP_SCENE_HOST_H */
