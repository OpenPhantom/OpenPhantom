/* mp_scene_host_report.h: what the host's half of a scene says in the run report, and the line
 * that measures a scene while it stands.
 *
 * The counters are counted in the three files of the host's scene and printed in
 * mp_scene_host_report.c. This header is the one record they agree on, and the report is handed it
 * together with the seat search's counters, so the scene's own state stays private to its file.
 *
 * The measuring line is here because it is the same kind of work: it reads and writes nothing
 * back. While a scene of the host's stands it says, at the beginning, as the hold falls and every
 * two seconds after, where the scripts of the scene stand: the actor whose script opened the door
 * and the actor that drives the host's body, each with its state, its script's state, its five
 * counters, its health and its clip, and beside them the four registers every script of the level
 * shares and whether a line is being voiced. A scene whose script waits for a register, a counter
 * or the end of a line stands still with nothing else to show for it.
 */
#ifndef MULTIPLAYER_MP_SCENE_HOST_REPORT_H
#define MULTIPLAYER_MP_SCENE_HOST_REPORT_H

#include "mp_scene_flow.h"
#include "mp_scene_rule.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* What the host's lines say: the scenes, the scenes given up, the host's way, the warps and the
 * actor holds. */
typedef struct mp_scene_host_counts {
    uint32_t began[MP_SCENE_KINDS];     /* scenes of the host's, by what set them off */
    uint32_t held;                      /* of those, a far player's: begun held for the host */
    uint32_t at_place;                  /* holds that fell with the host at his place */
    uint32_t already_there;             /* of the held, the host stood there and was not moved */
    uint32_t no_place;                  /* scenes the host had no place for */
    uint32_t dropped[2];                /* locks that were no scene of the host's: with no place
                                         * for him, and at the cap */
    uint32_t let_go_waits;              /* substeps a lock stayed owed under an engine menu */
    uint32_t owed_made_up;              /* refused releases made up after their actor went away */
    uint32_t hero_behind_lock;          /* scenes made a hero's by their own actor's spawn */
    uint32_t left_alone;     /* a hold that fell because every far player had left */
    uint32_t alone_doors;    /* a door heard with nobody else in the session: no scene begun */
    uint32_t adopted[MP_SCENE_KINDS];   /* scenes that ran with no door, taken over */
    uint32_t gave_up[MP_SCENE_MOVES];   /* scenes given up, by what the host was */
    uint32_t still_running;   /* scenes still running as their level ended */
    uint32_t second;
    uint32_t longest_hold;
    uint32_t named;
    uint32_t actor_held[MP_SCENE_KINDS];
    uint32_t seated;
    uint32_t refused_dead;
    uint32_t refused_mode;
    uint32_t refused_unread;
    uint32_t given_up;
    uint32_t ran_first;
    uint32_t fades_on_clock;
    uint32_t warps;
    uint32_t warp_bits;
    uint32_t warps_dropped;
    uint32_t reentries_ended;
    uint32_t respawns[MP_SCENE_RESPAWN_CALLERS];
    uint32_t lines;

    /* The host's way to the place of a scene a far player set off. */
    uint32_t own_by_teleport;
    uint32_t own_by_respawn;
    uint32_t own_after_reentry;   /* reached after the host lay dead on the way */
    uint32_t own_retries;
    uint32_t own_at_gun;          /* the hard way due, and the host at a gun */
    uint32_t own_unread;          /* waits begun for a mode that did not read */
    uint32_t own_longest;         /* substeps from the door to the place, the most */
    uint32_t input_held;
    uint32_t grabs_at_place;      /* grabs of the hero after a hold fell with the host there */
    uint32_t grabs_off;           /* of those, a unit or more from the place */
    uint32_t grabs_left;          /* grabs where the host was left: no place, alone, given up */
    uint32_t lines_left_out;      /* lines of scenes past their budget */

    /* The place. */
    uint32_t beside_actor;        /* places beside the scene's actor */
    uint32_t place_waits;         /* places read again for a player in the air or water */

    /* The measuring line. */
    uint32_t measures;
    uint32_t measures_left_out;   /* past sixteen a scene or the process's budget */
    uint32_t repairs;             /* scenes the player's own release ended */
} mp_scene_host_counts_t;

/* The host's lines, with the seat searches between them. */
void mp_scene_host_report(const mp_scene_host_counts_t *n, const mp_seat_counts_t *seats);

/* When a measuring line is written. */
typedef enum mp_scene_measure_when {
    MP_SCENE_MEASURE_BEGUN = 0,   /* in the substep the scene began */
    MP_SCENE_MEASURE_RELEASED,    /* as its hold fell */
    MP_SCENE_MEASURE_STANDING     /* every MP_SCENE_MEASURE_SUBSTEPS while it stands */
} mp_scene_measure_when_t;

/* How often a standing scene is measured, in substeps, and how many lines one scene and one
 * process get. */
#define MP_SCENE_MEASURE_SUBSTEPS  64u
#define MP_SCENE_MEASURE_LINES     16u
#define MP_SCENE_MEASURE_LINES_ALL 256u

/* What the line is told of the scene; the actors and the registers it reads itself. */
typedef struct mp_scene_measure {
    mp_scene_measure_when_t when;
    uint16_t                serial;
    uint32_t                substep;
    uint32_t                since;        /* substeps since the scene began */
    uintptr_t               door;         /* the actor whose script opened the door, 0 for none */
    uint32_t                door_key;
    bool                    door_keyed;
} mp_scene_measure_t;

/* One measuring line. Reads the two actors with guarded reads and writes nothing. */
void mp_scene_host_measure(const mp_scene_measure_t *what);

#endif /* MULTIPLAYER_MP_SCENE_HOST_REPORT_H */
