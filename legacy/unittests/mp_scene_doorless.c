/* A scene that runs on the host with no door heard: when the host takes it over, and how its
 * flow stands then.
 *
 * The case in front of it: a savegame loaded in the middle of a scene restores the lock, the
 * bars, the camera and the actor that drives the player's body, and no door opens. The host's
 * script ends it there. The developer menu and the free camera park the module as well, and are
 * no scene. What such a savegame leaves on a client is the release's to give back, and its rule
 * has a test of its own.
 *
 * And one older than any of it: a menu over a running world remembers the input mode it found
 * and puts it back as it closes, over a lock that may have fallen in the meantime.
 */
#include "unittest.h"

#include "mp_scene_doorless.h"
#include "mp_scene_flow.h"
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A host that would begin a scene at a door, with nothing of its own running, and no evidence. */
static mp_scene_adopt_look_t quiet_host(void)
{
    mp_scene_adopt_look_t look;

    memset(&look, 0, sizeof look);
    look.may_begin  = true;
    look.phase      = MP_SCENE_PHASE_NONE;
    look.lock_level = 0;
    return look;
}

/* `n` substeps of the same look; the answer of the last. */
static mp_scene_adopt_t adopt_for(uint32_t *seen, const mp_scene_adopt_look_t *look, unsigned n)
{
    mp_scene_adopt_t answer = MP_SCENE_ADOPT_NO;
    unsigned         i;

    for (i = 0u; i < n; ++i) {
        answer = mp_scene_adopt_step(seen, look);
    }
    return answer;
}

static void check_the_evidence(void)
{
    mp_scene_adopt_look_t look;
    uint32_t              seen = 0u;

    ut_section("the host takes a scene over on evidence, two substeps in a row");
    look = quiet_host();
    ut_check(adopt_for(&seen, &look, 100u) == MP_SCENE_ADOPT_NO, "nothing: never");
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(mp_scene_adopt_step(&seen, &look) == MP_SCENE_ADOPT_WAIT,
             "the lock at a script's level: one substep is not yet a scene");
    ut_check(mp_scene_adopt_step(&seen, &look) == MP_SCENE_ADOPT_LOCK,
             "and the second takes it over as a lock");
    seen       = 0u;
    look       = quiet_host();
    look.module_parked = true;
    look.driven        = true;
    ut_check(adopt_for(&seen, &look, 2u) == MP_SCENE_ADOPT_HERO,
             "the module parked under an actor that drives the body: the hero as an actor");
    seen            = 0u;
    look.lock_level = 7;
    ut_check(adopt_for(&seen, &look, 2u) == MP_SCENE_ADOPT_HERO,
             "both, a savegame's restore: the hero as an actor");
    seen            = 0u;
    look.lock_level = -1;
    ut_check(adopt_for(&seen, &look, 2u) == MP_SCENE_ADOPT_HERO,
             "a lock that does not read leaves the module's evidence");

    ut_section("what is no scene");
    seen               = 0u;
    look               = quiet_host();
    look.module_parked = true;
    ut_check(adopt_for(&seen, &look, 1000u) == MP_SCENE_ADOPT_NO,
             "the developer menu or the free camera: parked, nothing drives the body, never");
    look.lock_level = 1;
    ut_check(adopt_for(&seen, &look, 1000u) == MP_SCENE_ADOPT_NO,
             "and with a menu's lock at one, never");
    look               = quiet_host();
    look.lock_level    = MP_SCENE_LOCK_LEVEL - 1;
    look.driven        = true;
    ut_check(adopt_for(&seen, &look, 1000u) == MP_SCENE_ADOPT_NO,
             "a lock below a script's level, and an actor on a module that is not parked: never");
}

static void check_what_a_door_would_not_begin(void)
{
    static const mp_scene_phase_t OWN[3] = {
        MP_SCENE_PHASE_GATHERING, MP_SCENE_PHASE_RUNNING, MP_SCENE_PHASE_OVER
    };
    mp_scene_adopt_look_t look;
    uint32_t              seen = 0u;
    size_t                i;

    ut_section("what a door would not begin is not taken over either");
    look            = quiet_host();
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    look.may_begin  = false;
    ut_check(adopt_for(&seen, &look, 100u) == MP_SCENE_ADOPT_NO,
             "not bound, not hosting, or nobody joined: never");
    look.may_begin = true;
    for (i = 0u; i < 3u; ++i) {
        look.phase = OWN[i];
        ut_checkf(adopt_for(&seen, &look, 100u) == MP_SCENE_ADOPT_NO && seen == 0u,
                  "a scene of the host's own in phase %u: never, and the count starts over",
                  (unsigned)OWN[i]);
    }
    look.phase    = MP_SCENE_PHASE_NONE;
    look.given_up = true;
    ut_check(adopt_for(&seen, &look, 100u) == MP_SCENE_ADOPT_NO,
             "one given up that the engine may still play: never");

    ut_section("two substeps in a row, or none");
    seen          = 0u;
    look.given_up = false;
    (void)mp_scene_adopt_step(&seen, &look);
    look.phase = MP_SCENE_PHASE_GATHERING;
    (void)mp_scene_adopt_step(&seen, &look);
    look.phase = MP_SCENE_PHASE_NONE;
    ut_check(mp_scene_adopt_step(&seen, &look) == MP_SCENE_ADOPT_WAIT,
             "a substep of evidence, a door's scene between, then evidence again: waits");
    look.lock_level = 0;
    (void)mp_scene_adopt_step(&seen, &look);
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(mp_scene_adopt_step(&seen, &look) == MP_SCENE_ADOPT_WAIT,
             "and a substep without evidence between: waits as well");
}

/* A look of the host's flow with the engine's scene running, or not. */
static mp_scene_host_look_t engine_look(uint32_t now, bool running)
{
    mp_scene_host_look_t look;

    memset(&look, 0, sizeof look);
    look.now         = now;
    look.host_stands = true;
    look.running     = running;
    look.may_move    = MP_SCENE_MOVE_YES;
    return look;
}

static void check_the_flow_taken_over(void)
{
    mp_scene_host_flow_t flow;
    mp_scene_host_look_t look;
    uint32_t             now;

    ut_section("a scene taken over runs at once, with nothing held");
    memset(&flow, 0, sizeof flow);
    flow.serial = 7u;
    flow.last   = 3u;
    flow.grace  = 9u;
    ut_check(mp_scene_host_adopt(&flow, MP_SCENE_KIND_HERO, 100u), "taken");
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.holds && flow.seen_running &&
                 flow.serial == 8u && flow.kind == MP_SCENE_KIND_HERO,
             "running, holding nothing, seen running, a number of its own");
    ut_check(flow.began == 100u && flow.last == 100u && flow.phase_since == 100u &&
                 flow.grace == 0u && flow.hold_standing == 0u && flow.hold_dead == 0u &&
                 flow.released == MP_SCENE_RELEASE_NONE && !flow.given_up,
             "every field a beginning sets, so nothing counts from the scene before");
    ut_check(mp_scene_host_stands_now(&flow),
             "a scene that stands, by the one question the host's state is asked");
    ut_check(mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 101u, true) ==
                 MP_SCENE_BEGIN_SECOND,
             "a door heard while it runs is a second scene, counted");
    ut_check(!mp_scene_host_adopt(&flow, MP_SCENE_KIND_LOCK, 101u),
             "and no second taking over while it runs");
    for (now = 101u; now < 400u; ++now) {
        look = engine_look(now, true);
        (void)mp_scene_host_step(&flow, &look);
    }
    ut_check(flow.phase == MP_SCENE_PHASE_RUNNING && !flow.given_up,
             "while the engine runs it, it runs, with no cap of its own");
    look = engine_look(now, false);
    (void)mp_scene_host_step(&flow, &look);
    ut_check(flow.phase == MP_SCENE_PHASE_NONE && !mp_scene_host_stands_now(&flow),
             "the engine's end is its end: none, at once");

    ut_section("only from none, never a warp");
    memset(&flow, 0, sizeof flow);
    ut_check(!mp_scene_host_adopt(&flow, MP_SCENE_KIND_WARP, 1u) && flow.serial == 0u,
             "a warp is never taken over");
    (void)mp_scene_host_begin(&flow, MP_SCENE_KIND_LOCK, 1u, true);
    ut_check(!mp_scene_host_adopt(&flow, MP_SCENE_KIND_LOCK, 2u) &&
                 flow.phase == MP_SCENE_PHASE_GATHERING,
             "a scene of the host's that is held is left as it is");
    memset(&flow, 0, sizeof flow);
    flow.given_up = true;
    ut_check(!mp_scene_host_adopt(&flow, MP_SCENE_KIND_LOCK, 2u), "nor one given up");
}

/* What the engine does over a running world: a menu remembers the input mode it found and puts
 * it back as it closes; a lock that fell while it was open set the mode to play inside the menu,
 * and the close puts the lock's mode back over a lock of nought. */
static void check_the_input_after_a_menu(void)
{
    bool seen = false;

    ut_section("the input a menu leaves over a lock that fell while it was open");
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, 0, MP_SCENE_INPUT_MODE_LOCK),
             "no menu seen: the lock's mode alone is not the menu's doing");
    ut_check(!mp_scene_menu_left_the_input_held(&seen, true, 0, MP_SCENE_INPUT_MODE_LOCK) &&
                 seen,
             "a menu open: nothing yet, and it is seen");
    ut_check(mp_scene_menu_left_the_input_held(&seen, false, 0, MP_SCENE_INPUT_MODE_LOCK) &&
                 !seen,
             "closed, the lock at nought and the lock's mode back: given back, once");
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, 0, MP_SCENE_INPUT_MODE_LOCK),
             "and not again without another menu");
    seen = true;
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, MP_SCENE_LOCK_LEVEL,
                                                MP_SCENE_INPUT_MODE_LOCK) && !seen,
             "the lock still standing: its mode is right, nothing");
    seen = true;
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, 1, MP_SCENE_INPUT_MODE_LOCK),
             "a dialogue's lock at one: nothing");
    seen = true;
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, 0, MP_SCENE_INPUT_MODE_PLAY),
             "the menu put play back: nothing");
    seen = true;
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, -1, MP_SCENE_INPUT_MODE_LOCK),
             "a lock that does not read: nothing");
    seen = true;
    ut_check(!mp_scene_menu_left_the_input_held(&seen, false, 0, -1),
             "a mode that does not read: nothing");
}

static void check_the_camera(void)
{
    ut_section("a client keeps its own view, and the engine's own group goes through");
    ut_check(mp_scene_camera_refused_on_a_client(true, false, 7),
             "a client, any group of a scene: refused");
    ut_check(mp_scene_camera_refused_on_a_client(true, false, -1) &&
                 mp_scene_camera_refused_on_a_client(true, false, 0x0E),
             "a client, any group but the engine's own, the ones beside it as well: refused");
    ut_check(!mp_scene_camera_refused_on_a_client(true, false, MP_SCENE_CAMERA_GROUP_ENGINE),
             "a client, the fall, the gun or the loading screen: through");
    ut_check(!mp_scene_camera_refused_on_a_client(false, false, 7),
             "the host, or no session: through, its scene is its own");
    ut_check(!mp_scene_camera_refused_on_a_client(true, true, 7),
             "an arena: its own refusal by the script's address, not this one");
}

int main(void)
{
    check_the_evidence();
    check_what_a_door_would_not_begin();
    check_the_flow_taken_over();
    check_the_input_after_a_menu();
    check_the_camera();
    return ut_summary("a scene that runs with no door heard");
}
