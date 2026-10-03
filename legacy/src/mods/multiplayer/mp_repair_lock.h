/* mp_repair_lock.h: "Repair lock", the first of the developer menu's two buttons, carried out.
 *
 * Layer 3. A press gives this machine's own player his controls and his camera back, wherever
 * he is, on a host and on a client, and does nothing where nothing holds him. In this order,
 * between two substeps:
 *
 *   The look at what the engine holds, taken by the one place that gives a scene's hold back
 *   (mp_scene_free), with the button set.
 *
 *   What the mod itself holds, first, so that nothing of it holds again on the next substep:
 *   on a host what the mod holds of a scene; the chat's open line; and an input hold whose
 *   owner is gone, the pause menu's with no menu open or the chat's with no line open.
 *
 *   What the engine holds, planned from that look for everything the role may ask and counted
 *   from a lock of level one: the lock with its input mode, the bars, the camera's override,
 *   and the player module. A client's driving actor is removed; a host's is told to leave at
 *   the end of its tick, which ends its script; a stopped module nobody drives is set running,
 *   unless the developer menu holds it itself.
 *
 *   On a host that took something back from the engine, the host's scene is asked to latch its
 *   doors: the actor's script runs once more before it goes, and a door it opened then would
 *   undo the press. What the latch caught is said two seconds later.
 *
 * What a press on a host costs is in that last step and in the one before it. The actor's
 * script ends where it stood, and whatever it would have set afterwards is not set; an actor
 * the latch catches has its doors refused until the level ends.
 *
 * The decisions are mp_player_help_rule's; who hears the press and files the answer is
 * mp_player_help.c.
 */
#ifndef MULTIPLAYER_MP_REPAIR_LOCK_H
#define MULTIPLAYER_MP_REPAIR_LOCK_H

#include "mp_player_help_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* A press, from the frame pump. `overlay_holds` is the ask's flag: the developer menu still
 * holds the player, so a stopped module is left to it. `left_a_teleport` says that the caller
 * ended a teleport under way for this press, which is something that held. What comes back is
 * the answer, and `released` the word that goes with it. `substeps` is the pump's count. */
mp_player_help_verdict_t mp_repair_lock_press(bool overlay_holds, bool left_a_teleport,
                                              uint32_t substeps, uint16_t *released);

/* One drawn frame: the two looks a host's repair owes, once the world's clock has moved far
 * enough past the press. */
void mp_repair_lock_frame(uint32_t substeps);

/* The session ends: a look still owed is dropped with it. */
void mp_repair_lock_session_ended(void);

void mp_repair_lock_report(void);

#endif /* MULTIPLAYER_MP_REPAIR_LOCK_H */
