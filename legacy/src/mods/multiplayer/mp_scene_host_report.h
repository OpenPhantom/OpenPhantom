/* mp_scene_host_report.h: what the host's half of a scene for everybody says in the run report.
 *
 * The counters are counted in mp_scene_host.c and printed in mp_scene_host_report.c. This header
 * is the one record the two agree on, and the report is handed it together with the note's sender
 * and the seat search's counters, so the gathering's own state stays private to its file.
 */
#ifndef MULTIPLAYER_MP_SCENE_HOST_REPORT_H
#define MULTIPLAYER_MP_SCENE_HOST_REPORT_H

#include "mp_scene_flow.h"
#include "mp_scene_rule.h"
#include "mp_scene_send.h"
#include "mp_seat_rule.h"

#include <stdint.h>

/* What the host's lines say: the gatherings, the scenes given up, the moves, the warps and the
 * actor holds. */
typedef struct mp_scene_host_counts {
    uint32_t gathered[MP_SCENE_KINDS];
    uint32_t all_seated;
    uint32_t with_unseated;    /* released with everybody seated at a seat, and somebody had none */
    uint32_t around_none;      /* players no seat around the place answered for */
    uint32_t beside_a_seat;    /* of those, seated beside a seat handed out first */
    uint32_t went_on;
    uint32_t not_gathered;
    uint32_t left_alone;     /* a hold that fell because every far player had left */
    uint32_t alone_doors;    /* a door heard with nobody else in the session: no scene begun */
    uint32_t gave_up[MP_SCENE_MOVE_MODE + 1u];   /* scenes given up, by what the host was */
    uint32_t second;
    uint32_t longest_hold;
    uint32_t named;
    uint32_t actor_held[MP_SCENE_KINDS];
    uint32_t seated;
    uint32_t refused_dead;
    uint32_t refused_mode;
    uint32_t given_up;
    uint32_t ran_first;
    uint32_t fades_on_clock;
    uint32_t warps;
    uint32_t warp_bits;
    uint32_t warps_dropped;
    uint32_t reentries_ended;
    uint32_t respawns[MP_SCENE_RESPAWN_CALLERS];
    uint32_t lines;
} mp_scene_host_counts_t;

/* The host's lines, with the note's cadence and the seat searches between them. */
void mp_scene_host_report(const mp_scene_host_counts_t *n, const mp_scene_sender_t *sender,
                          const mp_seat_counts_t *seats);

#endif /* MULTIPLAYER_MP_SCENE_HOST_REPORT_H */
