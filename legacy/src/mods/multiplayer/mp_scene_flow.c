/* mp_scene_flow.c: the state machines of a scene of the host's, pure. See the header.
 *
 * SIZE NOTE: two machines the host's binding runs together, the host's scene and one player's
 * seat, and the rule of the host's input, which reads the two together. The decisions about a
 * scene that runs with no door are mp_scene_doorless's; only the host's taking one over stays
 * here, because it sets every field a beginning sets. Where the place is went to mp_scene_room.
 * The next seam is one player's seat, mp_scene_seat_start to mp_scene_seat_leave, which shares
 * only the move type with the rest; its tests are files of their own already, and the input rule
 * would go with it.
 */
#include "mp_scene_flow.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Substeps a second, the simulation's own rate, for turning a fade's seconds into a deadline. */
#define SUBSTEPS_A_SECOND 32.0f

/* What a deadline adds to the fade itself: a quarter of a second, for a tint the renderer sets its
 * end in only when it draws. */
#define FADE_SLACK_SUBSTEPS 8u

/* ==============================================================================================
 * The host's scene.
 * ============================================================================================ */

const char *mp_scene_release_text(mp_scene_release_t released)
{
    switch (released) {
    case MP_SCENE_RELEASE_AT_THE_PLACE:
        return "the host stood at his place";
    case MP_SCENE_RELEASE_ALONE:
        return "every far player had left the session";
    case MP_SCENE_RELEASE_NOBODY:
        return "the host had no place to be brought to and stayed where he stood";
    case MP_SCENE_RELEASE_NONE:
    default:
        return "no hold stood";
    }
}

const char *mp_scene_drop_text(mp_scene_drop_t dropped)
{
    switch (dropped) {
    case MP_SCENE_DROP_NO_PLACE:
        return "the host has no place where that player stands";
    case MP_SCENE_DROP_AT_THE_CAP:
        return "the host could not be brought there before the wait ran out";
    case MP_SCENE_DROP_NONE:
    default:
        return "nothing was dropped";
    }
}

const char *mp_scene_given_up_text(mp_scene_move_t host)
{
    switch (host) {
    case MP_SCENE_MOVE_DEAD:
        return "lay dead";
    case MP_SCENE_MOVE_MODE:
        return "could not be moved (a gun, the water, a push block, a jump or a ledge)";
    case MP_SCENE_MOVE_NO_BODY:
        return "had no body to be taken (a respawn or a load)";
    case MP_SCENE_MOVE_UNREAD:
        return "could not be read (its mode did not read)";
    case MP_SCENE_MOVE_YES:
    case MP_SCENE_MOVES:
    default:
        return "stood again only as the wait ran out";
    }
}

const char *mp_scene_move_text(mp_scene_move_t move)
{
    switch (move) {
    case MP_SCENE_MOVE_NO_BODY: return "no body, a load or a respawn";
    case MP_SCENE_MOVE_DEAD:    return "dead";
    case MP_SCENE_MOVE_MODE:    return "a mode the teleport may not move";
    case MP_SCENE_MOVE_UNREAD:  return "its mode did not read";
    case MP_SCENE_MOVE_YES:
    case MP_SCENE_MOVES:
    default:                    return "none";
    }
}

/* The next number after `serial`, never nought, which is the number of no scene. */
static uint16_t next_serial(uint16_t serial)
{
    uint16_t next = (uint16_t)(serial + 1u);

    return next == 0u ? 1u : next;
}

static void enter(mp_scene_host_flow_t *flow, mp_scene_phase_t phase, uint32_t now)
{
    flow->phase       = phase;
    flow->phase_since = now;
}

mp_scene_begin_t mp_scene_host_begin(mp_scene_host_flow_t *flow, mp_scene_kind_t kind,
                                     uint32_t now, bool holds)
{
    bool             inside;
    mp_scene_begin_t answer = MP_SCENE_BEGIN_NEW;

    if (flow == NULL || kind >= MP_SCENE_KINDS) {
        return MP_SCENE_BEGIN_SECOND;
    }
    /* A scene given up may still be played by the engine here: a door of it is not a new one. */
    inside = flow->phase == MP_SCENE_PHASE_GATHERING || flow->phase == MP_SCENE_PHASE_RUNNING ||
             flow->given_up;
    if (inside && kind != MP_SCENE_KIND_WARP) {
        return MP_SCENE_BEGIN_SECOND;
    }
    if (inside) {
        answer = MP_SCENE_BEGIN_WARP_OVER;
    }
    flow->serial        = next_serial(flow->serial);
    flow->kind          = kind;
    flow->holds         = holds && kind != MP_SCENE_KIND_WARP;
    flow->began         = now;
    flow->last          = now;
    flow->hold_standing = 0u;
    flow->hold_dead     = 0u;
    flow->seen_running  = false;
    flow->released      = MP_SCENE_RELEASE_NONE;
    flow->grace         = 0u;
    flow->given_up      = false;
    flow->dropped       = MP_SCENE_DROP_NONE;
    /* A warp gathers until the host has landed; a scene with nothing to wait for runs. */
    enter(flow, flow->holds || kind == MP_SCENE_KIND_WARP ? MP_SCENE_PHASE_GATHERING
                                                           : MP_SCENE_PHASE_RUNNING, now);
    return answer;
}

bool mp_scene_host_adopt(mp_scene_host_flow_t *flow, mp_scene_kind_t kind, uint32_t now)
{
    if (flow == NULL || kind == MP_SCENE_KIND_WARP || kind >= MP_SCENE_KINDS ||
        flow->phase != MP_SCENE_PHASE_NONE || flow->given_up) {
        return false;
    }
    (void)mp_scene_host_begin(flow, kind, now, false);
    flow->seen_running = true;
    return true;
}

/* The wait for the host has run out. The actor and the grab are free, and the engine plays the
 * scene here as it would alone, with the host dead or wherever he stands. Never a release into
 * running: the caller tells a scene given up from one whose host stood at his place, and only the
 * second is measured against that place. */
static void give_the_scene_up(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    flow->holds        = false;
    flow->given_up     = true;
    flow->given_up_for = look->host_stands ? look->may_move : MP_SCENE_MOVE_DEAD;
    flow->seen_running = look->running;
    flow->grace        = 0u;
    enter(flow, MP_SCENE_PHASE_OVER, look->now);
}

static bool out_of_time(const mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    return look->now - flow->began >= MP_SCENE_WAIT_CAP_SUBSTEPS;
}

/* A held lock a far player set off is no scene of the host's: the hold falls, nothing runs here,
 * and the binding lets the host go of what the lock took at the door. */
static void drop_the_lock(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look,
                          mp_scene_drop_t why)
{
    flow->holds   = false;
    flow->dropped = why;
    flow->grace   = 0u;
    enter(flow, MP_SCENE_PHASE_NONE, look->now);
}

/* A held lock or hero scene: the hold's clocks, then whether it falls. The substeps since the
 * last look belong to what this look found, the host standing or dead. A hold falls into running
 * only with the host standing at his place: a dead host is waited for, his re-entry bringing him
 * to the place, and so is one on his way to it. With every far player gone it falls at once, but
 * not under a host who is on his way to a place already: the place is read and the grab would
 * otherwise find him wherever he was. With no place, a hero scene is released where the host
 * stands and a lock is dropped, a dead host's as well. At the cap a hero scene is given up and a
 * lock dropped; the cap waits while the engine's respawn is bringing the host, because the module
 * it parks then is the respawn's own. */
static void step_the_hold(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look,
                          uint32_t passed)
{
    bool lock = flow->kind == MP_SCENE_KIND_LOCK;

    if (look->host_stands) {
        flow->hold_standing += passed;
    } else {
        flow->hold_dead += passed;
    }
    if (look->alone && !look->has_place) {
        /* With nobody else in the session there is nobody whose place the host would take: the
         * scene runs as it would with no session at all. */
        flow->released = MP_SCENE_RELEASE_ALONE;
    } else if (lock && look->no_place) {
        drop_the_lock(flow, look, MP_SCENE_DROP_NO_PLACE);
        return;
    } else if (look->host_stands && look->no_place) {
        flow->released = MP_SCENE_RELEASE_NOBODY;
    } else if (look->host_stands && !look->away && !look->place_pending) {
        flow->released = MP_SCENE_RELEASE_AT_THE_PLACE;
    } else {
        if (out_of_time(flow, look) && !look->own_respawning) {
            if (lock) {
                drop_the_lock(flow, look, MP_SCENE_DROP_AT_THE_CAP);
            } else {
                give_the_scene_up(flow, look);
            }
        }
        return;
    }
    flow->holds        = false;
    flow->seen_running = look->running;
    flow->grace        = 0u;
    enter(flow, MP_SCENE_PHASE_RUNNING, look->now);
}

/* Whether the engine has played the scene here, or plainly will not: seen running and stopped, or
 * never seen for the grace while the host could have been taken. A grab waits for nothing else,
 * so the grace counts only the substeps in which the host may be moved; a host at a gun, in the
 * water, behind a push block or dead is waited for instead. */
static bool the_engine_is_done(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look,
                               uint32_t passed)
{
    flow->seen_running = flow->seen_running || look->running;
    if (flow->seen_running) {
        return !look->running;
    }
    if (look->may_move != MP_SCENE_MOVE_YES) {
        return false;
    }
    flow->grace += passed;
    return flow->grace >= MP_SCENE_GRAB_GRACE_SUBSTEPS;
}

/* A scene that is done is none at once: nothing is told to anybody after it, so nothing lingers.
 * Over is kept for one case alone, a scene given up that the engine may still play here. */
bool mp_scene_host_step(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    mp_scene_phase_t before;
    uint32_t         passed;

    if (flow == NULL || look == NULL) {
        return false;
    }
    before     = flow->phase;
    passed     = look->now - flow->last;
    flow->last = look->now;
    switch (flow->phase) {
    case MP_SCENE_PHASE_GATHERING:
        if (flow->kind != MP_SCENE_KIND_WARP) {
            step_the_hold(flow, look, passed);
        } else if (look->warp_landed || look->now - flow->began >= MP_SCENE_WARP_CAP_SUBSTEPS) {
            enter(flow, MP_SCENE_PHASE_NONE, look->now);
        }
        break;
    case MP_SCENE_PHASE_RUNNING:
        /* The engine's grab comes a substep after the hold falls, so a scene counts as having
         * run only once it has been seen running. */
        if (the_engine_is_done(flow, look, passed)) {
            enter(flow, MP_SCENE_PHASE_NONE, look->now);
        } else if (!flow->seen_running && out_of_time(flow, look) &&
                   look->now - flow->phase_since >= MP_SCENE_GRAB_GRACE_SUBSTEPS) {
            /* A hold that fell near the cap still gets the grace of the grab it fell for. */
            give_the_scene_up(flow, look);
        }
        break;
    case MP_SCENE_PHASE_OVER:
        if (!flow->given_up || the_engine_is_done(flow, look, passed)) {
            flow->given_up = false;
            enter(flow, MP_SCENE_PHASE_NONE, look->now);
        }
        break;
    case MP_SCENE_PHASE_NONE:
    default:
        break;
    }
    return flow->phase != before;
}

bool mp_scene_host_takes_the_hero(mp_scene_host_flow_t *flow, uint32_t now)
{
    if (flow == NULL || flow->kind != MP_SCENE_KIND_LOCK || flow->began != now ||
        (flow->phase != MP_SCENE_PHASE_GATHERING && flow->phase != MP_SCENE_PHASE_RUNNING)) {
        return false;
    }
    flow->kind = MP_SCENE_KIND_HERO;
    return true;
}

bool mp_scene_host_stands_now(const mp_scene_host_flow_t *flow)
{
    if (flow == NULL) {
        return false;
    }
    if (flow->given_up) {
        return true;
    }
    return flow->kind != MP_SCENE_KIND_WARP && (flow->phase == MP_SCENE_PHASE_GATHERING ||
                                                flow->phase == MP_SCENE_PHASE_RUNNING);
}

bool mp_scene_host_still_running(const mp_scene_host_flow_t *flow)
{
    return flow != NULL && (flow->phase == MP_SCENE_PHASE_RUNNING || flow->given_up);
}

bool mp_scene_host_leave(mp_scene_host_flow_t *flow)
{
    bool held;

    if (flow == NULL) {
        return false;
    }
    held           = flow->holds;
    flow->holds    = false;
    flow->phase    = MP_SCENE_PHASE_NONE;
    flow->released = MP_SCENE_RELEASE_NONE;
    flow->given_up = false;
    flow->dropped  = MP_SCENE_DROP_NONE;
    flow->grace    = 0u;
    return held;
}

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

void mp_scene_seat_start(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds)
{
    if (flow == NULL) {
        return;
    }
    memset(flow, 0, sizeof *flow);
    flow->stage        = MP_SCENE_SEAT_WAITING;
    flow->ended        = MP_SCENE_SEAT_END_NONE;
    flow->since        = now;
    flow->last         = now;
    flow->fade_seconds = fade_seconds;
    flow->refused      = MP_SCENE_MOVE_YES;
}

void mp_scene_seat_start_gathering(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds,
                                   uint32_t hard_after)
{
    mp_scene_seat_start(flow, now, fade_seconds);
    if (flow != NULL) {
        flow->gathering  = true;
        flow->hard_after = hard_after;
    }
}

uint32_t mp_scene_seat_fade_deadline(float fade_seconds)
{
    float substeps = isfinite(fade_seconds) && fade_seconds > 0.0f
                         ? ceilf(fade_seconds * SUBSTEPS_A_SECOND)
                         : 0.0f;

    return (uint32_t)substeps + FADE_SLACK_SUBSTEPS;
}

static mp_scene_seat_act_t give_up(mp_scene_seat_flow_t *flow, mp_scene_seat_end_t ended)
{
    bool held = flow->fade_held;

    flow->stage     = MP_SCENE_SEAT_GIVEN_UP;
    flow->ended     = ended;
    flow->fade_held = false;
    return held ? MP_SCENE_SEAT_ACT_FADE_IN : MP_SCENE_SEAT_ACT_NONE;
}

/* Whether the hard way is due: a gathering's seat whose body has been in a mode the teleport may
 * not move for its bound, counted over every try, and is in one now. */
static bool hard_way_due(const mp_scene_seat_flow_t *flow, const mp_scene_seat_look_t *look)
{
    return flow->gathering && flow->hard_after != 0u && look->may_move == MP_SCENE_MOVE_MODE &&
           flow->mode_wait >= flow->hard_after;
}

static bool hard_way_left(const mp_scene_seat_flow_t *flow, const mp_scene_seat_look_t *look)
{
    return flow->hard_after != 0u && !flow->hard_spent && look->hard_ready;
}

/* The engine's respawn onto the seat. A fade this flow holds is given back first, on its own look:
 * the respawn fades the screen by itself, and every fade out of this flow has to be answered by
 * exactly one of its own fades in. */
static mp_scene_seat_act_t take_the_hard_way(mp_scene_seat_flow_t *flow,
                                             const mp_scene_seat_look_t *look)
{
    if (flow->fade_held) {
        flow->fade_held = false;
        return MP_SCENE_SEAT_ACT_FADE_IN;
    }
    flow->stage        = MP_SCENE_SEAT_RESPAWNING;
    flow->since        = look->now;
    flow->hard_spent   = true;
    flow->respawn_left = false;
    return MP_SCENE_SEAT_ACT_RESPAWN;
}

/* Out of tries: the hard way while it is left, otherwise the seat is given up. */
static mp_scene_seat_act_t the_last_way(mp_scene_seat_flow_t *flow,
                                        const mp_scene_seat_look_t *look)
{
    if (hard_way_left(flow, look)) {
        return take_the_hard_way(flow, look);
    }
    return give_up(flow, MP_SCENE_SEAT_END_TRIES);
}

/* A try that did not take: back to waiting, once more while tries are left. The screen stays dark
 * after a fade the body could not be moved at the end of, so a body that comes down is placed at
 * once rather than under a second fade; after a placement it did not take, the screen comes
 * back. */
static mp_scene_seat_act_t try_again(mp_scene_seat_flow_t *flow, const mp_scene_seat_look_t *look,
                                     bool screen_back)
{
    flow->stage = MP_SCENE_SEAT_WAITING;
    flow->since = look->now;
    if (flow->retries >= MP_SCENE_SEAT_RETRIES) {
        flow->tries_spent = true;
        return the_last_way(flow, look);
    }
    ++flow->retries;
    if (screen_back && flow->fade_held) {
        flow->fade_held = false;
        return MP_SCENE_SEAT_ACT_FADE_IN;
    }
    return MP_SCENE_SEAT_ACT_NONE;
}

static mp_scene_seat_act_t step_waiting(mp_scene_seat_flow_t *flow,
                                        const mp_scene_seat_look_t *look, uint32_t passed)
{
    if (!look->live) {
        return give_up(flow, MP_SCENE_SEAT_END_SCENE);
    }
    flow->mode_wait += look->may_move == MP_SCENE_MOVE_MODE ? passed : 0u;
    if (look->may_move != MP_SCENE_MOVE_YES) {
        flow->refused = look->may_move;
    }
    /* A player who dies in the dark sees his death: his re-entry is anchored on the seat, and a
     * try is not spent on it. */
    if (flow->gathering && look->may_move == MP_SCENE_MOVE_DEAD && flow->fade_held) {
        flow->fade_held = false;
        return MP_SCENE_SEAT_ACT_FADE_IN;
    }
    if (flow->tries_spent) {
        return the_last_way(flow, look);
    }
    if (look->may_move != MP_SCENE_MOVE_YES) {
        if (flow->wait_max != 0u && look->now - flow->since >= flow->wait_max) {
            flow->waited_out = true;
            return give_up(flow, MP_SCENE_SEAT_END_WAITED_OUT);
        }
        /* The screen is kept dark after a try only for a body about to come down or about to be
         * taken the hard way; with that way closed it comes back after the time a placement
         * gets, whatever keeps the body. */
        if (flow->fade_held && !hard_way_left(flow, look) &&
            look->now - flow->since >= MP_SCENE_PLACE_SUBSTEPS) {
            flow->fade_held = false;
            return MP_SCENE_SEAT_ACT_FADE_IN;
        }
        if (hard_way_due(flow, look)) {
            if (hard_way_left(flow, look)) {
                return take_the_hard_way(flow, look);
            }
            flow->hard_wanted = true;
        }
        return MP_SCENE_SEAT_ACT_NONE;
    }
    if (flow->fade_held) {
        flow->stage = MP_SCENE_SEAT_PLACED;
        flow->since = look->now;
        return MP_SCENE_SEAT_ACT_PLACE;
    }
    flow->stage      = MP_SCENE_SEAT_FADING;
    flow->since      = look->now;
    flow->fade_since = look->now;
    flow->fade_held  = true;
    return MP_SCENE_SEAT_ACT_FADE_OUT;
}

static mp_scene_seat_act_t step_fading(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look, uint32_t passed)
{
    if (!look->live) {
        return give_up(flow, MP_SCENE_SEAT_END_SCENE);
    }
    flow->mode_wait += look->may_move == MP_SCENE_MOVE_MODE ? passed : 0u;
    if (flow->gathering && look->may_move == MP_SCENE_MOVE_DEAD) {
        flow->refused   = MP_SCENE_MOVE_DEAD;
        flow->stage     = MP_SCENE_SEAT_WAITING;
        flow->since     = look->now;
        flow->fade_held = false;
        return MP_SCENE_SEAT_ACT_FADE_IN;
    }
    if (!look->fade_done &&
        look->now - flow->since < mp_scene_seat_fade_deadline(flow->fade_seconds)) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    flow->fade_on_clock = !look->fade_done;
    if (look->may_move != MP_SCENE_MOVE_YES) {
        flow->refused = look->may_move;
        if (!flow->gathering) {
            return give_up(flow, MP_SCENE_SEAT_END_DEADLINE);
        }
        return try_again(flow, look, false);
    }
    flow->stage = MP_SCENE_SEAT_PLACED;
    flow->since = look->now;
    return MP_SCENE_SEAT_ACT_PLACE;
}

static mp_scene_seat_act_t step_placed(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look)
{
    /* Handed over: the scene ending now does not take the seat back, the body is on its way. */
    if (look->at_seat) {
        flow->stage     = MP_SCENE_SEAT_DONE;
        flow->at_seat   = true;
        flow->fade_held = false;
        return MP_SCENE_SEAT_ACT_FADE_IN;
    }
    if (look->now - flow->since < MP_SCENE_PLACE_SUBSTEPS) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    if (!flow->gathering) {
        return give_up(flow, MP_SCENE_SEAT_END_DEADLINE);
    }
    return try_again(flow, look, true);
}

/* The engine's respawn: done once the module has left its running state, come back to it and the
 * body stands on the seat. A respawn the engine declined, or one that put the body elsewhere, ends
 * at its bound and the seat waits again, without the hard way, which is taken once. A scene that
 * wants the seat no longer leaves the respawn to land where it lands, with no fade of this flow. */
static mp_scene_seat_act_t step_respawning(mp_scene_seat_flow_t *flow,
                                           const mp_scene_seat_look_t *look)
{
    if (!look->live) {
        flow->stage = MP_SCENE_SEAT_IDLE;
        flow->ended = MP_SCENE_SEAT_END_SCENE;
        return MP_SCENE_SEAT_ACT_NONE;
    }
    flow->respawn_left = flow->respawn_left || !look->module_running;
    if (flow->respawn_left && look->module_running && look->at_seat) {
        flow->stage      = MP_SCENE_SEAT_DONE;
        flow->at_seat    = true;
        flow->by_respawn = true;
        return MP_SCENE_SEAT_ACT_NONE;
    }
    if (look->now - flow->since >= MP_SCENE_RESPAWN_SUBSTEPS) {
        flow->stage          = MP_SCENE_SEAT_WAITING;
        flow->since          = look->now;
        flow->respawn_failed = true;
    }
    return MP_SCENE_SEAT_ACT_NONE;
}

/* A seat to be kept that the body left, pushed or fallen off, is a try that did not take. */
static mp_scene_seat_act_t step_done(mp_scene_seat_flow_t *flow, const mp_scene_seat_look_t *look)
{
    if (!flow->gathering || !look->keep || !look->live || look->at_seat) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    flow->at_seat = false;
    return try_again(flow, look, false);
}

mp_scene_seat_act_t mp_scene_seat_step(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look)
{
    uint32_t passed;

    if (flow == NULL || look == NULL) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    passed            = look->now - flow->last;
    flow->last        = look->now;
    flow->hard_wanted = false;
    switch (flow->stage) {
    case MP_SCENE_SEAT_WAITING:
        return step_waiting(flow, look, passed);
    case MP_SCENE_SEAT_FADING:
        return step_fading(flow, look, passed);
    case MP_SCENE_SEAT_PLACED:
        return step_placed(flow, look);
    case MP_SCENE_SEAT_RESPAWNING:
        return step_respawning(flow, look);
    case MP_SCENE_SEAT_DONE:
        return step_done(flow, look);
    case MP_SCENE_SEAT_IDLE:
    case MP_SCENE_SEAT_GIVEN_UP:
    default:
        return MP_SCENE_SEAT_ACT_NONE;
    }
}

bool mp_scene_seat_respawn_under_way(const mp_scene_seat_flow_t *flow)
{
    return flow != NULL && flow->stage == MP_SCENE_SEAT_RESPAWNING && flow->respawn_left;
}

mp_scene_seat_act_t mp_scene_seat_leave(mp_scene_seat_flow_t *flow)
{
    bool held;

    if (flow == NULL) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    held            = flow->fade_held;
    flow->fade_held = false;
    flow->stage     = MP_SCENE_SEAT_IDLE;
    return held ? MP_SCENE_SEAT_ACT_FADE_IN : MP_SCENE_SEAT_ACT_NONE;
}

/* ==============================================================================================
 * The host's input while he is brought to the place of a scene.
 * ============================================================================================ */

/* While the hold stands: a host with a place in the fade, placed, at it, or waiting in the
 * dark. */
static mp_scene_input_t while_it_holds(const mp_scene_input_look_t *look)
{
    if (look->may_move == MP_SCENE_MOVE_DEAD) {
        return MP_SCENE_INPUT_DEAD;
    }
    if (!look->has_seat) {
        return MP_SCENE_INPUT_NONE;
    }
    switch (look->stage) {
    case MP_SCENE_SEAT_FADING:
    case MP_SCENE_SEAT_PLACED:
    case MP_SCENE_SEAT_DONE:
        return MP_SCENE_INPUT_HELD;
    case MP_SCENE_SEAT_WAITING:
        return look->fade_held ? MP_SCENE_INPUT_HELD : MP_SCENE_INPUT_TRY;
    case MP_SCENE_SEAT_IDLE:
    case MP_SCENE_SEAT_GIVEN_UP:
    case MP_SCENE_SEAT_RESPAWNING:
    default:
        return MP_SCENE_INPUT_TRY;
    }
}

mp_scene_input_t mp_scene_input_hold(const mp_scene_input_look_t *look)
{
    if (look == NULL || !look->hosting) {
        return MP_SCENE_INPUT_NOT_HOSTING;
    }
    if (look->phase == MP_SCENE_PHASE_GATHERING && look->holds) {
        return while_it_holds(look);
    }
    if (look->phase != MP_SCENE_PHASE_RUNNING) {
        return MP_SCENE_INPUT_OVER;
    }
    if (look->seen_running) {
        return MP_SCENE_INPUT_RUNS;
    }
    if (!look->held) {
        return MP_SCENE_INPUT_NONE;
    }
    return look->since_phase <= MP_SCENE_GRAB_GRACE_SUBSTEPS ? MP_SCENE_INPUT_HELD
                                                             : MP_SCENE_INPUT_NO_GRAB;
}

const char *mp_scene_input_text(mp_scene_input_t why)
{
    switch (why) {
    case MP_SCENE_INPUT_HELD:        return "it is held";
    case MP_SCENE_INPUT_RUNS:        return "the scene runs";
    case MP_SCENE_INPUT_NO_GRAB:     return "the grab did not show within its grace";
    case MP_SCENE_INPUT_OVER:        return "the scene is over";
    case MP_SCENE_INPUT_DEAD:        return "the host died";
    case MP_SCENE_INPUT_TRY:         return "a try did not take";
    case MP_SCENE_INPUT_NOT_HOSTING: return "this machine hosts no longer";
    case MP_SCENE_INPUT_NONE:
    case MP_SCENE_INPUTS:
    default:                         return "nothing holds it";
    }
}
