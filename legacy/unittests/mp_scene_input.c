/* The host's input while he is brought to the place of a scene: when the rule holds it, and that
 * every way out of a scene lets it go and never takes it again.
 *
 * The rule is mp_scene_flow's; the binding in mp_scene_own.c only reads the look and writes the
 * hold when the answer changes, which is what `apply` below does as well. Every exit is walked from
 * a held input: the scene seen running, the grab that never shows, a scene given up, a warp that
 * takes the scene over, the level's end and the session's. After each the input is free, and
 * stays free for a hundred more looks of the state that exit left: one exit and every way taking
 * it. A host with no place is never held, so the two ways a hold falls without one, every far
 * player gone and no place found, have no held input to let go.
 */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A host with a place, at it, while the hold stands. */
static mp_scene_input_look_t at_his_place(void)
{
    mp_scene_input_look_t look;

    memset(&look, 0, sizeof look);
    look.hosting  = true;
    look.phase    = MP_SCENE_PHASE_GATHERING;
    look.holds    = true;
    look.may_move = MP_SCENE_MOVE_YES;
    look.has_seat = true;
    look.stage    = MP_SCENE_SEAT_DONE;
    return look;
}

/* The binding: the rule's answer is the hold. */
static mp_scene_input_t apply(mp_scene_input_look_t *look)
{
    mp_scene_input_t why = mp_scene_input_hold(look);

    look->held = why == MP_SCENE_INPUT_HELD;
    return why;
}

/* A hundred more looks of the same state, the clock running: never held again. */
static bool stays_free(mp_scene_input_look_t look)
{
    unsigned i;

    for (i = 0u; i < 100u; ++i) {
        ++look.since_phase;
        if (apply(&look) == MP_SCENE_INPUT_HELD) {
            return false;
        }
    }
    return true;
}

static void check_while_the_hold_stands(void)
{
    mp_scene_input_look_t look;

    ut_section("while the hold stands");
    look = at_his_place();
    ut_check(apply(&look) == MP_SCENE_INPUT_HELD, "a host at his place: held");
    look.stage = MP_SCENE_SEAT_FADING;
    ut_check(apply(&look) == MP_SCENE_INPUT_HELD, "in the fade: held");
    look.stage = MP_SCENE_SEAT_PLACED;
    ut_check(apply(&look) == MP_SCENE_INPUT_HELD, "handed to the placement: held");
    look.stage     = MP_SCENE_SEAT_WAITING;
    look.fade_held = true;
    ut_check(apply(&look) == MP_SCENE_INPUT_HELD,
             "waiting in the dark after a fade his body jumped in: held, he lands and is placed");
    look.fade_held = false;
    ut_check(apply(&look) == MP_SCENE_INPUT_TRY && !look.held,
             "waiting in the light: let go, a try did not take");
    look.stage = MP_SCENE_SEAT_RESPAWNING;
    ut_check(apply(&look) == MP_SCENE_INPUT_TRY, "under the engine's respawn: not held");
    look       = at_his_place();
    look.may_move = MP_SCENE_MOVE_DEAD;
    ut_check(apply(&look) == MP_SCENE_INPUT_DEAD, "dead: let go, his re-entry needs no input");
    look          = at_his_place();
    look.has_seat = false;
    ut_check(apply(&look) == MP_SCENE_INPUT_NONE, "no place yet: nothing to hold");

    ut_section("a host with no place is never held, in any mode");
    {
        static const mp_scene_move_t MOVES[4] = { MP_SCENE_MOVE_YES, MP_SCENE_MOVE_MODE,
                                                  MP_SCENE_MOVE_UNREAD, MP_SCENE_MOVE_NO_BODY };
        size_t                       i;
        bool                         never = true;

        for (i = 0u; i < 4u; ++i) {
            look          = at_his_place();
            look.has_seat = false;
            look.may_move = MOVES[i];
            never = never && apply(&look) == MP_SCENE_INPUT_NONE && !look.held;
        }
        ut_check(never, "standing, swimming, unread or with no body: not held where he stands, "
                        "because he is not moved at all");
    }
}

typedef enum exit_way {
    EXIT_RUNS = 0,      /* the grab is seen and the scene runs */
    EXIT_NO_GRAB,       /* released, and the grab never shows */
    EXIT_GIVEN_UP,      /* the wait ran out: over */
    EXIT_WARP,          /* a warp took the scene over: the binding forgot the hold */
    EXIT_LEVEL,         /* the one exit: the level or the session ended */
    EXIT_NOT_HOSTING,   /* the transport came down */
    EXITS
} exit_way_t;

static const char *const EXIT_NAMES[EXITS] = {
    "the scene runs", "the grab never shows", "given up", "a warp took it over",
    "the level ended", "the host stopped hosting"
};

/* Drives a held input out by `way`; true when it ends free and stays free. */
static bool walk_out(exit_way_t way)
{
    mp_scene_input_look_t look = at_his_place();
    uint32_t              t;

    if (apply(&look) != MP_SCENE_INPUT_HELD) {
        return false;
    }
    switch (way) {
    case EXIT_RUNS:
    case EXIT_NO_GRAB:
        look.phase       = MP_SCENE_PHASE_RUNNING;
        look.holds       = false;
        look.since_phase = 0u;
        for (t = 0u; t <= MP_SCENE_GRAB_GRACE_SUBSTEPS; ++t) {
            look.since_phase = t;
            if (way == EXIT_RUNS && t == 3u) {
                look.seen_running = true;
            }
            (void)apply(&look);
            if (way == EXIT_RUNS && t >= 3u && look.held) {
                return false;
            }
        }
        look.since_phase = MP_SCENE_GRAB_GRACE_SUBSTEPS + 1u;
        (void)apply(&look);
        break;
    case EXIT_GIVEN_UP:
        look.phase = MP_SCENE_PHASE_OVER;
        look.holds = false;
        (void)apply(&look);
        break;
    case EXIT_WARP:
        look.held  = false;   /* the binding's own way out, then a warp holds nobody */
        look.holds = false;
        look.stage = MP_SCENE_SEAT_IDLE;
        (void)apply(&look);
        break;
    case EXIT_LEVEL:
        look.held  = false;
        look.phase = MP_SCENE_PHASE_NONE;
        look.holds = false;
        (void)apply(&look);
        break;
    case EXIT_NOT_HOSTING:
        look.hosting = false;
        (void)apply(&look);
        break;
    case EXITS:
    default:
        return false;
    }
    return !look.held && stays_free(look);
}

static void check_every_way_out(void)
{
    exit_way_t way;

    ut_section("every way out of a scene lets the held input go, and never takes it again");
    for (way = EXIT_RUNS; way < EXITS; way = (exit_way_t)(way + 1)) {
        ut_checkf(walk_out(way), "%s: free, and free a hundred looks later", EXIT_NAMES[way]);
    }

    ut_section("the grace of the grab, and the reasons the line gives");
    {
        mp_scene_input_look_t look = at_his_place();

        (void)apply(&look);
        look.phase       = MP_SCENE_PHASE_RUNNING;
        look.holds       = false;
        look.since_phase = MP_SCENE_GRAB_GRACE_SUBSTEPS;
        ut_check(apply(&look) == MP_SCENE_INPUT_HELD, "held on to the last substep of the grace");
        look.since_phase = MP_SCENE_GRAB_GRACE_SUBSTEPS + 1u;
        ut_check(apply(&look) == MP_SCENE_INPUT_NO_GRAB, "and let go past it");
        look.since_phase = 0u;
        ut_check(apply(&look) == MP_SCENE_INPUT_NONE,
                 "a hold not held when the scene was released is never taken in its grace");
    }
    {
        mp_scene_input_t a;
        mp_scene_input_t b;
        bool             apart = true;

        for (a = MP_SCENE_INPUT_HELD; a < MP_SCENE_INPUTS; a = (mp_scene_input_t)(a + 1)) {
            for (b = (mp_scene_input_t)(a + 1); b < MP_SCENE_INPUTS;
                 b = (mp_scene_input_t)(b + 1)) {
                apart = apart && strcmp(mp_scene_input_text(a), mp_scene_input_text(b)) != 0;
            }
        }
        ut_check(apart, "every reason has words of its own");
    }
}

int main(void)
{
    check_while_the_hold_stands();
    check_every_way_out();
    return ut_summary("the host's input at his place");
}
