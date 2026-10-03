/* mp_scene_doorless.h: a scene that runs on the host with no door heard.
 *
 * Layer 1, pure. A savegame saved in the middle of a scene restores it whole: the lock at the
 * level it stood at, the camera the scene had, the bars, and the actor that drove the player's
 * body with that body handed to it again. No script door opens for any of it. On the host the
 * scene's own script runs on and lets go of everything at its end.
 *
 * The host takes such a scene over as one of its own, on evidence only: the lock at a script's
 * level, or the module parked with an actor driving the player's body. The developer menu and
 * the free camera park the module as well, with no such actor and no lock, and are no scene. Two
 * substeps in a row, so that a single reading is not a scene. The binding is mp_scene_host.c.
 *
 * A client is in no scene, and what such a savegame leaves standing there is given back with no
 * count at all; that rule is mp_scene_free_rule, not this file.
 */
#ifndef MULTIPLAYER_MP_SCENE_DOORLESS_H
#define MULTIPLAYER_MP_SCENE_DOORLESS_H

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stdint.h>

/* Substeps in a row the evidence has to stand before the host takes a scene over. */
#define MP_SCENE_ADOPT_SUBSTEPS 2u

typedef struct mp_scene_adopt_look {
    bool             may_begin;      /* what a door asks: bound, hosting, a far player joined */
    mp_scene_phase_t phase;          /* the host's scene */
    bool             given_up;       /* one given up that the engine may still play */
    int32_t          lock_level;     /* -1 where it does not read */
    bool             module_parked;  /* nought, a running state in its store, and a body */
    bool             driven;         /* an actor with the handover bit drives the player's body */
} mp_scene_adopt_look_t;

typedef enum mp_scene_adopt {
    MP_SCENE_ADOPT_NO = 0,
    MP_SCENE_ADOPT_WAIT,     /* the evidence stands, not yet for MP_SCENE_ADOPT_SUBSTEPS */
    MP_SCENE_ADOPT_LOCK,     /* take it over as a lock */
    MP_SCENE_ADOPT_HERO      /* take it over as a scene with the hero as an actor */
} mp_scene_adopt_t;

/* One substep. `seen` counts the substeps in a row the evidence stood, and is the caller's to keep
 * from one substep to the next; anything but NONE as the host's phase starts it over. */
mp_scene_adopt_t mp_scene_adopt_step(uint32_t *seen, const mp_scene_adopt_look_t *look);

#endif /* MULTIPLAYER_MP_SCENE_DOORLESS_H */
