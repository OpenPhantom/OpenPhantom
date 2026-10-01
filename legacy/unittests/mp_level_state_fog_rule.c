/* mp_level_state_fog_rule.c: the director's fog followed from its commands.
 *
 * The sequences are the shipped ones: the gas room of FEDSHIP (start 10.001/16.002 and a twenty
 * second ramp to pale green at 1.0001; out of the room a one second ramp back and the band
 * 16.0016/56.0056; back in 10.001/16.0016 and a one second green ramp; at the end the band two
 * hundred times), the swamp's two edges and the race's. The old window of the last four commands is
 * kept below as the reference it replaced, because the case that matters is the one it lost.
 */
#include "unittest.h"

#include "mp_level_state_fog_rule.h"
#include "mp_level_state_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t bits(float value)
{
    uint32_t out;

    memcpy(&out, &value, sizeof out);
    return out;
}

static float value(uint32_t word)
{
    float out;

    memcpy(&out, &word, sizeof out);
    return out;
}

static void run(mp_level_fog_model_t *m, float seconds)
{
    int32_t steps = (int32_t)(seconds * 32.0f + 0.5f);
    int32_t i;

    for (i = 0; i < steps; ++i) {
        (void)mp_level_fog_tick(m, MP_LEVEL_FOG_SUBSTEP_SECONDS);
    }
}

/* The window of four the fog travelled in until this state: a client that met a note replayed the
 * four commands it carried and nothing older. */
typedef struct old_window {
    int32_t  command[4];
    uint32_t a2[4];
    int      count;
} old_window_t;

static void old_note(old_window_t *w, int32_t command, uint32_t a2)
{
    if (w->count == 4) {
        memmove(&w->command[0], &w->command[1], 3u * sizeof w->command[0]);
        memmove(&w->a2[0], &w->a2[1], 3u * sizeof w->a2[0]);
        --w->count;
    }
    w->command[w->count] = command;
    w->a2[w->count]      = a2;
    ++w->count;
}

static bool old_carries_green(const old_window_t *w)
{
    int i;

    for (i = 0; i < w->count; ++i) {
        if (w->command[i] == MP_LEVEL_FOG_RAMP_GREEN) {
            return true;
        }
    }
    return false;
}

static void check_the_effects(void)
{
    mp_level_fog_model_t m;

    ut_section("what each command does, as the engine does it");
    mp_level_fog_model_init(&m);
    ut_check(mp_level_fog_apply(&m, 6, 0, bits(10.001f)) == MP_LEVEL_FOG_CHANGED &&
                 (m.state.flags & MP_LEVEL_FOG_HAS_START) != 0u && m.state.cur == bits(10.001f),
             "a start is the start and the one a ramp works from");
    ut_check(mp_level_fog_apply(&m, 6, 0, bits(10.001f)) == MP_LEVEL_FOG_SAME,
             "the same start again changes nothing");
    ut_check(mp_level_fog_apply(&m, 6, 0, bits(-1.0f)) == MP_LEVEL_FOG_IGNORED &&
                 m.state.cur == bits(10.001f),
             "a start below zero is ignored, as the engine's test against 0.0 does");
    ut_check(mp_level_fog_apply(&m, 6, 0, 0x7FC00000u) == MP_LEVEL_FOG_IGNORED,
             "and so is a NaN, which compares unordered and so below");
    ut_check(mp_level_fog_apply(&m, 7, 0, bits(16.002f)) == MP_LEVEL_FOG_CHANGED &&
                 m.state.end == bits(16.002f),
             "an end is the end");
    ut_check(mp_level_fog_apply(&m, 11, 0, bits(1.0f)) == MP_LEVEL_FOG_IGNORED &&
                 mp_level_fog_apply(&m, 11, 20, bits(-2.0f)) == MP_LEVEL_FOG_IGNORED,
             "a ramp of no length or to a target below zero is ignored");
    ut_check(mp_level_fog_apply(&m, 11, 20, bits(1.0001f)) == MP_LEVEL_FOG_CHANGED &&
                 (m.state.flags & MP_LEVEL_FOG_COLOUR) != 0u && m.state.colour[0] == 0xAAu &&
                 m.state.colour[1] == 0xFFu && m.state.colour[2] == 0xAAu &&
                 m.state.left == 20u && m.state.span == bits(20.0f) &&
                 (m.state.flags & MP_LEVEL_FOG_KEEP_WORLD) == 0u,
             "the green ramp gives pale green, the fog bit, and twenty seconds to go");
    ut_check(mp_level_fog_apply(&m, 8, 0, 0u) == MP_LEVEL_FOG_NOT_FOG,
             "a command that is not fog is not the fog's");
}

static void check_the_ramp(void)
{
    mp_level_fog_model_t m;
    float                half;

    ut_section("a ramp counts down in simulation time and ends where the engine's does");
    mp_level_fog_model_init(&m);
    (void)mp_level_fog_apply(&m, 6, 0, bits(10.0f));
    (void)mp_level_fog_apply(&m, 11, 20, bits(2.0f));
    run(&m, 10.0f);
    ut_checkf(m.state.left == 10u, "ten seconds in, ten are left (%u)", (unsigned)m.state.left);
    half = mp_level_fog_start_now(&m.state);
    ut_checkf(fabsf(half - 6.0f) < 0.001f,
              "and the start drawn is half way, 6.0, as the engine's step gives it (%.4f)",
              (double)half);
    run(&m, 9.0f);
    ut_check(m.state.left == 1u, "the last second is still a second");
    ut_check(mp_level_fog_tick(&m, 0.5f) == false || m.state.left == 1u,
             "half of it changes nothing that travels");
    run(&m, 1.0f);
    ut_check(m.state.left == 0u && m.state.cur == bits(2.0f) &&
                 (m.state.flags & MP_LEVEL_FOG_COLOUR) != 0u,
             "run out: the target is the start, and the green ramp keeps its green");

    ut_section("the ramp back puts the level's own fog back when it runs out");
    (void)mp_level_fog_apply(&m, 12, 1, bits(16.0016f));
    ut_check((m.state.flags & MP_LEVEL_FOG_KEEP_WORLD) != 0u &&
                 (m.state.flags & MP_LEVEL_FOG_COLOUR) != 0u,
             "the ramp back leaves the colour as it is while it runs");
    (void)mp_level_fog_apply(&m, 6, 0, bits(16.0016f));
    (void)mp_level_fog_apply(&m, 7, 0, bits(56.0056f));
    run(&m, 1.0f);
    ut_check((m.state.flags & MP_LEVEL_FOG_RESTORED) != 0u &&
                 (m.state.flags & (MP_LEVEL_FOG_HAS_START | MP_LEVEL_FOG_HAS_END |
                                   MP_LEVEL_FOG_COLOUR)) == 0u,
             "and when it has run, the band the script set a substep after it and the green are "
             "the level's own again, as the engine's restore makes them");
    ut_check(mp_level_fog_apply(&m, 6, 0, bits(16.0016f)) == MP_LEVEL_FOG_CHANGED,
             "so the same start after that is a change again");
}

static void check_the_gas_room_end(void)
{
    mp_level_fog_model_t m;
    old_window_t         old;
    unsigned             changed = 0;
    int                  i;

    ut_section("the colour and the fog bit survive the edges that follow them");
    mp_level_fog_model_init(&m);
    memset(&old, 0, sizeof old);
    (void)mp_level_fog_apply(&m, 6, 0, bits(10.001f));
    old_note(&old, 6, bits(10.001f));
    (void)mp_level_fog_apply(&m, 7, 0, bits(16.002f));
    old_note(&old, 7, bits(16.002f));
    (void)mp_level_fog_apply(&m, 11, 20, bits(1.0001f));
    old_note(&old, 11, bits(1.0001f));
    for (i = 0; i < 5; ++i) {
        changed += mp_level_fog_apply(&m, 6, 0, bits(16.0016f)) == MP_LEVEL_FOG_CHANGED ? 1u : 0u;
        changed += mp_level_fog_apply(&m, 7, 0, bits(56.0056f)) == MP_LEVEL_FOG_CHANGED ? 1u : 0u;
        old_note(&old, 6, bits(16.0016f));
        old_note(&old, 7, bits(56.0056f));
    }
    ut_check(!old_carries_green(&old),
             "the old window of four, the reference: five following pairs pushed the green out, so "
             "a client that met that note never saw it");
    ut_check((m.state.flags & MP_LEVEL_FOG_COLOUR) != 0u && m.state.colour[1] == 0xFFu,
             "the state keeps the green and the fog bit through all five");
    ut_checkf(changed == 2u,
              "and of the ten edges only the first two changed anything; the other eight are the "
              "end's repeats and do not travel (%u)", changed);
}

static bool plan_has(const mp_level_fog_command_t *plan, size_t count, int32_t command)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (plan[i].command == command) {
            return true;
        }
    }
    return false;
}

static void check_the_heal(void)
{
    mp_level_fog_model_t   m;
    mp_level_fog_model_t   again;
    mp_level_fog_command_t plan[MP_LEVEL_FOG_HEAL_MAX];
    size_t                 count;
    size_t                 i;

    ut_section("the commands that bring another engine to a state");
    mp_level_fog_model_init(&m);
    ut_check(mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX) == 0u,
             "no fog said yet: nothing to play");

    (void)mp_level_fog_apply(&m, 6, 0, bits(8.0008f));
    (void)mp_level_fog_apply(&m, 7, 0, bits(40.004f));
    count = mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX);
    ut_check(count == 2u && plan[0].command == 6 && plan[0].a2 == bits(8.0008f) &&
                 plan[1].command == 7 && plan[1].a2 == bits(40.004f),
             "the swamp's two edges: its start and its end, in the engine's order");

    (void)mp_level_fog_apply(&m, 11, 20, bits(1.0f));
    run(&m, 5.0f);
    count = mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX);
    ut_check(count == 3u && plan[2].command == 11 && plan[2].a1 == 15 &&
                 plan[2].a2 == bits(1.0f),
             "a green ramp five seconds in: the start where it stands, the end, and the green "
             "ramp over the fifteen seconds left");
    mp_level_fog_model_init(&again);
    for (i = 0; i < count; ++i) {
        (void)mp_level_fog_apply(&again, plan[i].command, plan[i].a1, plan[i].a2);
    }
    ut_checkf(fabsf(mp_level_fog_start_now(&again.state) - mp_level_fog_start_now(&m.state)) <
                  0.001f,
              "and an engine that plays them draws the same start (%.4f against %.4f)",
              (double)mp_level_fog_start_now(&again.state),
              (double)mp_level_fog_start_now(&m.state));
    run(&m, 7.5f);
    run(&again, 7.5f);
    ut_checkf(fabsf(mp_level_fog_start_now(&again.state) - mp_level_fog_start_now(&m.state)) <
                  0.001f,
              "and still does half way down the rest (%.4f against %.4f)",
              (double)mp_level_fog_start_now(&again.state),
              (double)mp_level_fog_start_now(&m.state));

    run(&m, 10.0f);
    count = mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX);
    ut_check(plan_has(plan, count, 11) && plan[count - 1u].a1 == 1,
             "the ramp run out and the green still up: a one second green ramp from the start to "
             "itself gives the colour and moves nothing");

    (void)mp_level_fog_apply(&m, 12, 3, bits(16.0f));
    count = mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX);
    ut_check(count == 4u && plan[2].command == 11 && plan[3].command == 12 && plan[3].a1 == 3,
             "a ramp back on green: the green first, then the ramp back that runs");
    run(&m, 3.0f);
    count = mp_level_fog_heal_plan(&m.state, plan, MP_LEVEL_FOG_HEAL_MAX);
    ut_check(count == 1u && plan[0].command == 12 && plan[0].a1 == 1,
             "the level's own fog put back: one second back to it");
    ut_check(mp_level_fog_heal_plan(&m.state, plan, 0u) == 0u,
             "and a plan with no room writes nothing");
}

static void check_start_now(void)
{
    mp_level_fog_state_t s;

    ut_section("the start drawn is never a division by nothing");
    memset(&s, 0, sizeof s);
    s.flags  = (uint8_t)MP_LEVEL_FOG_RAMP;
    s.cur    = bits(4.0f);
    s.target = bits(2.0f);
    s.span   = 0u;
    s.left   = 3u;
    ut_check(value(bits(mp_level_fog_start_now(&s))) == 2.0f,
             "a length of 0 is a ramp that has run out: the target");
    s.left = 0u;
    ut_check(mp_level_fog_start_now(&s) == 4.0f, "no ramp: the start itself");
}

static void check_the_substep(void)
{
    ut_section("the length of a substep, as a side reads it");
    ut_check(mp_level_fog_substep_seconds(1.0f / 64.0f) == 1.0f / 64.0f &&
                 mp_level_fog_substep_seconds(1.0f / 32.0f) == 1.0f / 32.0f,
             "both steps the engine runs are taken as read");
    ut_check(mp_level_fog_substep_seconds(MP_LEVEL_FOG_SUBSTEP_CEILING) ==
                 MP_LEVEL_FOG_SUBSTEP_CEILING,
             "and so is the longest step the engine hands a frame");
    ut_check(mp_level_fog_substep_seconds(0.0f) == MP_LEVEL_FOG_SUBSTEP_SECONDS &&
                 mp_level_fog_substep_seconds(-1.0f / 32.0f) == MP_LEVEL_FOG_SUBSTEP_SECONDS &&
                 mp_level_fog_substep_seconds(0.2f) == MP_LEVEL_FOG_SUBSTEP_SECONDS &&
                 mp_level_fog_substep_seconds((float)nan("")) == MP_LEVEL_FOG_SUBSTEP_SECONDS &&
                 mp_level_fog_substep_seconds((float)HUGE_VAL) == MP_LEVEL_FOG_SUBSTEP_SECONDS,
             "nothing, a negative, a step no frame is given, NaN and infinity are the engine's own "
             "1/32 s");
}

int main(void)
{
    check_the_effects();
    check_the_ramp();
    check_the_gas_room_end();
    check_the_heal();
    check_start_now();
    check_the_substep();

    return ut_summary("mp_level_state_fog_rule");
}
