/* The host at the place of a scene a far player set off, and a gathering's seat on its way: the
 * hold that waits for the host at his place and falls the moment he stands there, the place
 * itself, and a seat that is tried again and taken the hard way rather than given up.
 *
 * The machines are mp_scene_flow's and the rule of the place mp_scene_room's; mp_scene_flow.c in
 * this directory walks the machines along their paths, mp_scene_flow_wait.c the bounds of their
 * waits. What is here is the host taking the place, each case driven the way the host's binding
 * drives it.
 *
 * SIZE NOTE: over 600 lines. The hold and a gathering's seat are walked together because the seat
 * is what keeps the host away from his place; the seam, when it grows, is the seat's half, from
 * check_a_try_that_did_not_take to check_every_gathering_sequence, into a file of its own.
 */
#include "unittest.h"

#include "mp_scene_flow.h"
#include "mp_scene_room.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The hold.
 * ============================================================================================ */

typedef struct hold_case {
    bool stands;
    bool has_place;
    bool away;
    bool pending;
    bool respawning;
    bool alone;
    bool no_place;
} hold_case_t;

static mp_scene_host_look_t hold_look(uint32_t now, const hold_case_t *c)
{
    mp_scene_host_look_t look;

    memset(&look, 0, sizeof look);
    look.now            = now;
    look.host_stands    = c->stands;
    look.has_place      = c->has_place;
    look.away           = c->away;
    look.place_pending  = c->pending;
    look.own_respawning = c->respawning;
    look.alone          = c->alone;
    look.no_place       = c->no_place;
    look.may_move       = c->stands ? MP_SCENE_MOVE_MODE : MP_SCENE_MOVE_DEAD;
    return look;
}

/* Steps the scene one look a substep from `from` to `to` with the same case; the substep its
 * hold fell in, or 0 when it still stands. */
static uint32_t hold_until(mp_scene_host_flow_t *flow, uint32_t from, uint32_t to,
                           const hold_case_t *c)
{
    uint32_t now;

    for (now = from; now <= to; ++now) {
        mp_scene_host_look_t look = hold_look(now, c);

        (void)mp_scene_host_step(flow, &look);
        if (!flow->holds) {
            return now;
        }
    }
    return 0u;
}

/* A scene a far player set off: held from the door. */
static void begin(mp_scene_host_flow_t *flow, mp_scene_kind_t kind)
{
    memset(flow, 0, sizeof *flow);
    (void)mp_scene_host_begin(flow, kind, 0u, true);
}

static void check_the_hold_waits_for_the_host_at_his_place(void)
{
    mp_scene_host_flow_t flow;
    hold_case_t          c;

    ut_section("a far player set the scene off: the hold waits for the host at his place");
    memset(&c, 0, sizeof c);
    c.stands    = true;
    c.has_place = true;
    c.away      = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, 48u, &c) == 0u && hold_until(&flow, 49u, 96u, &c) == 0u &&
                 hold_until(&flow, 97u, 600u, &c) == 0u,
             "the host away from his place: no fall at 48, at 96 nor at 600");

    begin(&flow, MP_SCENE_KIND_HERO);
    (void)hold_until(&flow, 1u, 99u, &c);
    c.away = false;
    ut_checkf(hold_until(&flow, 100u, 100u, &c) == 100u &&
                  flow.released == MP_SCENE_RELEASE_AT_THE_PLACE,
              "the host at his place: it falls on that look, nobody else is waited for "
              "(released %u)", (unsigned)flow.released);

    ut_section("a host pushed off his place before the look is waited for again");
    begin(&flow, MP_SCENE_KIND_LOCK);
    c.away = true;
    (void)hold_until(&flow, 1u, 30u, &c);
    ut_check(flow.holds, "thirty looks away: held");
    c.pending = true;
    c.away    = false;
    ut_check(hold_until(&flow, 31u, 40u, &c) == 0u,
             "at a place that is still being read: held, the place may yet be another");
    c.pending = false;
    ut_check(hold_until(&flow, 41u, 41u, &c) == 41u,
             "the place settled and he is at it: it falls");

    ut_section("a held scene whose host stays, with no place to go to, falls at the first look");
    memset(&c, 0, sizeof c);
    c.stands = true;
    begin(&flow, MP_SCENE_KIND_LOCK);
    ut_check(hold_until(&flow, 1u, 1u, &c) == 1u &&
                 flow.released == MP_SCENE_RELEASE_AT_THE_PLACE,
             "a host who stands where the scene is played already is at his place");
    begin(&flow, MP_SCENE_KIND_LOCK);
    c.stands = false;
    ut_check(hold_until(&flow, 1u, 20u, &c) == 0u && flow.hold_dead == 20u,
             "the same host dead is waited for, his dead time counted");
    c.stands = true;
    ut_check(hold_until(&flow, 21u, 21u, &c) == 21u && flow.hold_standing == 1u,
             "and released on the look he stands again");
}

static void check_alone_no_place_and_the_place_read_again(void)
{
    mp_scene_host_flow_t flow;
    hold_case_t          c;

    ut_section("alone, no place after the place was read again, and the wait for it");
    memset(&c, 0, sizeof c);
    c.stands = true;
    c.alone  = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, 1u, &c) == 1u && flow.released == MP_SCENE_RELEASE_ALONE,
             "with every far player gone and no place it falls at once");

    c.has_place = true;
    c.away      = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, 200u, &c) == 0u,
             "with every far player gone and the host on his way it does not fall: he "
             "finishes the way");
    c.away = false;
    ut_check(hold_until(&flow, 201u, 201u, &c) == 201u &&
                 flow.released == MP_SCENE_RELEASE_AT_THE_PLACE,
             "and is released at the place");

    memset(&c, 0, sizeof c);
    c.stands  = true;
    c.pending = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, MP_SCENE_PLACE_WAIT_SUBSTEPS, &c) == 0u,
             "while the place is read again the hold stands, whatever else is true");
    c.pending  = false;
    c.no_place = true;
    ut_check(hold_until(&flow, MP_SCENE_PLACE_WAIT_SUBSTEPS + 1u,
                        MP_SCENE_PLACE_WAIT_SUBSTEPS + 1u, &c) != 0u &&
                 flow.released == MP_SCENE_RELEASE_NOBODY,
             "and once the host has no place it falls on the next look");
}

static void check_the_cap(void)
{
    mp_scene_host_flow_t flow;
    hold_case_t          c;

    ut_section("the cap: given up with the host away, waiting while the respawn brings him");
    memset(&c, 0, sizeof c);
    c.stands    = true;
    c.has_place = true;
    c.away      = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    (void)hold_until(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS - 1u, &c);
    ut_check(flow.holds && !flow.given_up, "a substep before the cap the hold stands");
    (void)hold_until(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS, MP_SCENE_WAIT_CAP_SUBSTEPS, &c);
    ut_check(flow.given_up && flow.given_up_for == MP_SCENE_MOVE_MODE,
             "at the cap with the host away it is given up, the host where he could not be moved");

    begin(&flow, MP_SCENE_KIND_HERO);
    (void)hold_until(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS - 10u, &c);
    c.respawning = true;
    (void)hold_until(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS - 9u, MP_SCENE_WAIT_CAP_SUBSTEPS + 60u, &c);
    ut_check(flow.holds && !flow.given_up,
             "while the engine's respawn brings him, the module dying or respawning, the cap "
             "waits");
    c.respawning = false;
    (void)hold_until(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS + 61u, MP_SCENE_WAIT_CAP_SUBSTEPS + 61u, &c);
    ut_check(flow.given_up, "and once the module is read running again the cap is the cap");

    ut_section("a respawn the engine declined in silence leaves the cap as it is");
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS, &c) == MP_SCENE_WAIT_CAP_SUBSTEPS &&
                 flow.given_up,
             "the module never left its running state, so nothing waits past the cap");

    ut_section("the host arrives at 600: released there, and the grab still has its grace");
    begin(&flow, MP_SCENE_KIND_HERO);
    (void)hold_until(&flow, 1u, 599u, &c);
    c.away = false;
    ut_checkf(hold_until(&flow, 600u, 600u, &c) == 600u && !flow.given_up &&
                  flow.released == MP_SCENE_RELEASE_AT_THE_PLACE &&
                  flow.phase == MP_SCENE_PHASE_RUNNING,
              "released on the look he arrives (released %u)", (unsigned)flow.released);
    {
        uint32_t now;
        bool     waits = true;

        for (now = 601u; now < 600u + MP_SCENE_GRAB_GRACE_SUBSTEPS; ++now) {
            mp_scene_host_look_t look = hold_look(now, &c);

            (void)mp_scene_host_step(&flow, &look);
            waits = waits && !flow.given_up && flow.phase == MP_SCENE_PHASE_RUNNING;
        }
        ut_check(waits, "past the cap the released scene still waits the grab's grace, to 663");
        {
            mp_scene_host_look_t look = hold_look(now, &c);

            (void)mp_scene_host_step(&flow, &look);
        }
        ut_checkf(flow.given_up && now == 664u,
                  "and a grab that never shows for a host it cannot take gives it up at %u",
                  (unsigned)now);
    }

    ut_section("a dead host with a place is waited for, and given up at the cap");
    memset(&c, 0, sizeof c);
    c.has_place = true;
    c.away      = true;
    begin(&flow, MP_SCENE_KIND_HERO);
    ut_check(hold_until(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS, &c) == MP_SCENE_WAIT_CAP_SUBSTEPS &&
                 flow.given_up_for == MP_SCENE_MOVE_DEAD,
             "given up with the host dead");
}

/* ==============================================================================================
 * A gathering's seat.
 * ============================================================================================ */

typedef struct seat_case {
    bool            live;
    mp_scene_move_t move;
    bool            faded;
    bool            at_seat;
    bool            hard_ready;
    bool            running;
    bool            keep;
} seat_case_t;

static seat_case_t movable(void)
{
    seat_case_t c;

    memset(&c, 0, sizeof c);
    c.live    = true;
    c.move    = MP_SCENE_MOVE_YES;
    c.running = true;
    return c;
}

static mp_scene_seat_act_t seat_step(mp_scene_seat_flow_t *flow, uint32_t now,
                                     const seat_case_t *c)
{
    mp_scene_seat_look_t look;

    memset(&look, 0, sizeof look);
    look.now            = now;
    look.live           = c->live;
    look.may_move       = c->move;
    look.fade_done      = c->faded;
    look.at_seat        = c->at_seat;
    look.hard_ready     = c->hard_ready;
    look.module_running = c->running;
    look.keep           = c->keep;
    return mp_scene_seat_step(flow, &look);
}

static void start_gathering(mp_scene_seat_flow_t *flow)
{
    mp_scene_seat_start_gathering(flow, 0u, MP_SCENE_FADE_SECONDS, MP_SCENE_HARD_HOST_SUBSTEPS);
}

static void check_a_try_that_did_not_take(void)
{
    mp_scene_seat_flow_t flow;
    seat_case_t          c = movable();

    ut_section("a fade that ends with the body in the air: tried again, the screen kept dark");
    start_gathering(&flow);
    ut_check(seat_step(&flow, 1u, &c) == MP_SCENE_SEAT_ACT_FADE_OUT, "the screen goes dark");
    c.move  = MP_SCENE_MOVE_MODE;
    c.faded = true;
    ut_check(seat_step(&flow, 2u, &c) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_WAITING && flow.fade_held && flow.retries == 1u,
             "jumped in the dark: back to waiting, still dark, one try spent");
    c.move = MP_SCENE_MOVE_YES;
    ut_check(seat_step(&flow, 3u, &c) == MP_SCENE_SEAT_ACT_PLACE &&
                 flow.stage == MP_SCENE_SEAT_PLACED,
             "landed: placed at once, with no second fade out");
    c.at_seat = true;
    ut_check(seat_step(&flow, 4u, &c) == MP_SCENE_SEAT_ACT_FADE_IN && !flow.fade_held &&
                 flow.stage == MP_SCENE_SEAT_DONE,
             "on the seat: the one fade in");

    ut_section("a placement the body did not take: tried again with the screen back");
    start_gathering(&flow);
    c = movable();
    c.faded = true;
    (void)seat_step(&flow, 1u, &c);
    (void)seat_step(&flow, 2u, &c);
    ut_check(seat_step(&flow, 2u + MP_SCENE_PLACE_SUBSTEPS, &c) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_WAITING && flow.retries == 1u && !flow.fade_held,
             "no arrival in its time: waiting again, a try spent, the screen back");

    ut_section("out of tries: the hard way while it is left, the seat given up otherwise");
    start_gathering(&flow);
    c = movable();
    c.faded = true;
    {
        uint32_t now  = 1u;
        int      tries;

        for (tries = 0; tries < 3; ++tries) {
            c.at_seat = false;
            (void)seat_step(&flow, now++, &c);
            (void)seat_step(&flow, now++, &c);
            now += MP_SCENE_PLACE_SUBSTEPS;
            (void)seat_step(&flow, now++, &c);
        }
        ut_checkf(flow.stage == MP_SCENE_SEAT_GIVEN_UP && flow.ended == MP_SCENE_SEAT_END_TRIES &&
                      flow.retries == MP_SCENE_SEAT_RETRIES && !flow.fade_held,
                  "three placements not taken, the hard way closed: given up after its tries "
                  "(stage %u, retries %u)", (unsigned)flow.stage, (unsigned)flow.retries);
    }
    start_gathering(&flow);
    c.hard_ready = true;
    {
        uint32_t            now = 1u;
        int                 tries;
        mp_scene_seat_act_t act = MP_SCENE_SEAT_ACT_NONE;

        for (tries = 0; tries < 3; ++tries) {
            (void)seat_step(&flow, now++, &c);
            (void)seat_step(&flow, now++, &c);
            now += MP_SCENE_PLACE_SUBSTEPS;
            act = seat_step(&flow, now++, &c);
        }
        ut_check(act == MP_SCENE_SEAT_ACT_FADE_IN && flow.stage == MP_SCENE_SEAT_WAITING,
                 "with the hard way open, the screen comes back first");
        ut_check(seat_step(&flow, now, &c) == MP_SCENE_SEAT_ACT_RESPAWN &&
                     flow.stage == MP_SCENE_SEAT_RESPAWNING && flow.hard_spent,
                 "and the next look asks the engine's respawn");
    }
}

static void check_the_hard_way(void)
{
    mp_scene_seat_flow_t flow;
    seat_case_t          c = movable();
    uint32_t             now;
    bool                 quiet = true;

    ut_section("the hard way after a second in a mode the teleport may not move");
    start_gathering(&flow);
    c.move       = MP_SCENE_MOVE_MODE;
    c.hard_ready = true;
    for (now = 1u; now < MP_SCENE_HARD_HOST_SUBSTEPS; ++now) {
        quiet = quiet && seat_step(&flow, now, &c) == MP_SCENE_SEAT_ACT_NONE;
    }
    ut_check(quiet && flow.stage == MP_SCENE_SEAT_WAITING, "31 substeps swimming: it waits");
    ut_check(seat_step(&flow, MP_SCENE_HARD_HOST_SUBSTEPS, &c) == MP_SCENE_SEAT_ACT_RESPAWN,
             "the 32nd: the engine's respawn, with its own hero");

    ut_section("the clock runs over every try");
    start_gathering(&flow);
    c = movable();
    c.hard_ready = true;
    c.move       = MP_SCENE_MOVE_MODE;
    for (now = 1u; now <= 20u; ++now) {
        (void)seat_step(&flow, now, &c);
    }
    c.move = MP_SCENE_MOVE_YES;
    (void)seat_step(&flow, 21u, &c);
    c.move = MP_SCENE_MOVE_MODE;
    for (now = 22u; now <= 21u + mp_scene_seat_fade_deadline(MP_SCENE_FADE_SECONDS); ++now) {
        (void)seat_step(&flow, now, &c);
    }
    ut_checkf(flow.stage == MP_SCENE_SEAT_WAITING && flow.fade_held && flow.retries == 1u &&
                  flow.mode_wait >= MP_SCENE_HARD_HOST_SUBSTEPS,
              "twenty substeps waiting and the fade in a jump: %u counted, one try spent",
              (unsigned)flow.mode_wait);
    ut_check(seat_step(&flow, now++, &c) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 seat_step(&flow, now++, &c) == MP_SCENE_SEAT_ACT_RESPAWN,
             "the hard way due: the screen back, then the respawn");

    ut_section("never the hard way while the body is dead, has no body, or its mode did not read");
    {
        static const mp_scene_move_t NOT[3] = { MP_SCENE_MOVE_DEAD, MP_SCENE_MOVE_NO_BODY,
                                                MP_SCENE_MOVE_UNREAD };
        size_t i;

        for (i = 0u; i < 3u; ++i) {
            bool never = true;

            start_gathering(&flow);
            c            = movable();
            c.hard_ready = true;
            c.move       = NOT[i];
            for (now = 1u; now <= 400u; ++now) {
                never = never && seat_step(&flow, now, &c) != MP_SCENE_SEAT_ACT_RESPAWN;
            }
            ut_checkf(never && flow.stage == MP_SCENE_SEAT_WAITING && flow.mode_wait == 0u,
                      "%s: 400 substeps waited, no respawn", mp_scene_move_text(NOT[i]));
        }
    }

    ut_section("at a gun the hard way is not taken, and it says it wanted to be");
    start_gathering(&flow);
    c            = movable();
    c.move       = MP_SCENE_MOVE_MODE;
    c.hard_ready = false;
    for (now = 1u; now <= 100u; ++now) {
        (void)seat_step(&flow, now, &c);
    }
    ut_check(flow.stage == MP_SCENE_SEAT_WAITING && flow.hard_wanted && !flow.hard_spent,
             "the way closed by the binding: waiting, the hard way wanted and not taken");
}

/* A try that keeps the screen dark waits for the body to come down only while the hard way could
 * take it; with that way closed, and for a mode that did not read, the screen comes back after
 * the time a placement gets. */
static void check_the_dark_wait(void)
{
    static const mp_scene_move_t STUCK[2] = { MP_SCENE_MOVE_MODE, MP_SCENE_MOVE_UNREAD };
    size_t                       i;

    ut_section("waiting in the dark ends with the screen back when the hard way is closed");
    for (i = 0u; i < 2u; ++i) {
        mp_scene_seat_flow_t flow;
        seat_case_t          c = movable();
        uint32_t             now;
        bool                 dark = true;

        start_gathering(&flow);
        c.faded = true;
        (void)seat_step(&flow, 1u, &c);
        c.move = MP_SCENE_MOVE_MODE;
        (void)seat_step(&flow, 2u, &c);
        c.move = STUCK[i];
        for (now = 3u; now < 2u + MP_SCENE_PLACE_SUBSTEPS; ++now) {
            dark = dark && seat_step(&flow, now, &c) == MP_SCENE_SEAT_ACT_NONE && flow.fade_held;
        }
        ut_checkf(dark && flow.stage == MP_SCENE_SEAT_WAITING,
                  "%s: dark for the time a placement gets", mp_scene_move_text(STUCK[i]));
        ut_checkf(seat_step(&flow, now, &c) == MP_SCENE_SEAT_ACT_FADE_IN && !flow.fade_held &&
                      flow.stage == MP_SCENE_SEAT_WAITING,
                  "%s: then the screen back, still waiting", mp_scene_move_text(STUCK[i]));
    }
}

static void check_the_respawn(void)
{
    mp_scene_seat_flow_t flow;
    seat_case_t          c = movable();
    uint32_t             now;

    ut_section("the respawn is done once the module left, came back and the body stands there");
    start_gathering(&flow);
    c.move       = MP_SCENE_MOVE_MODE;
    c.hard_ready = true;
    for (now = 1u; now <= MP_SCENE_HARD_HOST_SUBSTEPS; ++now) {
        (void)seat_step(&flow, now, &c);
    }
    ut_check(flow.stage == MP_SCENE_SEAT_RESPAWNING && !mp_scene_seat_respawn_under_way(&flow),
             "asked, and not under way before the module left its running state");
    c.at_seat = true;
    (void)seat_step(&flow, now++, &c);
    ut_check(flow.stage == MP_SCENE_SEAT_RESPAWNING,
             "the body on the seat with the module never gone: not yet, the respawn has not run");
    c.running = false;
    c.at_seat = false;
    (void)seat_step(&flow, now++, &c);
    ut_check(mp_scene_seat_respawn_under_way(&flow), "under way once the module left");
    c.running = true;
    (void)seat_step(&flow, now++, &c);
    ut_check(flow.stage == MP_SCENE_SEAT_RESPAWNING && mp_scene_seat_respawn_under_way(&flow),
             "back running, not on the seat: not yet, and still under way, so the cap waits for "
             "the substep the body stands there");
    c.at_seat = true;
    ut_check(seat_step(&flow, now++, &c) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_DONE && flow.by_respawn && !flow.fade_held,
             "on the seat after the round trip: done, by the respawn, no fade of this flow");

    ut_section("a respawn that never lands ends at its bound, and is not taken again");
    start_gathering(&flow);
    c = movable();
    c.move       = MP_SCENE_MOVE_MODE;
    c.hard_ready = true;
    for (now = 1u; now <= MP_SCENE_HARD_HOST_SUBSTEPS; ++now) {
        (void)seat_step(&flow, now, &c);
    }
    {
        bool never = flow.stage == MP_SCENE_SEAT_RESPAWNING;
        int  i;

        for (i = 0; i < 16; ++i) {
            (void)seat_step(&flow, now++, &c);
            never = never && flow.stage == MP_SCENE_SEAT_RESPAWNING &&
                    !mp_scene_seat_respawn_under_way(&flow);
        }
        ut_check(never, "asked with the module running all along: never under way, so a respawn "
                        "the engine declined in silence never holds the cap");
    }
    now += MP_SCENE_RESPAWN_SUBSTEPS;
    (void)seat_step(&flow, now++, &c);
    ut_check(flow.stage == MP_SCENE_SEAT_WAITING && flow.respawn_failed,
             "back to waiting, the line due");
    ut_check(seat_step(&flow, now++, &c) == MP_SCENE_SEAT_ACT_NONE && flow.hard_wanted &&
                 flow.stage == MP_SCENE_SEAT_WAITING,
             "due again and not taken: once a scene");

    ut_section("a scene that wants the seat no longer leaves the respawn alone");
    start_gathering(&flow);
    c = movable();
    c.move       = MP_SCENE_MOVE_MODE;
    c.hard_ready = true;
    for (now = 1u; now <= MP_SCENE_HARD_HOST_SUBSTEPS; ++now) {
        (void)seat_step(&flow, now, &c);
    }
    c.live = false;
    ut_check(seat_step(&flow, now, &c) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_IDLE && flow.ended == MP_SCENE_SEAT_END_SCENE,
             "idle, with no fade to give back");
}

static void check_a_death_and_a_seat_left(void)
{
    mp_scene_seat_flow_t flow;
    seat_case_t          c = movable();

    ut_section("a death in the dark: the screen back, waiting, no try spent");
    start_gathering(&flow);
    (void)seat_step(&flow, 1u, &c);
    c.move = MP_SCENE_MOVE_DEAD;
    ut_check(seat_step(&flow, 2u, &c) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_WAITING && flow.retries == 0u,
             "dead in the fade: waiting with the screen back");

    start_gathering(&flow);
    c       = movable();
    c.faded = true;
    (void)seat_step(&flow, 1u, &c);
    c.move = MP_SCENE_MOVE_MODE;
    (void)seat_step(&flow, 2u, &c);
    c.move = MP_SCENE_MOVE_DEAD;
    ut_check(seat_step(&flow, 3u, &c) == MP_SCENE_SEAT_ACT_FADE_IN && flow.retries == 1u &&
                 flow.stage == MP_SCENE_SEAT_WAITING,
             "dead while waiting in the dark: the screen back, and the try already spent stays "
             "one");

    ut_section("a seat to be kept that the body left is a try that did not take");
    start_gathering(&flow);
    c         = movable();
    c.faded   = true;
    c.keep    = true;
    (void)seat_step(&flow, 1u, &c);
    (void)seat_step(&flow, 2u, &c);
    c.at_seat = true;
    (void)seat_step(&flow, 3u, &c);
    c.at_seat = false;
    ut_check(seat_step(&flow, 4u, &c) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_WAITING && flow.retries == 1u,
             "pushed off the place while the hold stands: waiting again, a try spent");
    start_gathering(&flow);
    c.keep    = false;
    c.at_seat = false;
    (void)seat_step(&flow, 1u, &c);
    (void)seat_step(&flow, 2u, &c);
    c.at_seat = true;
    (void)seat_step(&flow, 3u, &c);
    c.at_seat = false;
    (void)seat_step(&flow, 4u, &c);
    ut_check(flow.stage == MP_SCENE_SEAT_DONE, "a seat not to be kept stays done");
}

/* The inputs of one look. G_MODE_FADED is a body in a mode the teleport may not move as the fade
 * ends, the try that keeps the screen dark; G_MODE_LATE is forty substeps of it with the hard way
 * closed, which fills the hard way's clock and runs out the dark wait. */
enum {
    G_MODE, G_MOVE, G_DEAD, G_FADED, G_AT_SEAT, G_ENDED, G_LATE, G_LEAVE, G_MODULE_OFF,
    G_MODE_FADED, G_MODE_LATE, G_INPUTS
};

#define G_DEPTH 7

typedef struct reached {
    unsigned respawn;       /* the hard way asked */
    unsigned tries_spent;   /* out of tries */
    unsigned dark_waiting;  /* waiting with the screen held dark */
} reached_t;

static seat_case_t gathering_input(int input)
{
    seat_case_t c = movable();

    c.hard_ready = input != G_MODE_LATE;
    c.keep       = true;
    c.live       = input != G_ENDED;
    c.move       = input == G_DEAD ? MP_SCENE_MOVE_DEAD
                 : (input == G_MODE || input == G_MODE_FADED || input == G_MODE_LATE)
                     ? MP_SCENE_MOVE_MODE
                     : MP_SCENE_MOVE_YES;
    c.faded      = input == G_FADED || input == G_AT_SEAT || input == G_MODE_FADED;
    c.at_seat    = input == G_AT_SEAT;
    c.running    = input != G_MODULE_OFF;
    return c;
}

/* One sequence; true when every fade out was answered by exactly one fade in and no respawn was
 * asked in the dark. */
static bool one_sequence(unsigned code, reached_t *reached)
{
    mp_scene_seat_flow_t flow;
    uint32_t             now  = 0u;
    unsigned             rest = code;
    int                  dark = 0;
    bool                 good = true;
    int                  depth;

    start_gathering(&flow);
    for (depth = 0; depth < G_DEPTH; ++depth) {
        int                 input = (int)(rest % (unsigned)G_INPUTS);
        seat_case_t         c     = gathering_input(input);
        mp_scene_seat_act_t act;

        rest /= (unsigned)G_INPUTS;
        now += input == G_LATE ? 200u : (input == G_MODE_LATE ? 40u : 1u);
        act = input == G_LEAVE ? mp_scene_seat_leave(&flow) : seat_step(&flow, now, &c);
        dark += act == MP_SCENE_SEAT_ACT_FADE_OUT ? 1 : 0;
        dark -= act == MP_SCENE_SEAT_ACT_FADE_IN ? 1 : 0;
        reached->respawn += act == MP_SCENE_SEAT_ACT_RESPAWN ? 1u : 0u;
        reached->tries_spent += flow.tries_spent ? 1u : 0u;
        reached->dark_waiting += flow.stage == MP_SCENE_SEAT_WAITING && flow.fade_held ? 1u : 0u;
        if (dark < 0 || dark > 1 || (dark == 1) != flow.fade_held ||
            (act == MP_SCENE_SEAT_ACT_RESPAWN && dark != 0)) {
            good = false;
        }
    }
    dark -= mp_scene_seat_leave(&flow) == MP_SCENE_SEAT_ACT_FADE_IN ? 1 : 0;
    return good && dark == 0 && !flow.fade_held;
}

static void check_every_gathering_sequence(void)
{
    unsigned  total = 1u;
    unsigned  code;
    unsigned  bad = 0u;
    reached_t reached;
    int       depth;

    memset(&reached, 0, sizeof reached);
    for (depth = 0; depth < G_DEPTH; ++depth) {
        total *= (unsigned)G_INPUTS;
    }
    ut_section("every sequence of a gathering seat's inputs up to seven long gives every fade "
               "back");
    for (code = 0u; code < total; ++code) {
        bad += one_sequence(code, &reached) ? 0u : 1u;
    }
    ut_checkf(bad == 0u, "%u sequence(s): a fade out not answered by exactly one fade in, or a "
              "respawn asked in the dark, %u time(s)", total, bad);
    ut_checkf(reached.respawn > 0u && reached.tries_spent > 0u && reached.dark_waiting > 0u,
              "and the walk reaches the new ways: the respawn %u, out of tries %u, waiting in "
              "the dark %u look(s)", reached.respawn, reached.tries_spent,
              reached.dark_waiting);
}

/* ==============================================================================================
 * The place, and what still runs.
 * ============================================================================================ */

static void check_the_place(void)
{
    ut_section("the place of the far player the script meant");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_FOUND, true, 0u) == MP_SCENE_PLACE_TAKE,
             "on the floor: the host takes it");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVING, true, 0u) ==
                     MP_SCENE_PLACE_READ_AGAIN &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVING, true,
                                     MP_SCENE_PLACE_WAIT_SUBSTEPS - 1u) ==
                     MP_SCENE_PLACE_READ_AGAIN,
             "in the air or over water: read again for a second");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVING, true,
                                 MP_SCENE_PLACE_WAIT_SUBSTEPS) == MP_SCENE_PLACE_BESIDE_ACTOR &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVING, false,
                                     MP_SCENE_PLACE_WAIT_SUBSTEPS) == MP_SCENE_PLACE_NOBODY,
             "after it: beside the actor for a hero scene, no place for a lock");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVER, true, 0u) == MP_SCENE_PLACE_NOBODY &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READ_MOVER, false, 0u) ==
                     MP_SCENE_PLACE_NOBODY,
             "on a mover: no place at once, no reading again and no actor, for either kind");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_UNREAD, true, 0u) ==
                     MP_SCENE_PLACE_BESIDE_ACTOR &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READ_UNREAD, false, 0u) ==
                     MP_SCENE_PLACE_NOBODY,
             "his body unread: beside the actor at once for a hero scene, no place for a lock");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_NONE_FREE, true, 0u) ==
                     MP_SCENE_PLACE_BESIDE_ACTOR &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READ_NONE_FREE, false, 0u) ==
                     MP_SCENE_PLACE_NOBODY,
             "nothing free on it or around it: the same");
    ut_check(mp_scene_place_rule(MP_SCENE_PLACE_READ_NO_PROBES, true, 0u) ==
                     MP_SCENE_PLACE_NOBODY &&
                 mp_scene_place_rule(MP_SCENE_PLACE_READS, true, 0u) == MP_SCENE_PLACE_NOBODY,
             "no probes to search with, or a reading this rule does not know: no place");
    {
        mp_scene_place_read_t a;
        mp_scene_place_read_t b;
        bool                  apart = true;

        for (a = MP_SCENE_PLACE_READ_FOUND; a < MP_SCENE_PLACE_READ_NO_PROBES;
             a = (mp_scene_place_read_t)(a + 1)) {
            for (b = (mp_scene_place_read_t)(a + 1); b <= MP_SCENE_PLACE_READ_NO_PROBES;
                 b = (mp_scene_place_read_t)(b + 1)) {
                apart = apart && strcmp(mp_scene_place_text(a), mp_scene_place_text(b)) != 0;
            }
        }
        ut_check(apart, "every reading has words of its own for the host's line");
    }

    ut_section("what stands, and what is still running as a level ends");
    {
        mp_scene_host_flow_t flow;

        begin(&flow, MP_SCENE_KIND_HERO);
        ut_check(!mp_scene_host_still_running(&flow) && mp_scene_host_stands_now(&flow),
                 "a scene that is held is not running yet, and stands all the same");
        flow.phase = MP_SCENE_PHASE_RUNNING;
        ut_check(mp_scene_host_still_running(&flow) && mp_scene_host_stands_now(&flow),
                 "one released into running is both");
        flow.phase    = MP_SCENE_PHASE_OVER;
        flow.given_up = true;
        ut_check(mp_scene_host_still_running(&flow) && mp_scene_host_stands_now(&flow),
                 "and so is one given up the engine may still play");
        flow.given_up = false;
        flow.phase    = MP_SCENE_PHASE_NONE;
        ut_check(!mp_scene_host_still_running(&flow) && !mp_scene_host_stands_now(&flow),
                 "none is neither");
    }

    ut_section("a mode that did not read has its own words");
    ut_check(strcmp(mp_scene_given_up_text(MP_SCENE_MOVE_UNREAD),
                    mp_scene_given_up_text(MP_SCENE_MOVE_MODE)) != 0 &&
                 strcmp(mp_scene_move_text(MP_SCENE_MOVE_UNREAD),
                        mp_scene_move_text(MP_SCENE_MOVE_MODE)) != 0,
             "in the line of a scene given up and in a refusal");
    {
        static const mp_scene_release_t RELEASES[4] = {
            MP_SCENE_RELEASE_NONE, MP_SCENE_RELEASE_AT_THE_PLACE, MP_SCENE_RELEASE_ALONE,
            MP_SCENE_RELEASE_NOBODY
        };
        size_t i;
        size_t j;
        bool   apart = true;

        for (i = 0u; i < 4u; ++i) {
            for (j = i + 1u; j < 4u; ++j) {
                apart = apart && strcmp(mp_scene_release_text(RELEASES[i]),
                                        mp_scene_release_text(RELEASES[j])) != 0;
            }
        }
        ut_check(apart, "and every reason a hold fell for has words of its own");
    }
}

int main(void)
{
    check_the_hold_waits_for_the_host_at_his_place();
    check_alone_no_place_and_the_place_read_again();
    check_the_cap();
    check_a_try_that_did_not_take();
    check_the_hard_way();
    check_the_dark_wait();
    check_the_respawn();
    check_a_death_and_a_seat_left();
    check_every_gathering_sequence();
    check_the_place();
    return ut_summary("the host at the place, and a gathering's seat");
}
