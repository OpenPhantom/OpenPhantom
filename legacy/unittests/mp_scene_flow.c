/* A scene of the host's, as its state machines: the host's scene and one player's seat.
 *
 * Each machine is walked along the paths it is meant to take, and then over every sequence of its
 * inputs up to six long: whatever happened, the one exit leaves it with nothing held and no fade
 * standing. A state that has one way out has to be tested by walking every way in.
 *
 * Two machines with one check function per path and one exhaustive walk each. The bounds of the
 * waits are mp_scene_flow_wait.c's and the hold at the host's place mp_scene_hold.c's. */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A fade as long as the engine's own respawn fade, for a deadline worth measuring. */
#define LONG_FADE_SECONDS 1.0f

/* ==============================================================================================
 * The host's scene.
 * ============================================================================================ */

/* A look with the host at his place: standing, with a place, not away from it. */
static mp_scene_host_look_t look_at(uint32_t now, bool stands, bool at_place, bool running)
{
    mp_scene_host_look_t look;

    memset(&look, 0, sizeof look);
    look.now         = now;
    look.host_stands = stands;
    look.has_place   = true;
    look.away        = !at_place;
    look.running     = running;
    look.may_move    = stands ? MP_SCENE_MOVE_YES : MP_SCENE_MOVE_DEAD;
    return look;
}

static void check_a_scene_of_the_hosts_own(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a scene the host set off himself runs at once, with nothing held");
    memset(&flow, 0, sizeof flow);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 100u, false) == MP_SCENE_BEGIN_NEW,
             "a scene of its own");
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.holds && flow.serial == 1u &&
                 mp_scene_host_stands_now(&flow),
             "running, holding nothing, the first scene of the world, and it stands");
    look = look_at(100u, true, true, true);
    ut_check(!mp_scene_host_step(&flow, &look) && flow.seen_running,
             "the lock is up in the very substep of the door");
    look = look_at(300u, true, true, true);
    ut_check(!mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_RUNNING,
             "it runs as long as the lock stands");
    look = look_at(301u, true, true, false);
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE &&
                 !mp_scene_host_stands_now(&flow),
             "the lock falls and none runs at once: nothing lingers after a scene");

    ut_section("a hero scene of the host's own waits for its grab");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 10u, false);
    look = look_at(10u, true, true, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.seen_running,
             "the grab comes a substep after the door");
    look = look_at(11u, true, true, true);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.seen_running, "and shows on the next look");
    look = look_at(400u, true, true, false);
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE,
             "the scene is none when the put-back runs");
}

static void check_a_far_players_scene(void)
{
    mp_scene_host_flow_t flow;
    uint32_t             now;

    ut_section("a far player's scene holds until the host stands at his place, then runs");
    memset(&flow, 0, sizeof flow);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 100u, true) == MP_SCENE_BEGIN_NEW,
             "a scene of its own");
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds && flow.serial == 1u &&
                 mp_scene_host_stands_now(&flow),
             "gathering, holding its actor, and standing from the door on");
    for (now = 101u; now < 110u; ++now) {
        mp_scene_host_look_t look = look_at(now, true, false, false);

        (void)mp_scene_host_step(&flow, &look);
    }
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds,
             "still held while the host is on his way");
    {
        mp_scene_host_look_t look = look_at(110u, true, true, false);

        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_RUNNING &&
                     !flow.holds && flow.released == MP_SCENE_RELEASE_AT_THE_PLACE,
                 "the first look that finds him at his place: the hold falls, with no wait "
                 "for anybody else");
        ut_check(flow.hold_standing == 10u && flow.hold_dead == 0u,
                 "ten substeps held, all of them with the host standing");
    }
    {
        mp_scene_host_look_t look = look_at(111u, true, true, true);

        (void)mp_scene_host_step(&flow, &look);
        look = look_at(300u, true, true, false);
        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE,
                 "it runs, and is none the moment the engine is done");
    }

    ut_section("a dead host is waited for, and his time is counted apart");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, true);
    for (now = 1u; now <= 200u; ++now) {
        mp_scene_host_look_t look = look_at(now, false, true, false);

        (void)mp_scene_host_step(&flow, &look);
    }
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds && flow.hold_dead == 200u,
             "two hundred substeps with the host dead hold the hero, even at his place");
    {
        mp_scene_host_look_t look = look_at(201u, true, true, false);

        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_RUNNING &&
                     flow.released == MP_SCENE_RELEASE_AT_THE_PLACE && flow.hold_standing == 1u,
                 "he stands again at his place: released on that look");
    }
    /* What the cap does to a hold is mp_scene_flow_wait.c's: it gives the scene up, which a
     * release into running with the host dead would not. */
}

static void check_a_second_scene_and_a_warp(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a second scene is counted and begins nothing; a warp takes over");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 3u, true) == MP_SCENE_BEGIN_SECOND &&
                 flow.kind == MP_SCENE_KIND_LOCK && flow.serial == 1u && flow.holds,
             "a hero inside a held lock scene leaves it as it was");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 4u, true) ==
                     MP_SCENE_BEGIN_WARP_OVER &&
                 flow.kind == MP_SCENE_KIND_WARP && flow.serial == 2u && !flow.holds &&
                 flow.phase == MP_SCENE_PHASE_GATHERING && !mp_scene_host_stands_now(&flow),
             "a warp moves the host whatever this does: it takes over, holds nothing whatever "
             "it was asked, and is no scene that stands");

    ut_section("a warp gathers until the host has landed");
    look             = look_at(10u, true, false, false);
    look.warp_landed = true;
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE,
             "the landing ends it, and none runs at once");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 0u, false);
    look = look_at(MP_SCENE_WARP_CAP_SUBSTEPS - 1u, true, false, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING, "and nothing else before its cap");
    look = look_at(MP_SCENE_WARP_CAP_SUBSTEPS, true, false, false);
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE,
             "a warp the engine dropped ends at the cap");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 300u, false) == MP_SCENE_BEGIN_NEW &&
                 flow.serial == 2u,
             "a scene after an ended one is a new one");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 301u, true) == MP_SCENE_BEGIN_SECOND &&
                 !flow.holds && flow.phase == MP_SCENE_PHASE_RUNNING,
             "a held door inside a scene that runs holds nothing: it is a second scene");

    ut_section("the numbers never come back to nought, and a door of no kind begins nothing");
    memset(&flow, 0, sizeof flow);
    flow.serial = 0xFFFFu;
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 0u, false);
    ut_check(flow.serial == 1u, "a scene after the last number is number one again");
    memset(&flow, 0, sizeof flow);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KINDS, 0u, true) == MP_SCENE_BEGIN_SECOND &&
                 flow.phase == MP_SCENE_PHASE_NONE && !flow.holds && flow.serial == 0u,
             "a kind past the last is refused and leaves the flow as it was");
    ut_check(mp_scene_host_begin(NULL, MP_SCENE_KIND_LOCK, 0u, true) == MP_SCENE_BEGIN_SECOND &&
                 !mp_scene_host_stands_now(NULL) && !mp_scene_host_leave(NULL),
             "and no flow at all begins, stands and leaves nothing");
}

/* Every sequence of fourteen inputs up to six long, and after each a leave: nothing held, none
 * runs, nothing given up, nothing dropped, no scene stands, and the leave said a hold stood
 * exactly when one did. Between the inputs: a hold only ever stands while a lock or a hero scene
 * gathers; a scene never goes from gathering to running on a look with the host dead or away from
 * his place, unless every far player is gone and he has no place; a scene given up is over, holds
 * nothing and still stands, and is never a lock; over is never anything but a scene given up; and
 * only a held lock is ever dropped, on the look that leaves none standing. */
enum { H_LOCK, H_HERO, H_OWN, H_WARP, H_AT_PLACE, H_AWAY, H_DEAD, H_RUNS, H_LANDED, H_ALONE,
       H_STUCK, H_LATE, H_NO_PLACE, H_LEAVE, H_INPUTS };

static void host_apply(mp_scene_host_flow_t *flow, int input, uint32_t *now)
{
    mp_scene_host_look_t look;

    *now += 7u;
    switch (input) {
    case H_LOCK:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_LOCK, *now, true);
        break;
    case H_HERO:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_HERO, *now, true);
        break;
    case H_OWN:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_HERO, *now, false);
        break;
    case H_WARP:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_WARP, *now, false);
        break;
    case H_LEAVE:
        (void)mp_scene_host_leave(flow);
        break;
    default:
        /* Late is the cap passing with the host dead; stuck a host standing at a gun at his
         * place; no place a host standing where no place for him was found. */
        *now += input == H_LATE ? MP_SCENE_WAIT_CAP_SUBSTEPS : 0u;
        look = look_at(*now, input != H_DEAD && input != H_LATE, input != H_AWAY,
                       input == H_RUNS);
        look.warp_landed = input == H_LANDED;
        look.alone       = input == H_ALONE;
        look.has_place   = input != H_ALONE && input != H_NO_PLACE;
        look.no_place    = input == H_NO_PLACE;
        look.may_move    = input == H_STUCK ? MP_SCENE_MOVE_MODE
                                            : (look.host_stands ? MP_SCENE_MOVE_YES
                                                                : MP_SCENE_MOVE_DEAD);
        (void)mp_scene_host_step(flow, &look);
        break;
    }
}

/* What one input may not leave behind: a count for each broken rule. */
typedef struct host_walk {
    unsigned bad_hold;
    unsigned not_there;
    unsigned bad_given_up;
    unsigned bad_over;
    unsigned bad_drop;
} host_walk_t;

/* What stood before the input, for the rules that compare the two sides of it. */
typedef struct host_before {
    mp_scene_phase_t phase;
    mp_scene_kind_t  kind;
    bool             held;
    mp_scene_drop_t  dropped;
} host_before_t;

static void judge_step(const mp_scene_host_flow_t *flow, int input, const host_before_t *before,
                       host_walk_t *walk)
{
    bool was_a_held_lock = before->phase == MP_SCENE_PHASE_GATHERING && before->held &&
                           before->kind == MP_SCENE_KIND_LOCK;

    if (flow->holds && (flow->phase != MP_SCENE_PHASE_GATHERING ||
                        flow->kind == MP_SCENE_KIND_WARP || input == H_ALONE)) {
        ++walk->bad_hold;
    }
    if ((input == H_DEAD || input == H_LATE || input == H_AWAY) &&
        before->phase == MP_SCENE_PHASE_GATHERING && before->held &&
        flow->phase == MP_SCENE_PHASE_RUNNING) {
        ++walk->not_there;
    }
    if (flow->given_up && (flow->phase != MP_SCENE_PHASE_OVER || flow->holds ||
                           !mp_scene_host_stands_now(flow) ||
                           (was_a_held_lock && flow->kind == MP_SCENE_KIND_LOCK))) {
        ++walk->bad_given_up;
    }
    if (flow->phase == MP_SCENE_PHASE_OVER && !flow->given_up) {
        ++walk->bad_over;
    }
    /* A reason that was not there before this input was set by it: only on a held lock, and
     * with none standing after. */
    if (flow->dropped != MP_SCENE_DROP_NONE && before->dropped == MP_SCENE_DROP_NONE &&
        (!was_a_held_lock || flow->phase != MP_SCENE_PHASE_NONE || flow->holds)) {
        ++walk->bad_drop;
    }
}

static void check_every_host_sequence(void)
{
    unsigned    sequences = 0u;
    unsigned    bad_exit  = 0u;
    host_walk_t walk;
    unsigned    code;
    unsigned    total = 1u;
    int         depth;

    memset(&walk, 0, sizeof walk);
    for (depth = 0; depth < 6; ++depth) {
        total *= (unsigned)H_INPUTS;
    }
    ut_section("every sequence of the host's inputs up to six long ends in the one exit");
    for (code = 0u; code < total; ++code) {
        mp_scene_host_flow_t flow;
        uint32_t             now = 0u;
        unsigned             rest = code;
        bool                 held;

        memset(&flow, 0, sizeof flow);
        for (depth = 0; depth < 6; ++depth) {
            int           input = (int)(rest % (unsigned)H_INPUTS);
            host_before_t before;

            before.phase   = flow.phase;
            before.kind    = flow.kind;
            before.held    = flow.holds;
            before.dropped = flow.dropped;
            host_apply(&flow, input, &now);
            rest /= (unsigned)H_INPUTS;
            judge_step(&flow, input, &before, &walk);
        }
        held = flow.holds;
        if (mp_scene_host_leave(&flow) != held || flow.holds || flow.given_up ||
            flow.dropped != MP_SCENE_DROP_NONE || flow.phase != MP_SCENE_PHASE_NONE ||
            mp_scene_host_stands_now(&flow)) {
            ++bad_exit;
        }
        ++sequences;
    }
    ut_checkf(walk.bad_hold == 0u, "%u sequence(s), a hold outside a gathering lock or hero scene "
              "or after a look with nobody else there %u time(s)", sequences, walk.bad_hold);
    ut_checkf(walk.not_there == 0u,
              "a held scene that went on to run on a look with the host dead or away from his "
              "place %u time(s)", walk.not_there);
    ut_checkf(walk.bad_given_up == 0u, "a scene given up that was not over, held something, did "
              "not stand or was a held lock %u time(s)", walk.bad_given_up);
    ut_checkf(walk.bad_over == 0u, "a scene over that was not one given up %u time(s)",
              walk.bad_over);
    ut_checkf(walk.bad_drop == 0u, "a drop of anything but a held lock, or one that left a scene "
              "standing or held %u time(s)", walk.bad_drop);
    ut_checkf(bad_exit == 0u, "and the leave left something held, running or standing %u time(s)",
              bad_exit);
}

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

static mp_scene_seat_look_t seat_look(uint32_t now, bool live, mp_scene_move_t move,
                                      bool fade_done, bool at_seat)
{
    mp_scene_seat_look_t look;

    memset(&look, 0, sizeof look);
    look.now       = now;
    look.live      = live;
    look.may_move  = move;
    look.fade_done = fade_done;
    look.at_seat   = at_seat;
    return look;
}

static void check_one_seat(void)
{
    mp_scene_seat_flow_t flow;
    mp_scene_seat_look_t look;

    ut_section("a seat: wait, fade out, place, stand, fade in");
    memset(&flow, 0, sizeof flow);
    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_MODE, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_WAITING && flow.refused == MP_SCENE_MOVE_MODE,
             "a player on a ledge waits, and the reason is kept");
    look = seat_look(2u, true, MP_SCENE_MOVE_YES, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_OUT && flow.fade_held,
             "standing: the screen goes dark");
    look = seat_look(3u, true, MP_SCENE_MOVE_YES, true, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_PLACE &&
                 flow.stage == MP_SCENE_SEAT_PLACED && !flow.fade_on_clock,
             "dark: the seat goes to the placement");
    look = seat_look(4u, true, MP_SCENE_MOVE_YES, true, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_NONE,
             "and waits for the body");
    look = seat_look(5u, false, MP_SCENE_MOVE_YES, true, true);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_DONE && flow.at_seat && !flow.fade_held,
             "on its seat: the screen comes back, even with the scene already running");
    ut_check(mp_scene_seat_leave(&flow) == MP_SCENE_SEAT_ACT_NONE,
             "and an exit after it has nothing to give back");

    ut_section("the fade's own clock, a death in the dark, a scene that ends first");
    mp_scene_seat_start(&flow, 100u, MP_SCENE_FADE_SECONDS);
    look = seat_look(100u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    look = seat_look(100u + mp_scene_seat_fade_deadline(MP_SCENE_FADE_SECONDS) - 1u, true,
                     MP_SCENE_MOVE_YES, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_NONE,
             "a tint that never says it is done is waited for until the deadline");
    look = seat_look(100u + mp_scene_seat_fade_deadline(MP_SCENE_FADE_SECONDS), true,
                     MP_SCENE_MOVE_YES, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_PLACE && flow.fade_on_clock,
             "and then the seat goes on the fade's own clock");
    ut_check(mp_scene_seat_fade_deadline(MP_SCENE_FADE_SECONDS) >= 8u &&
                 mp_scene_seat_fade_deadline(LONG_FADE_SECONDS) >= 32u,
             "a deadline is never shorter than the fade itself");

    /* A gathering's seat goes back to waiting where these give up, which mp_scene_hold.c walks;
     * a plain seat, which mp_scene_seat_start begins, gives up, and that is what they hold. */
    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    look = seat_look(2u, true, MP_SCENE_MOVE_DEAD, true, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_GIVEN_UP && flow.refused == MP_SCENE_MOVE_DEAD,
             "a plain seat whose player dies in the dark is not moved, and gets the screen "
             "back");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, false, MP_SCENE_MOVE_YES, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_NONE &&
                 flow.stage == MP_SCENE_SEAT_GIVEN_UP,
             "a scene that ended before the player could be moved takes nobody afterwards");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    look = seat_look(2u, false, MP_SCENE_MOVE_YES, false, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 !flow.fade_held,
             "and one that ends in the dark gives the screen back");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, true, false);
    (void)mp_scene_seat_step(&flow, &look);
    (void)mp_scene_seat_step(&flow, &look);
    look = seat_look(1u + MP_SCENE_PLACE_SUBSTEPS, true, MP_SCENE_MOVE_YES, true, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_GIVEN_UP && !flow.at_seat,
             "a plain seat's placement the body never took is given up at its deadline, the "
             "screen back");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    ut_check(mp_scene_seat_leave(&flow) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_IDLE && !flow.fade_held,
             "the exit in the dark gives the held fade back, which a session ending mid-level "
             "would otherwise leave black");
}

static void check_a_seat_writes_only_the_place(void)
{
    mp_scene_seat_flow_t flow;
    mp_scene_seat_look_t look;
    unsigned             count[5] = { 0u, 0u, 0u, 0u, 0u };
    uint32_t             now;

    ut_section("a plain seat is a fade, a place and a fade, and nothing of the hero");
    mp_scene_seat_start(&flow, 0u, LONG_FADE_SECONDS);
    for (now = 1u; now < 200u; ++now) {
        look = seat_look(now, true, MP_SCENE_MOVE_YES, now > 32u, now > 40u);
        ++count[mp_scene_seat_step(&flow, &look)];
    }
    ut_check(count[MP_SCENE_SEAT_ACT_FADE_OUT] == 1u && count[MP_SCENE_SEAT_ACT_PLACE] == 1u &&
                 count[MP_SCENE_SEAT_ACT_FADE_IN] == 1u && count[MP_SCENE_SEAT_ACT_RESPAWN] == 0u,
             "one fade out, one placement and one fade in, and never the engine's respawn: no "
             "health and no hero are written, which that respawn would have done");
    ut_check(flow.fade_seconds == LONG_FADE_SECONDS, "under the fade it was started with");
}

enum { S_WAIT_MODE, S_MOVE, S_DEAD, S_FADED, S_AT_SEAT, S_ENDED, S_LATE, S_LEAVE, S_INPUTS };

static void check_every_seat_sequence(void)
{
    unsigned total = 1u;
    unsigned code;
    unsigned bad = 0u;
    int      depth;

    for (depth = 0; depth < 6; ++depth) {
        total *= (unsigned)S_INPUTS;
    }
    ut_section("every sequence of a seat's inputs up to six long gives every fade back");
    for (code = 0u; code < total; ++code) {
        mp_scene_seat_flow_t flow;
        uint32_t             now  = 0u;
        unsigned             rest = code;
        int                  dark = 0;

        memset(&flow, 0, sizeof flow);
        mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
        for (depth = 0; depth < 6; ++depth) {
            int                  input = (int)(rest % (unsigned)S_INPUTS);
            mp_scene_seat_look_t look;
            mp_scene_seat_act_t  act;

            rest /= (unsigned)S_INPUTS;
            now += input == S_LATE ? 100u : 1u;
            look = seat_look(now, input != S_ENDED,
                             input == S_DEAD ? MP_SCENE_MOVE_DEAD
                                             : (input == S_WAIT_MODE ? MP_SCENE_MOVE_MODE
                                                                     : MP_SCENE_MOVE_YES),
                             input == S_FADED || input == S_AT_SEAT, input == S_AT_SEAT);
            act = input == S_LEAVE ? mp_scene_seat_leave(&flow) : mp_scene_seat_step(&flow, &look);
            dark += act == MP_SCENE_SEAT_ACT_FADE_OUT ? 1 : 0;
            dark -= act == MP_SCENE_SEAT_ACT_FADE_IN ? 1 : 0;
            if (dark < 0 || dark > 1 || (dark == 1) != flow.fade_held) {
                ++bad;
            }
        }
        dark -= mp_scene_seat_leave(&flow) == MP_SCENE_SEAT_ACT_FADE_IN ? 1 : 0;
        if (dark != 0 || flow.fade_held) {
            ++bad;
        }
    }
    ut_checkf(bad == 0u, "%u sequence(s): a fade out not answered by exactly one fade in %u "
              "time(s)", total, bad);
}

int main(void)
{
    check_a_scene_of_the_hosts_own();
    check_a_far_players_scene();
    check_a_second_scene_and_a_warp();
    check_every_host_sequence();
    check_one_seat();
    check_a_seat_writes_only_the_place();
    check_every_seat_sequence();
    return ut_summary("the scene's flow");
}
