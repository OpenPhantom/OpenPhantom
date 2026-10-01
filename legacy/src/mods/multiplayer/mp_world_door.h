/* mp_world_door.h: which world a player is in belongs to the host, and these are the doors out of
 * it that a client used to be able to take alone.
 */
#ifndef MP_WORLD_DOOR_H
#define MP_WORLD_DOOR_H

#include "mp_director_rule.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum mp_world_door {
    MP_WORLD_DOOR_LOAD,        /* the load button of the pause screen and of the death screen */
    MP_WORLD_DOOR_LEVEL_END,   /* a script saying this level is over */
    MP_WORLD_DOOR_FAILURE,     /* a script saying it was failed */
    MP_WORLD_DOOR_COUNT
} mp_world_door_t;

/* Only two of the director's twenty commands end a level: their class says so
 * (mp_director_rule.h). */
bool mp_world_door_command_ends_level(int32_t command);

/* The one module that hears a class of the director's commands: `hand` is asked before the engine's
 * arm, and a true answer withholds the arm, which the script reads as the arm having done nothing.
 * One hand per class; a second is refused and said so, and the level doors stay the door's own.
 * `name` says in the report who has the class. */
typedef bool (*mp_world_door_hand_fn_t)(void *actor, int32_t command, int32_t a1, int32_t a2);
bool mp_world_door_hand(mp_director_class_t cls, mp_world_door_hand_fn_t hand, const char *name);

/* The line the picture should show right now, or NULL. The door holds its own clock, because a
 * refusal is a moment rather than a state the band could read again next frame. */
const char *mp_world_door_notice(void);

bool mp_world_door_install(void);
void mp_world_door_report(void);

#endif /* MP_WORLD_DOOR_H */
