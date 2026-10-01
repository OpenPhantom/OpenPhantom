/* mp_npc_shot_impact.h: which bolt's impact is running, so that its sub shot can be tied to it.
 *
 * The impact of an explosive spawns a sub shot beneath itself with shooter class 0: a rocket a
 * ring, a thermal detonator a fireball. The fireball posts an area ring every substep, and with
 * class 0 nothing in it says whose it is. The shot constructor's hull sees the sub shot being made
 * and asks this module whose impact made it, which is the object of the shot the impact runs for.
 *
 * Without a started session the hull passes every call straight to the engine and says nothing.
 */
#ifndef MULTIPLAYER_MP_NPC_SHOT_IMPACT_H
#define MULTIPLAYER_MP_NPC_SHOT_IMPACT_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves shot_impact, proves that its two sub shot spawns call `shot_spawn`, and hulls it.
 * False, with nothing hulled and a line saying why, when any of that fails. Idempotent. */
bool mp_npc_shot_impact_install(uintptr_t shot_spawn);
bool mp_npc_shot_impact_installed(void);

/* True while an impact runs, with the object of the shot it runs for, or 0 when that did not
 * read. Outside every impact false, and `parent` is left alone. */
bool mp_npc_shot_impact_parent(uint32_t *parent);

/* Impacts seen in a session, and where the hull stands, for the relay's report. */
uint32_t  mp_npc_shot_impact_seen(void);
uintptr_t mp_npc_shot_impact_site(void);

#endif /* MULTIPLAYER_MP_NPC_SHOT_IMPACT_H */
