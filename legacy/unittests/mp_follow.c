/* mp_follow.c: the host changes the world, and its clients go with it.
 *
 * The two decisions, pinned with no session and no game in the process:
 *
 *   - which level begin on the host is a world change: not the first one of a generation, which
 *     is the level its start named, and every later one under the same generation;
 *   - what a client does with a world change: leave a running level once, wait while it lets go
 *     or while a start is driven, then start, and leave a change it finds at the title with no
 *     level behind it to the lobby screen.
 *
 * Without this module a start has no second consumer at all: a host that finishes its level is
 * alone in the next one.
 */
#include "unittest.h"

#include "mp_bridge_appearance.h"
#include "mp_bridge_lobby.h"
#include "mp_follow.h"
#include "mp_lobby.h"
#include "mp_scene_rule.h"
#include "mp_world_door.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static void check_the_host(void)
{
    ut_section("a host's level begin is a world change only after the start's own level");

    ut_check(!mp_follow_host_changed_world(false, 3u, true, 3u),
             "no started session, no change: a single player level is nobody's");
    ut_check(!mp_follow_host_changed_world(true, 3u, false, 0u),
             "the first level begin of all is the one the start named");
    ut_check(!mp_follow_host_changed_world(true, 4u, true, 3u),
             "and so is the first under a new generation, which a lobby start raised");
    ut_check(mp_follow_host_changed_world(true, 4u, true, 4u),
             "a second level under the same generation is a new world: the next level, a restore "
             "or the level begun again");
    ut_check(mp_follow_host_changed_world(true, 0u, true, 0u),
             "a generation that wrapped to nought is still compared, not ordered");
}

static void check_the_client(void)
{
    ut_section("a client leaves a running level once, then starts the host's");

    ut_check(mp_follow_client_step(false, true, false, false, false) == MP_FOLLOW_NOTHING,
             "no world change: nothing, whatever else is true");
    ut_check(mp_follow_client_step(true, true, false, false, false) == MP_FOLLOW_LEAVE,
             "a world change and a running level: leave it");
    ut_check(mp_follow_client_step(true, true, true, false, false) == MP_FOLLOW_WAIT,
             "and leave it once, not on every frame the level takes to let go");
    ut_check(mp_follow_client_step(true, false, true, false, false) == MP_FOLLOW_WAIT,
             "out of the level and the campaign not yet on its way to the title: a start now "
             "would be driven into whatever screen the level's end still shows");
    ut_check(mp_follow_client_step(true, false, true, true, true) == MP_FOLLOW_WAIT,
             "at the title with a start still being driven: wait for it");
    ut_check(mp_follow_client_step(true, false, true, true, false) == MP_FOLLOW_START,
             "at the title and nothing driven: start the host's world");

    ut_section("a change found at the title with nothing left is not the follower's");
    ut_check(mp_follow_client_step(true, false, false, true, false) == MP_FOLLOW_NOTHING,
             "that is the lobby screen's start, and taking it here would race the screen");
}

/* A far player's hero is judged on samples a replay delay old. Warned on the first sample after
 * the body is built, every character swap inside a session reads as a mismatch naming the hero
 * of the swap before it, while the roster line printed a moment earlier already shows the new
 * one; a field run over the internet logged four of those. The first two checks below pin what a
 * warning on the first sample gets right; the rest are the grace. */
static void check_the_hero_verdict(void)
{
    ut_section("a hero difference waits out the replay delay before it is called one");

    ut_check(mp_appearance_hero_verdict(-1, 2, 999u, 64u) == MP_APPEARANCE_WAIT,
             "no body built yet, so there is nothing to hold the wire against");
    ut_check(mp_appearance_hero_verdict(2, 2, 1u, 64u) == MP_APPEARANCE_AGREED,
             "the same hero on the first sample is an agreement and is said so");
    ut_check(mp_appearance_hero_verdict(0, 2, 1u, 64u) == MP_APPEARANCE_WAIT,
             "a fresh body and the hero of the swap before it is the delay, not a disagreement");
    ut_check(mp_appearance_hero_verdict(0, 2, 63u, 64u) == MP_APPEARANCE_WAIT,
             "still inside the grace on its last sample");
    ut_check(mp_appearance_hero_verdict(0, 2, 64u, 64u) == MP_APPEARANCE_DISAGREES,
             "and past it the difference is the finding the warning was written for");
    ut_check(mp_appearance_hero_verdict(0, 0, 64u, 64u) == MP_APPEARANCE_AGREED,
             "an agreement outlasts the grace and is never a warning");
}

/* Which world a player is in belongs to the host, and a client has two ways to leave that
 * agreement without anybody noticing: the load button of the pause and death screens, and a
 * script of its own that ends or fails the level. Both are held, and nothing else is: a
 * host, a side with no session and a side whose session has ended play the retail game.
 *
 * The doors had a predicate of their own for that, a copy of the scene gate's logic, and now ask
 * the scene gate's one. The copy is kept here as the reference over all eight inputs, so the day
 * the shared question answers otherwise than the doors did is the day this fails. */
static bool doors_as_they_were(bool is_client, bool started, bool ended)
{
    return is_client && started && !ended;
}

static void check_the_doors_out_of_a_world(void)
{
    unsigned input;

    ut_section("a client in a running session takes none of the doors out of its world");

    ut_check(mp_scene_client_of_a_started_session(true, false, true),
             "a client whose session has started and not ended is held at every door");
    ut_check(!mp_scene_client_of_a_started_session(true, false, false),
             "a host is held at none: the world is its own to change");
    ut_check(!mp_scene_client_of_a_started_session(false, false, true),
             "and a client whose session has not started yet is in nobody's world");
    ut_check(!mp_scene_client_of_a_started_session(true, true, true),
             "an ended session leaves the retail game behind it, doors and all");
    for (input = 0; input < 8u; ++input) {
        bool client  = (input & 1u) != 0u;
        bool started = (input & 2u) != 0u;
        bool ended   = (input & 4u) != 0u;

        ut_checkf(mp_scene_client_of_a_started_session(started, ended, client) ==
                      doors_as_they_were(client, started, ended),
                  "input %u: the shared question answers as the doors did", input);
    }

    ut_section("and only the two commands that end a level are a door at all");
    ut_check(mp_world_door_command_ends_level(1), "command 1 writes the level outcome");
    ut_check(mp_world_door_command_ends_level(14), "command 14 writes the failure");
    ut_check(!mp_world_door_command_ends_level(0) && !mp_world_door_command_ends_level(2) &&
             !mp_world_door_command_ends_level(13) && !mp_world_door_command_ends_level(15) &&
             !mp_world_door_command_ends_level(19),
             "the other eighteen are camera work, fades, sounds and counters");
}

static void check_the_movie_that_opens_a_level(void)
{
    ut_section("a movie of the host's announces the next world only when it opens that level");

    ut_check(mp_follow_movie_opens_level("scene2", "movie\\scene2"),
             "the level table names the movie with its folder, the note without it");
    ut_check(mp_follow_movie_opens_level("SCENE2", "movie\\scene2"),
             "compared the way the engine compares a file name, without regard to case");
    ut_check(mp_follow_movie_opens_level("arena", "movie\\arena"),
             "the pod race opens with arena, which is its own opening and no replay");
    ut_check(mp_follow_movie_opens_level("scene3", "movie/scene3"), "either separator");
    ut_check(!mp_follow_movie_opens_level("scene2", "movie\\scene1"),
             "a movie that is not the level's opening announces nothing");
    ut_check(!mp_follow_movie_opens_level("scene8", "movie\\scene8"),
             "the ending never does, whatever a table says, because it opens no level");
    ut_check(!mp_follow_movie_opens_level("", "movie\\"),
             "a movie whose name did not fit the note is nobody's opening");
    ut_check(!mp_follow_movie_opens_level("scene1", ""),
             "a level that opens with no movie has none to match");
    ut_check(!mp_follow_movie_opens_level(NULL, "movie\\scene1") &&
             !mp_follow_movie_opens_level("scene1", NULL), "and nothing matches nothing");
    ut_check(!mp_follow_movie_opens_level("scene1x", "movie\\scene1") &&
             !mp_follow_movie_opens_level("scene", "movie\\scene1"),
             "a name is matched whole and not by its front");
}

/* The generation the host's setup note carries now, which every announcement raises. */
static uint8_t generation_now(void)
{
    mp_lobby_setup_t setup;

    memset(&setup, 0, sizeof setup);
    ut_check(mp_bridge_lobby_setup(&setup), "the setup note reads");
    return setup.generation;
}

/* The sequence of a campaign on the host, through the functions the engine's broadcasts and the
 * movie note reach: the lobby's start, a level begin, the next level's movie, the level begin
 * behind it. The test never raises the generation by hand; the lobby's start and every
 * announcement raise it. */
static void check_the_host_sequence(void)
{
    mp_lobby_setup_t setup;
    uint8_t          start;

    ut_section("the host's campaign, through the real functions: one announcement per world");

    memset(&setup, 0, sizeof setup);
    setup.mode = 1u;
    memcpy(setup.level, "level\\swamp.b3d", 16u);
    mp_bridge_lobby_set_setup(&setup);
    mp_bridge_lobby_start();
    start = generation_now();

    mp_follow_note_host_movie_in("scene2", "movie\\scene2");
    ut_check(generation_now() == start,
             "the opening movie of the level the lobby started announces nothing");
    mp_follow_note_level_begin();
    ut_check(generation_now() == start, "nor does that level's begin");

    mp_follow_note_host_movie_in("scene3", "movie\\scene3");
    ut_check(generation_now() == (uint8_t)(start + 1u),
             "the next level's movie announces the next world as it begins");
    mp_follow_note_level_begin();
    ut_check(generation_now() == (uint8_t)(start + 1u),
             "and the level begin behind it announces nothing a second time");

    mp_follow_note_host_movie_in("scene8", "movie\\scene8");
    mp_follow_note_host_movie_in("scene4", "movie\\scene3");
    mp_follow_note_host_movie_in("scene4", "");
    ut_check(generation_now() == (uint8_t)(start + 1u),
             "the ending, a movie that opens another level and one with no row announce nothing");

    mp_follow_note_level_begin();
    ut_check(generation_now() == (uint8_t)(start + 2u),
             "a level begun again with no movie in front of it is announced at its begin");
}

int main(void)
{
    check_the_host();
    check_the_client();
    check_the_hero_verdict();
    check_the_doors_out_of_a_world();
    check_the_movie_that_opens_a_level();
    check_the_host_sequence();
    return ut_summary("mp_follow");
}
