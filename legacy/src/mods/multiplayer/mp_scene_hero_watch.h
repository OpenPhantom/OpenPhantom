/* mp_scene_hero_watch.h: the hero a scene drives on the host, watched, and put onto a goal its walk
 * cannot reach.
 *
 * Layer 2, the binding of mp_scene_hero_rule: it finds the actor that drives the host's own body,
 * reads its walk after the enemy tick, and writes the actor when the rule says the walk is stuck.
 * It runs only on the host, only in a session and only while the player module is parked by the
 * engine's grab, which is the only time an actor drives the player's body at all.
 *
 * The actor is found by what it is, not by the door that began the scene: its flags carry the
 * handover bit and its body is the player's own object. A scene's second door and a scene given up
 * find it the same way. Once found it is confirmed every substep by those two reads, and the actor
 * list is walked again only when they no longer hold.
 *
 * That finding is the one test of what drives the player's body, and three more askers share it
 * rather than keep a copy: the host taking over a scene that runs with no door
 * (mp_scene_doorless), the release of what a scene holds, which removes that actor on a client
 * (mp_scene_free), and the line that measures a standing scene.
 */
#ifndef MULTIPLAYER_MP_SCENE_HERO_WATCH_H
#define MULTIPLAYER_MP_SCENE_HERO_WATCH_H

#include <stdbool.h>
#include <stdint.h>

/* One substep on the host, after the enemy tick: find or confirm the hosted actor, read its walk,
 * and put it onto its goal when the walk is stuck. `scene_serial` names the scene in the line. */
void mp_scene_hero_watch_tick(uint32_t substep, uint16_t scene_serial);

/* The actor that drives this machine's player's body now, nought for none: the module parked as
 * the grab leaves it, and an actor with the handover bit whose body is the player's own object,
 * which only the grab hands an actor, or a savegame restoring one. On the host the watch's own
 * finding of the substep; on a client, where the watch does not run, looked for when asked. The
 * actor list is walked at most once a substep. */
uintptr_t mp_scene_hero_watch_driver(uint32_t substep);

/* Whether a hero the engine drives here had a walking wish within the last 32 substeps. */
bool mp_scene_hero_watch_walking(void);

/* The actor the engine's grab is being asked for, from inside that question: the one the actor
 * list's tick stands at, read off the list's own cursor. Nought where the list or its cursor does
 * not read or no actor's link names the cursor. One walk of the actor list a call, so a caller
 * asks only when the answer can matter. */
uintptr_t mp_scene_hero_watch_asker(void);

void mp_scene_hero_watch_report(void);

#endif /* MULTIPLAYER_MP_SCENE_HERO_WATCH_H */
