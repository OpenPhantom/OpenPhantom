/* A scene for everybody, as its state machines: the host's scene, a client's mirror and one
 * player's seat. The fourth, the seating of all of them, is mp_scene_seating.c's.
 *
 * Each machine is walked along the paths it is meant to take, and then over every sequence of its
 * inputs up to six long: whatever happened, the one exit leaves it with nothing held, no lock owed
 * and no fade standing. A state that has one way out has to be tested by walking every way in.
 *
 * SIZE NOTE: over 600 lines, three machines with one check function per path and one exhaustive
 * walk each. The seating is tested in mp_scene_seating.c, against its stand-ins for the seat
 * search. */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The host's scene.
 * ============================================================================================ */

static mp_scene_host_look_t look_at(uint32_t now, bool stands, bool seated, bool running)
{
    mp_scene_host_look_t look;

    memset(&look, 0, sizeof look);
    look.now             = now;
    look.host_stands     = stands;
    look.everyone_seated = seated;
    look.running         = running;
    return look;
}

static void check_what_a_kind_says(void)
{
    ut_section("a lock and a hero scene lock a client and give it the bars; a warp moves it");
    ut_check(mp_scene_what_of(MP_SCENE_KIND_LOCK) == (MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS),
             "a lock: the lock and the bars");
    ut_check(mp_scene_what_of(MP_SCENE_KIND_HERO) ==
                 (MP_SCENE_WHAT_HERO | MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS),
             "the hero as an actor: the same, and the hero");
    ut_check(mp_scene_what_of(MP_SCENE_KIND_WARP) == MP_SCENE_WHAT_WARP, "a warp: only itself");

    ut_section("a scene runs for everybody while it gathers or runs, and a warp is no scene");
    ut_check(mp_scene_for_all_now(MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_LOCK) &&
                 mp_scene_for_all_now(MP_SCENE_PHASE_RUNNING, MP_SCENE_WHAT_HERO),
             "a lock gathering and a hero running");
    ut_check(!mp_scene_for_all_now(MP_SCENE_PHASE_OVER, MP_SCENE_WHAT_LOCK) &&
                 !mp_scene_for_all_now(MP_SCENE_PHASE_NONE, MP_SCENE_WHAT_LOCK),
             "not once it is over, and not when none runs");
    ut_check(!mp_scene_for_all_now(MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_WARP),
             "and not a warp, which moves the players and plays no scene");
}

static void check_a_lock_scene(void)
{
    mp_scene_host_flow_t flow;
    uint32_t             now;

    ut_section("a lock scene holds until everybody is seated, then runs, then is over");
    memset(&flow, 0, sizeof flow);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 100u) == MP_SCENE_BEGIN_NEW,
             "a scene of its own");
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds && flow.serial == 1u,
             "gathering, holding its actor, the first scene of the world");
    for (now = 101u; now < 105u; ++now) {
        mp_scene_host_look_t look = look_at(now, true, false, true);

        (void)mp_scene_host_step(&flow, &look);
    }
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds,
             "still gathering while somebody is not at their seat");
    {
        mp_scene_host_look_t look = look_at(105u, true, true, true);

        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_RUNNING &&
                     !flow.holds && flow.released == MP_SCENE_RELEASE_SEATED,
                 "everybody seated: the hold falls and the scene runs");
    }
    {
        mp_scene_host_look_t look = look_at(300u, true, true, true);

        ut_check(!mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_RUNNING,
                 "it runs as long as the lock stands");
        look = look_at(301u, true, true, false);
        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_OVER,
                 "the lock falls and the scene is over");
        look = look_at(301u + MP_SCENE_OVER_SUBSTEPS - 1u, true, true, false);
        (void)mp_scene_host_step(&flow, &look);
        ut_check(flow.phase == MP_SCENE_PHASE_OVER, "over is said for two seconds");
        look = look_at(301u + MP_SCENE_OVER_SUBSTEPS, true, true, false);
        ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_NONE,
                 "and then none runs");
    }
}

static void check_the_bound_and_a_dead_host(void)
{
    mp_scene_host_flow_t flow;
    uint32_t             now;

    ut_section("the hold's second and a half counts only while the host stands");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u);
    for (now = 1u; now <= 200u; ++now) {
        mp_scene_host_look_t look = look_at(now, false, false, false);

        (void)mp_scene_host_step(&flow, &look);
    }
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds && flow.hold_dead == 200u,
             "two hundred substeps with the host dead hold the hero and count as nothing");
    for (now = 201u; flow.holds && now < 1000u; ++now) {
        mp_scene_host_look_t look = look_at(now, true, false, false);

        (void)mp_scene_host_step(&flow, &look);
    }
    ut_checkf(flow.phase == MP_SCENE_PHASE_RUNNING &&
                  flow.released == MP_SCENE_RELEASE_BOUND &&
                  now - 1u == 200u + MP_SCENE_HOLD_SUBSTEPS,
              "the bound runs out %u substeps after the host stands again (on %u)",
              (unsigned)MP_SCENE_HOLD_SUBSTEPS, (unsigned)(now - 1u));
    /* What the cap does to a hold is mp_scene_flow_wait.c's: it gives the scene up for
     * everybody, which a release into running with the host dead would not. */
}

static void check_a_hero_scene_and_its_grab(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a hero scene runs once the grab shows");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 10u);
    look = look_at(11u, true, true, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.seen_running,
             "released on the substep before the engine's grab");
    look = look_at(12u, true, true, true);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && flow.seen_running,
             "the grab shows on the next");
    look = look_at(400u, true, true, false);
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_OVER,
             "and the scene is over when the put-back runs");
    /* A grab that does not come is mp_scene_flow_wait.c's: the scene waits for a host that may
     * not be moved, and gives up only on one that could be taken, or at the cap. The fixed grace
     * this check used to hold ended the scene for the clients while the engine still waited. */
}

static void check_a_second_scene_and_a_warp(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a second scene is counted and not gathered; a warp takes over");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u);
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 3u) == MP_SCENE_BEGIN_SECOND &&
                 flow.kind == MP_SCENE_KIND_LOCK && flow.serial == 1u && flow.holds,
             "a hero inside a gathering lock scene leaves it as it was");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 4u) == MP_SCENE_BEGIN_WARP_OVER &&
                 flow.kind == MP_SCENE_KIND_WARP && flow.serial == 2u && !flow.holds &&
                 flow.warp_serial == 1u && flow.phase == MP_SCENE_PHASE_GATHERING,
             "a warp moves the host whatever this does, so the others follow it");

    ut_section("a warp gathers until the host has landed");
    look             = look_at(10u, true, false, false);
    look.warp_landed = true;
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_OVER,
             "the landing ends it");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 0u);
    look = look_at(MP_SCENE_WARP_CAP_SUBSTEPS - 1u, true, false, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_GATHERING, "and nothing else before its cap");
    look = look_at(MP_SCENE_WARP_CAP_SUBSTEPS, true, false, false);
    ut_check(mp_scene_host_step(&flow, &look) && flow.phase == MP_SCENE_PHASE_OVER,
             "a warp the engine dropped ends at the cap");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 300u) == MP_SCENE_BEGIN_NEW &&
                 flow.serial == 2u,
             "a scene after an over one is a new one");

    ut_section("the numbers never come back to nought");
    memset(&flow, 0, sizeof flow);
    flow.serial      = 0xFFFFu;
    flow.warp_serial = 0xFFu;
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 0u);
    ut_check(flow.serial == 1u && flow.warp_serial == 1u,
             "a scene and a warp after the last number are number one again");
}

/* Every sequence of twelve inputs up to six long, and after each a leave: nothing held, none runs,
 * nothing given up, and the leave said a hold stood exactly when one did. Between the inputs, a
 * hold only ever stands while a lock or a hero scene gathers, and never after a look with nobody
 * else there; a scene never goes from gathering to running on a look with the host dead; and a
 * scene given up is over, holds nothing and is no scene for everybody. */
enum { H_LOCK, H_HERO, H_WARP, H_SEATED, H_WAITING, H_DEAD, H_RUNS, H_LANDED, H_ALONE, H_STUCK,
       H_LATE, H_LEAVE, H_INPUTS };

static void host_apply(mp_scene_host_flow_t *flow, int input, uint32_t *now)
{
    mp_scene_host_look_t look;

    *now += 7u;
    switch (input) {
    case H_LOCK:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_LOCK, *now);
        break;
    case H_HERO:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_HERO, *now);
        break;
    case H_WARP:
        (void)mp_scene_host_begin(flow, MP_SCENE_KIND_WARP, *now);
        break;
    case H_LEAVE:
        (void)mp_scene_host_leave(flow);
        break;
    default:
        /* Late is the cap passing with the host dead; stuck a host standing at a gun. */
        *now += input == H_LATE ? MP_SCENE_WAIT_CAP_SUBSTEPS : 0u;
        look = look_at(*now, input != H_DEAD && input != H_LATE, input == H_SEATED,
                       input == H_RUNS);
        look.warp_landed = input == H_LANDED;
        look.alone       = input == H_ALONE;
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
    unsigned corpse;
    unsigned bad_given_up;
} host_walk_t;

static void judge_step(const mp_scene_host_flow_t *flow, int input, mp_scene_phase_t before,
                       host_walk_t *walk)
{
    if (flow->holds && (flow->phase != MP_SCENE_PHASE_GATHERING ||
                        flow->kind == MP_SCENE_KIND_WARP || input == H_ALONE)) {
        ++walk->bad_hold;
    }
    if ((input == H_DEAD || input == H_LATE) && before == MP_SCENE_PHASE_GATHERING &&
        flow->kind != MP_SCENE_KIND_WARP && flow->phase == MP_SCENE_PHASE_RUNNING) {
        ++walk->corpse;
    }
    if (flow->given_up &&
        (flow->phase != MP_SCENE_PHASE_OVER || flow->holds ||
         mp_scene_for_all_now((uint8_t)flow->phase, mp_scene_what_of(flow->kind)))) {
        ++walk->bad_given_up;
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
            int              input  = (int)(rest % (unsigned)H_INPUTS);
            mp_scene_phase_t before = flow.phase;

            host_apply(&flow, input, &now);
            rest /= (unsigned)H_INPUTS;
            judge_step(&flow, input, before, &walk);
        }
        held = flow.holds;
        if (mp_scene_host_leave(&flow) != held || flow.holds || flow.given_up ||
            flow.phase != MP_SCENE_PHASE_NONE) {
            ++bad_exit;
        }
        ++sequences;
    }
    ut_checkf(walk.bad_hold == 0u, "%u sequence(s), a hold outside a gathering lock or hero scene "
              "or after a look with nobody else there %u time(s)", sequences, walk.bad_hold);
    ut_checkf(walk.corpse == 0u,
              "a scene that went on to run for everybody on a look with the host "
              "dead %u time(s)", walk.corpse);
    ut_checkf(walk.bad_given_up == 0u, "a scene given up that was not over, held something or ran "
              "for everybody %u time(s)", walk.bad_given_up);
    ut_checkf(bad_exit == 0u, "and the leave left something held or running %u time(s)",
              bad_exit);
}

/* ==============================================================================================
 * A client's mirror.
 * ============================================================================================ */

static mp_scene_note_t note_of(uint16_t serial, uint8_t generation, uint8_t phase, uint8_t what)
{
    mp_scene_note_t note;

    memset(&note, 0, sizeof note);
    note.serial       = serial;
    note.generation   = generation;
    note.phase        = phase;
    note.what         = what;
    note.trigger_slot = MP_SCENE_TRIGGER_UNKNOWN;
    return note;
}

static uint32_t mirror_at(mp_scene_mirror_t *mirror, uint32_t now_ms, bool heard, bool may,
                          mp_scene_let_go_t *why)
{
    mp_scene_mirror_look_t look;

    look.now_ms     = now_ms;
    look.host_heard = heard;
    look.may_lock   = may;
    return mp_scene_mirror_step(mirror, &look, why);
}

static void check_the_mirror(void)
{
    const uint8_t     lock = MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS;
    mp_scene_mirror_t mirror;
    mp_scene_note_t   note;
    mp_scene_let_go_t why = MP_SCENE_LET_GO_NONE;
    uint32_t          act;

    ut_section("a note of the host's scene holds a client, once it may be");
    memset(&mirror, 0, sizeof mirror);
    note = note_of(4u, 2u, MP_SCENE_PHASE_GATHERING, lock);
    ut_check(mp_scene_mirror_take(&mirror, &note, 3u, 0u) == MP_SCENE_TAKE_FOREIGN &&
                 !mirror.known,
             "a note of another world is refused");
    ut_check(mp_scene_mirror_take(&mirror, &note, 2u, 0u) == MP_SCENE_TAKE_NEW, "taken");
    act = mirror_at(&mirror, 10u, true, false, &why);
    ut_check(act == 0u && !mirror.locked,
             "a player in the air or dead is not locked yet: the lock waits for the same answer "
             "the move does");
    act = mirror_at(&mirror, 20u, true, true, &why);
    ut_check(act == (MP_SCENE_MIRROR_RAISE | MP_SCENE_MIRROR_BARS) && mirror.locked,
             "then the lock and the bars");
    act = mirror_at(&mirror, 30u, true, true, &why);
    ut_check(act == MP_SCENE_MIRROR_RAISE,
             "and the lock again on every substep, the bars once: a state, not an event");
    ut_check(mp_scene_mirror_take(&mirror, &note, 2u, 40u) == MP_SCENE_TAKE_REPEAT,
             "the same note again changes nothing");
    note.phase = MP_SCENE_PHASE_RUNNING;
    ut_check(mp_scene_mirror_take(&mirror, &note, 2u, 50u) == MP_SCENE_TAKE_NEW,
             "a new phase of the same scene is taken");
    ut_check(mirror_at(&mirror, 60u, true, true, &why) == MP_SCENE_MIRROR_RAISE,
             "and held on");
    note = note_of(3u, 2u, MP_SCENE_PHASE_OVER, lock);
    ut_check(mp_scene_mirror_take(&mirror, &note, 2u, 70u) == MP_SCENE_TAKE_OLDER,
             "an older scene's end, arriving late, does not end this one");
    note = note_of(4u, 2u, MP_SCENE_PHASE_OVER, lock);
    (void)mp_scene_mirror_take(&mirror, &note, 2u, 80u);
    act = mirror_at(&mirror, 90u, true, true, &why);
    ut_check(act == MP_SCENE_MIRROR_LET_GO && why == MP_SCENE_LET_GO_OVER && !mirror.locked,
             "over: the one release");
    ut_check(mirror_at(&mirror, 100u, true, true, &why) == 0u, "and nothing after it");

    ut_section("a host not heard for two seconds lets the client go; a slow note does not");
    memset(&mirror, 0, sizeof mirror);
    note = note_of(9u, 1u, MP_SCENE_PHASE_GATHERING, lock);
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 1000u);
    (void)mirror_at(&mirror, 1000u, true, true, &why);
    act = mirror_at(&mirror, 1000u + 5000u, true, true, &why);
    ut_check(act == MP_SCENE_MIRROR_RAISE,
             "five seconds with no new note and the host heard: still held, the note waits in "
             "the channel behind the world");
    act = mirror_at(&mirror, 6000u + MP_SCENE_SILENCE_MS - 1u, false, true, &why);
    ut_check(act == MP_SCENE_MIRROR_RAISE, "and held until the silence is two seconds long");
    act = mirror_at(&mirror, 6000u + MP_SCENE_SILENCE_MS, false, true, &why);
    ut_check(act == MP_SCENE_MIRROR_LET_GO && why == MP_SCENE_LET_GO_SILENT,
             "then let go rather than left standing");
    act = mirror_at(&mirror, 9000u, true, true, &why);
    ut_check(act == 0u, "a host heard again after that does not lock the player again on an "
                        "old note");

    ut_section("a warp does not lock; the exit releases what the mirror raised, once");
    memset(&mirror, 0, sizeof mirror);
    note = note_of(2u, 1u, MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_WARP);
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 0u);
    ut_check(mirror_at(&mirror, 10u, true, true, &why) == 0u, "a warp's note locks nobody");
    note = note_of(3u, 1u, MP_SCENE_PHASE_GATHERING, lock);
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 20u);
    (void)mirror_at(&mirror, 30u, true, true, &why);
    ut_check(mp_scene_mirror_leave(&mirror) == MP_SCENE_MIRROR_LET_GO && !mirror.known,
             "a level's end releases the lock and forgets the note");
    ut_check(mp_scene_mirror_leave(&mirror) == 0u, "and a second exit has nothing to release");
}

/* A line spoken within the reach of the place a scene gathers around is that scene's, so a client
 * needs the place of the scene its mirror holds, and no other. */
static void check_the_place_the_mirror_keeps(void)
{
    const uint8_t     lock = MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS;
    mp_scene_mirror_t mirror;
    mp_scene_note_t   note;

    ut_section("the mirror keeps where the scene gathers, from its newest note");
    memset(&mirror, 0, sizeof mirror);
    note           = note_of(5u, 1u, MP_SCENE_PHASE_GATHERING, lock);
    note.anchor[0] = 12.0f;
    note.anchor[1] = -3.0f;
    note.anchor[2] = 40.0f;
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 0u);
    ut_check(mirror.anchor[0] == 12.0f && mirror.anchor[1] == -3.0f && mirror.anchor[2] == 40.0f,
             "a new scene brings its place");
    note.anchor[0] = 20.0f;
    ut_check(mp_scene_mirror_take(&mirror, &note, 1u, 10u) == MP_SCENE_TAKE_REPEAT &&
                 mirror.anchor[0] == 20.0f,
             "a repeat carrying a place read since is still a repeat, and brings the place");
    note           = note_of(4u, 1u, MP_SCENE_PHASE_OVER, lock);
    note.anchor[0] = 99.0f;
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 20u);
    note           = note_of(6u, 2u, MP_SCENE_PHASE_GATHERING, lock);
    note.anchor[0] = 99.0f;
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 30u);
    ut_check(mirror.anchor[0] == 20.0f,
             "an older scene's note and a note of another world move nothing");
    (void)mp_scene_mirror_leave(&mirror);
    ut_check(mirror.anchor[0] == 0.0f && mirror.anchor[1] == 0.0f && mirror.anchor[2] == 0.0f,
             "and the one exit forgets it");
}

/* Nine inputs, every sequence up to six long, then the exit: every raise that took the lock is
 * answered by exactly one release, and the lock is never taken while the player may not be. */
enum { M_GATHER, M_RUN, M_OVER, M_WARP, M_FOREIGN, M_HEARD_MAY, M_HEARD_NOT, M_SILENT, M_LEAVE,
       M_INPUTS };

static void check_every_mirror_sequence(void)
{
    unsigned total = 1u;
    unsigned code;
    unsigned bad_pairs = 0u;
    unsigned bad_raise = 0u;
    int      depth;

    for (depth = 0; depth < 6; ++depth) {
        total *= (unsigned)M_INPUTS;
    }
    ut_section("every sequence of a client's inputs up to six long releases each lock once");
    for (code = 0u; code < total; ++code) {
        mp_scene_mirror_t mirror;
        mp_scene_let_go_t why;
        uint32_t          now      = 0u;
        unsigned          rest     = code;
        unsigned          raised   = 0u;
        unsigned          released = 0u;
        uint16_t          serial   = 1u;

        memset(&mirror, 0, sizeof mirror);
        for (depth = 0; depth < 6; ++depth) {
            int             input = (int)(rest % (unsigned)M_INPUTS);
            mp_scene_note_t note;
            uint32_t        act = 0u;
            bool            was = mirror.locked;

            rest /= (unsigned)M_INPUTS;
            now += 100u;
            switch (input) {
            case M_GATHER:
                note = note_of(++serial, 1u, MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_LOCK);
                (void)mp_scene_mirror_take(&mirror, &note, 1u, now);
                break;
            case M_RUN:
                note = note_of(serial, 1u, MP_SCENE_PHASE_RUNNING, MP_SCENE_WHAT_LOCK);
                (void)mp_scene_mirror_take(&mirror, &note, 1u, now);
                break;
            case M_OVER:
                note = note_of(serial, 1u, MP_SCENE_PHASE_OVER, MP_SCENE_WHAT_LOCK);
                (void)mp_scene_mirror_take(&mirror, &note, 1u, now);
                break;
            case M_WARP:
                note = note_of(++serial, 1u, MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_WARP);
                (void)mp_scene_mirror_take(&mirror, &note, 1u, now);
                break;
            case M_FOREIGN:
                note = note_of(++serial, 2u, MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_LOCK);
                (void)mp_scene_mirror_take(&mirror, &note, 1u, now);
                break;
            case M_HEARD_MAY:
                act = mirror_at(&mirror, now, true, true, &why);
                break;
            case M_HEARD_NOT:
                act = mirror_at(&mirror, now, true, false, &why);
                if (!was && mirror.locked) {
                    ++bad_raise;
                }
                break;
            case M_SILENT:
                now += MP_SCENE_SILENCE_MS;
                act = mirror_at(&mirror, now, false, true, &why);
                break;
            case M_LEAVE:
            default:
                act = mp_scene_mirror_leave(&mirror);
                break;
            }
            if (!was && mirror.locked) {
                ++raised;
            }
            if ((act & MP_SCENE_MIRROR_LET_GO) != 0u) {
                ++released;
            }
        }
        if ((mp_scene_mirror_leave(&mirror) & MP_SCENE_MIRROR_LET_GO) != 0u) {
            ++released;
        }
        if (raised != released || mirror.locked) {
            ++bad_pairs;
        }
    }
    ut_checkf(bad_pairs == 0u, "%u sequence(s): a lock raised and not released exactly once in "
              "%u", total, bad_pairs);
    ut_checkf(bad_raise == 0u, "and a lock taken from a player who may not be moved %u time(s)",
              bad_raise);
}

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

static mp_scene_seat_look_t seat_look(uint32_t now, bool live, mp_scene_move_t move,
                                      bool fade_done, bool at_seat)
{
    mp_scene_seat_look_t look;

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
                 mp_scene_seat_fade_deadline(MP_SCENE_WARP_FADE_SECONDS) >= 32u,
             "a deadline is never shorter than the fade itself");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    look = seat_look(2u, true, MP_SCENE_MOVE_DEAD, true, false);
    ut_check(mp_scene_seat_step(&flow, &look) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_GIVEN_UP && flow.refused == MP_SCENE_MOVE_DEAD,
             "a player who dies in the dark is not moved, and gets the screen back");

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
             "a placement the body never took is given up at its deadline, the screen back");

    mp_scene_seat_start(&flow, 0u, MP_SCENE_FADE_SECONDS);
    look = seat_look(1u, true, MP_SCENE_MOVE_YES, false, false);
    (void)mp_scene_seat_step(&flow, &look);
    ut_check(mp_scene_seat_leave(&flow) == MP_SCENE_SEAT_ACT_FADE_IN &&
                 flow.stage == MP_SCENE_SEAT_IDLE && !flow.fade_held,
             "the exit in the dark gives the held fade back, which a session ending mid-level "
             "would otherwise leave black");
}

static void check_a_warp_writes_only_the_place(void)
{
    mp_scene_seat_flow_t flow;
    mp_scene_seat_look_t look;
    unsigned             count[4] = { 0u, 0u, 0u, 0u };
    uint32_t             now;

    ut_section("a client's warp is a fade, a place and a fade, and nothing of the hero");
    mp_scene_seat_start(&flow, 0u, MP_SCENE_WARP_FADE_SECONDS);
    for (now = 1u; now < 200u; ++now) {
        look = seat_look(now, true, MP_SCENE_MOVE_YES, now > 32u, now > 40u);
        ++count[mp_scene_seat_step(&flow, &look)];
    }
    ut_check(count[MP_SCENE_SEAT_ACT_FADE_OUT] == 1u && count[MP_SCENE_SEAT_ACT_PLACE] == 1u &&
                 count[MP_SCENE_SEAT_ACT_FADE_IN] == 1u,
             "one fade out, one placement and one fade in: the only three things the flow can "
             "ask for, so no health and no hero are written, which the engine's own respawn "
             "would have done");
    ut_check(flow.fade_seconds == MP_SCENE_WARP_FADE_SECONDS,
             "under the engine's own fade of a second");
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

static void check_a_late_warp(void)
{
    ut_section("a client hearing of a warp moves once, and not when it is there already");
    ut_check(mp_scene_warp_wanted(0u, 1u, true, 50.0f) == MP_SCENE_WARP_MOVE,
             "a first warp with a seat for this player far away: move");
    ut_check(mp_scene_warp_wanted(1u, 1u, true, 50.0f) == MP_SCENE_WARP_KNOWN,
             "the same warp again: known, never a second move");
    ut_check(mp_scene_warp_wanted(1u, 2u, false, 50.0f) == MP_SCENE_WARP_NO_SEAT,
             "a warp with no seat for this player, who was dead when it was handed out");
    ut_check(mp_scene_warp_wanted(1u, 2u, true, MP_SCENE_WARP_NEAR - 0.5f) ==
                 MP_SCENE_WARP_NEAR_ALREADY,
             "a player near the target already, come back there by its re-entry, stays");
    ut_check(mp_scene_warp_wanted(0u, 0u, true, 50.0f) == MP_SCENE_WARP_KNOWN,
             "no warp at all is nothing to follow");
}

int main(void)
{
    check_what_a_kind_says();
    check_a_lock_scene();
    check_the_bound_and_a_dead_host();
    check_a_hero_scene_and_its_grab();
    check_a_second_scene_and_a_warp();
    check_every_host_sequence();
    check_the_mirror();
    check_the_place_the_mirror_keeps();
    check_every_mirror_sequence();
    check_one_seat();
    check_a_warp_writes_only_the_place();
    check_every_seat_sequence();
    check_a_late_warp();
    return ut_summary("the scene's flow");
}
