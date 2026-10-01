/* mp_director_rule.c: the script director's twenty commands by class. See the header. */
#include "mp_director_rule.h"

#include <stddef.h>
#include <stdint.h>

/* One class per arm of the jump table, in its order. Commands 0 and 13 have no arm worth the name;
 * 10 flips the flicker bit and no shipped script sends it, 16 the shadow bit, which is part of the
 * body's state and has one writer there. */
static const mp_director_class_t CLASS_OF[MP_DIRECTOR_COMMANDS] = {
    MP_DIRECTOR_NONE,         /*  0 */
    MP_DIRECTOR_LEVEL_DOOR,   /*  1 the level outcome, 3 */
    MP_DIRECTOR_SHIELD,       /*  2 raise it */
    MP_DIRECTOR_SHIELD,       /*  3 drop it */
    MP_DIRECTOR_SHIELD,       /*  4 show or hide it */
    MP_DIRECTOR_SHIELD,       /*  5 its radius */
    MP_DIRECTOR_FOG,          /*  6 the near edge */
    MP_DIRECTOR_FOG,          /*  7 the far edge */
    MP_DIRECTOR_NODES,        /*  8 the weapon put away */
    MP_DIRECTOR_NODES,        /*  9 the weapon shown */
    MP_DIRECTOR_BODY_FLAGS,   /* 10 the flicker bit */
    MP_DIRECTOR_FOG,          /* 11 the ramp to pale green */
    MP_DIRECTOR_FOG,          /* 12 the ramp back to black */
    MP_DIRECTOR_NONE,         /* 13 */
    MP_DIRECTOR_LEVEL_DOOR,   /* 14 the failure and its reason */
    MP_DIRECTOR_ESCORT,       /* 15 the escort's health as a share */
    MP_DIRECTOR_BODY_FLAGS,   /* 16 the shadow bit */
    MP_DIRECTOR_SOUND_STOP,   /* 17 the actor's channel freed */
    MP_DIRECTOR_CRAWL,        /* 18 a text id */
    MP_DIRECTOR_BLAST,        /* 19 an explosion at node 9, 11 or 12 */
};

typedef struct class_row {
    mp_director_replay_t replay;
    const char          *name;
} class_row_t;

static const class_row_t CLASSES[MP_DIRECTOR_CLASSES] = {
    { MP_DIRECTOR_REPLAY_NEVER,    "none"       },
    { MP_DIRECTOR_REPLAY_NEVER,    "level door" },
    { MP_DIRECTOR_REPLAY_REPLICA,  "shield"     },
    { MP_DIRECTOR_REPLAY_STAND_IN, "fog"        },
    { MP_DIRECTOR_REPLAY_NEVER,    "nodes"      },
    { MP_DIRECTOR_REPLAY_NEVER,    "body flags" },
    { MP_DIRECTOR_REPLAY_NEVER,    "escort"     },
    { MP_DIRECTOR_REPLAY_REPLICA,  "sound stop" },
    { MP_DIRECTOR_REPLAY_STAND_IN, "crawl"      },
    { MP_DIRECTOR_REPLAY_NEVER,    "blast"      },
};

mp_director_class_t mp_director_class_of(int32_t command)
{
    if (command < 0 || command >= MP_DIRECTOR_COMMANDS) {
        return MP_DIRECTOR_NONE;
    }
    return CLASS_OF[command];
}

mp_director_replay_t mp_director_replay_of(mp_director_class_t cls)
{
    if ((size_t)cls >= (size_t)MP_DIRECTOR_CLASSES) {
        return MP_DIRECTOR_REPLAY_NEVER;
    }
    return CLASSES[cls].replay;
}

const char *mp_director_class_name(mp_director_class_t cls)
{
    if ((size_t)cls >= (size_t)MP_DIRECTOR_CLASSES) {
        return "none";
    }
    return CLASSES[cls].name;
}
