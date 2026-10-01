/* mp_arena.h: a team deathmatch map is the map with nobody living on it.
 *
 * Two mechanisms, because the engine has two ways of putting an actor into a level and only one of
 * them can be talked out of it by data. The activation scan asks each placement for its spawn
 * state and skips anything that is not zero, so a pass that writes 2 over every enemy placement
 * empties the level before it is played and needs no hook at all. The script spawner does not ask,
 * so it needs a gate on the spawner itself.
 *
 * Nothing here cleans up. A placement that has been buried never becomes an actor, which is a
 * different and much cheaper thing than an actor that is removed once it exists.
 */
#ifndef MULTIPLAYER_MP_ARENA_H
#define MULTIPLAYER_MP_ARENA_H

#include <stdbool.h>
#include <stdint.h>

/* Does this placement get buried? The whole rule, over plain values, so that it can be driven
 * without a game and so that the pass and the gate cannot come to different answers.
 *
 * The question is not what the placement is, it is whether the level needs it. That is a change
 * of mind and it is worth saying why. The rule used to bury two classes, the riflemen and the
 * tanks, on the reading that the class says what a placement builds. It does not: at the engine's
 * own spawner the class at placement+0x34 is copied to actor+0x1C and from there into the body's
 * object class at +0x04 and shooter class at +0x08, and nowhere else. Every later reader is about
 * perception: the pair collider treats object class 0 as inert and never collides equal classes,
 * the solid band that is pushed, blocks a door or is crushed is classes 1 to 9, the pickup handler
 * tests its own bands, and a point and a bark are awarded only when the victim is 2 and the
 * killer 1. The editor's own table (at 0x0058D690 in the editor image) calls the bands None,
 * Player, Enemy, Civilian, Tripod, Turret, AI Tank, NPC Ally and then the pickups. So burying by
 * class left every civilian, every ally, every scripted Player-class actor and all the traffic
 * standing: 590 visible placements across the eleven levels, 49 of them in Theed alone, in a game
 * that is supposed to be an empty arena. Theed as shipped now: 199 placements, 162 buried, 37
 * standing.
 *
 * What a level genuinely needs from a placement is that its script may drive a mover: a lift, a
 * grate, a platform door. That is one field, `moverSlot[4]`, and it is the only thing standing
 * between an emptied arena and a level whose lifts do not come.
 *
 * So three arms, in this order:
 *
 *   a CUTSCENE HANDOVER goes under first, whatever else it is, because such a placement parks the
 *   player module at state 0 when it wakes and every respawn and every placed pose is gated on
 *   that state reading 1. Three of the twenty five also carry an enemy class, so a filter reading
 *   the class alone would have had half of them either way;
 *   a PICKUP stays, because an arena wants its weapons and its health;
 *   a MOVER DRIVER stays, because the level is built out of what it drives;
 *   everything else goes under.
 *
 * `drives_a_mover` is any of the four slots being set. */
bool mp_arena_buries(uint32_t placement_flags, int32_t class_id, bool drives_a_mover);

/* One pass over the level's placement table, writing the buried state over every placement the
 * rule above answers for. Returns how many were written on this pass.
 *
 * Only meaningful while a world stands, so it belongs in the level-opened path and not in an
 * install. Calling it twice is harmless: a placement already buried is counted and left alone.
 *
 * It does nothing while the arena is off, and it has to be said here because for a long time it
 * did not: the pass ran at the beginning of every level whatever the game was, and only the
 * spawner gate ever asked. A co-operative campaign was quietly played without its riflemen, and
 * when the rule widened it was played without anybody at all. */
uint32_t mp_arena_clear_enemies(void);

/* The gate on the engine's spawner, which is what catches the script spawner. Idempotent. */
bool mp_arena_install(void);

/* Whether the gate on the spawner is in place. */
bool mp_arena_installed(void);

/* Who hears of an actor the spawner made under a copy's key, with the actor, at once: the copies'
 * module on a client, which parks it before the engine can tick it. NULL hears nobody. */
typedef void (*mp_arena_spawn_listener_t)(uint32_t index, uintptr_t actor);
void mp_arena_set_spawn_listener(mp_arena_spawn_listener_t listener);

/* Who hears every spawn that went through, with its placement record, its index and the address
 * the call returns to, whoever asked: a script, the engine's own scan or restore, or a DLL.
 * Called from inside the engine, so it counts and at most writes a line. Answers whether the gate
 * on the spawner stands, which is whether it will ever be called. NULL hears nobody. */
typedef void (*mp_arena_placement_listener_t)(uintptr_t placement, int32_t index,
                                              uintptr_t caller);
bool mp_arena_set_placement_listener(mp_arena_placement_listener_t listener);

/* On in team deathmatch, off in cooperative play. With it off the gate lets everything through,
 * so cooperative play sees the level the campaign authored. */
void mp_arena_set_active(bool active);

/* What the pass buried and what the gate refused, for the run report. */
void mp_arena_report(void);

#endif /* MULTIPLAYER_MP_ARENA_H */
