/* mp_scene_own.c: the host on his way to the place of a scene a far player set off. See
 * mp_scene_host.h and mp_scene_host_internal.h.
 *
 * The host is the actor of every scene, and a scene a far player set off is played where that
 * player's body was tested: the host is brought there under a short fade through the placement,
 * out of the three modes the engine's own grab parks a player from. A try that does not take is
 * tried again, twice; a host that stays in a mode the teleport may not move for a second is
 * brought by the engine's own respawn with his own hero, once, the way the engine itself takes a
 * player out of any mode for a script's warp. The hold of the scene waits for him at the place, so
 * the grab finds him there. Nobody else is moved.
 *
 * Two things about the code are worth having in front of a maintainer:
 *
 * The input hold is a level, decided by mp_scene_input_hold at the head of every substep and
 * again once the seat has stepped, so that a fade begun in this substep is already held for the
 * next player tick, and written only when it changes. Every way out of a scene, the warp that
 * takes it over included, ends in a substep the rule finds no reason in, so there is no edge to
 * miss.
 *
 * The grab's line is written from inside the engine, through the listener mp_cutscene calls, where
 * the hero block is this machine's own: no bank window is open in the enemy tick.
 */
#include "mp_scene_host_internal.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"
#include "mp_scene_room.h"
#include "mp_seat.h"
#include "mp_start.h"
#include "mp_target.h"

#include "common/logging.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct own_state {
    bool                 wanted;
    float                place[3];
    float                heading;
    mp_scene_seat_flow_t seat;
    uint32_t             began;            /* the substep the way began */
    uint32_t             fade_substeps;    /* from the fade out to the placement, measured */
    uint32_t             respawn_asked;    /* the substep the engine's respawn was asked */
    uint32_t             respawn_substeps; /* from it to the body at the place */
    bool                 was_dead;         /* the host lay dead on the way: his re-entry came */
    bool                 unread_seen;      /* waiting for a mode that did not read, this spell */
    bool                 said_hard_left;   /* the hard way due and not taken, said once */
    bool                 said_respawn_failed;
    bool                 named_set;
    bool                 input_held;
    bool                 counted_done;     /* the arrival of this scene is counted */
} own_state_t;

static own_state_t os;

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static bool module_reads(uint32_t *module)
{
    *module = 0u;
    return mp_scene_bind_module_state(module);
}

/* ==============================================================================================
 * The way to the place.
 * ============================================================================================ */

void mp_scene_own_start(const float place[3], float heading)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    memset(&os.seat, 0, sizeof os.seat);
    os.wanted = true;
    memcpy(os.place, place, sizeof os.place);
    os.heading             = heading;
    os.began               = sh->substep;
    os.fade_substeps       = 0u;
    os.respawn_asked       = 0u;
    os.respawn_substeps    = 0u;
    os.was_dead            = false;
    os.unread_seen         = false;
    os.said_hard_left      = false;
    os.said_respawn_failed = false;
    os.counted_done        = false;
    mp_scene_seat_start_gathering(&os.seat, sh->substep, MP_SCENE_FADE_SECONDS,
                                  MP_SCENE_HARD_HOST_SUBSTEPS);
}

static void do_seat(mp_scene_seat_act_t act)
{
    switch (act) {
    case MP_SCENE_SEAT_ACT_FADE_OUT:
        mp_scene_bind_fade_out(os.seat.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_PLACE:
        (void)mp_start_place_for_scene(os.place, os.heading);
        break;
    case MP_SCENE_SEAT_ACT_FADE_IN:
        mp_scene_bind_fade_in(os.seat.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_RESPAWN:
    case MP_SCENE_SEAT_ACT_NONE:
    default:
        break;
    }
}

/* The engine's respawn onto the place, and the line that says why. */
static void take_the_hard_way(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    bool                    asked;

    os.respawn_asked = sh->substep;
    asked            = mp_respawn_move_living(os.place, os.heading);
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    if (asked) {
        log_info("the host goes to the place of scene %u: by the engine's respawn, after %u "
                 "substep(s) in a mode the teleport may not move", (unsigned)sh->flow.serial,
                 (unsigned)os.seat.mode_wait);
    } else {
        log_warning("the host's way to the place of scene %u: the engine's respawn was refused at "
                    "the door of the respawn module, so the seat waits for it to land",
                    (unsigned)sh->flow.serial);
    }
}

/* The hard way was due and is not taken: at a gun, whose camera and turret the respawn would leave
 * behind, or because it was taken once already. Said once a scene. */
static void say_the_hard_way_left(void)
{
    mp_scene_host_shared_t *sh  = mp_scene_host_shared();
    bool                    gun = mp_scene_bind_mode_is_gun();

    if (os.said_hard_left) {
        return;
    }
    os.said_hard_left = true;
    sh->n.own_at_gun += gun ? 1u : 0u;
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    if (gun) {
        log_warning("the host stayed at a gun for scene %u: the engine's respawn would leave the "
                    "gun's camera and turret behind", (unsigned)sh->flow.serial);
    } else {
        log_info("the host's way to the place of scene %u: the engine's respawn is not taken "
                 "again, it %s, so the teleport alone waits for the host",
                 (unsigned)sh->flow.serial,
                 os.seat.hard_spent ? "was taken once already" : "may not take the host now");
    }
}

/* How a seat ended, as it ends. An arrival is counted once a scene: a host brought back to his
 * place after he was pushed off it arrived there before. */
static void count_the_end(mp_scene_seat_stage_t after)
{
    mp_scene_host_shared_t *sh   = mp_scene_host_shared();
    uint32_t                took = sh->substep - os.began;

    if (after == MP_SCENE_SEAT_DONE) {
        if (os.counted_done) {
            return;
        }
        os.counted_done = true;
        ++sh->n.seated;
        sh->n.own_by_respawn  += os.seat.by_respawn ? 1u : 0u;
        sh->n.own_by_teleport += os.seat.by_respawn ? 0u : 1u;
        sh->n.own_after_reentry += os.was_dead ? 1u : 0u;
        sh->n.own_longest = took > sh->n.own_longest ? took : sh->n.own_longest;
        return;
    }
    if (after != MP_SCENE_SEAT_GIVEN_UP && after != MP_SCENE_SEAT_IDLE) {
        return;
    }
    if (os.seat.ended == MP_SCENE_SEAT_END_TRIES) {
        ++sh->n.given_up;
    } else if (os.seat.refused == MP_SCENE_MOVE_DEAD) {
        ++sh->n.refused_dead;
    } else if (os.seat.refused == MP_SCENE_MOVE_MODE || os.seat.refused == MP_SCENE_MOVE_NO_BODY) {
        ++sh->n.refused_mode;
    } else if (os.seat.refused == MP_SCENE_MOVE_UNREAD) {
        ++sh->n.refused_unread;
    } else {
        ++sh->n.ran_first;
    }
}

/* The host at his place arrived there: by which way, after how long. */
static void say_the_arrival(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    if (os.seat.by_respawn || !mp_scene_host_line_allowed()) {
        return;
    }
    log_info("the host goes to the place of scene %u: by the teleport after %u substep(s) of "
             "waiting (%u retries)", (unsigned)sh->flow.serial,
             (unsigned)(os.seat.fade_since - os.began), (unsigned)os.seat.retries);
}

/* The seat's look: the body at the place by the box of the engine's own arrival test, which the
 * hero's first walk starts from. */
static void look_at_the_seat(mp_scene_seat_look_t *look)
{
    mp_scene_host_shared_t *sh     = mp_scene_host_shared();
    uint32_t                module = 0u;
    float                   body[3];

    memset(look, 0, sizeof *look);
    look->now            = sh->substep;
    look->live           = sh->flow.phase == MP_SCENE_PHASE_GATHERING;
    look->may_move       = mp_scene_bind_may_move();
    look->fade_done      = mp_scene_bind_fade_done();
    look->at_seat        = mp_target_player_position(0u, body) && mp_scene_at_place(body, os.place);
    look->hard_ready     = mp_scene_bind_hard_way_open();
    look->module_running = module_reads(&module) && module == MP_HERO_MODULE_RUNNING;
    look->keep           = sh->flow.holds;
}

void mp_scene_own_step(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    mp_scene_seat_look_t    look;
    mp_scene_seat_act_t     act;
    mp_scene_seat_stage_t   before;
    uint32_t                retries;

    if (!os.wanted) {
        return;
    }
    look_at_the_seat(&look);
    before  = os.seat.stage;
    retries = os.seat.retries;
    act     = mp_scene_seat_step(&os.seat, &look);
    os.was_dead = os.was_dead || look.may_move == MP_SCENE_MOVE_DEAD;
    if (look.may_move == MP_SCENE_MOVE_UNREAD && !os.unread_seen) {
        ++sh->n.own_unread;
    }
    os.unread_seen = look.may_move == MP_SCENE_MOVE_UNREAD;
    sh->n.own_retries += os.seat.retries - retries;
    if (act == MP_SCENE_SEAT_ACT_PLACE) {
        os.fade_substeps = sh->substep - os.seat.fade_since;
        sh->n.fades_on_clock += os.seat.fade_on_clock ? 1u : 0u;
    }
    if (act == MP_SCENE_SEAT_ACT_RESPAWN) {
        take_the_hard_way();
    }
    if (os.seat.hard_wanted) {
        say_the_hard_way_left();
    }
    if (os.seat.respawn_failed && !os.said_respawn_failed) {
        os.said_respawn_failed = true;
        if (mp_scene_host_line_allowed()) {
            log_warning("the engine's respawn did not bring the host to the place of scene %u "
                        "within %u substep(s), so the seat waits for the teleport again",
                        (unsigned)sh->flow.serial, (unsigned)MP_SCENE_RESPAWN_SUBSTEPS);
        }
    }
    if (os.seat.stage != before) {
        if (os.seat.stage == MP_SCENE_SEAT_DONE) {
            os.respawn_substeps = os.seat.by_respawn ? sh->substep - os.respawn_asked : 0u;
            say_the_arrival();
        }
        count_the_end(os.seat.stage);
    }
    do_seat(act);
}

/* ==============================================================================================
 * The questions the scene asks of the way.
 * ============================================================================================ */

bool mp_scene_own_wanted(void)
{
    return os.wanted;
}

bool mp_scene_own_away(void)
{
    uint32_t module = 0u;
    float    body[3];

    if (!os.wanted) {
        return false;
    }
    return os.seat.stage != MP_SCENE_SEAT_DONE || !module_reads(&module) ||
           module != MP_HERO_MODULE_RUNNING || !mp_target_player_position(0u, body) ||
           !mp_scene_at_place(body, os.place);
}

bool mp_scene_own_respawning(void)
{
    return os.wanted && mp_scene_seat_respawn_under_way(&os.seat);
}

/* While a scene of the host's stands and the host lies dead, his re-entry is anchored on the
 * scene: on the place he was to be brought to, and with no such place on where the scene is
 * played. For the whole of the scene and not only while its hold stands: a host who dies in the
 * middle of his own scene would otherwise come back beside a far player, who may be anywhere,
 * while the scene's actors go on measuring him where it is played. */
void mp_scene_own_name_the_anchor(bool stands)
{
    mp_scene_host_shared_t *sh    = mp_scene_host_shared();
    bool                    known = os.wanted || sh->anchor_known;
    bool                    want  = mp_scene_host_stands_now(&sh->flow) && known && !stands;

    if (want && !os.named_set) {
        mp_seat_name_anchor(os.wanted ? os.place : sh->anchor,
                            os.wanted ? os.heading : sh->heading);
        os.named_set = true;
        ++sh->n.named;
    } else if (!want && os.named_set) {
        mp_seat_name_anchor(NULL, 0.0f);
        os.named_set = false;
    }
}

void mp_scene_own_stage(char *out, size_t size)
{
    if (!os.wanted) {
        (void)text_format(out, size, "was not moved");
    } else if (os.seat.stage == MP_SCENE_SEAT_DONE && os.seat.by_respawn) {
        (void)text_format(out, size, "was brought by the engine's respawn after %u substep(s)",
                          (unsigned)os.respawn_substeps);
    } else if (os.seat.stage == MP_SCENE_SEAT_DONE) {
        (void)text_format(out, size, "stood at the place after %u substep(s) of fade",
                          (unsigned)os.fade_substeps);
    } else {
        (void)text_format(out, size, "was still waiting (the last refusal: %s)",
                          mp_scene_move_text(os.seat.refused));
    }
}

/* ==============================================================================================
 * The input hold.
 * ============================================================================================ */

static void hold_the_input(bool held, const char *why)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    os.input_held = held;
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_SCENE, held);
    if (held) {
        ++sh->n.input_held;
    }
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    if (held) {
        log_info("the host's input is held at the place of scene %u until it runs",
                 (unsigned)sh->flow.serial);
    } else {
        log_info("the host's input at the place of scene %u is let go: %s",
                 (unsigned)sh->flow.serial, why);
    }
}

/* The look of the rule. The host's mode is read only while a hold stands, where the rule asks
 * it. */
static void look_at_the_input(mp_scene_input_look_t *look)
{
    mp_scene_host_shared_t     *sh   = mp_scene_host_shared();
    const mp_scene_host_flow_t *flow = &sh->flow;

    memset(look, 0, sizeof *look);
    look->hosting      = sh->installed && mp_target_hosting();
    look->phase        = flow->phase;
    look->holds        = flow->holds;
    look->may_move     = MP_SCENE_MOVE_YES;
    look->has_seat     = os.wanted;
    look->stage        = os.seat.stage;
    look->fade_held    = os.seat.fade_held;
    look->held         = os.input_held;
    look->seen_running = flow->seen_running;
    look->since_phase  = sh->substep - flow->phase_since;
    if (look->hosting && flow->phase == MP_SCENE_PHASE_GATHERING && flow->holds) {
        look->may_move = mp_scene_bind_may_move();
    }
}

void mp_scene_own_hold_input(void)
{
    mp_scene_input_look_t look;
    mp_scene_input_t      why;

    look_at_the_input(&look);
    why = mp_scene_input_hold(&look);
    if ((why == MP_SCENE_INPUT_HELD) != os.input_held) {
        hold_the_input(why == MP_SCENE_INPUT_HELD, mp_scene_input_text(why));
    }
}

/* ==============================================================================================
 * The grab, and the way out.
 * ============================================================================================ */

void mp_scene_own_grabbed(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    const float            *place;
    float                   body[3];
    float                   off;

    if (!sh->installed || !mp_target_hosting() || sh->flow.serial == 0u ||
        sh->flow.phase == MP_SCENE_PHASE_NONE || !mp_target_player_position(0u, body)) {
        return;
    }
    /* Measured only against a place the hold fell at with the host on it: a host left where he
     * stood, with no place, every far player gone, the scene given up or one he set off himself,
     * was never brought to one. */
    place = !sh->released_at_place ? NULL : (os.wanted ? os.place : sh->anchor);
    if (place == NULL) {
        ++sh->n.grabs_left;
        if (mp_scene_host_line_allowed()) {
            log_info("the hero of scene %u was taken at %.2f %.2f %.2f, with no place the host "
                     "was brought to", (unsigned)sh->flow.serial, (double)body[0],
                     (double)body[1], (double)body[2]);
        }
        return;
    }
    off = distance(body, place);
    ++sh->n.grabs_at_place;
    sh->n.grabs_off += off >= MP_SCENE_ARRIVED_DISTANCE ? 1u : 0u;
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    if (off < MP_SCENE_ARRIVED_DISTANCE) {
        log_info("the hero of scene %u was taken at %.2f %.2f %.2f, %.2f u from the place the "
                 "host was brought to", (unsigned)sh->flow.serial, (double)body[0],
                 (double)body[1], (double)body[2], (double)off);
    } else {
        log_warning("the hero of scene %u was taken at %.2f %.2f %.2f, %.2f u from the place "
                    "the host was brought to", (unsigned)sh->flow.serial, (double)body[0],
                    (double)body[1], (double)body[2], (double)off);
    }
}

void mp_scene_own_forget(mp_scene_own_end_t why)
{
    do_seat(mp_scene_seat_leave(&os.seat));
    if (os.named_set) {
        mp_seat_name_anchor(NULL, 0.0f);
    }
    if (os.input_held) {
        hold_the_input(false, why == MP_SCENE_OWN_END_WARP   ? "a warp took the scene over"
                              : why == MP_SCENE_OWN_END_NEXT ? "another scene begins"
                                                             : "the scene ended");
    }
    memset(&os, 0, sizeof os);
}
