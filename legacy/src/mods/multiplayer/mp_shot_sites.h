/* mp_shot_sites.h: where the engine's AI fires, so that a shot can be told by its caller.
 *
 * shot_spawn is called from 21 places, and the class a shot is fired with is the side it fights
 * on. Class 1, the player's side, comes out of two kinds of caller: the player's own three weapons,
 * which push the literal, and the AI, which pushes the firing actor's own class. An actor of class
 * 1 is an ally: a Help copy an editor spawned, or one of the 46 placements the shipped levels
 * author on the player's side. After the call the engine cannot tell the two apart, and the
 * multiplayer took every class 1 shot for the player's own, so an ally's bolt travelled as the
 * host's and hurt a far player the ally never fought.
 *
 * The AI calls that pass an actor's class are nine: four in op_shoot, three in fire_emplacement
 * and two in enemy_scriptedDeath. erratic_tick always passes 3 and is not one of them. Each is
 * found by the instructions around its call, and a match counts only when its call really reaches
 * shot_spawn, which is what makes a ten byte pattern a place rather than an idiom. All nine
 * resolve or none is used: a table that knows some of the AI's calls would take an ally for the
 * player at the rest, and the one that knows none is the behaviour of every build before it.
 */
#ifndef MULTIPLAYER_MP_SHOT_SITES_H
#define MULTIPLAYER_MP_SHOT_SITES_H

#include <stdbool.h>
#include <stdint.h>

#define MP_SHOT_SITES_AI 9u

/* Finds the nine calls against `shot_spawn`, the resolved head of the routine. Idempotent. False,
 * with a line in the log, when any of them does not resolve; nothing is patched either way. */
bool mp_shot_sites_resolve(uintptr_t shot_spawn);
bool mp_shot_sites_resolved(void);

/* Whether `return_address` is the instruction after one of the nine calls, which is where a hull
 * on shot_spawn's head sees the engine's own caller return to. False while unresolved. */
bool mp_shot_sites_is_ai(uintptr_t return_address);

/* Whose a shot is, as the shot hull sees it being made. */
typedef enum mp_shot_whose {
    MP_SHOT_NOBODYS = 0,   /* the engine's own effects, and this machine's copy of a bolt */
    MP_SHOT_PLAYERS,       /* the local player's: an event of the player's own */
    MP_SHOT_NPCS           /* an actor's, an ally's included: an NPC's bolt */
} mp_shot_whose_t;

/* The rule, pure. A class above 1 is an actor's. Class 1 is the player's unless the AI fired it,
 * which is an ally, or this machine is firing its copy of somebody else's bolt, which is nobody's
 * to tell again. Class 0 is the engine's own. */
mp_shot_whose_t mp_shot_whose(int32_t shooter_class, bool from_the_ai, bool a_copy);

#endif /* MULTIPLAYER_MP_SHOT_SITES_H */
