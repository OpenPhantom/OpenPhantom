/* mp_script_sound_rule.c: the decisions about a sound an actor's script plays.
 *
 * What would be silent if it were wrong:
 *
 *   a music record that also carries the loop bit taken for a loop, so a client plays music in
 *   every substep without asking whom it is meant for;
 *   a script that plays the same sound in every substep of a state flooding the world events, or
 *   an edge after a pause swallowed as a hold;
 *   an event the world events refused never tried again while the call is held;
 *   music meant for nobody because the script never asked for a player, or meant for a player
 *   the host last heard of minutes ago;
 *   a client that plays its own script's sound for an actor whose life the host describes, or a
 *   single player run that is handled at all;
 *   two loops on one replica.
 *
 * The old rules ride along as references: the loop was once told by the handle the engine hands
 * the call alone, and every call was once one event.
 */
#include "unittest.h"

#include "mp_script_sound_rule.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's own choice of handle at the site: the actor's loop cell for the loop bit, the
 * shared cell otherwise. It was the first rule proposed for telling a loop, and it is wrong for
 * the records that carry a music bit beside the loop bit. */
static bool old_is_loop(uint32_t flags)
{
    return (flags & MP_SCRIPT_SOUND_FLAG_LOOP) != 0u;
}

static void check_the_classes(void)
{
    static const uint32_t LOOP_AND_MUSIC[] = { 0x218u, 0x210u | 0x400u, 0x18u, 0x418u };
    size_t   i;
    uint32_t flags;
    bool     agree_elsewhere = true;

    ut_section("a call is music, then a loop, then a sound played once, in the engine's order");
    ut_check(mp_script_sound_classify(0u) == MP_SCRIPT_SOUND_ONCE, "no flags is played once");
    ut_check(mp_script_sound_classify(0x224u) == MP_SCRIPT_SOUND_ONCE,
             "3D, a fixed place and no duplicate is still played once");
    ut_check(mp_script_sound_classify(0x12u) == MP_SCRIPT_SOUND_LOOP,
             "the steam's loop, falloff and the loop bit, is a loop");
    ut_check(mp_script_sound_classify(0x8u) == MP_SCRIPT_SOUND_MUSIC_STATE, "0x8 is a state");
    ut_check(mp_script_sound_classify(0x400u) == MP_SCRIPT_SOUND_MUSIC_SEQUENCE,
             "0x400 is a sequence");
    ut_check(mp_script_sound_classify(0x408u) == MP_SCRIPT_SOUND_MUSIC_STATE,
             "both music bits: the state, because the engine asks for it first and returns");
    for (i = 0; i < sizeof LOOP_AND_MUSIC / sizeof LOOP_AND_MUSIC[0]; ++i) {
        mp_script_sound_class_t cls = mp_script_sound_classify(LOOP_AND_MUSIC[i]);

        ut_checkf(cls == MP_SCRIPT_SOUND_MUSIC_STATE || cls == MP_SCRIPT_SOUND_MUSIC_SEQUENCE,
                  "flags %03X carry the loop bit and are music, as the engine plays them",
                  (unsigned)LOOP_AND_MUSIC[i]);
        ut_checkf(old_is_loop(LOOP_AND_MUSIC[i]),
                  "and the old rule took flags %03X for a loop, which is the defect held here",
                  (unsigned)LOOP_AND_MUSIC[i]);
    }
    /* Everywhere without a music bit the two rules are one. */
    for (flags = 0; flags < 0x2000u; ++flags) {
        if ((flags & MP_SCRIPT_SOUND_FLAG_MUSIC_STATE) != 0u ||
            (flags & MP_SCRIPT_SOUND_FLAG_MUSIC_SEQUENCE) != 0u) {
            continue;
        }
        agree_elsewhere = agree_elsewhere &&
                          ((mp_script_sound_classify(flags) == MP_SCRIPT_SOUND_LOOP) ==
                           old_is_loop(flags));
    }
    ut_check(agree_elsewhere, "without a music bit the loop is the loop bit, over 8192 flag words");
}

static void check_the_route(void)
{
    int  described;
    bool all_engine = true;

    ut_section("who handles a call, and single player untouched");
    for (described = 0; described < 2; ++described) {
        all_engine = all_engine &&
                     mp_script_sound_route(false, false, described != 0) ==
                         MP_SCRIPT_SOUND_TO_ENGINE;
    }
    ut_check(all_engine, "no host and no client of a session: every call goes to the engine");
    ut_check(mp_script_sound_route(true, false, true) == MP_SCRIPT_SOUND_BY_HOST &&
                 mp_script_sound_route(true, false, false) == MP_SCRIPT_SOUND_BY_HOST,
             "the host of a started session handles every call of its scripts, a peer there or "
             "not");
    ut_check(mp_script_sound_route(false, true, true) == MP_SCRIPT_SOUND_WITHHELD,
             "a client withholds its own call for an actor whose life the host describes");
    ut_check(mp_script_sound_route(false, true, false) == MP_SCRIPT_SOUND_TO_ENGINE,
             "and lets the engine play it for an actor the host never described");
    ut_check(!mp_script_sound_output_is_the_hosts(false, true) &&
                 !mp_script_sound_output_is_the_hosts(true, false) &&
                 mp_script_sound_output_is_the_hosts(true, true),
             "the output predicate: only a client of a started session, only a life of the host's");
    ut_check(mp_script_sound_host_plays(MP_SCRIPT_SOUND_ONCE, false) &&
                 mp_script_sound_host_plays(MP_SCRIPT_SOUND_LOOP, false),
             "the host plays every sound and every loop of its own scripts, meant or not");
    ut_check(!mp_script_sound_host_plays(MP_SCRIPT_SOUND_MUSIC_STATE, false) &&
                 !mp_script_sound_host_plays(MP_SCRIPT_SOUND_MUSIC_SEQUENCE, false) &&
                 mp_script_sound_host_plays(MP_SCRIPT_SOUND_MUSIC_SEQUENCE, true),
             "and music only when it is meant for the host's own player");
}

/* Calls a pair on the substeps in `calls`, marking every event out, and counts the events. */
static unsigned events_for(const uint32_t *calls, size_t count, bool every_out)
{
    mp_script_sound_edges_t edges;
    unsigned                events = 0;
    size_t                  i;

    memset(&edges, 0, sizeof edges);
    for (i = 0; i < count; ++i) {
        int32_t row = -1;

        if (mp_script_sound_edge_note(&edges, 0x1000u, 7u, calls[i], &row) !=
            MP_SCRIPT_SOUND_HELD) {
            ++events;
            if (every_out) {
                mp_script_sound_edge_out(&edges, row, calls[i]);
            }
        }
    }
    return events;
}

static void check_the_edge(void)
{
    uint32_t every[100];
    uint32_t paused[3]  = { 10u, 12u, 14u };
    uint32_t held[3]    = { 10u, 11u, 13u };
    uint32_t twice[2]   = { 10u, 10u };
    uint32_t wrapped[4] = { 0xFFFFFFFEu, 0xFFFFFFFFu, 0u, 1u };
    size_t   i;

    ut_section("a sound played once: an edge, a hold, and once a second while held");
    for (i = 0; i < 100u; ++i) {
        every[i] = (uint32_t)(1000u + i);
    }
    ut_check(events_for(every, 100u, true) == 4u,
             "a call in every substep for 100 substeps makes four events, one a second, where "
             "the old rule made 100");
    ut_check(events_for(paused, 3u, true) == 3u, "a substep of pause between calls: three edges");
    ut_check(events_for(held, 3u, true) == 2u,
             "a call, the next substep held, then a pause and an edge again");
    ut_check(events_for(twice, 2u, true) == 1u, "the same call twice in one substep is one event");
    ut_check(events_for(wrapped, 4u, true) == 1u, "the substep count wraps without an edge");
    ut_check(events_for(every, 5u, false) == 5u,
             "an event nobody took out is tried again in every held substep until one goes");
}

static void check_the_table(void)
{
    mp_script_sound_edges_t edges;
    int32_t                 row = 0;
    uint16_t                call;

    ut_section("the table remembers pairs called in the last substep and nothing else");
    memset(&edges, 0, sizeof edges);
    for (call = 0; call < MP_SCRIPT_SOUND_EDGES; ++call) {
        (void)mp_script_sound_edge_note(&edges, 0x2000u, call, 50u, &row);
    }
    ut_check(mp_script_sound_edge_note(&edges, 0x2000u, 999u, 50u, &row) == MP_SCRIPT_SOUND_EDGE &&
                 row < 0 && edges.full == 1u,
             "a sixty fifth pair in one substep is an edge with no row, and it is counted");
    ut_check(mp_script_sound_edge_note(&edges, 0x2000u, 999u, 52u, &row) == MP_SCRIPT_SOUND_EDGE &&
                 row >= 0,
             "two substeps later every old row is stale and one is taken again");
    ut_check(mp_script_sound_edge_note(&edges, 0x2004u, 3u, 52u, &row) == MP_SCRIPT_SOUND_EDGE,
             "another actor with a call the first one had is its own pair");
    mp_script_sound_edge_out(&edges, -1, 52u);
    ut_check(edges.full == 1u, "marking the row nobody got does nothing");
}

static mp_script_music_ask_t ask_everybody(void)
{
    mp_script_music_ask_t ask;

    memset(&ask, 0, sizeof ask);
    ask.fresh     = 320u;
    ask.listeners = 0x0Fu;
    ask.nearest   = 3u;
    return ask;
}

static void check_whom_music_means(void)
{
    mp_script_music_ask_t ask;
    mp_script_music_why_t why = MP_SCRIPT_MUSIC_REASONS;

    ut_section("whom a script's music is meant for, one function for the host and every peer");
    ask = ask_everybody();
    ut_check(mp_script_music_meant(&ask, &why) == 0x0Fu && why == MP_SCRIPT_MUSIC_FOR_EVERYBODY,
             "a script that never asked for a player: everybody, so no music is heard by nobody");

    ask = ask_everybody();
    ask.answered = true;
    ask.answered_player = true;
    ask.answered_bank   = 2u;
    ask.answered_age    = 320u;
    ask.awake_for       = 0x01u;
    ut_check(mp_script_music_meant(&ask, &why) == 0x05u && why == MP_SCRIPT_MUSIC_FOR_THE_ANSWER,
             "a fresh answer: that player, and the host who would wake the actor too");

    ask.answered_age = 321u;
    ut_check(mp_script_music_meant(&ask, &why) == 0x09u && why == MP_SCRIPT_MUSIC_FOR_THE_NEAREST,
             "the same answer a substep too old: the nearest player and the one near it");

    ask.attacked      = true;
    ask.attacker_bank = 1u;
    ut_check(mp_script_music_meant(&ask, &why) == 0x03u &&
                 why == MP_SCRIPT_MUSIC_FOR_THE_ATTACKER,
             "an old answer and a fresh hit: the player who hurt it");

    ask = ask_everybody();
    ask.answered        = true;
    ask.answered_player = false;
    ut_check(mp_script_music_meant(&ask, &why) == 0x08u && why == MP_SCRIPT_MUSIC_FOR_THE_NEAREST,
             "a fresh answer that was an ally: no player, so the nearest");

    ask.nearest   = MP_SCRIPT_MUSIC_NO_BANK;
    ask.awake_for = 0x02u;
    ut_check(mp_script_music_meant(&ask, &why) == 0x02u,
             "nobody placed to be nearest: only the ones who would wake it");

    ask = ask_everybody();
    ask.listeners       = 0x03u;
    ask.answered        = true;
    ask.answered_player = true;
    ask.answered_bank   = 3u;
    ask.awake_for       = 0x0Cu;
    ut_check(mp_script_music_meant(&ask, &why) == 0x00u,
             "a player music cannot be claimed for gets none, however it is meant");
}

static void check_nearest_and_hold(void)
{
    const float at[3]        = { 0.0f, 0.0f, 0.0f };
    const float where[4][3]  = { { 5.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f },
                                 { 0.0f, 3.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    bool        placed[4]    = { true, true, true, false };
    bool        nobody[4]    = { false, false, false, false };

    ut_section("the nearest player, and how long a claim holds");
    ut_check(mp_script_music_nearest(at, where, placed, 4u) == 1u,
             "bank 1 at 3 u, before bank 2 at the same distance, and bank 3 is not placed");
    ut_check(mp_script_music_nearest(at, where, nobody, 4u) == MP_SCRIPT_MUSIC_NO_BANK,
             "nobody placed is nobody nearest");
    ut_check(mp_script_music_held(true, 2611u, 100u, 100u) == 2611u &&
                 mp_script_music_held(true, 2611u, 100u, 100u + MP_SCRIPT_MUSIC_HOLD - 1u) ==
                     2611u,
             "a claim is said for the window after the call that made it");
    ut_check(mp_script_music_held(true, 2611u, 100u, 100u + MP_SCRIPT_MUSIC_HOLD) == 0u,
             "and not a substep longer");
    ut_check(mp_script_music_held(false, 2611u, 100u, 100u) == 0u, "no claim says nothing");
    ut_check(mp_script_music_held(true, 7u, 0xFFFFFFFFu, 3u) == 7u, "across the wrap");
    ut_check(MP_SCRIPT_MUSIC_HOLD == MP_WORLD_EVENT_WINDOW,
             "the hold is the world events' window, one measure of lateness");
}

static void check_the_loop_steps(void)
{
    ut_section("one loop per replica, whatever the host asks in between");
    ut_check(mp_actor_loop_step(false, 0u, false, 0u, false) == MP_ACTOR_LOOP_NOTHING,
             "nothing wanted, nothing started: nothing");
    ut_check(mp_actor_loop_step(true, 5u, false, 0u, false) == MP_ACTOR_LOOP_START,
             "wanted and not started: start it");
    ut_check(mp_actor_loop_step(true, 5u, true, 5u, true) == MP_ACTOR_LOOP_KEEP,
             "wanted and playing: keep it");
    ut_check(mp_actor_loop_step(true, 5u, true, 5u, false) == MP_ACTOR_LOOP_START,
             "wanted, started, and the engine's distance cut ended it: start it again");
    ut_check(mp_actor_loop_step(true, 6u, true, 5u, true) == MP_ACTOR_LOOP_REPLACE,
             "another call wanted while one plays: stop it first, never two");
    ut_check(mp_actor_loop_step(true, 6u, true, 5u, false) == MP_ACTOR_LOOP_START,
             "another call wanted and the old one ended: start the new one");
    ut_check(mp_actor_loop_step(false, 0u, true, 5u, true) == MP_ACTOR_LOOP_STOP,
             "no longer wanted and playing: stop it");
    ut_check(mp_actor_loop_step(false, 0u, true, 5u, false) == MP_ACTOR_LOOP_FORGET,
             "no longer wanted and already ended: forget it");
}

int main(void)
{
    check_the_classes();
    check_the_route();
    check_the_edge();
    check_the_table();
    check_whom_music_means();
    check_nearest_and_hold();
    check_the_loop_steps();
    return ut_summary("the scripts' sounds, the rule");
}
