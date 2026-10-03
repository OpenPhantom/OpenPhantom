/* mp_scene_host.h: a scene belongs to the host alone.
 *
 * Layer 3. The rule: a scene, a lock or a scene with the hero as an actor, is the host's. A
 * client is in no scene: nothing of one takes it, and it goes on playing. When a far player sets
 * a scene off, the host is brought to that player's place and the scene waits until he stands
 * there; then it plays as it does with nobody else in the world. Nobody else is waited for, moved
 * or held. The decisions are mp_scene_flow's, the engine is mp_scene_bind's, whose a script's
 * doors are is mp_scene_claim's, and this is the half that runs them:
 *
 *   The scene watch hears a scene begin at one of its doors, inside the engine, and says which
 *   player the script meant. Here the scene's state begins at once. A scene the host set off
 *   himself runs from that moment with nothing held. A scene a far player set off begins held:
 *   its lock is the host's from the door on, the actor whose script opened the door is skipped in
 *   the script runner, and the hero's grab answers "not yet" through the one predicate the
 *   put-back asks as well.
 *
 *   In the substep that follows, the place, through the one seat search the re-entry and the
 *   arrival use. The host takes the place of the far player the script meant, the floor under
 *   that player's body, read again for a second while he is in the air or the water, and beside
 *   the scene's actor for a hero scene where it cannot be stood on. A host who stands there
 *   already is not moved.
 *
 *   The host is moved under a short fade, through the placement, tried again rather than given
 *   up, and brought by the engine's respawn after a second in a mode the teleport may not move;
 *   his input is held at the place until the scene runs. The moment he stands there the holds
 *   fall and the scene runs; never with the host dead or away from his place, and a hero scene
 *   waits as well for a host its grab cannot take. At twenty seconds from the beginning a hero
 *   scene is given up and plays here as it would alone, unless the engine's respawn is bringing
 *   the host. With nobody else in the session nothing is held at all.
 *
 *   A lock the host has no place for, a lift a far player rides above all, or cannot be brought
 *   to in those twenty seconds, is no scene of the host's. He is let go at once of what the lock
 *   took at the door, through the one place a player is given back what a scene holds, and the
 *   lock's script plays on for the player it meant, its doors refused here until a run of it is
 *   the host's.
 *
 *   A warp is the engine's: it sends the host to its target, and no far player follows it.
 *
 * While a scene of the host's stands, the far bodies are passable here, so that they are not in
 * the way of its actors, and the host's re-entry is pointed at the scene. The actors of the scene
 * mean the host when they ask for the player; every other actor goes on answering every player.
 *
 * Every way out of a scene's world takes the one exit: the holds fall, a held fade is given back,
 * the host's input is let go and the named anchor of his re-entry taken away. A second scene while
 * one is held or runs is counted and begins nothing; a warp takes over, because the engine moves
 * the host whatever this does.
 *
 * Installed on the session's way in, never when the DLL loads; everything here asks first whether
 * this machine hosts.
 */
#ifndef MULTIPLAYER_MP_SCENE_HOST_H
#define MULTIPLAYER_MP_SCENE_HOST_H

#include "mp_scene_claim_rule.h"
#include "mp_scene_flow.h"
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Binds the scene and the release of what a scene holds together, and puts their one exit where
 * every way out of a level and a session runs. Idempotent. */
bool mp_scene_install(void);

/* A door as the scene watch hears it. */
typedef struct mp_scene_door {
    mp_scene_kind_t kind;
    uintptr_t       actor;            /* the actor whose script opened it */
    uint8_t         bank;             /* the player its script meant, 0 the host */
    int32_t         hero_placement;   /* a hero's door: the placement the hero is spawned on;
                                       * -1 for any other door */
    int32_t         warp_hero;        /* a warp: the hero, and the target and heading the engine
                                       * is about to take */
    const float    *warp_at;
    float           warp_heading;
} mp_scene_door_t;

/* From the scene watch, at a door, inside the engine. True when a scene of its own began with it;
 * false for a door inside a scene that stands, which is counted, and where no scene is begun at
 * all: this machine not hosting, nobody else in the session, or a warp the engine drops. */
bool mp_scene_host_began(const mp_scene_door_t *door);

/* From the scene watch, at a hero's door, inside the engine: whether `actor` is the actor of the
 * scene that stands and spawned the hero in the substep its lock began that scene. The scene is a
 * hero's from there, with its hero on placement `hero_placement`, and no second scene is begun
 * or counted. False for any other actor and any later substep. */
bool mp_scene_host_hero_behind_the_lock(uintptr_t actor, int32_t hero_placement);

/* From the scene watch's hull on the script runner: whether this actor's script is held while the
 * host is brought to the scene's place. Answered for the actor whose script opened the door,
 * while the hold stands. */
bool mp_scene_host_holds(uintptr_t actor);

/* From the scene watch, for every respawn the engine is asked for on the host, sorted by caller. */
void mp_scene_host_note_respawn(mp_scene_respawn_caller_t caller);

/* One substep of the host's scene, from the bridge's post-tick half, with the substep count. */
void mp_scene_host_tick(uint32_t substep);

/* Whether a scene of the host's stands: a lock or a hero scene held or running, or one given up
 * that the engine may still play here. Always false on a client, which is in no scene. `known`,
 * when given, says what the scene knows to tell its lines by: its place, its number, and that it
 * is this player's own. */
bool mp_scene_host_stands(mp_scene_known_t *known);

/* The button's way out of a scene of the host: what the one exit does, without the bookkeeping a
 * level's end makes. True when a scene of the host stood. The marks of what its actors took stay
 * for the caller: mp_scene_host_latch writes their placements down, and a caller that does not
 * latch lets them fall (mp_scene_claim_forget_marks). From between two substeps, never from
 * inside the engine. */
bool mp_scene_host_repair(void);

/* After a repair took something back from the engine on a host. Written down at once, and
 * refused their doors until the level ends: the placement of the hero the repair sends away, on
 * which no hero takes the host again, every placement whose actor had taken the camera, the lock
 * or the bars here, and the actor of the scene just left. Then every mark falls, and for the next
 * MP_SCENE_LATCH_SUBSTEPS substeps every actor whose script opens a door is written down as well.
 *
 * The window is counted on the clock of the host's scene, from the substep its tick saw last, and
 * never from `substep`: a caller outside the scene cannot read that clock, and the count it has
 * to hand stands still on a host with nobody joined. `substep` is said in the line and decides
 * nothing. From between two substeps, never from inside the engine. */
void mp_scene_host_latch(uint32_t substep);

/* The placements the latch holds, for the line; returns how many were written. */
size_t mp_scene_host_latched(uint32_t *keys, size_t max);

/* The one exit, for every way out of a scene's world. The judgement of spoken lines and the seat
 * search are told as well, because their level ends there too: the lines named each level and the
 * seats that ended a life belong to the world they were counted in. */
void mp_scene_reset(void);

/* The host's lines where this side hosted, a client's where it is one, and the one question. */
void mp_scene_report(void);

#endif /* MULTIPLAYER_MP_SCENE_HOST_H */
