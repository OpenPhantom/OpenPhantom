/* mp_scene_room.h: where the host stands for a scene a far player set off: on the place of the
 * player whose test set it off.
 *
 * Layer 1, pure. The binding that reads the engine is mp_scene_seats.c for the place and
 * mp_scene_scope.c for where the scene's actor stands; what is here is every decision they make.
 *
 *   The PLACE. A scene a far player set off is played where his body stood when its script tested
 *   him, so the host is put there, on the floor under it. A player in the air or over water is read
 *   again for a second, because he lands or climbs out. Where the place cannot be stood on, a
 *   scene with the hero as an actor takes a seat beside the actor whose script set it off, from
 *   where the hero walks his way as he would from the place; a lock has no such second place,
 *   because the actor of a lock may be one that directs from anywhere. A player on a mover is
 *   left alone: the host is not put on a lift somebody else rides. Otherwise the host has no
 *   place, and a lock he has no place for is no scene of his at all (mp_scene_flow drops it).
 *
 *   Whether the host stands THERE ALREADY. A host who stands at the place is not moved at all. For
 *   a scene with the hero as an actor that is the box the host's own seat counts as reached, and
 *   no more than half a unit of height, which is the step the hero's walk climbs; for a lock, two
 *   units on the plane and one in height, the box the engine's own facing test looks in.
 *
 * Nobody else has a place: a far player goes on playing where he is.
 *
 * References: Source's scripted sequences put the actor on its mark rather than walk it there, and
 * start only once it stands there.
 */
#ifndef MULTIPLAYER_MP_SCENE_ROOM_H
#define MULTIPLAYER_MP_SCENE_ROOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long the place of a far player who set a scene off is read again while he is in the air or
 * over water, in substeps: a second. */
#define MP_SCENE_PLACE_WAIT_SUBSTEPS 32u

/* What the search of the place answered, as the rule reads it. */
typedef enum mp_scene_place_read {
    MP_SCENE_PLACE_READ_FOUND = 0,  /* a point a body can stand on */
    MP_SCENE_PLACE_READ_NONE_FREE,  /* the floor is there, and nothing on or around it is free */
    MP_SCENE_PLACE_READ_MOVING,     /* in the air, swimming or over a drop */
    MP_SCENE_PLACE_READ_MOVER,      /* on a mover */
    MP_SCENE_PLACE_READ_UNREAD,     /* the body could not be read */
    MP_SCENE_PLACE_READ_NO_PROBES,  /* no level, or the probes did not resolve */
    MP_SCENE_PLACE_READS
} mp_scene_place_read_t;

/* What that answer leads to. */
typedef enum mp_scene_place_step {
    MP_SCENE_PLACE_TAKE = 0,        /* the point found is the host's place */
    MP_SCENE_PLACE_READ_AGAIN,      /* read the far player's place again on the next substep */
    MP_SCENE_PLACE_BESIDE_ACTOR,    /* search a seat beside the actor whose script set it off */
    MP_SCENE_PLACE_NOBODY           /* the host has no place and stays where he stands */
} mp_scene_place_step_t;

/* The rule for the place of the far player who set a scene off. `hero` is a scene with the hero as
 * an actor, the one kind whose actor is a place the hero walks from; the actor of a lock may be one
 * that directs from anywhere. `waited` is how many substeps the place has been read again. */
mp_scene_place_step_t mp_scene_place_rule(mp_scene_place_read_t read, bool hero, uint32_t waited);

/* The answer as the host's line says it in brackets. */
const char *mp_scene_place_text(mp_scene_place_read_t read);

/* Within how much height a body counts as standing at a place at all. */
#define MP_SCENE_ROOM_HEIGHT 2.0f

/* Whether a body stands at a place: within a unit on the plane and two in height, the box the
 * engine's own arrival test of a walk uses for a box of a unit. */
bool mp_scene_at_place(const float body[3], const float place[3]);

/* The boxes a host is found standing at the place already in. A hero's walk climbs half a unit
 * and no more, so a host further below or above the place of a hero scene would never reach its
 * first node. The lock's box is the one the engine's facing test of a conversation looks in: two
 * units on the plane, one in height. */
#define MP_SCENE_NEAR_HERO_HEIGHT 0.5f
#define MP_SCENE_NEAR_LOCK_PLANE  2.0f
#define MP_SCENE_NEAR_LOCK_HEIGHT 1.0f

/* Whether the host stands where a scene is to be played already, so that he is not moved. `place`
 * is the point found for him; `trigger` is where the body of the player the script meant stands.
 * A hero scene measures the place, a lock the body its script tested. */
bool mp_scene_host_is_there(bool hero, const float host[3], const float place[3],
                            const float trigger[3]);

/* The heading a body at `from` takes to face `toward`, in degrees, by the engine's own formula for
 * the heading of a walk. */
float mp_scene_facing(const float from[3], const float toward[3]);

#endif /* MULTIPLAYER_MP_SCENE_ROOM_H */
