/* mp_scene_host_report.c: what the host's half of a scene for everybody says in the run report.
 * See the header.
 *
 * Split from mp_scene_host.c along the seam that file's size note named. Everything here reads
 * the counters it is handed and the scene gates' own counts, and writes nothing into the engine or
 * into the gathering.
 */
#include "mp_scene_host_report.h"

#include "mp_cutscene.h"
#include "mp_seat.h"

#include "common/logging.h"

#include <stdint.h>
#include <string.h>

void mp_scene_host_report(const mp_scene_host_counts_t *n, const mp_scene_sender_t *sender,
                          const mp_seat_counts_t *seats)
{
    mp_cutscene_counts_t cut;

    memset(&cut, 0, sizeof cut);
    mp_cutscene_counts(&cut);
    log_info("  the scenes gathered (the host): %u gathered (%u by a lock, %u by the hero as an "
             "actor, %u by a warp), %u with every far player at their seat, %u with a far player "
             "no seat answered for, %u that went on after the bound, %u not gathered because the "
             "place could not be read or stood on a mover, over water or a drop, %u let go because "
             "every far player had left; %u second scene(s) counted and not gathered, %u not "
             "begun because nobody else was in the session; the longest hold %u substep(s); the "
             "host's re-entry was pointed at a gathering seat %u time(s)",
             (unsigned)(n->gathered[MP_SCENE_KIND_LOCK] + n->gathered[MP_SCENE_KIND_HERO] +
                        n->gathered[MP_SCENE_KIND_WARP]),
             (unsigned)n->gathered[MP_SCENE_KIND_LOCK], (unsigned)n->gathered[MP_SCENE_KIND_HERO],
             (unsigned)n->gathered[MP_SCENE_KIND_WARP], (unsigned)n->all_seated,
             (unsigned)n->with_unseated, (unsigned)n->went_on, (unsigned)n->not_gathered,
             (unsigned)n->left_alone, (unsigned)n->second, (unsigned)n->alone_doors,
             (unsigned)n->longest_hold, (unsigned)n->named);
    log_info("  the scenes given up (the host): %u after the wait for the host ran out, %u of "
             "them with the host dead, %u where it could not be moved, %u with no body to be "
             "taken, %u standing again only at the end; each was over for the far players and "
             "played here as it would alone",
             (unsigned)(n->gave_up[MP_SCENE_MOVE_DEAD] + n->gave_up[MP_SCENE_MOVE_MODE] +
                        n->gave_up[MP_SCENE_MOVE_NO_BODY] + n->gave_up[MP_SCENE_MOVE_YES]),
             (unsigned)n->gave_up[MP_SCENE_MOVE_DEAD], (unsigned)n->gave_up[MP_SCENE_MOVE_MODE],
             (unsigned)n->gave_up[MP_SCENE_MOVE_NO_BODY],
             (unsigned)n->gave_up[MP_SCENE_MOVE_YES]);
    log_info("  the grabs (the host): %u grab(s) of the hero, %u held for a gathering, the longest "
             "%u substep(s); %u put-back(s) refused while a hold stood", (unsigned)cut.grabs,
             (unsigned)cut.grabs_held, (unsigned)n->longest_hold, (unsigned)cut.putbacks_held);
    mp_scene_sender_report(sender);
    log_info("  the moves (the host): %u seated, %u refused because the body was dead and %u for "
             "its mode (anything the engine would not park for a scene: a jump, a fall, the "
             "water, a ledge, a push block or a gun), %u given up at the deadline, %u not moved "
             "because the scene ran first; %u fade(s) ended on their own clock rather than the "
             "engine's", (unsigned)n->seated, (unsigned)n->refused_dead,
             (unsigned)n->refused_mode, (unsigned)n->given_up, (unsigned)n->ran_first,
             (unsigned)n->fades_on_clock);
    log_info("  the warps (the host): %u taken by the engine of this host (%u quest bit(s) "
             "changed), %u dropped by the engine because the player module was not running, %u "
             "waiting re-entry(ies) ended; the respawns asked of this host: %u by the warp of a "
             "script, %u by the hero swap, %u by another caller in the executable, %u by a DLL",
             (unsigned)n->warps, (unsigned)n->warp_bits, (unsigned)n->warps_dropped,
             (unsigned)n->reentries_ended, (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_WARP],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_SWAP],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_IMAGE],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_DLL]);
    mp_seat_report_searches("the seat for a scene:", seats);
    log_info("  the seat for a scene, beside a seat: %u player(s) found none around the place, %u "
             "of them one beside a seat handed out first, %u none at all",
             (unsigned)n->around_none, (unsigned)n->beside_a_seat,
             (unsigned)(n->around_none - n->beside_a_seat));
    log_info("  the actor holds (the host): %u script run(s) of a scene's actor held back for a "
             "gathering, %u of them for a lock, %u for the hero as an actor",
             (unsigned)(n->actor_held[MP_SCENE_KIND_LOCK] + n->actor_held[MP_SCENE_KIND_HERO]),
             (unsigned)n->actor_held[MP_SCENE_KIND_LOCK],
             (unsigned)n->actor_held[MP_SCENE_KIND_HERO]);
}
