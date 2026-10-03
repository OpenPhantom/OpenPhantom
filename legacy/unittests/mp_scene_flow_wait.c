/* What a scene of the host's waits for, and where the waiting ends: every far player gone, a host
 * that lies dead or cannot be moved, a host with no place, a lock dropped as no scene of the
 * host's, a hero's door behind a lock's, and a seat that sets itself a bound.
 *
 * The machines are mp_scene_flow's; mp_scene_flow.c in this directory walks their paths and every
 * short sequence of their inputs. What is here are the bounds on those waits, each driven the way
 * the host's binding drives them. */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A look at a host with a place: standing or not, at the place or away from it. */
static mp_scene_host_look_t host_look(uint32_t now, bool stands, bool at_place, bool running)
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

/* The host's own seat as its binding steps it: live while the scene is still held. */
static mp_scene_seat_act_t step_own_seat(mp_scene_seat_flow_t *seat,
                                         const mp_scene_host_flow_t *flow, uint32_t now)
{
    mp_scene_seat_look_t look;

    memset(&look, 0, sizeof look);
    look.now      = now;
    look.live     = flow->phase == MP_SCENE_PHASE_GATHERING;
    look.may_move = MP_SCENE_MOVE_YES;
    return mp_scene_seat_step(seat, &look);
}

/* Steps the host's scene from `from` to `to`, one look a substep, all alike but the clock. */
static void steps(mp_scene_host_flow_t *flow, uint32_t from, uint32_t to,
                  const mp_scene_host_look_t *like)
{
    mp_scene_host_look_t look = *like;
    uint32_t             now;

    for (now = from; now <= to; ++now) {
        look.now = now;
        (void)mp_scene_host_step(flow, &look);
    }
}

/* ==============================================================================================
 * Every far player leaves.
 * ============================================================================================ */

/* A host whose clients have all gone holds no scene for anybody: with no place to go to, the hold
 * falls on the next look, dead or alive. A host on his way to a place finishes it, because the
 * place is read already and the hero's grab would otherwise take him wherever the fade left
 * him. */
static void check_every_far_player_leaves(void)
{
    static const mp_scene_kind_t kinds[2] = { MP_SCENE_KIND_LOCK, MP_SCENE_KIND_HERO };
    static const char *const     names[2] = { "a lock scene", "a hero scene" };
    size_t                       i;

    for (i = 0u; i < 2u; ++i) {
        mp_scene_host_flow_t flow;
        mp_scene_seat_flow_t seat;
        mp_scene_host_look_t look;
        mp_scene_seat_act_t  act;

        ut_section(names[i]);
        memset(&flow, 0, sizeof flow);
        (void)mp_scene_host_begin(&flow, kinds[i], 0u, true);
        look           = host_look(1u, true, false, false);
        look.has_place = false;
        look.away      = false;
        look.place_pending = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_check(flow.holds, "it holds while the place of the far player is still read");
        look.now   = 2u;
        look.alone = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_checkf(!flow.holds && flow.phase == MP_SCENE_PHASE_RUNNING &&
                      flow.released == MP_SCENE_RELEASE_ALONE,
                  "the last far player leaves before the host has a place: the hold falls on "
                  "the next look and the scene runs as it would alone (phase %u, released %u)",
                  (unsigned)flow.phase, (unsigned)flow.released);

        memset(&flow, 0, sizeof flow);
        (void)mp_scene_host_begin(&flow, kinds[i], 0u, true);
        mp_scene_seat_start_gathering(&seat, 0u, MP_SCENE_FADE_SECONDS,
                                      MP_SCENE_HARD_HOST_SUBSTEPS);
        look = host_look(1u, true, false, false);
        (void)mp_scene_host_step(&flow, &look);
        act = step_own_seat(&seat, &flow, 1u);
        ut_check(flow.holds && act == MP_SCENE_SEAT_ACT_FADE_OUT && seat.fade_held,
                 "it holds while the host is on his way, and his seat is dark");
        look       = host_look(2u, true, false, false);
        look.alone = true;
        (void)mp_scene_host_step(&flow, &look);
        act = step_own_seat(&seat, &flow, 2u);
        ut_check(flow.holds && flow.phase == MP_SCENE_PHASE_GATHERING &&
                     seat.stage != MP_SCENE_SEAT_GIVEN_UP,
                 "the last far player leaves while the host is on his way: the hold stands and "
                 "his seat is not given up, so he is not left where the fade found him");
        look       = host_look(3u, true, true, false);
        look.alone = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_check(!flow.holds && flow.released == MP_SCENE_RELEASE_AT_THE_PLACE,
                 "he reaches the place: released there, as if somebody were still with him");
        ut_checkf(strcmp(mp_scene_release_text(MP_SCENE_RELEASE_ALONE),
                         mp_scene_release_text(MP_SCENE_RELEASE_AT_THE_PLACE)) != 0,
                  "the host's line tells the two apart: because %s",
                  mp_scene_release_text(MP_SCENE_RELEASE_ALONE));
    }

    ut_section("a host alone and dead, with no place");
    {
        mp_scene_host_flow_t flow;
        mp_scene_host_look_t look;

        memset(&flow, 0, sizeof flow);
        (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, true);
        look           = host_look(1u, false, true, false);
        look.has_place = false;
        look.alone     = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_check(!flow.holds && flow.released == MP_SCENE_RELEASE_ALONE,
                 "even with the host dead: with nobody else in the session there is nobody "
                 "whose place he would take, and the engine plays it as it would");
    }
}

/* ==============================================================================================
 * A host that lies dead, or cannot be moved.
 * ============================================================================================ */

/* A host dead for the whole wait of a hero scene: the scene is given up at the cap, and it never
 * ran with him dead. The engine's scene goes on here as it would alone, and it still stands: a
 * door heard while it does is that scene's, not a new one. Once it has ended here, a door is a
 * scene of its own again. */
static void check_a_host_dead_to_the_cap(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a host dead for twenty seconds: a hero scene is given up");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, true);
    look = host_look(0u, false, false, true);
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS - 1u, &look);
    ut_check(flow.holds && !flow.given_up, "held to the substep before the cap");
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS, MP_SCENE_WAIT_CAP_SUBSTEPS, &look);
    ut_checkf(flow.given_up && flow.given_up_for == MP_SCENE_MOVE_DEAD && !flow.holds &&
                  flow.phase == MP_SCENE_PHASE_OVER && mp_scene_host_stands_now(&flow),
              "at the cap it is given up: over, nothing held, and standing still, because the "
              "engine may yet play it here (phase %u, given up %u)", (unsigned)flow.phase,
              (unsigned)flow.given_up);

    look.running = true;
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS + 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 200u, &look);
    ut_check(flow.given_up && flow.phase == MP_SCENE_PHASE_OVER &&
                 mp_scene_host_still_running(&flow),
             "while the engine plays it here it stays given up, however long");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 900u, true) ==
                     MP_SCENE_BEGIN_SECOND &&
                 !flow.holds && flow.serial == 1u,
             "and a lock door of it is that scene's, counted and holding nothing");
    look.running = false;
    steps(&flow, 901u, 901u, &look);
    ut_check(!flow.given_up && flow.phase == MP_SCENE_PHASE_NONE &&
                 !mp_scene_host_stands_now(&flow),
             "when the engine's scene ends here, none runs and none stands");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 902u, true) == MP_SCENE_BEGIN_NEW,
             "and the next door is a scene of its own again");
}

/* A hero scene whose host stands at a gun, in the water or behind a push block: the engine's grab
 * cannot take him, so the scene waits until he may be moved, and the grab comes. */
static void check_a_host_that_cannot_be_moved(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a hero scene waits for a host who may not be moved, longer than the grace");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, false);
    look          = host_look(0u, true, true, false);
    look.may_move = MP_SCENE_MOVE_MODE;
    steps(&flow, 1u, 300u, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_RUNNING && mp_scene_host_stands_now(&flow) &&
                  !flow.given_up,
              "three hundred substeps at the gun: still running and standing (phase %u)",
              (unsigned)flow.phase);
    look = host_look(301u, true, true, true);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && flow.seen_running,
             "he leaves the gun, and the grab comes: the scene plays");

    ut_section("the grace counts only the substeps the host could have been taken");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 1u, false);
    look = host_look(0u, true, true, false);
    steps(&flow, 2u, 31u, &look);                  /* thirty takeable */
    look.may_move = MP_SCENE_MOVE_MODE;
    steps(&flow, 32u, 331u, &look);                /* three hundred at the gun */
    look.may_move = MP_SCENE_MOVE_YES;
    steps(&flow, 332u, 332u + MP_SCENE_GRAB_GRACE_SUBSTEPS - 32u, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING, "sixty three takeable substeps: still waiting");
    steps(&flow, 333u + MP_SCENE_GRAB_GRACE_SUBSTEPS - 32u,
          333u + MP_SCENE_GRAB_GRACE_SUBSTEPS - 32u,
          &look);
    ut_check(flow.phase == MP_SCENE_PHASE_NONE && !flow.given_up,
             "the sixty fourth: a grab that did not come for a host it could take is not coming, "
             "and the scene is none as one that never ran");

    ut_section("a host who may not be moved for twenty seconds: given up");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, false);
    look          = host_look(0u, true, true, false);
    look.may_move = MP_SCENE_MOVE_MODE;
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS, &look);
    ut_checkf(flow.given_up && flow.given_up_for == MP_SCENE_MOVE_MODE &&
                  flow.phase == MP_SCENE_PHASE_OVER,
              "at the cap, counted from the beginning (phase %u, given up %u)",
              (unsigned)flow.phase, (unsigned)flow.given_up);
    look.may_move = MP_SCENE_MOVE_YES;
    look.running  = true;
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS + 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 100u, &look);
    ut_check(flow.given_up &&
                 mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 800u, true) ==
                     MP_SCENE_BEGIN_SECOND,
             "the engine grabs him later and plays it here: a door of it is no scene of its "
             "own");
    look.running = false;
    steps(&flow, 801u, 801u, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_NONE && !flow.given_up, "until it ends here");
}

/* A hero scene whose host has no place: the far player's could not be read, or stands on a mover,
 * over water or a drop, and none beside the scene's actor either. The host stays where he stands
 * and nothing is waited for, so the hold falls at the first look with the host standing. */
static void check_a_host_with_no_place(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("the host of a hero scene with no place is released where he stands");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u, true);
    look           = host_look(1u, false, true, true);
    look.has_place = false;
    look.no_place  = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.holds, "with the host dead it waits for him all the same");
    look           = host_look(2u, true, true, true);
    look.has_place = false;
    look.no_place  = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_RUNNING && flow.released == MP_SCENE_RELEASE_NOBODY,
              "he stands: it runs, released because %s", mp_scene_release_text(flow.released));
    ut_check(strcmp(mp_scene_release_text(MP_SCENE_RELEASE_NOBODY),
                    mp_scene_release_text(MP_SCENE_RELEASE_AT_THE_PLACE)) != 0,
             "and the line does not say that he stood at his place");

    ut_section("while the place is still read the hold stands, whatever else is true");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look               = host_look(1u, true, true, false);
    look.has_place     = false;
    look.place_pending = true;
    steps(&flow, 1u, 40u, &look);
    ut_check(flow.holds && flow.phase == MP_SCENE_PHASE_GATHERING,
             "forty looks with the host standing and no place yet: held");
}

/* A lock a far player set off is played where that player stands, a lift above all. A host who has
 * no place there, or cannot be brought there before the wait runs out, has no part in it: the lock
 * is dropped, nothing of it stands here, and the binding is told why in the substep it happens. A
 * hero scene is never dropped. */
static void check_a_lock_that_is_dropped(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a lock with no place for the host is dropped at the first look");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look           = host_look(1u, true, true, true);
    look.has_place = false;
    look.no_place  = true;
    ut_check(mp_scene_host_step(&flow, &look), "the phase changes on that look");
    ut_checkf(flow.phase == MP_SCENE_PHASE_NONE && !flow.holds && !flow.given_up &&
                  flow.dropped == MP_SCENE_DROP_NO_PLACE && !mp_scene_host_stands_now(&flow) &&
                  flow.released == MP_SCENE_RELEASE_NONE,
              "none stands, nothing is held, it was not given up and not released: dropped "
              "because %s", mp_scene_drop_text(flow.dropped));

    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look           = host_look(1u, false, true, true);
    look.has_place = false;
    look.no_place  = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_NONE && flow.dropped == MP_SCENE_DROP_NO_PLACE,
             "with the host dead as well: a dead host is not waited for at a place he has not");

    ut_section("a lock whose host cannot be brought there is dropped at the cap, not given up");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look = host_look(0u, false, false, true);
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS - 1u, &look);
    ut_check(flow.holds && flow.dropped == MP_SCENE_DROP_NONE,
             "held to the substep before the cap");
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS, MP_SCENE_WAIT_CAP_SUBSTEPS, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_NONE && !flow.given_up && !flow.holds &&
                  flow.dropped == MP_SCENE_DROP_AT_THE_CAP,
              "at the cap: dropped because %s", mp_scene_drop_text(flow.dropped));
    ut_check(strcmp(mp_scene_drop_text(MP_SCENE_DROP_NO_PLACE),
                    mp_scene_drop_text(MP_SCENE_DROP_AT_THE_CAP)) != 0,
             "and the line tells the two apart");

    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look                = host_look(0u, true, false, false);
    look.own_respawning = true;
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 40u, &look);
    ut_check(flow.holds && flow.dropped == MP_SCENE_DROP_NONE,
             "past the cap while the engine's respawn brings the host: still held");

    ut_section("the one exit and the next door take the reason away");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, true);
    look           = host_look(1u, true, true, true);
    look.has_place = false;
    look.no_place  = true;
    (void)mp_scene_host_step(&flow, &look);
    (void)mp_scene_host_leave(&flow);
    ut_check(flow.dropped == MP_SCENE_DROP_NONE, "the exit forgets it");
    look.no_place = true;
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 2u, true);
    look.now = 3u;
    (void)mp_scene_host_step(&flow, &look);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 4u, true);
    ut_check(flow.dropped == MP_SCENE_DROP_NONE && flow.holds,
             "and so does a new scene begun without it");

    ut_section("a lock the host set off himself is never dropped");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u, false);
    look          = host_look(1u, true, true, true);
    look.no_place = true;
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 10u, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && flow.dropped == MP_SCENE_DROP_NONE,
             "it runs for as long as the engine plays it");
}

/* One script raises the lock and spawns the hero right behind it. The hero's door is the same
 * scene's, and the scene is a hero scene from there: looked for a place as one, and given up at
 * the cap rather than dropped. */
static void check_a_hero_behind_the_lock(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a hero's door in the substep the lock began the scene makes it a hero scene");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 7u, true);
    ut_check(mp_scene_host_takes_the_hero(&flow, 7u) && flow.kind == MP_SCENE_KIND_HERO &&
                 flow.holds && flow.phase == MP_SCENE_PHASE_GATHERING && flow.serial == 1u,
             "the same scene, still held, now a hero's");
    ut_check(!mp_scene_host_takes_the_hero(&flow, 7u),
             "a second hero's door changes nothing more");
    look           = host_look(8u, true, true, true);
    look.has_place = false;
    look.no_place  = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && flow.dropped == MP_SCENE_DROP_NONE &&
                 flow.released == MP_SCENE_RELEASE_NOBODY,
             "with no place it is released where the host stands, as a hero scene is, not "
             "dropped");

    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 7u, false);
    ut_check(mp_scene_host_takes_the_hero(&flow, 7u) && flow.kind == MP_SCENE_KIND_HERO &&
                 flow.phase == MP_SCENE_PHASE_RUNNING,
             "a scene the host set off himself becomes one the same way, and runs on");

    ut_section("a substep later, or in any other scene, it is no door of this scene");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 7u, true);
    ut_check(!mp_scene_host_takes_the_hero(&flow, 8u) && flow.kind == MP_SCENE_KIND_LOCK,
             "a hero's door one substep after the lock's");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 7u, true);
    ut_check(!mp_scene_host_takes_the_hero(&flow, 7u), "a scene that is a hero's already");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_WARP, 7u, false);
    ut_check(!mp_scene_host_takes_the_hero(&flow, 7u) && flow.kind == MP_SCENE_KIND_WARP,
             "a warp");
    memset(&flow, 0, sizeof flow);
    ut_check(!mp_scene_host_takes_the_hero(&flow, 0u) && !mp_scene_host_takes_the_hero(NULL, 0u),
             "no scene at all, and no flow");
}

/* ==============================================================================================
 * A seat with a bound of its own.
 * ============================================================================================ */

static mp_scene_seat_act_t seat_at(mp_scene_seat_flow_t *seat, uint32_t now, mp_scene_move_t move)
{
    mp_scene_seat_look_t look;

    memset(&look, 0, sizeof look);
    look.now      = now;
    look.live     = true;
    look.may_move = move;
    return mp_scene_seat_step(seat, &look);
}

/* A seat that nothing else ends waits for its body only as long as it says: ten seconds, and it
 * is given up then, with nothing held. A gathering's seat keeps no such bound, its scene ends
 * it. */
static void check_a_seat_with_a_bound(void)
{
    mp_scene_seat_flow_t seat;
    uint32_t             now;
    mp_scene_seat_act_t  act = MP_SCENE_SEAT_ACT_NONE;

    ut_section("a seat with a bound waits ten seconds for its body, and is given up then");
    mp_scene_seat_start(&seat, 100u, MP_SCENE_FADE_SECONDS);
    seat.wait_max = MP_SCENE_WARP_WAIT_SUBSTEPS;
    for (now = 101u; now < 100u + MP_SCENE_WARP_WAIT_SUBSTEPS; ++now) {
        act = seat_at(&seat, now, MP_SCENE_MOVE_DEAD);
    }
    ut_check(seat.stage == MP_SCENE_SEAT_WAITING && act == MP_SCENE_SEAT_ACT_NONE,
             "dead for a substep short of ten seconds: still waiting");
    act = seat_at(&seat, 100u + MP_SCENE_WARP_WAIT_SUBSTEPS, MP_SCENE_MOVE_DEAD);
    ut_checkf(seat.stage == MP_SCENE_SEAT_GIVEN_UP && seat.waited_out && !seat.fade_held &&
                  act == MP_SCENE_SEAT_ACT_NONE && seat.refused == MP_SCENE_MOVE_DEAD &&
                  seat.ended == MP_SCENE_SEAT_END_WAITED_OUT,
              "at ten seconds it is given up, never dark, the reason kept (stage %u)",
              (unsigned)seat.stage);
    ut_check(seat_at(&seat, 100u + MP_SCENE_WARP_WAIT_SUBSTEPS + 1u, MP_SCENE_MOVE_YES) ==
                 MP_SCENE_SEAT_ACT_NONE,
             "and a body movable after that is not moved any more");

    mp_scene_seat_start(&seat, 0u, MP_SCENE_FADE_SECONDS);
    seat.wait_max = MP_SCENE_WARP_WAIT_SUBSTEPS;
    (void)seat_at(&seat, 50u, MP_SCENE_MOVE_MODE);
    ut_check(seat_at(&seat, 60u, MP_SCENE_MOVE_YES) == MP_SCENE_SEAT_ACT_FADE_OUT &&
                 !seat.waited_out,
             "one movable within the ten seconds goes under the fade");

    mp_scene_seat_start_gathering(&seat, 0u, MP_SCENE_FADE_SECONDS,
                                  MP_SCENE_HARD_CLIENT_SUBSTEPS);
    for (now = 1u; now <= 2000u; ++now) {
        (void)seat_at(&seat, now, MP_SCENE_MOVE_MODE);
    }
    ut_check(seat.stage == MP_SCENE_SEAT_WAITING && !seat.waited_out,
             "a gathering's seat has no bound of its own: its scene ends the wait");
}

int main(void)
{
    check_every_far_player_leaves();
    check_a_host_dead_to_the_cap();
    check_a_host_that_cannot_be_moved();
    check_a_host_with_no_place();
    check_a_lock_that_is_dropped();
    check_a_hero_behind_the_lock();
    check_a_seat_with_a_bound();
    return ut_summary("what a scene of the host's waits for");
}
