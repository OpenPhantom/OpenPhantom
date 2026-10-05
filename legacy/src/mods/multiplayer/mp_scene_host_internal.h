/* mp_scene_host_internal.h: what the three files of the host's scene share, and nobody else.
 *
 * Layer 3. The host's half of a scene is three files along the seams its size named:
 * mp_scene_host.c keeps the scene, its door, its substep and its one exit; mp_scene_seats.c finds
 * the place the host is brought to; mp_scene_own.c takes the host to his place and keeps him
 * there. The scene's state is one record, which mp_scene_host.c owns and the other two are handed
 * through this header, so that none of them keeps a copy of what another one decides. The public
 * interface of all three is mp_scene_host.h.
 */
#ifndef MULTIPLAYER_MP_SCENE_HOST_INTERNAL_H
#define MULTIPLAYER_MP_SCENE_HOST_INTERNAL_H

#include "mp_scene_flow.h"
#include "mp_scene_host_report.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_scene_host_shared {
    bool                 installed;
    uint32_t             substep;
    mp_scene_host_flow_t flow;

    /* The scene as the door heard it. */
    uint8_t   bank;           /* the player its script meant, 0 the host */
    uintptr_t actor;          /* the actor whose script opened the door */
    uint32_t  actor_key;
    bool      actor_keyed;
    bool      actor_at_known; /* where that actor stood as the door opened */
    float     actor_at[3];
    bool      hero_keyed;     /* a hero's door: the placement the hero was spawned on */
    uint32_t  hero_key;
    float     warp_at[3];
    float     warp_heading;
    int32_t   warp_hero;
    uint16_t  warp_told;      /* the number the level's journal gave the warp, 0 when it took
                               * none and no far player hears of it */

    /* Where the scene is played and the host stands. */
    bool      anchor_known;   /* the place of the scene is known */
    bool      no_place;       /* the host has no place: none to read, or none to stand on */
    float     anchor[3];
    float     heading;        /* the way the host faces at his place */
    uint8_t   trigger_slot;

    /* The hold fell with the host at his place, and not away. Only then is the hero's grab
     * measured against that place; it is false again with a scene given up and with every new
     * one. */
    bool      released_at_place;
    uint32_t  scene_lines;    /* lines written for the scene that stands */
    uint32_t  measured;       /* measuring lines written for it */
    uint32_t  measured_at;    /* the substep of the last one */

    mp_seat_counts_t       seat_counts;
    mp_scene_host_counts_t n;
} mp_scene_host_shared_t;

/* The one record, owned by mp_scene_host.c. */
mp_scene_host_shared_t *mp_scene_host_shared(void);

/* Whether a scene's own line may still be written: the first MP_SCENE_EVENT_LINES of each scene
 * are, up to MP_SCENE_EVENT_LINES_ALL in the process, and the rest are counted only. One scene
 * writes about seven lines, more when the host's input changes hands, so a budget for the whole
 * process fell silent after a handful of scenes. */
#define MP_SCENE_EVENT_LINES     16u
#define MP_SCENE_EVENT_LINES_ALL 512u
bool mp_scene_host_line_allowed(void);

/* The actor whose script opened the scene's door. An address the pool gave another actor since is
 * not. */
bool mp_scene_host_is_the_actor(uintptr_t actor);

/* ==============================================================================================
 * mp_scene_seats.c: the place.
 * ============================================================================================ */

/* The substep after the door: the place of the host, or the wait for it while the player the
 * script meant is in the air or the water. */
void mp_scene_seats_start(void);

/* One substep of that wait. Nothing once the place is known. */
void mp_scene_seats_step(void);

/* Whether the place of the player the script meant is still being read again. */
bool mp_scene_seats_place_pending(void);

/* Everything the place of a scene kept, for the next one. */
void mp_scene_seats_forget(void);

/* ==============================================================================================
 * mp_scene_own.c: the host on his way to his place.
 * ============================================================================================ */

/* Why the host's way is forgotten, for the line of his input. */
typedef enum mp_scene_own_end {
    MP_SCENE_OWN_END_SCENE = 0,   /* the scene ended, or its world did */
    MP_SCENE_OWN_END_WARP,        /* a warp took the scene over */
    MP_SCENE_OWN_END_NEXT         /* another scene begins, and nothing of the last one's is kept */
} mp_scene_own_end_t;

/* The host is to stand at `place`, facing `heading`. */
void mp_scene_own_start(const float place[3], float heading);

/* One substep of his way, after the place. */
void mp_scene_own_step(void);

/* The way out for every exit: a held fade given back, the input let go, the named anchor taken
 * away. */
void mp_scene_own_forget(mp_scene_own_end_t why);

/* Whether the host has a place this scene. */
bool mp_scene_own_wanted(void);

/* Whether he has a place and does not stand at it now: his seat not done, the module not running,
 * or the body off the place. */
bool mp_scene_own_away(void);

/* Whether the engine's respawn is bringing him: his seat in it, and the module seen out of its
 * running state since it was asked. */
bool mp_scene_own_respawning(void);

/* The place of the scene, the anchor his re-entry is pointed at while the scene stands and he
 * lies dead: his own place where he has one, otherwise where the scene is played. */
void mp_scene_own_name_the_anchor(bool stands);

/* The input hold, a level taken at the head of every substep. */
void mp_scene_own_hold_input(void);

/* What the host was when the hold fell, for the line that says so: `out` gets the text after "the
 * host". */
void mp_scene_own_stage(char *out, size_t size);

/* From the grab listener of mp_cutscene, inside the engine: the hero of the scene was taken. */
void mp_scene_own_grabbed(void);

#endif /* MULTIPLAYER_MP_SCENE_HOST_INTERNAL_H */
