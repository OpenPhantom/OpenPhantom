/* mp_scene_free.h: the one place a player is given back what a scene holds of him.
 *
 * Layer 2, the binding of mp_scene_free_rule: it takes the look the rule decides on, and it
 * carries a plan out through the engine's own ways back, in an order that leaves nothing to
 * take the player again:
 *
 *   the actor that drives the player's body first, so no script of it runs after the rest;
 *   then the camera's override, the lock, the input mode and the bars, which is the order the
 *   engine's own script ends take them in;
 *   then the engine's store of a parked module, and a stopped module nobody drives.
 *
 * Every one of them goes through a function of the engine or a single word of its own, never
 * through a cell held at a value:
 *
 *   the lock          Dialog_LeaveInputLock, called at its head with the level the end of the
 *                     camera dolly opcode uses, 99, which lets go of a lock at any level and
 *                     sets the input mode to play as the lock falls
 *   the camera        bapview_overrideOff, which writes a constant nought and so takes nothing
 *                     from anybody when no override stood
 *   the bars          the letterbox, told to go
 *   the input mode    the engine's own setter, with play
 *   a client's actor  the engine's own removal of the actor, which puts the player back first
 *   a host's actor    the removal reason a script's own "remove me" writes, so the engine
 *                     removes the actor at the end of its next tick
 *
 * A call at a function's head runs through every hull another module has put on it, as a call
 * of the engine's own would.
 *
 * Who plans what is the caller's: a client asks on every substep for everything a scene left
 * behind there (mp_scene_client.c), the host's scene asks for a host a scene took with no place
 * to stand at, and the developer menu's button asks on either role for whatever holds the
 * player who pressed it (mp_repair_lock.c).
 */
#ifndef MULTIPLAYER_MP_SCENE_FREE_H
#define MULTIPLAYER_MP_SCENE_FREE_H

#include "mp_scene_free_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolves the lock's release and the clearing of the camera's override, and says in one line
 * what resolved and where. Idempotent. True when the lock's release is bound; the camera is
 * optional, and without it the look says so and no plan holds it. Called on the session's way
 * in, after the scene gates and the sites of the host's scene. */
bool mp_scene_free_install(void);

/* What holds this machine's own player now. `overlay_holds` and `button` are left false: they
 * are the caller's to set, from the request it answers. The lock reads -1 where its release is
 * not bound, so that nothing is planned that could not be carried out. */
void mp_scene_free_look(mp_scene_free_look_t *out, uint32_t substep);

/* Carries `plan` out and answers what was given back, as the same bits; the host's actor counts
 * as given back once the engine has been asked to remove it. Writes one line for what it gave
 * back, 32 times in a process, and counts everything. Outside the engine's own walk of the
 * actor list: from the bridge's task in a substep, or between two substeps. */
uint32_t mp_scene_free_now(uint32_t plan, uint32_t substep);

void mp_scene_free_report(void);

#endif /* MULTIPLAYER_MP_SCENE_FREE_H */
