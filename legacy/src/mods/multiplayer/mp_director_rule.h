/* mp_director_rule.h: what each of the script director's twenty commands is, and how a client may
 * replay one.
 *
 * Layer 1, pure. The director is one engine function a script calls with a command number and two
 * arguments, and its jump table has twenty arms. They are different kinds of thing, and the
 * multiplayer answers them differently: some end the level and a client must never run them, some
 * set the fog and travel in the level's state, some are the state of one actor and reach a replica
 * through its record, and some are moments. Each command has exactly one class here, and every
 * other question about a command (does it end the level, is it fog) is derived from this one
 * table, so two lists cannot come to disagree about the same number.
 *
 * How a client may replay a class is the other column, and it is what the arms read:
 *
 *   never      the arm is not replayed at all: the level door, the node masks, the body flags and
 *              the escort bar, which travel as state or not at all, and the blast, which will be
 *              replayed at its own function rather than through the director.
 *   stand-in   the arm reads nothing of its actor past the head of the director, which reads one
 *              word of it, so any readable actor stands in: the fog and the crawling text.
 *   replica    the arm reads the actor's body, so only the replica of that very actor will do: the
 *              shield and the sound stop. A stand-in would free channel 0 for the sound stop.
 */
#ifndef MULTIPLAYER_MP_DIRECTOR_RULE_H
#define MULTIPLAYER_MP_DIRECTOR_RULE_H

#include <stdint.h>

/* How many commands the director's jump table has. */
#define MP_DIRECTOR_COMMANDS 20

typedef enum mp_director_class {
    MP_DIRECTOR_NONE = 0,     /* an arm that does nothing, and any number past the table */
    MP_DIRECTOR_LEVEL_DOOR,   /* ends the level or fails it */
    MP_DIRECTOR_SHIELD,       /* the droideka's shield: raise, drop, show, size */
    MP_DIRECTOR_FOG,          /* the fog's two edges and its two ramps */
    MP_DIRECTOR_NODES,        /* a weapon hidden or shown on the actor's model */
    MP_DIRECTOR_BODY_FLAGS,   /* a bit of the body's flag word: the flicker, the shadow */
    MP_DIRECTOR_ESCORT,       /* the escort's health bar */
    MP_DIRECTOR_SOUND_STOP,   /* the actor's own looping sound ends */
    MP_DIRECTOR_CRAWL,        /* a line of text crawls across the screen */
    MP_DIRECTOR_BLAST,        /* an explosion at a node of the actor */
    MP_DIRECTOR_CLASSES
} mp_director_class_t;

typedef enum mp_director_replay {
    MP_DIRECTOR_REPLAY_NEVER = 0,
    MP_DIRECTOR_REPLAY_STAND_IN,
    MP_DIRECTOR_REPLAY_REPLICA
} mp_director_replay_t;

mp_director_class_t  mp_director_class_of(int32_t command);
mp_director_replay_t mp_director_replay_of(mp_director_class_t cls);
const char          *mp_director_class_name(mp_director_class_t cls);

#endif /* MULTIPLAYER_MP_DIRECTOR_RULE_H */
