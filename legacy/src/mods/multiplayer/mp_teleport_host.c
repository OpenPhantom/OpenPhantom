/* mp_teleport_host.c: a client's own player put beside the host, because the player asked. See
 * the header.
 *
 * Two things about the code are worth having in front of a maintainer:
 *
 * The stage of the seat wish is asked after every look and before a seat is used. The wish moves
 * on to its fallback, an authored point of the level, inside a look that found nothing, once it
 * has looked around a good anchor for three seconds; the next look would then answer with a place
 * that is not beside the host. The line the seat search writes as it moves on says that it takes
 * that point, which for this caller is not so, and the line written here right after it says
 * that the teleport is given up.
 *
 * The seat machine is the one the host is brought to a scene by, with a bound on its wait: it
 * tries a placement that did not take again, twice, takes the engine's respawn once after two
 * seconds in a mode the teleport may not move, and gives up a body that cannot be moved for ten
 * seconds. It is never told that its scene ended; a level that changes or a session that ends
 * takes the exit here, which gives a held fade back.
 */
#include "mp_teleport_host.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"
#include "mp_scene_flow.h"
#include "mp_seat.h"
#include "mp_session_now.h"
#include "mp_start.h"
#include "mp_wallclock.h"

#include "common/logging.h"
#include "common/player_help_note.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The log's name for this caller of the seat search. */
static const char TELEPORT_WHO[] = "the teleport to the host";

typedef enum teleport_phase {
    TELEPORT_IDLE = 0,
    TELEPORT_SEARCHING,   /* a place beside the host is looked for */
    TELEPORT_MOVING       /* the seat machine brings the body there */
} teleport_phase_t;

typedef struct teleport_counts {
    uint32_t taken;                /* presses the door let through */
    uint32_t by_teleport;          /* landed through the placement */
    uint32_t by_respawn;           /* landed through the engine's respawn */
    uint32_t ended_searching;      /* no place, or the door shut during the search */
    uint32_t given_up_moving;      /* the seat machine gave the body up */
    uint32_t left;                 /* ended from outside: a repair, a new level, the session */
    uint32_t placements_refused;
    uint32_t respawns_asked;
    uint32_t respawns_refused;
    uint32_t longest;              /* substeps from a press to its landing, the most */
} teleport_counts_t;

typedef struct teleport_state {
    teleport_phase_t     phase;
    uint8_t              world;      /* the world it began in */
    uint32_t             began;      /* the substep count at the press */
    uint32_t             began_ms;   /* and the wall clock, for the lines */
    mp_seat_wish_t       wish;
    mp_seat_counts_t     seat_counts;
    float                seat[3];
    float                heading;
    mp_scene_seat_flow_t flow;
    bool                 said_respawn_failed;
    bool                 said_hard_left;
    teleport_counts_t    n;
} teleport_state_t;

static teleport_state_t tp;

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

bool mp_teleport_host_bound(void)
{
    return mp_seat_probes_resolved() && mp_scene_bind_ready();
}

/* A level of a started session runs here: the session by the one answer the world holds ask,
 * and the level by the game mode cell the seat search reads. */
static bool level_of_a_session(void)
{
    return mp_session_now_plays_in_a_running_session(NULL) && mp_seat_level_running();
}

/* ==============================================================================================
 * The door.
 * ============================================================================================ */

/* Everything the door asks, read on this frame. The host's pose is the one resolved for his
 * puppet; in the middle of a scene of his own it is still where he stands, because a parked
 * host sends his position from his model. */
static void look_at_the_door(mp_teleport_look_t *look, bool overlay_holds,
                             mp_bridge_far_pose_t *host)
{
    size_t bank = mp_bridge_far_bank_of_slot((uint8_t)MP_BRIDGE_HOST_SLOT);
    float  own[3];
    float  heading = 0.0f;

    memset(look, 0, sizeof *look);
    memset(host, 0, sizeof *host);
    look->is_client        = mp_bridge_drain_is_client();
    look->level_of_session = level_of_a_session();
    look->bound            = mp_teleport_host_bound();
    look->dead             = mp_respawn_player_is_a_corpse();
    look->busy             = mp_respawn_pending();
    look->teleporting      = tp.phase != TELEPORT_IDLE;
    look->at_gun           = mp_scene_bind_mode_is_gun();
    look->overlay_holds    = overlay_holds;
    if (bank == 0u) {
        return;   /* no far bank shows the host: this machine is the host, or he has not come */
    }
    look->host_elsewhere = mp_bridge_far_in_another_world(bank, MP_FAR_READER_OTHER, NULL);
    look->host_pose      = mp_bridge_far_pose(bank, MP_FAR_READER_OTHER, host);
    look->host_dead      = look->host_pose && host->dead;
    look->host_stands    = look->host_pose && mp_bridge_far_pose_stands(host);
    if (look->host_pose && mp_scene_bind_own_pose(own, &heading)) {
        look->distance_known = true;
        look->distance       = distance(own, host->position);
    }
}

mp_player_help_verdict_t mp_teleport_host_ask(bool overlay_holds, uint32_t substeps)
{
    mp_teleport_look_t       look;
    mp_bridge_far_pose_t     host;
    mp_player_help_verdict_t verdict;
    char                     far_off[48];

    look_at_the_door(&look, overlay_holds, &host);
    verdict = mp_teleport_door(&look);
    if (verdict.outcome != PLAYER_HELP_OUTCOME_OPEN) {
        log_info("teleport to host: %s, %s",
                 verdict.outcome == PLAYER_HELP_OUTCOME_NOTHING ? "nothing to do" : "refused",
                 mp_player_help_reason_text(verdict.reason));
        return verdict;
    }
    ++tp.n.taken;
    tp.phase               = TELEPORT_SEARCHING;
    tp.world               = mp_bridge_far_world();
    tp.began               = substeps;
    tp.began_ms            = mp_wallclock_ms();
    tp.said_respawn_failed = false;
    tp.said_hard_left      = false;
    memset(&tp.flow, 0, sizeof tp.flow);
    mp_seat_wish_beside_body(&tp.wish, TELEPORT_WHO, mp_bridge_drain_my_slot());
    if (look.distance_known) {
        (void)text_format(far_off, sizeof far_off, "%.2f u from this player",
                          (double)look.distance);
    } else {
        (void)text_format(far_off, sizeof far_off, "this player's own place did not read");
    }
    log_info("teleport to host: taken; the host stands at %.2f %.2f %.2f, %s, and a free place "
             "beside him is searched for %u substep(s) at most",
             (double)host.position[0], (double)host.position[1], (double)host.position[2],
             far_off, (unsigned)MP_TELEPORT_SEARCH_SUBSTEPS);
    return verdict;
}

/* ==============================================================================================
 * The move.
 * ============================================================================================ */

/* The engine's respawn onto the place, the way out of a mode the teleport may not move. */
static void take_the_hard_way(void)
{
    ++tp.n.respawns_asked;
    if (!mp_respawn_move_living(tp.seat, tp.heading)) {
        ++tp.n.respawns_refused;
        log_warning("teleport to host: the engine's respawn was refused at the door of the "
                    "respawn module, so the place waits for it to land");
        return;
    }
    log_info("teleport to host: this player goes to the place by the engine's respawn, after "
             "%u substep(s) in a mode the teleport may not move", (unsigned)tp.flow.mode_wait);
}

static void carry_out(mp_scene_seat_act_t act)
{
    switch (act) {
    case MP_SCENE_SEAT_ACT_FADE_OUT:
        mp_scene_bind_fade_out(tp.flow.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_PLACE:
        if (!mp_start_place_for_scene(tp.seat, tp.heading)) {
            ++tp.n.placements_refused;
            log_warning("teleport to host: the placement would not take the place at %.2f %.2f "
                        "%.2f, so this try does not take", (double)tp.seat[0],
                        (double)tp.seat[1], (double)tp.seat[2]);
        }
        break;
    case MP_SCENE_SEAT_ACT_FADE_IN:
        mp_scene_bind_fade_in(tp.flow.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_RESPAWN:
        take_the_hard_way();
        break;
    case MP_SCENE_SEAT_ACT_NONE:
    default:
        break;
    }
}

/* What the seat machine could not do, each said once a teleport. */
static void say_what_did_not_take(void)
{
    if (tp.flow.respawn_failed && !tp.said_respawn_failed) {
        tp.said_respawn_failed = true;
        log_warning("teleport to host: the engine's respawn did not bring this player to the "
                    "place within %u substep(s), so the place waits for the teleport again",
                    (unsigned)MP_SCENE_RESPAWN_SUBSTEPS);
    }
    if (tp.flow.hard_wanted && !tp.said_hard_left) {
        tp.said_hard_left = true;
        log_info("teleport to host: the engine's respawn is due and not taken, it %s, so the "
                 "teleport alone waits for this player",
                 tp.flow.hard_spent ? "was taken once already" : "may not take this player now");
    }
}

static void say_the_end(const mp_player_help_verdict_t *ended, uint32_t substeps)
{
    uint32_t took    = substeps - tp.began;
    uint32_t took_ms = mp_wallclock_ms() - tp.began_ms;

    if (ended->outcome == PLAYER_HELP_OUTCOME_DONE) {
        tp.n.by_respawn  += tp.flow.by_respawn ? 1u : 0u;
        tp.n.by_teleport += tp.flow.by_respawn ? 0u : 1u;
        tp.n.longest      = took > tp.n.longest ? took : tp.n.longest;
        log_info("teleport to host: landed at %.2f %.2f %.2f, %s, %u ms and %u substep(s) after "
                 "the press (%u retries)", (double)tp.seat[0], (double)tp.seat[1],
                 (double)tp.seat[2],
                 tp.flow.by_respawn ? "by the engine's respawn" : "by the teleport",
                 (unsigned)took_ms, (unsigned)took, (unsigned)tp.flow.retries);
        return;
    }
    ++tp.n.given_up_moving;
    log_info("teleport to host: given up %u ms and %u substep(s) after the press, after %u "
             "retries: this player %s", (unsigned)took_ms, (unsigned)took,
             (unsigned)tp.flow.retries, mp_scene_given_up_text(tp.flow.refused));
}

static bool move(uint32_t substeps, mp_player_help_verdict_t *ended)
{
    mp_scene_seat_look_t  look;
    mp_scene_seat_stage_t before = tp.flow.stage;
    float                 here[3];
    float                 heading = 0.0f;
    uint32_t              module  = 0u;

    if (!level_of_a_session()) {
        ended->outcome = PLAYER_HELP_OUTCOME_REFUSED;
        (void)mp_teleport_host_leave("no level of a started session runs any more",
                                     &ended->reason);
        ended->reason = PLAYER_HELP_REASON_NO_LEVEL;
        return true;
    }
    memset(&look, 0, sizeof look);
    look.now            = substeps;
    look.live           = true;
    look.may_move       = mp_scene_bind_may_move();
    look.fade_done      = mp_scene_bind_fade_done();
    look.at_seat        = mp_scene_bind_own_pose(here, &heading) &&
                          distance(here, tp.seat) < MP_SCENE_ARRIVED_DISTANCE;
    look.hard_ready     = mp_scene_bind_hard_way_open();
    look.module_running = mp_scene_bind_module_state(&module) &&
                          module == MP_HERO_MODULE_RUNNING;
    carry_out(mp_scene_seat_step(&tp.flow, &look));
    say_what_did_not_take();
    if (tp.flow.stage == before || !mp_teleport_moved(tp.flow.stage, ended)) {
        return false;
    }
    say_the_end(ended, substeps);
    tp.phase = TELEPORT_IDLE;
    return true;
}

/* ==============================================================================================
 * The search.
 * ============================================================================================ */

static void begin_the_move(const float seat[3], float heading, uint32_t substeps)
{
    memcpy(tp.seat, seat, sizeof tp.seat);
    tp.heading = heading;
    mp_scene_seat_start_gathering(&tp.flow, substeps, MP_SCENE_FADE_SECONDS,
                                  MP_SCENE_HARD_CLIENT_SUBSTEPS);
    tp.flow.wait_max = MP_SCENE_WARP_WAIT_SUBSTEPS;
    tp.phase         = TELEPORT_MOVING;
    log_info("teleport to host: a free place beside the host at %.2f %.2f %.2f facing %.2f, "
             "found after %u substep(s) of search; this player is moved there under a fade of "
             "%.2f s", (double)seat[0], (double)seat[1], (double)seat[2], (double)heading,
             (unsigned)(substeps - tp.began), (double)MP_SCENE_FADE_SECONDS);
}

static bool search(uint32_t substeps, mp_player_help_verdict_t *ended)
{
    mp_teleport_look_t   look;
    mp_bridge_far_pose_t host;
    float                seat[3];
    float                heading  = 0.0f;
    uint32_t             searched = substeps - tp.began;
    bool                 found;

    look_at_the_door(&look, false, &host);
    look.teleporting = false;   /* this search is the teleport that is under way */
    *ended = mp_teleport_door(&look);
    if (ended->outcome != PLAYER_HELP_OUTCOME_OPEN) {
        ++tp.n.ended_searching;
        tp.phase = TELEPORT_IDLE;
        log_info("teleport to host: ended after %u substep(s) of search, %s",
                 (unsigned)searched, mp_player_help_reason_text(ended->reason));
        return true;
    }
    mp_seat_wish_follow(&tp.wish, host.position, host.heading);
    found = mp_seat_wish_step(&tp.wish, substeps, &tp.seat_counts, seat, &heading);
    switch (mp_teleport_search(found, tp.wish.stage == MP_SEAT_STAGE_ANCHOR, searched)) {
    case MP_TELEPORT_SEARCH_SEATED:
        begin_the_move(seat, heading, substeps);
        return move(substeps, ended);
    case MP_TELEPORT_SEARCH_NO_SEAT:
        ++tp.n.ended_searching;
        tp.phase       = TELEPORT_IDLE;
        ended->outcome = PLAYER_HELP_OUTCOME_REFUSED;
        ended->reason  = PLAYER_HELP_REASON_NO_SEAT;
        log_info("teleport to host: given up after %u substep(s) of search, no free place "
                 "beside the host answered%s", (unsigned)searched,
                 tp.wish.stage == MP_SEAT_STAGE_ANCHOR
                     ? " in that time: the host rides a lift, falls or swims, or every place "
                       "around him is refused"
                     : ": the seat search moved on to an authored point of the level, which a "
                       "teleport does not take, it puts the player beside the host or nowhere");
        return true;
    case MP_TELEPORT_SEARCH_GOES_ON:
    default:
        return false;
    }
}

/* ==============================================================================================
 * The frame, the exit and the report.
 * ============================================================================================ */

bool mp_teleport_host_frame(uint32_t substeps, mp_player_help_verdict_t *ended)
{
    if (tp.phase == TELEPORT_IDLE || ended == NULL) {
        return false;
    }
    if (mp_bridge_far_world() != tp.world) {
        ended->outcome = PLAYER_HELP_OUTCOME_REFUSED;
        (void)mp_teleport_host_leave("the level changed under it", &ended->reason);
        return true;
    }
    return tp.phase == TELEPORT_SEARCHING ? search(substeps, ended) : move(substeps, ended);
}

bool mp_teleport_host_leave(const char *why, uint8_t *reason)
{
    bool fade_back;

    if (tp.phase == TELEPORT_IDLE) {
        return false;
    }
    if (reason != NULL) {
        *reason = mp_teleport_left_reason(mp_bridge_far_world() != tp.world);
    }
    /* A body already handed to the placement or to the respawn is on its way and lands where
     * it lands; what is taken back is the dark screen. */
    fade_back = tp.phase == TELEPORT_MOVING &&
                mp_scene_seat_leave(&tp.flow) == MP_SCENE_SEAT_ACT_FADE_IN;
    if (fade_back) {
        mp_scene_bind_fade_in(tp.flow.fade_seconds);
    }
    ++tp.n.left;
    tp.phase = TELEPORT_IDLE;
    log_info("teleport to host: left %u ms after the press, %s; %s",
             (unsigned)(mp_wallclock_ms() - tp.began_ms), why != NULL ? why : "ended from outside",
             fade_back ? "the fade it held was given back" : "it held no fade");
    return true;
}

void mp_teleport_host_report(void)
{
    log_info("  the teleport to the host: %u taken at the door, %u landed by the teleport, %u by "
             "the engine's respawn, %u ended in the search, %u given up on the way, %u left by a "
             "repair, a new level or the session's end; %u placement(s) refused, %u respawn(s) "
             "asked, %u of them refused at the door; the longest took %u substep(s)%s",
             (unsigned)tp.n.taken, (unsigned)tp.n.by_teleport, (unsigned)tp.n.by_respawn,
             (unsigned)tp.n.ended_searching, (unsigned)tp.n.given_up_moving, (unsigned)tp.n.left,
             (unsigned)tp.n.placements_refused, (unsigned)tp.n.respawns_asked,
             (unsigned)tp.n.respawns_refused, (unsigned)tp.n.longest,
             tp.phase != TELEPORT_IDLE ? "; one is under way" : "");
    if (tp.n.taken != 0u) {
        mp_seat_report_searches("the teleport's seat:", &tp.seat_counts);
    }
}
