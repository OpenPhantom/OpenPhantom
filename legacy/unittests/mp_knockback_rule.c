/* What a contact with code 0x22 does to an actor, and what a throw looks like on a client.
 *
 * The host half first: the four answers in the order the handler tests the actor, then the state
 * the handler leaves held against them, then which touch of a wave is its first and whether a
 * refused report was met by that player's wave. Then the client half: a record entering state 6,
 * against the clip last written to the replica.
 */
#include "unittest.h"

#include "mp_enemy_wire.h"
#include "mp_knockback_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The handler's own numbers, spelled out once so the checks read as sentences. */
#define ACTIVE   1
#define STANDBY  MP_KNOCKBACK_STATE_STANDBY
#define THROWN   MP_KNOCKBACK_STATE_THROWN
#define LANDING  MP_KNOCKBACK_STATE_LANDING
#define FALLING  8
#define GET_UP   MP_KNOCKBACK_STATE_GET_UP
#define DEATH    11
#define SHATTER  13
#define CORPSE   MP_KNOCKBACK_STATE_CORPSE
#define NO_THROW MP_KNOCKBACK_FLAG_NO_THROW

/* A flag the knockback itself sets, and one a script sets for another reason. Neither may be read
 * as the no-throw flag. */
#define THROW_PENDING 0x4000000u
#define OTHER_FLAG    0x10u

static void check_the_order_the_handler_reads(void)
{
    int32_t state;
    unsigned wrong = 0u;

    ut_section("a contact with code 0x22, in the order the handler reads the actor");
    ut_check(mp_knockback_touch_of(0, ACTIVE, 0u) == MP_KNOCKBACK_DOWN &&
                 mp_knockback_touch_of(-5, ACTIVE, 0u) == MP_KNOCKBACK_DOWN,
             "an actor with no health left is turned away before any arm");
    ut_check(mp_knockback_touch_of(0, ACTIVE, NO_THROW) == MP_KNOCKBACK_DOWN,
             "no health with the no-throw flag is still no health, not no-throw");
    ut_check(mp_knockback_touch_of(5, CORPSE, 0u) == MP_KNOCKBACK_DOWN,
             "a corpse with health above zero is a corpse");
    ut_check(mp_knockback_touch_of(5, STANDBY, 0u) == MP_KNOCKBACK_DOWN &&
                 mp_knockback_touch_of(5, STANDBY, NO_THROW) == MP_KNOCKBACK_DOWN,
             "standing by comes before every arm, the no-throw flag included");
    ut_check(mp_knockback_touch_of(5, THROWN, 0u) == MP_KNOCKBACK_ALREADY &&
                 mp_knockback_touch_of(5, LANDING, 0u) == MP_KNOCKBACK_ALREADY,
             "an actor thrown or landing from a throw is already in one");
    ut_check(mp_knockback_touch_of(5, THROWN, NO_THROW) == MP_KNOCKBACK_ALREADY,
             "state 6 with the no-throw flag is already thrown, not no-throw");
    ut_check(mp_knockback_touch_of(5, ACTIVE, NO_THROW) == MP_KNOCKBACK_NO_THROW,
             "an active actor with the no-throw flag keeps standing");
    ut_check(mp_knockback_touch_of(5, ACTIVE, 0u) == MP_KNOCKBACK_THROWN &&
                 mp_knockback_touch_of(1, ACTIVE, THROW_PENDING | OTHER_FLAG) ==
                     MP_KNOCKBACK_THROWN,
             "an active actor without it is thrown, whatever other flag it carries");
    ut_check(mp_knockback_touch_of(5, FALLING, 0u) == MP_KNOCKBACK_THROWN &&
                 mp_knockback_touch_of(5, GET_UP, 0u) == MP_KNOCKBACK_THROWN,
             "falling and getting up are not a throw: the arm throws again");
    ut_check(mp_knockback_touch_of(5, DEATH, 0u) == MP_KNOCKBACK_THROWN &&
                 mp_knockback_touch_of(5, SHATTER, 0u) == MP_KNOCKBACK_THROWN,
             "a death state with health above zero runs into the arm, and the engine throws it");

    for (state = 0; state <= 16; ++state) {
        wrong += mp_knockback_touch_of(0, state, NO_THROW) != MP_KNOCKBACK_DOWN ? 1u : 0u;
        wrong += mp_knockback_touch_of(-1, state, 0u) != MP_KNOCKBACK_DOWN ? 1u : 0u;
    }
    ut_checkf(wrong == 0u, "no health is turned away in every one of the 17 states: %u wrong",
              wrong);
}

static void check_what_the_handler_left(void)
{
    ut_section("the state the handler left, held against the answer");
    ut_check(mp_knockback_touch_agrees(MP_KNOCKBACK_THROWN, ACTIVE, THROWN),
             "a throw that left state 6 agrees");
    ut_check(!mp_knockback_touch_agrees(MP_KNOCKBACK_THROWN, ACTIVE, ACTIVE),
             "a throw that left the actor active does not: the engine did otherwise");
    ut_check(mp_knockback_touch_agrees(MP_KNOCKBACK_NO_THROW, ACTIVE, ACTIVE),
             "no-throw that left the actor as it was agrees");
    ut_check(!mp_knockback_touch_agrees(MP_KNOCKBACK_NO_THROW, ACTIVE, THROWN),
             "no-throw that ended in state 6 does not");
    ut_check(mp_knockback_touch_agrees(MP_KNOCKBACK_DOWN, STANDBY, STANDBY) &&
                 mp_knockback_touch_agrees(MP_KNOCKBACK_ALREADY, LANDING, LANDING),
             "standing by and landing, left alone, agree");
    ut_check(!mp_knockback_touch_agrees(MP_KNOCKBACK_ALREADY, LANDING, THROWN),
             "landing turned into a throw does not");
}

static void check_the_first_touch_of_a_wave(void)
{
    static mp_knockback_memory_t memory;
    uint32_t i;

    ut_section("the first touch of each wave on each actor");
    memset(&memory, 0, sizeof memory);
    ut_check(mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 100u),
             "a wave's first touch on an actor is the first");
    ut_check(!mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 100u),
             "a second touch in the same substep goes on the same overlap");
    ut_check(!mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 101u) &&
                 !mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 102u),
             "and so does one in each following substep");
    ut_check(mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 104u),
             "a substep without a touch ends the overlap, and the next touch is a first again");
    ut_check(mp_knockback_touch(&memory, 0xA000u, 0x2000u, 1u, 104u),
             "the same wave on another actor is a first");
    ut_check(mp_knockback_touch(&memory, 0xB000u, 0x1000u, 1u, 104u),
             "and another wave on the same actor is a first");

    /* One pair more than the memory holds, one a substep. The newest keeps its overlap; the
     * oldest is given up for it, and a pair given up reads as a first touch, never as an overlap
     * that did not happen. */
    memset(&memory, 0, sizeof memory);
    (void)mp_knockback_touch(&memory, 0xC000u, 0x3000u, 1u, 300u);
    for (i = 1u; i <= MP_KNOCKBACK_PAIRS; ++i) {
        (void)mp_knockback_touch(&memory, 0xC000u + i, 0x3000u, 1u, 300u + i);
    }
    ut_check(!mp_knockback_touch(&memory, 0xC000u + MP_KNOCKBACK_PAIRS, 0x3000u, 1u,
                                 301u + MP_KNOCKBACK_PAIRS),
             "with every place taken the newest pair still goes on its overlap");
    ut_check(mp_knockback_touch(&memory, 0xC000u, 0x3000u, 1u, 301u),
             "and the oldest was given up for it: its next touch is a first, not an overlap");

    /* The oldest in the second place, not the first: the first pair touched again last. */
    memset(&memory, 0, sizeof memory);
    for (i = 0u; i < MP_KNOCKBACK_PAIRS; ++i) {
        (void)mp_knockback_touch(&memory, 0xD000u + i, 0x5000u, 1u, 400u + i);
    }
    (void)mp_knockback_touch(&memory, 0xD000u, 0x5000u, 1u, 500u);
    (void)mp_knockback_touch(&memory, 0xE000u, 0x5000u, 1u, 500u);
    ut_check(!mp_knockback_touch(&memory, 0xD000u, 0x5000u, 1u, 501u),
             "the pair given up is the one touched longest ago, wherever it is held");
}

static void check_a_level_end(void)
{
    static mp_knockback_memory_t memory;

    ut_section("what a level's end or a session's end does to the memory");
    memset(&memory, 0, sizeof memory);
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 100u);
    mp_knockback_refused(&memory, 0x2000u, 1u, 100u);
    mp_knockback_refused(&memory, 0x3000u, 1u, 100u);
    mp_knockback_forget(&memory);
    ut_checkf(memory.cut == 2u && mp_knockback_waiting_count(&memory, 101u) == 0u,
              "every report still waiting is counted as cut off, and waits no longer (%u)",
              memory.cut);
    ut_check(mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 101u),
             "and a pair held before it is forgotten: the next touch is a first");
    ut_checkf(memory.refused == memory.met + memory.unmet + memory.cut + memory.unwatched +
                                    mp_knockback_waiting_count(&memory, 101u),
              "the numbers still add up to the refused reports: %u", memory.refused);
}

static uint32_t settled(const mp_knockback_memory_t *memory)
{
    return memory->met + memory->unmet + memory->unwatched;
}

static void check_a_refused_report_and_its_wave(void)
{
    static mp_knockback_memory_t memory;
    uint32_t i;

    ut_section("a refused report, met by that player's wave on the same actor or not");
    memset(&memory, 0, sizeof memory);
    mp_knockback_refused(&memory, 0x1000u, 1u, 100u);
    ut_check(memory.refused == 1u && mp_knockback_waiting_count(&memory, 100u) == 1u,
             "a refused report waits for its wave");
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 103u);
    ut_check(memory.met == 1u && mp_knockback_waiting_count(&memory, 103u) == 0u,
             "the puppet's wave touching the same actor three substeps later meets it");

    mp_knockback_refused(&memory, 0x1000u, 1u, 104u);
    ut_check(memory.met == 2u,
             "a report arriving after the wave touched is met at once: the push happened here");

    memset(&memory, 0, sizeof memory);
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 2u, 100u);
    (void)mp_knockback_touch(&memory, 0xB000u, 0x1000u, 0u, 100u);
    mp_knockback_refused(&memory, 0x1000u, 1u, 101u);
    ut_check(memory.met == 0u && mp_knockback_waiting_count(&memory, 101u) == 1u,
             "a wave of another player, or this machine's own, held before the report does not "
             "meet it at once");

    memset(&memory, 0, sizeof memory);
    mp_knockback_refused(&memory, 0x1000u, 1u, 100u);
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 2u, 103u);
    (void)mp_knockback_touch(&memory, 0xB000u, 0x1000u, 0u, 103u);
    (void)mp_knockback_touch(&memory, 0xC000u, 0x2000u, 1u, 103u);
    ut_check(memory.met == 0u,
             "another player's wave, this machine's own, or the same wave on another actor meets "
             "nothing");
    ut_check(mp_knockback_waiting_count(&memory, 100u + MP_KNOCKBACK_WINDOW) == 1u &&
                 memory.unmet == 0u,
             "at the last substep of the window it still waits");
    ut_check(mp_knockback_waiting_count(&memory, 101u + MP_KNOCKBACK_WINDOW) == 0u &&
                 memory.unmet == 1u,
             "one substep later it is counted as met by none: what the rule costs");

    memset(&memory, 0, sizeof memory);
    mp_knockback_refused(&memory, 0x1000u, 1u, 100u);
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 100u + MP_KNOCKBACK_WINDOW);
    ut_check(memory.met == 1u, "a wave at the window's last substep still meets it");
    mp_knockback_refused(&memory, 0x1000u, 1u, 200u);
    (void)mp_knockback_touch(&memory, 0xA001u, 0x1000u, 1u, 201u + MP_KNOCKBACK_WINDOW);
    ut_check(memory.met == 1u && memory.unmet == 1u,
             "and one substep past it does not; the report is counted as met by none");

    memset(&memory, 0, sizeof memory);
    (void)mp_knockback_touch(&memory, 0xA000u, 0x1000u, 1u, 100u);
    mp_knockback_refused(&memory, 0x1000u, 1u, 101u + MP_KNOCKBACK_WINDOW);
    ut_check(memory.met == 0u && mp_knockback_waiting_count(&memory, 101u + MP_KNOCKBACK_WINDOW)
                                     == 1u,
             "a wave further back than the window does not meet a report either");

    memset(&memory, 0, sizeof memory);
    mp_knockback_refused(&memory, 0u, 1u, 100u);
    mp_knockback_refused(&memory, 0x1000u, 0u, 100u);
    ut_check(memory.unwatched == 2u,
             "a report with no actor here, or from no far player, cannot be watched");
    for (i = 0; i < MP_KNOCKBACK_WAITING + 1u; ++i) {
        mp_knockback_refused(&memory, 0x4000u + i, 1u, 300u);
    }
    ut_check(memory.unwatched == 3u,
             "a report past the room there is to wait is counted, not dropped");
    ut_checkf(memory.refused == settled(&memory) + mp_knockback_waiting_count(&memory, 300u),
              "the four numbers add up to the refused reports: %u = %u met + %u none + %u "
              "unwatched + %u waiting", memory.refused, memory.met, memory.unmet,
              memory.unwatched, mp_knockback_waiting_count(&memory, 300u));
}

static mp_knockback_edges_t count_one(bool known, uint32_t state_before, uint32_t clip_before,
                                      uint32_t state, uint32_t clip, bool wrote)
{
    mp_knockback_edges_t edges;

    memset(&edges, 0, sizeof edges);
    mp_knockback_edges_count(&edges, known, state_before, clip_before, state, clip, wrote);
    return edges;
}

static void check_a_throw_on_a_client(void)
{
    mp_knockback_edges_t edges;
    uint32_t             state;
    unsigned             in_throw = 0u;

    ut_section("a record entering the knockback, as the client writes it");
    /* The order the host sends them in: its records are read after the pair pass that set the
     * state, and the engine starts the throw's clip in the next substep's pose commit. */
    memset(&edges, 0, sizeof edges);
    mp_knockback_edges_count(&edges, true, ACTIVE, 5u, THROWN, 5u, true);
    mp_knockback_edges_count(&edges, true, THROWN, 5u, THROWN, 13u, true);
    mp_knockback_edges_count(&edges, true, THROWN, 13u, THROWN, 13u, true);
    ut_checkf(edges.throws == 1u && edges.clip_moved == 1u,
              "walk, then thrown with the walk clip, then thrown with the throw clip: one throw "
              "and one record that brought its clip (%u, %u)", edges.throws, edges.clip_moved);
    edges = count_one(true, ACTIVE, 5u, THROWN, 13u, true);
    ut_check(edges.throws == 1u && edges.clip_moved == 1u,
             "both records folded into one flush: the throw and its clip in one record");
    edges = count_one(true, THROWN, 13u, THROWN, 13u, true);
    ut_check(edges.throws == 0u && edges.clip_moved == 0u && edges.in_throw == 1u,
             "thrown to thrown with the same clip is no new throw and brings no clip");
    edges = count_one(true, ACTIVE | MP_ENEMY_HAS_HEAD, 5u, THROWN | MP_ENEMY_HAS_HEAD, 13u,
                      true);
    ut_check(edges.throws == 1u, "the presence bits above the state are not the state");
    edges = count_one(true, THROWN | MP_ENEMY_HAS_HEAD, 13u, THROWN, 13u, true);
    ut_check(edges.throws == 0u, "and a record that only lost one is no throw");
    edges = count_one(true, THROWN, 0x10Du, THROWN, 0x0Du, true);
    ut_check(edges.clip_moved == 0u, "a clip is its low byte, as the pose writer reads it");
    edges = count_one(false, 0u, 0u, THROWN, 13u, true);
    ut_check(edges.throws == 0u && edges.clip_moved == 0u && edges.no_before == 1u,
             "the first record after a binding has nothing before it and is counted apart");
    edges = count_one(true, ACTIVE, 5u, THROWN, 13u, false);
    ut_check(edges.throws == 0u && edges.in_throw == 0u && edges.no_before == 0u,
             "a record that did not reach the replica counts nowhere");

    for (state = 0u; state <= 16u; ++state) {
        edges = count_one(true, state, 1u, state, 1u, true);
        in_throw += edges.in_throw;
    }
    ut_checkf(in_throw == 4u, "states 6 to 9 are the throw, and only they: %u of 17", in_throw);
}

int main(void)
{
    check_the_order_the_handler_reads();
    check_what_the_handler_left();
    check_the_first_touch_of_a_wave();
    check_a_refused_report_and_its_wave();
    check_a_level_end();
    check_a_throw_on_a_client();
    return ut_summary("mp_knockback_rule");
}
