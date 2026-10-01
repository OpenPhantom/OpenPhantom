/* What a scene for everybody waits for, and where the waiting ends: every far player gone, a host
 * that lies dead or cannot be moved, a client's warp that cannot be taken, a note that has to stand
 * on its own.
 *
 * The machines are mp_scene_flow's; mp_scene_flow.c in this directory walks their paths and every
 * short sequence of their inputs. What is here are the bounds on those waits, each driven the way
 * the host's and a client's bindings drive them. */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_scene_host_look_t host_look(uint32_t now, bool stands, bool seated, bool running)
{
    mp_scene_host_look_t look;

    memset(&look, 0, sizeof look);
    look.now             = now;
    look.host_stands     = stands;
    look.everyone_seated = seated;
    look.running         = running;
    return look;
}

/* The host's own seat as its binding steps it: live while the scene still gathers. */
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

/* ==============================================================================================
 * Every far player leaves.
 * ============================================================================================ */

/* A host whose clients have all gone holds no scene: the hold falls on the next look whatever the
 * seats say, the actor and the grab are free again, and the host's own seat, in the dark or not
 * yet, gives its fade back. Without this a host stays held there, with a black screen when its
 * own seat was fading, because nothing steps the gathering once nobody is joined. */
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
        (void)mp_scene_host_begin(&flow, kinds[i], 0u);
        mp_scene_seat_start(&seat, 0u, MP_SCENE_FADE_SECONDS);
        look = host_look(1u, true, false, false);
        (void)mp_scene_host_step(&flow, &look);
        act = step_own_seat(&seat, &flow, 1u);
        ut_check(flow.holds && act == MP_SCENE_SEAT_ACT_FADE_OUT && seat.fade_held,
                 "it holds while a far player walks to its seat, and the host's own seat is dark");

        look       = host_look(2u, true, false, false);
        look.alone = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_checkf(!flow.holds && flow.phase == MP_SCENE_PHASE_RUNNING &&
                      flow.released == MP_SCENE_RELEASE_ALONE,
                  "the last far player leaves: the hold falls on the next look and the scene runs "
                  "as it would alone (phase %u, released %u)", (unsigned)flow.phase,
                  (unsigned)flow.released);
        act = step_own_seat(&seat, &flow, 2u);
        ut_check(act == MP_SCENE_SEAT_ACT_FADE_IN && !seat.fade_held &&
                     seat.stage == MP_SCENE_SEAT_GIVEN_UP,
                 "and the host's own seat gives its held fade back at once");
        ut_checkf(strcmp(mp_scene_release_text(flow.released),
                         mp_scene_release_text(MP_SCENE_RELEASE_SEATED)) != 0,
                  "the host's line says why: because %s", mp_scene_release_text(flow.released));
    }

    ut_section("a host alone while its scene is still being gathered for");
    {
        mp_scene_host_flow_t flow;
        mp_scene_seat_flow_t seat;
        mp_scene_host_look_t look;

        memset(&flow, 0, sizeof flow);
        (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u);
        mp_scene_seat_start(&seat, 0u, MP_SCENE_FADE_SECONDS);
        look       = host_look(1u, false, false, false);
        look.alone = true;
        (void)mp_scene_host_step(&flow, &look);
        ut_check(!flow.holds && flow.released == MP_SCENE_RELEASE_ALONE,
                 "even with the host dead: with nobody else in the session there is nobody to "
                 "play the scene for, and the engine plays it as it would");
        ut_check(step_own_seat(&seat, &flow, 1u) == MP_SCENE_SEAT_ACT_NONE &&
                     seat.stage == MP_SCENE_SEAT_GIVEN_UP && !seat.fade_held,
                 "a seat not yet dark is given up with nothing to give back");
    }
}

/* ==============================================================================================
 * A host that lies dead, or cannot be moved.
 * ============================================================================================ */

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

static bool for_all(const mp_scene_host_flow_t *flow)
{
    return mp_scene_for_all_now((uint8_t)flow->phase, mp_scene_what_of(flow->kind));
}

/* A host dead when its scene begins, whom the script meant himself, so he has no seat to wait for:
 * the far players are all seated at once, and the old hold fell on that and let a lock scene play
 * on with a corpse. It holds until the host stands again, and runs then. */
static void check_a_host_dead_as_the_scene_begins(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a host dead at the beginning holds the scene until he stands again");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u);
    look          = host_look(0u, false, true, true);
    look.may_move = MP_SCENE_MOVE_DEAD;
    steps(&flow, 1u, 300u, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_GATHERING && flow.holds && for_all(&flow),
              "three hundred substeps dead with every far player seated: still held (phase %u)",
              (unsigned)flow.phase);
    look = host_look(301u, true, true, true);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.holds &&
                 flow.released == MP_SCENE_RELEASE_SEATED && !flow.given_up,
             "he stands again, back at the gathering by his re-entry: the scene runs for all");
}

/* A host dead for the whole wait: the scene is given up for everybody at the cap, and it never ran
 * for everybody with him dead. The engine's lock scene goes on here as it would alone, and a door
 * heard while it does is that scene's, not a new one; once it has ended here, a door is a scene of
 * its own again. */
static void check_a_host_dead_to_the_cap(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a host dead for twenty seconds: the scene is given up for everybody");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u);
    look          = host_look(0u, false, false, true);
    look.may_move = MP_SCENE_MOVE_DEAD;
    steps(&flow, 1u, MP_SCENE_WAIT_CAP_SUBSTEPS - 1u, &look);
    ut_check(flow.holds && !flow.given_up, "held to the substep before the cap");
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS, MP_SCENE_WAIT_CAP_SUBSTEPS, &look);
    ut_checkf(flow.given_up && flow.given_up_for == MP_SCENE_MOVE_DEAD && !flow.holds &&
                  flow.phase == MP_SCENE_PHASE_OVER && !for_all(&flow),
              "at the cap it is given up: over for everybody, nothing held, no scene for all "
              "(phase %u, given up %u)", (unsigned)flow.phase, (unsigned)flow.given_up);

    look.running = true;
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS + 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 200u, &look);
    ut_check(flow.given_up && flow.phase == MP_SCENE_PHASE_OVER && !for_all(&flow),
             "while the engine plays it here it stays over for everybody, however long");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 900u) == MP_SCENE_BEGIN_SECOND &&
                 !flow.holds && flow.serial == 1u,
             "and a lock door of it is that scene's, counted and not gathered");
    look.running = false;
    steps(&flow, 901u, 901u, &look);
    ut_check(!flow.given_up && flow.phase == MP_SCENE_PHASE_NONE,
             "when the engine's scene ends here, none runs");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 902u) == MP_SCENE_BEGIN_NEW,
             "and the next door is a scene of its own again");
}

/* A hero scene whose host stands at a gun, in the water or behind a push block: the engine's grab
 * cannot take him, and a fixed grace of two seconds used to end the scene for the clients while
 * the engine went on waiting and played it later, ungathered. Now it waits until he may be moved,
 * and the grab comes. */
static void check_a_host_that_cannot_be_moved(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a hero scene waits for a host who may not be moved, longer than the grace");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u);
    look = host_look(1u, true, true, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.seen_running,
             "everybody seated, the host at his gun: released, and no grab yet");
    look          = host_look(0u, true, true, false);
    look.may_move = MP_SCENE_MOVE_MODE;
    steps(&flow, 2u, 300u, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_RUNNING && for_all(&flow) && !flow.given_up,
              "three hundred substeps at the gun: still running for everybody, the clients held "
              "(phase %u)", (unsigned)flow.phase);
    look = host_look(301u, true, true, true);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && flow.seen_running,
             "he leaves the gun, and the grab comes: the scene plays for everybody");

    ut_section("the grace counts only the substeps the host could have been taken");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u);
    look = host_look(1u, true, true, false);
    (void)mp_scene_host_step(&flow, &look);
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
    ut_check(flow.phase == MP_SCENE_PHASE_OVER && !flow.given_up,
             "the sixty fourth: a grab that did not come for a host it could take is not coming, "
             "and the scene is over as one that never ran");

    ut_section("a host who may not be moved for twenty seconds: given up for everybody");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 0u);
    look = host_look(1u, true, true, false);
    (void)mp_scene_host_step(&flow, &look);
    look          = host_look(0u, true, true, false);
    look.may_move = MP_SCENE_MOVE_MODE;
    steps(&flow, 2u, MP_SCENE_WAIT_CAP_SUBSTEPS, &look);
    ut_checkf(flow.given_up && flow.given_up_for == MP_SCENE_MOVE_MODE &&
                  flow.phase == MP_SCENE_PHASE_OVER && !for_all(&flow),
              "at the cap, counted from the beginning (phase %u, given up %u)",
              (unsigned)flow.phase, (unsigned)flow.given_up);
    look.may_move = MP_SCENE_MOVE_YES;
    look.running  = true;
    steps(&flow, MP_SCENE_WAIT_CAP_SUBSTEPS + 1u, MP_SCENE_WAIT_CAP_SUBSTEPS + 100u, &look);
    ut_check(flow.given_up && !for_all(&flow) &&
                 mp_scene_host_begin(&flow, MP_SCENE_KIND_HERO, 800u) == MP_SCENE_BEGIN_SECOND,
             "the engine grabs him later and plays it here alone: still no scene for everybody, "
             "and a door of it is no scene of its own");
    look.running = false;
    steps(&flow, 801u, 801u, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_NONE && !flow.given_up, "until it ends here");
}

/* A gathering that found no place to seat anybody, its anchor unreadable or on a mover, over water
 * or a drop: every player stays where it stands and nobody is waited for, so the hold falls at the
 * first look with the host standing. It used to fall as "everyone stood at their seat", which is
 * true of nobody, and was counted as a success. */
static void check_a_scene_that_gathered_nobody(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    ut_section("a scene that gathered nobody says so, and does not read as everybody seated");
    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u);
    look                 = host_look(1u, false, true, true);
    look.may_move        = MP_SCENE_MOVE_DEAD;
    look.nobody_gathered = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.holds, "with the host dead it waits for him all the same");
    look = host_look(2u, true, true, true);
    look.nobody_gathered = true;
    (void)mp_scene_host_step(&flow, &look);
    ut_checkf(flow.phase == MP_SCENE_PHASE_RUNNING && flow.released == MP_SCENE_RELEASE_NOBODY,
              "he stands: it runs, released because %s", mp_scene_release_text(flow.released));
    ut_check(strcmp(mp_scene_release_text(MP_SCENE_RELEASE_NOBODY),
                    mp_scene_release_text(MP_SCENE_RELEASE_SEATED)) != 0,
             "and the line does not say that everyone stood at their seat");
}

/* The hold that falls when everybody with a seat stands at it: a far player no seat answered for is
 * not at a seat, and a field run counted a scene as "with every far player at their seat" with
 * slot 1 held 38 units away. `arrived` of `wanted` stood at their seat and `unseated` had none. */
static mp_scene_release_t released_with(uint32_t arrived, uint32_t wanted, uint32_t unseated)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;

    memset(&flow, 0, sizeof flow);
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 0u);
    look          = host_look(1u, true, arrived == wanted, false);
    look.unseated = unseated;
    (void)mp_scene_host_step(&flow, &look);
    return flow.released;
}

static void check_a_scene_with_a_player_no_seat_answered_for(void)
{
    ut_section("everybody with a seat at it, and one far player with none, is not everybody");
    ut_check(released_with(1u, 1u, 1u) == MP_SCENE_RELEASE_SEATED_SOME,
             "one seated and at the seat, one with none: everybody with a seat stood at it");
    ut_check(released_with(1u, 1u, 0u) == MP_SCENE_RELEASE_SEATED,
             "one seated and at the seat, nobody without: everybody stood at their seat");
    ut_check(released_with(0u, 0u, 1u) == MP_SCENE_RELEASE_SEATED_SOME,
             "nobody far seated, one with none, as in the field run: not everybody either");
    ut_check(released_with(0u, 1u, 1u) == MP_SCENE_RELEASE_NONE,
             "and a seated player not at the seat yet holds the scene as before");
    ut_check(strcmp(mp_scene_release_text(MP_SCENE_RELEASE_SEATED_SOME),
                    mp_scene_release_text(MP_SCENE_RELEASE_SEATED)) != 0,
             "the line says which of the two it was");
}

/* ==============================================================================================
 * A client's warp.
 * ============================================================================================ */

static mp_scene_seat_act_t seat_at(mp_scene_seat_flow_t *seat, uint32_t now, mp_scene_move_t move)
{
    mp_scene_seat_look_t look;

    memset(&look, 0, sizeof look);
    look.now      = now;
    look.live     = true;   /* the host's note keeps the warp's number all along */
    look.may_move = move;
    return mp_scene_seat_step(seat, &look);
}

/* A client's seat for a warp begun while its player was dead, swimming or at a gun waited for as
 * long as the host's note kept the warp's number, and moved the player minutes later. It waits ten
 * seconds and is given up then, with nothing held; a gathering's seat keeps no such bound, its
 * scene ends it. */
static void check_a_warp_seat_waits_ten_seconds(void)
{
    mp_scene_seat_flow_t seat;
    uint32_t             now;
    mp_scene_seat_act_t  act = MP_SCENE_SEAT_ACT_NONE;

    ut_section("a client's warp seat waits ten seconds for its body, and is given up then");
    mp_scene_seat_start(&seat, 100u, MP_SCENE_WARP_FADE_SECONDS);
    seat.wait_max = MP_SCENE_WARP_WAIT_SUBSTEPS;
    for (now = 101u; now < 100u + MP_SCENE_WARP_WAIT_SUBSTEPS; ++now) {
        act = seat_at(&seat, now, MP_SCENE_MOVE_DEAD);
    }
    ut_check(seat.stage == MP_SCENE_SEAT_WAITING && act == MP_SCENE_SEAT_ACT_NONE,
             "dead for a substep short of ten seconds: still waiting");
    act = seat_at(&seat, 100u + MP_SCENE_WARP_WAIT_SUBSTEPS, MP_SCENE_MOVE_DEAD);
    ut_checkf(seat.stage == MP_SCENE_SEAT_GIVEN_UP && seat.waited_out && !seat.fade_held &&
                  act == MP_SCENE_SEAT_ACT_NONE && seat.refused == MP_SCENE_MOVE_DEAD,
              "at ten seconds it is given up, never dark, the reason kept (stage %u)",
              (unsigned)seat.stage);
    ut_check(seat_at(&seat, 100u + MP_SCENE_WARP_WAIT_SUBSTEPS + 1u, MP_SCENE_MOVE_YES) ==
                 MP_SCENE_SEAT_ACT_NONE,
             "and a body movable after that is not moved any more");

    mp_scene_seat_start(&seat, 0u, MP_SCENE_WARP_FADE_SECONDS);
    seat.wait_max = MP_SCENE_WARP_WAIT_SUBSTEPS;
    (void)seat_at(&seat, 50u, MP_SCENE_MOVE_MODE);
    ut_check(seat_at(&seat, 60u, MP_SCENE_MOVE_YES) == MP_SCENE_SEAT_ACT_FADE_OUT &&
                 !seat.waited_out,
             "one movable within the ten seconds goes under the fade as before");

    mp_scene_seat_start(&seat, 0u, MP_SCENE_FADE_SECONDS);
    for (now = 1u; now <= 2000u; ++now) {
        (void)seat_at(&seat, now, MP_SCENE_MOVE_MODE);
    }
    ut_check(seat.stage == MP_SCENE_SEAT_WAITING && !seat.waited_out,
             "a gathering's seat has no bound of its own: its scene ends the wait");
}

/* ==============================================================================================
 * A note that stands on its own.
 * ============================================================================================ */

static mp_scene_note_t scene_note(uint16_t serial, uint8_t phase, uint8_t what, uint8_t my_slot)
{
    mp_scene_note_t note;

    memset(&note, 0, sizeof note);
    note.serial              = serial;
    note.generation          = 1u;
    note.phase               = phase;
    note.what                = what;
    note.trigger_slot        = MP_SCENE_TRIGGER_UNKNOWN;
    note.seats               = 1u;
    note.seat[0].slot        = my_slot;
    note.seat[0].position[0] = 40.0f;
    return note;
}

/* The channel keeps only the newest copy of the scene's note, so a gathering note still on its way
 * can be replaced by the running one, and a client that only ever read "running" was never
 * gathered. Every note of the scene carries the seats, so any of them gathers a client the scene
 * has not gathered yet, and its seat lives as long as the scene runs for everybody. */
static void check_a_running_note_gathers_a_client(void)
{
    const uint8_t        lock = MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS;
    mp_scene_note_t      note = scene_note(7u, MP_SCENE_PHASE_RUNNING, lock, 2u);
    mp_scene_mirror_t    mirror;
    mp_scene_seat_flow_t seat;
    mp_scene_seat_look_t look;
    bool                 seat_given;

    ut_section("a running note gathers a client the gathering note never reached");
    seat_given = mp_scene_note_seat_of(&note, 2u) != NULL;
    ut_check(seat_given && mp_scene_note_gathers(&note, 0u, seat_given),
             "the first note this client reads says running, and it carries this client's seat: "
             "it gathers");
    ut_check(!mp_scene_note_gathers(&note, 7u, seat_given),
             "a scene that has gathered this client does not gather it twice");
    ut_check(!mp_scene_note_gathers(&note, 0u, false), "a note with no seat for it gathers nobody");
    note.phase = MP_SCENE_PHASE_OVER;
    ut_check(!mp_scene_note_gathers(&note, 0u, true), "a scene that is over gathers nobody");
    note = scene_note(8u, MP_SCENE_PHASE_GATHERING, MP_SCENE_WHAT_WARP, 2u);
    ut_check(!mp_scene_note_gathers(&note, 0u, true),
             "a warp is followed by its own number, not gathered for");

    ut_section("and the seat it hands out lives while the scene runs for everybody");
    memset(&mirror, 0, sizeof mirror);
    note = scene_note(7u, MP_SCENE_PHASE_RUNNING, lock, 2u);
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 0u);
    ut_check(mp_scene_seat_wanted(&mirror, 7u, false),
             "a gathering's seat of a running scene is still wanted");
    mp_scene_seat_start(&seat, 0u, MP_SCENE_FADE_SECONDS);
    memset(&look, 0, sizeof look);
    look.now      = 1u;
    look.live     = mp_scene_seat_wanted(&mirror, 7u, false);
    look.may_move = MP_SCENE_MOVE_YES;
    ut_check(mp_scene_seat_step(&seat, &look) == MP_SCENE_SEAT_ACT_FADE_OUT,
             "and the client goes under the fade to its seat");
    note.phase = MP_SCENE_PHASE_OVER;
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 10u);
    ut_check(!mp_scene_seat_wanted(&mirror, 7u, false) && mp_scene_seat_wanted(&mirror, 7u, true),
             "once the scene is over a gathering's seat is not wanted; a warp's lives by its "
             "number");
    note = scene_note(9u, MP_SCENE_PHASE_GATHERING, lock, 2u);
    (void)mp_scene_mirror_take(&mirror, &note, 1u, 20u);
    ut_check(!mp_scene_seat_wanted(&mirror, 7u, false) && !mp_scene_seat_wanted(&mirror, 7u, true),
             "and a newer scene ends every seat of an older one");
}

int main(void)
{
    check_every_far_player_leaves();
    check_a_host_dead_as_the_scene_begins();
    check_a_host_dead_to_the_cap();
    check_a_host_that_cannot_be_moved();
    check_a_scene_that_gathered_nobody();
    check_a_scene_with_a_player_no_seat_answered_for();
    check_a_warp_seat_waits_ten_seconds();
    check_a_running_note_gathers_a_client();
    return ut_summary("what a scene for everybody waits for");
}
