/* mp_cutscene.h: no scene takes the player away in an arena.
 *
 * Why burying the placements is not enough. A cutscene is not a placement, it is an OPCODE: the
 * script op that locks the player runs out of whatever machine holds it, and in Theed nine such
 * machines exist. Emptying the level of everything that does not drive a mover takes most of them
 * with it, the Queen at the level start included, but it deliberately KEEPS the lift scripts,
 * because they are the only way to the upper floors. And a lift script locks the player while it
 * moves.
 *
 * That is the freeze this file exists for. The lock and its release live in DIFFERENT placements:
 * the lift's own machine takes the lock, and a second, separate machine gives it back. In a
 * campaign the second one is reached because the level is full of scripts that release; in an
 * emptied arena most of those are gone. A player who rides a lift and is never given back is a
 * player who has to kill the process.
 *
 * Counted in Theed: nine machines lock the player, the arena pass takes the Queen at the level
 * start with it, and six survive precisely because they drive a lift, each of them the sequence
 * "distance test against the player under 0.9 units, lock with id 5 and no bars, set mover slot
 * 0 to 1, sound". The release is in a different placement, a machine that locks with id -1 and
 * dollies the camera to -1; it carries no presence bit, sits in no reveal list, and is the target
 * of none of the level's eleven scripted spawns. In a campaign some other release clears the
 * level 5 lock in passing; in an emptied arena there are far fewer of those.
 *
 * So the lock is refused at the door rather than released afterwards. Five engine functions, all
 * of them small leaves with their own entry. Four are hulled while the arena is on or while a
 * scene belongs to the host, and the fifth is hulled always:
 *
 *   the input lock itself, which is what parks the player;
 *   the letterbox bars, which are drawn unconditionally before the lock is even asked for;
 *   the hero as a script's actor, which is the one that takes the player away;
 *   the camera, for the three callers that are a script and for nobody else;
 *   the put-back of the hero, which is the fifth and is refused where the grab is gated, on a
 *   client holding the host's scene and on a host gathering the players, and no grab is standing.
 *
 * The camera override as well, and only for the three callers that are a script. It has seven,
 * and three of the others are ordinary play a deathmatch reaches constantly: the death camera
 * when a fall turns fatal, mounting a tripod gun, and the death screen itself. Taking the camera
 * from a player who falls is a worse thing than a view that swings for a moment. The argument
 * does not say who asked, which is why this door was left open; the RETURN ADDRESS does, and the
 * three script sites are resolved by their own patterns. The override itself is found through
 * the calls those sites make to it, because another module may already have put its branch on
 * the override's head.
 *
 * ==============================================================================================
 * The rule this file is held to, and the neighbours it was checked against.
 *
 *   Whoever refuses a raise must refuse the matching lower in the same file, unless it can be
 *   shown that the lower is itself conditional.
 *
 * Refusing the enter of the lock is safe under that rule, and the reason is written out: the
 * engine's release only unwinds while the lock level is above zero, so a release arriving for a
 * lock this file never took does nothing at all. The bars are safe for the same kind of reason,
 * and it is stated where they are hulled: the clearing direction is never refused.
 *
 * For the hero the sentence was never written, and that is exactly where it broke. The grab was
 * refused on a client and the put-back was not, and the put-back is not conditional: it writes
 * the module state back out of a store that on such a machine holds nought. A client came out of
 * a scene of the host's with a player module at nought, which is a player who cannot move, turn,
 * or be spawned again. The put-back is now hulled beside the grab, with one predicate, the
 * engine's own store.
 *
 * The neighbours, checked under the same rule. The camera release writes a constant nought
 * rather than a saved value, so it takes nothing from anybody and stays open. The use latch
 * counts itself down in the engine and does not hang off its setter. The arena gate and the
 * world doors have no lower at all. The parking of actors is paired already and is the model
 * this repair follows. The pause and the film are not hulled yet, and the pause is a pair when
 * it is: its two module messages are a raise and a lower.
 * ==============================================================================================
 *
 * NOT the cell. Clamping the lock's own cell to zero looks cheaper and is wrong: the engine calls
 * its input-mode restore only on the way DOWN through zero, so a cell held at zero means the
 * restore never fires and the input mode stays where the scene put it.
 *
 * SIZE NOTE: both halves of the hero pair live here on purpose. Splitting the grab from the
 * put-back would be the very shape of mistake the rule above exists to stop, so the seam this
 * file takes is the pure arithmetic instead, in mp_scene_rule.
 */
#ifndef MULTIPLAYER_MP_CUTSCENE_H
#define MULTIPLAYER_MP_CUTSCENE_H

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolves every site and hulls it. Idempotent. False when not one of them resolved, in which
 * case nothing is hulled and the arena runs with its scenes intact; a partial resolve hulls what
 * it found and says which is missing, because most of a suppressed scene is still better than a
 * frozen player. */
bool mp_cutscene_install(void);

/* On while the arena is on, and the arena is on only in a deathmatch. With it off the lock, the
 * bars and the camera call the engine's own, so a co-operative campaign plays its scenes exactly
 * as it always did, and that now includes the put-back of the hero: see below. */
void mp_cutscene_set_suppressed(bool suppressed);

/* A scene belongs to the world, and the world belongs to the host. On a client in a running
 * session the four things a scene takes from the player, the lock at level five, the bars, the
 * camera and the hero as an actor, are refused when a script of THIS machine asks for them.
 * Scripts of this machine are actors the host does not steer: a replica let go, or one this
 * machine's own activation scan woke where that scan still runs; on a client of a started session
 * mp_world_anchor holds it.
 *
 * The put-back of the hero asks the same predicate as the grab, mp_scene_hero_is_gated_here, and
 * where that holds it is refused while no grab is standing. A host grabs and puts back as the
 * retail game does; asking the engine's store there as well is what stood a host in a caption
 * fade on 2026-09-20. */
void mp_cutscene_set_client_holds_back(bool holds);

/* The lock level a script's scene takes, which the scene rule names for every reader. */
#define MP_CUTSCENE_LOCK_LEVEL MP_SCENE_LOCK_LEVEL

/* Two doors a scene comes through, for whoever measures which player a scene's script meant. */
typedef enum mp_cutscene_door {
    MP_CUTSCENE_DOOR_LOCK = 0,   /* a raise of the lock, told BEFORE it is made */
    MP_CUTSCENE_DOOR_CAMERA      /* a camera take that is let through */
} mp_cutscene_door_t;

/* Where the doors stand: the lock's entry, and the addresses the lock opcode's and the camera
 * dolly opcode's camera takes return to. Nought for whatever did not resolve or was not hulled. */
typedef struct mp_cutscene_doors {
    uintptr_t lock_entry;
    uintptr_t lock_take_return;
    uintptr_t dolly_take_return;
} mp_cutscene_doors_t;

/* Told on the host's path only, from inside the engine, with the address the call returns to and
 * the lock level or camera group it was given. It counts and at most writes a line; for a camera
 * take it also answers whether the take may go on. False keeps the host's view: a camera alone
 * that a far player's scene asked for would swing it across the map. The release of a camera writes
 * a constant nought, so a refused take leaves nothing held. The answer for a lock is not read. */
typedef bool (*mp_cutscene_door_listener_t)(mp_cutscene_door_t door, uintptr_t caller,
                                            int32_t argument);

/* Sets who hears the doors, NULL for nobody, and says where they stand. */
void mp_cutscene_set_door_listener(mp_cutscene_door_listener_t listener,
                                   mp_cutscene_doors_t *doors);

/* Where the doors stand, for a module that does not listen. */
void mp_cutscene_doors(mp_cutscene_doors_t *doors);

/* A host gathering the players for a scene: the grab of the hero waits for them, and the put-back
 * asks the same predicate (mp_scene_hero_is_gated_here). Set by the gathering, taken back by its
 * one exit. */
void mp_cutscene_set_gather_holds(bool holds);

/* The camera of a spoken line, asked from inside the engine at the speak entry's own camera take
 * and at no other: false keeps this machine's view, because the line it belongs to is not
 * presented here. The judgement of the line is its only answer at that take; the door listener
 * refuses only the camera dolly. NULL for nobody. */
typedef bool (*mp_cutscene_line_camera_fn_t)(void);
void mp_cutscene_set_line_camera(mp_cutscene_line_camera_fn_t judge);

/* The engine's own lock and bars, called past the gates above. A client's mirror of the host's
 * scene raises the lock and draws the bars through these, and nothing else does: they are the
 * host's scene on this machine, not a script of this machine's. 0 and nothing when unbound. */
int32_t mp_cutscene_engine_lock(int32_t level);
void    mp_cutscene_engine_bars(bool on);

/* What the gathering and the mirror report from here. */
typedef struct mp_cutscene_counts {
    uint32_t grabs;                        /* the engine's grab took the hero for a scene */
    uint32_t grabs_held;                   /* asked while a gathering held the grab */
    uint32_t putbacks_held;                /* refused while a gathering held the grab */
    uint32_t cameras_refused_far;          /* a far player's camera alone, kept off the host */
    uint32_t locks_refused_for_the_host;   /* a lock a script of this client asked for */
} mp_cutscene_counts_t;

void mp_cutscene_counts(mp_cutscene_counts_t *out);

/* The cell the dialogue and scene lock keeps its level in, read out of the lock's own compare
 * and its own load behind the hull, which have to agree; 0 when the lock is not hulled or they do
 * not. A menu that runs over the world closes when the level rises, as the engine's own cheat
 * console does. The one derivation of this cell: everything that reads the lock's level asks here.
 */
uintptr_t mp_cutscene_lock_level_cell(void);

/* The lock's level as it stands, -1 where the cell is not known or does not read. */
int32_t mp_cutscene_lock_level(void);

void mp_cutscene_report(void);

#endif /* MULTIPLAYER_MP_CUTSCENE_H */
