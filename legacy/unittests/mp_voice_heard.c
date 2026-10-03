/* unittests/mp_voice_heard.c: whether a line this player is meant to hear reaches his ears, against
 * the stand-in engine of mp_voice_stand_in.
 *
 * Three things the judgement of a line does, and each is walked here against a bank of twelve
 * channels laid out as the engine's: a line is presented within four full volume radii of a voice
 * and no farther; a presented line is handed priority 101 and every path puts 90 back; the engine's
 * answer to a line and the end of its voice are read and named, with a line of the host's scene
 * always named and every other line up to a cap each level, and a scene summed up once when it
 * ends. And once a client has said a line of the host again and the engine voiced it, an older
 * voice lets go of the handle it shares with it; a line the engine did not voice, the same line
 * still latched above all, leaves every voice what it owned.
 */
#include "unittest.h"

#include "mp_voice_stand_in.h"

#include "mp_armed.h"
#include "mp_lobby.h"
#include "mp_voice.h"
#include "mp_voice_report.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PRESENTED_PRIORITY 101
#define LOCK_OF_A_SCENE    5

static void at(float x)
{
    si.body[0] = x;
    si.body[1] = 0.0f;
    si.body[2] = 0.0f;
}

static bool said_again_at(float x)
{
    at(x);
    return si_say_again();
}

/* ==============================================================================================
 * The hearing radius.
 * ============================================================================================ */

static void check_the_hearing_radius(void)
{
    si_spoken_t said;

    ut_section("a client says a line of the host again within sixteen units of its body");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    ut_check(said_again_at(16.0f), "sixteen units off, the line is said");
    ut_check(!said_again_at(16.5f), "half a unit farther, it is held back");
    ut_check(!said_again_at(30.0f), "and thirty units off, where the engine would still play it at "
             "an eighth of its amplitude, nobody is shown a subtitle for it");
    si.lock = LOCK_OF_A_SCENE;
    ut_check(said_again_at(30.0f), "under a scene's lock the radius is thirty two units");
    ut_check(!said_again_at(33.0f), "and no farther");
    si.lock = 0;

    ut_section("a host keeps a line it does not hear alive while a player stands within a hundred");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    at(20.0f);
    said = si_speak();
    ut_check(said.volume_during == 0.0f && said.handed != (const void *)SI_LINE_AT,
             "a host twenty units off keeps its own line alive at no volume, at the eye");
    at(16.0f);
    said = si_speak();
    ut_check(said.volume_during == SI_RESTING && said.handed == (const void *)SI_LINE_AT,
             "and sixteen units off presents it at its own place");
    at(100.5f);
    said = si_speak();
    ut_check(said.volume_during == SI_RESTING && said.handed != (const void *)SI_LINE_AT,
             "past a hundred units with nobody near, it is withheld and put past the admission");

    ut_section("the binding says what the lines are heard within");

    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("presented (this body within 16.0 u)"),
             "the report prints the radius outside a scene's lock");
}

/* ==============================================================================================
 * The priority of a presented line.
 * ============================================================================================ */

static void check_the_priority(void)
{
    si_spoken_t       said;
    mp_voice_replay_t replay;
    int32_t           line;
    static const float beside[3] = { 10.0f, 0.0f, 0.0f };

    ut_section("a presented line with a place is handed priority 101, and 90 comes back after");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    at(0.0f);
    said = si_speak();
    ut_check(said.priority_during == PRESENTED_PRIORITY &&
                 said.priority_after == SI_PRIORITY_RESTING,
             "the engine reads 101 inside the call and puts 90 back itself");
    at(0.0f);
    si.call.has_a_record = false;
    said = si_speak();
    ut_check(said.priority_during == PRESENTED_PRIORITY &&
                 said.priority_after == SI_PRIORITY_RESTING,
             "the engine returns before the voice: 90 is written back here");
    at(0.0f);
    si.call.starts = 0;
    said = si_speak();
    ut_check(said.priority_after == SI_PRIORITY_RESTING, "and so when no line started");

    ut_section("and no other line is");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    si.far_players = 1u;
    memcpy(si.far_at, beside, sizeof si.far_at);
    said = si_speak();
    ut_check(said.priority_during == SI_PRIORITY_RESTING && said.volume_during == 0.0f,
             "a line kept alive silent keeps 90: it must not take a channel anybody hears");
    si.far_players = 0u;
    said = si_speak();
    ut_check(said.priority_during == SI_PRIORITY_RESTING, "nor does a withheld one");
    at(0.0f);
    said = si_say(&si_other_body, NULL);
    ut_check(said.priority_during == SI_PRIORITY_RESTING, "nor a line with no place, a flat voice");

    ut_section("a line of the host said again is raised the same way");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    at(0.0f);
    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(replay.say && si.priority == PRESENTED_PRIORITY, "101 while it is said");
    mp_voice_replay_end(line, 1);
    ut_check(si.priority == SI_PRIORITY_RESTING, "and 90 after");
}

/* ==============================================================================================
 * The engine's answer, line by line.
 * ============================================================================================ */

static bool line_says(int32_t line, const char *text)
{
    char head[64];

    (void)text_format(head, sizeof head, "line %d,", (int)line);
    return si_logged(head) && si_logged(text);
}

static void a_presented_line(void)
{
    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    mp_voice_world_ended();
    at(0.0f);
    si_forget_the_log();
}

static void check_the_engine_answer(void)
{
    si_spoken_t said;
    size_t      channel;

    ut_section("every judged line says what the engine made of it");

    a_presented_line();
    said = si_speak();
    ut_check(line_says(said.line, "; the eye 0.00 u away, lock 0; the engine: voiced on channel 5"),
             "a presented line voiced on a channel names it");

    a_presented_line();
    si.voices = 0;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: refused: voices are off"), "voices off");

    a_presented_line();
    si.latch = si_next_line() + 1;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: refused: the same line was still latched"),
             "the same line still latched");

    a_presented_line();
    for (channel = 0u; channel < SI_CHANNELS; ++channel) {
        si_bank_hold(channel, 200u, true, NULL);
    }
    si.call.channel = -1;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: refused: no channel below priority 101"),
             "twelve channels busy with sounds ranked above it");

    a_presented_line();
    si_bank_hold(3u, 90u, true, &si.bark);
    si.call.channel = -1;
    said = si_speak();
    ut_check(line_says(said.line,
                       "the engine: refused: its wav or another still held it (QGm3219.wav on "
                       "channel 3)"),
             "an older voice of a line still on a channel, named with its wav");

    a_presented_line();
    si.call.channel = -1;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: refused otherwise"), "any other refusal");

    a_presented_line();
    si.call.has_a_record = false;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: refused otherwise"),
             "a line with no record leaves the latch where it was");

    a_presented_line();
    for (channel = 0u; channel < SI_CHANNELS; ++channel) {
        si_bank_hold(channel, 90u, true, NULL);
    }
    said = si_speak();
    ut_check(line_says(said.line, "voiced on channel 5, taken from a lower sound"),
             "with every channel busy at ninety, a line at a hundred and one takes one");

    a_presented_line();
    si.call.starts            = 0;
    si.call.holds_the_speaker = true;
    said = si_speak();
    ut_check(line_says(said.line, "the engine: not asked"),
             "a new line the engine only shows the subtitle of was never asked for a voice");

    ut_section("and the report sums the answers to the lines presented");

    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("the engine's answer to the lines presented here: "),
             "the report has a line for the engine's answer");
    ut_check(si_logged("1 still latched, 1 voices off, 1 no channel, 1 a wav still held, 2 other"),
             "with every refusal walked above counted by its cause");
    ut_check(si_logged("line(s) took a channel from a lower sound"),
             "and a line for the priority");
    ut_check(si_logged("heard aloud"), "and the silent voices carry the number heard aloud");
}

/* ==============================================================================================
 * The older voice.
 * ============================================================================================ */

static uint32_t owner_is_the_handle(void)
{
    return (uint32_t)(uintptr_t)&si.bark;
}

static void check_the_older_voice_lets_go(void)
{
    mp_voice_replay_t replay;
    int32_t           line;

    ut_section("once a client has said a line again, an older voice lets go of its handle");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    at(0.0f);
    si_bank_hold(2u, 90u, true, &si.bark);
    si_bank_hold(7u, 50u, true, NULL);
    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(replay.say && si_bank_owner(2u) == owner_is_the_handle(),
             "before the call nothing is let go: the engine may still refuse the line");
    mp_voice_replay_end(line, 1);
    ut_check(si_say_again() && si.bark == 5 && si_bank_owner(5u) == owner_is_the_handle(),
             "the line said again is voiced on channel 5, which owns the handle");
    ut_check(si_bank_owner(2u) == 0u,
             "and the voice that owned the same handle owns nothing any more, and plays on");
    ut_check(si_bank_owner(7u) == 0u && si_bank_owner(9u) == 0u,
             "a sound of no line is not touched");

    ut_section("a line the engine did not voice lets nothing go");

    /* Two droids of one script say the same line one after the other: the host sends both, and
     * the engine here refuses the second at its latch before it touches the handle. */
    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    at(0.0f);
    line = si_next_line();
    ut_check(si_say_again_line(line) && si.bark == 5, "a line of the host is voiced on channel 5");
    ut_check(si_say_again_line(line), "and the same line is said again at once");
    ut_check(si.bark == 5 && si_bank_owner(5u) == owner_is_the_handle(),
             "the engine refused it at its latch, so the voice on channel 5 keeps the handle, and "
             "its own end still writes -1 into it and lets the block close");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    at(0.0f);
    si_bank_hold(2u, 90u, true, &si.bark);
    si.call.channel = -1;
    ut_check(si_say_again() && si.bark == -1 && si_bank_owner(2u) == owner_is_the_handle(),
             "a line the engine gave no channel leaves the older voice its owner: the handle "
             "already reads -1, and that is all its end writes");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    si_bank_hold(2u, 90u, true, &si.bark);
    line   = si_next_line();
    at(500.0f);
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(!replay.say && si_bank_owner(2u) == (uint32_t)(uintptr_t)&si.bark,
             "a line not said here lets nothing go");

    ut_section("a host only counts what the engine does to a line's handle");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    at(0.0f);
    si_bank_hold(2u, 90u, true, &si.bark);
    (void)si_speak();
    ut_check(si_bank_owner(2u) == (uint32_t)(uintptr_t)&si.bark,
             "the host's older voice keeps its owner, as in a single player game");

    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("older voice(s) let go of the handle after a line was said again here") &&
                 si_logged(", 1 older voice(s) let go"),
             "the report counts the one let go, not the lines the engine did not voice");
}

/* ==============================================================================================
 * How a voice ends.
 * ============================================================================================ */

static void check_how_a_voice_ends(void)
{
    si_spoken_t said;
    int32_t     line;

    ut_section("the end of a named line's voice is named with how it came");

    a_presented_line();
    said = si_speak();
    si.bark = -1;
    si_bank_free(5u);
    (void)si_message(SI_FRAME_2D);
    ut_check(line_says(said.line, "ended here") && si_logged(", at its own end"),
             "a channel freed by its own end");

    a_presented_line();
    said = si_speak();
    si.bark = -1;
    (void)si_message(SI_FRAME_2D);
    ut_check(si_logged("its handle was taken by the end of an older voice"),
             "a handle gone while its channel plays on with the block open was taken");

    a_presented_line();
    said = si_speak();
    si.bark         = -1;
    si.block_active = 0u;
    (void)si_message(SI_FRAME_2D);
    si.block_active = 1u;
    ut_check(si_logged("the block closed while it still played"),
             "with the block shut it was the close that let it go");

    a_presented_line();
    said = si_speak();
    si.call.channel = 6;
    (void)si_speak();
    ut_check(si_logged("a newer line began while it still played"),
             "a new line said over a voice that still plays");
    (void)said;

    ut_section("a line the engine refused at its latch leaves the voice that plays to its end");

    a_presented_line();
    line = si_next_line();
    (void)si_say_line(&si_other_body, SI_LINE_AT, line);
    (void)si_say_line(&si_actor_body, SI_LINE_AT, line);
    ut_check(!si_logged("a newer line began while it still played"),
             "a second speaker's same line, refused at the latch, ends no voice");
    si.bark = -1;
    si_bank_free(5u);
    (void)si_message(SI_FRAME_2D);
    ut_check(si_logged_count(", at its own end") == 1u,
             "the voice that went on playing ends at its own end, once");

    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("1 handle(s) taken by an older voice's end"),
             "the report counts the one taken");
}

/* ==============================================================================================
 * The cap each level, and a scene of the host's.
 * ============================================================================================ */

static void check_the_cap_and_the_scene(void)
{
    size_t index;

    ut_section("lines are named up to sixty four each level, and then only counted");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    mp_voice_world_ended();
    si_forget_the_log();
    for (index = 0u; index < MP_VOICE_LINES_NAMED + 6u; ++index) {
        (void)said_again_at(500.0f);
    }
    ut_check(si_logged_count("a line was judged here:") == MP_VOICE_LINES_NAMED,
             "sixty four lines of a level are named");
    mp_voice_world_ended();
    si_forget_the_log();
    (void)said_again_at(500.0f);
    ut_check(si_logged_count("a line was judged here:") == 1u,
             "and the next level names its lines from the first again");

    ut_section("while a scene of the host's stands every line is named, and the scene is summed "
               "up once");

    for (index = 0u; index < MP_VOICE_LINES_NAMED; ++index) {
        (void)said_again_at(500.0f);
    }
    si.scene_for_all = true;
    si.serial        = 7u;
    si.gathered      = true;
    si.anchor_known  = true;
    memset(si.anchor, 0, sizeof si.anchor);
    si_forget_the_log();
    (void)said_again_at(500.0f);
    (void)said_again_at(500.0f);
    ut_check(si_logged_count("a line was judged here:") == 2u,
             "past the cap, both lines of the scene are named");
    (void)si_message(SI_FRAME_2D);
    ut_check(!si_logged("the lines of a scene that ended here:"),
             "and nothing is summed up while it runs");
    si.scene_for_all = false;
    (void)si_message(SI_FRAME_2D);
    ut_check(si_logged("the lines of a scene that ended here: scene 7, 2 judged, 2 of the scene, "
                       "2 presented, 2 voiced, 0 refused by the engine, 0 withheld"),
             "at its falling edge the scene is summed up, every line presented and voiced");
    si_forget_the_log();
    (void)si_message(SI_FRAME_2D);
    ut_check(!si_logged("the lines of a scene that ended here:"), "once");

    si.scene_for_all = true;
    si.serial        = 8u;
    (void)said_again_at(500.0f);
    si_forget_the_log();
    mp_voice_world_ended();
    ut_check(si_logged("the lines of a scene that ended here: scene 8, 1 judged"),
             "a level that ends under a scene sums the scene up there");
    si.scene_for_all = false;
}

int main(void)
{
    ut_check(si_start(), "the rule binds against the stand-in engine");
    ut_check(si_logged("the lines are heard here within 16.0 u of this body, 32.0 u under a "
                       "scene's lock (4.00 full-volume radii of a voice, 4.0 and 8.0 read at "
                       "00417321 and 00417310, the lock level 5 at 00417302)"),
             "the binding says what a line is heard within, and where it read it");
    ut_check(si_logged("a presented voice is handed priority 101"),
             "and what priority a presented voice is handed");
    ut_check(si_logged("the engine's answer to a line is read here:"),
             "and that the engine's answer is read");

    /* The engine's answer first, so the report's sum is of its lines alone. */
    check_the_engine_answer();
    check_the_hearing_radius();
    check_the_priority();
    check_the_older_voice_lets_go();
    check_how_a_voice_ends();
    check_the_cap_and_the_scene();
    return ut_summary("mp_voice_heard");
}
