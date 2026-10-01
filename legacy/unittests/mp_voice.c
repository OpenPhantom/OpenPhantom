/* unittests/mp_voice.c: the judgement of a spoken line, carried out, against a stand-in engine.
 *
 * The stand-in is mp_voice_stand_in: cells in the test's memory, a field setter, a bank of twelve
 * channels, and a six byte dialogue module the hull really hulls; the session is the real reading
 * of the setup note.
 *
 * What it holds the module to: the one question on both roles, the lines of a scene for all and
 * the players it gathered, field 0 back at its resting value on every path a call can take, and
 * the subtitle option written back in the very message that held it back. The hearing radius and
 * the engine's answer are mp_voice_heard's.
 */
#include "unittest.h"

#include "mp_voice_stand_in.h"

#include "mp_lobby.h"
#include "mp_voice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A line nobody here is near, spoken where the eye stands. Judged, it is withheld and handed a
 * place past the admission, so that the engine refuses it; not judged, the engine is handed its own
 * place and decides by its eye. */
static bool a_line_is_judged(void)
{
    return si_speak().handed != (const void *)SI_LINE_AT;
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

/* Asked before any line is judged anywhere in this process, which is the state a client and a
 * host with no spoken line report from. */
static void check_the_camera_line_is_always_printed(void)
{
    ut_section("the report prints the camera of a far line on every machine, at nought as well");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("the camera of a far line: 0 refused on the host, 0 passed"),
             "a client that has judged no line prints it, with its two noughts");
    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("the camera of a far line: 0 refused on the host, 0 passed"),
             "and so does a host before its first spoken line");
}

/* After every path the other checks walked, the report's must-be-nought witnesses. */
static void check_the_witnesses_stay_at_nought(void)
{
    ut_section("the report's witnesses stay at nought after every path above");

    si_forget_the_log();
    mp_voice_report();
    ut_check(si_logged("left at nought after a call 0 time(s) (must be 0)"),
             "field 0 was never left at nought after a call");
    ut_check(si_logged("the option left changed after a frame 0 time(s) (must be 0)"),
             "and the subtitle option never left changed after a frame");
    ut_check(si_logged("left changed after a call 0 time(s) (must be 0)"),
             "and field 5 never left raised after a call");
    ut_check(!si_logged("the camera of a far line: 0 refused on the host, 0 passed"),
             "while the camera line now counts the takes asked above");
}

/* ==============================================================================================
 * The one question, on both roles.
 * ============================================================================================ */

static void check_the_session_for_host_and_client(void)
{
    mp_voice_replay_t replay;
    int32_t           line;

    ut_section("a session the host started runs for the host and for a client alike");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    ut_check(a_line_is_judged(), "a host with a client joined judges its lines");
    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    ut_check(a_line_is_judged(), "and so does the client");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, false);
    ut_check(a_line_is_judged(),
             "a host judges before any client has joined, and after the last one left");
    si_set_the_world(true, MP_LOBBY_F_STARTED, true, false);
    ut_check(a_line_is_judged(),
             "a client judges in its first level before its first substep has run");

    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(!replay.say && replay.measured,
             "and a far line of the host that arrives then is held back, not said unjudged");

    ut_section("and runs for nobody without a started session");

    si_set_the_world(false, 0u, false, false);
    ut_check(!a_line_is_judged(), "with no setup note, a single player game, nothing is judged");
    si_set_the_world(true, 0u, true, true);
    ut_check(!a_line_is_judged(), "a lobby that has not started judges nothing, joined or not");
    si_set_the_world(true, (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED), false, true);
    ut_check(!a_line_is_judged(), "a host that ended the session judges nothing");
    si_set_the_world(true, (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED), true, true);
    ut_check(!a_line_is_judged(), "nor does a client told it is over");

    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(replay.say && replay.place == (const void *)SI_LINE_AT,
             "a line of the host is then said as the engine decides");
    mp_voice_replay_end(line, 1);
}

static void check_a_host_whose_clients_left(void)
{
    static const float beside[3] = { 10.0f, 0.0f, 0.0f };
    si_spoken_t        said;

    ut_section("a host whose clients all left measures nobody where they last stood");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    si.far_players = 1u;
    memcpy(si.far_at, beside, sizeof si.far_at);
    ut_check(si_speak().volume_during == 0.0f,
             "with a client joined beside the line, the host keeps it alive at no volume");

    si.joined = false;
    said      = si_speak();
    ut_check(said.handed != (const void *)SI_LINE_AT && said.volume_during == SI_RESTING,
             "with nobody joined the range gate is not refreshed, and the line is withheld");
}

/* ==============================================================================================
 * The lines of a scene for all.
 * ============================================================================================ */

static void in_a_scene(const float anchor[3], bool gathered)
{
    si.scene_for_all = true;
    si.anchor_known  = true;
    si.gathered      = gathered;
    si.serial        = 2u;
    memcpy(si.anchor, anchor, sizeof si.anchor);
}

/* Where the scene gathers is the one thing both sides know of it: a client is never told who
 * speaks a line, so the host does not ask either, and a line is the scene's on every machine when
 * it is spoken within the engine's admission of that place. */
static void check_the_lines_of_a_scene(void)
{
    static const float near_line[3] = { 0.0f, 30.0f, 0.0f };
    static const float actor_off[3] = { 0.0f, 90.0f, 0.0f };
    static const float far_off[3]   = { 0.0f, 400.0f, 0.0f };
    si_spoken_t        said;
    mp_voice_replay_t  replay;
    int32_t            line;

    ut_section("a scene for all presents the lines spoken near where it gathers to a player it "
               "gathered");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    in_a_scene(actor_off, true);
    said = si_say(&si_actor_body, SI_LINE_AT);
    ut_check(said.handed == (const void *)SI_LINE_AT && said.camera,
             "on the host a line spoken ninety units from where the scene gathers is presented "
             "far from this body, and keeps its camera");
    said = si_say(&si_other_body, SI_LINE_AT);
    ut_check(said.handed == (const void *)SI_LINE_AT && said.camera,
             "whoever speaks it: the host asks the place, as a client does");
    in_a_scene(near_line, true);
    said = si_say(&si_other_body, SI_LINE_AT);
    ut_check(said.handed == (const void *)SI_LINE_AT && said.camera,
             "and so is a line thirty units from it");
    in_a_scene(far_off, true);
    said = si_say(&si_actor_body, SI_LINE_AT);
    ut_check(said.handed != (const void *)SI_LINE_AT && !said.camera,
             "a line four hundred units from it is none of the scene's, whoever speaks it, and is "
             "refused its camera as outside a scene");
    in_a_scene(near_line, false);
    said = si_say(&si_actor_body, SI_LINE_AT);
    ut_check(said.handed != (const void *)SI_LINE_AT && !said.camera,
             "a host the scene did not gather judges a line of it by the radius");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    in_a_scene(actor_off, true);
    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(replay.say, "a client says a line of the host spoken ninety units from where the "
             "scene gathers, which the host presents as well");
    mp_voice_replay_end(line, 1);
    in_a_scene(near_line, false);
    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(!replay.say, "unless the scene left this player where it stood, far from the line");
    in_a_scene(far_off, true);
    line   = si_next_line();
    replay = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(!replay.say, "and holds back one far from that place and from this body");
    si.scene_for_all = false;
}

/* ==============================================================================================
 * Field 0 and its resting value.
 * ============================================================================================ */

/* A host far from the line with a client beside it: the line is kept alive at no volume. */
static void a_line_kept_alive(void)
{
    static const float beside[3] = { 10.0f, 0.0f, 0.0f };

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    si.far_players = 1u;
    memcpy(si.far_at, beside, sizeof si.far_at);
}

static void check_the_resting_value_on_every_path(void)
{
    si_spoken_t       said;
    mp_voice_replay_t replay;
    int32_t           line;

    ut_section("field 0 goes to nought for a line kept alive, and back to 2.0 on every path");

    a_line_kept_alive();
    said = si_speak();
    ut_check(said.volume_during == 0.0f && said.volume_after == SI_RESTING,
             "the engine reaches the voice and puts the field back itself: 2.0 after");

    a_line_kept_alive();
    si.call.has_a_record = false;
    said = si_speak();
    ut_check(said.volume_during == 0.0f && said.volume_after == SI_RESTING,
             "the engine returns before the voice (no record): 2.0 written here");

    a_line_kept_alive();
    si.voices = 0;
    said = si_speak();
    ut_check(said.volume_during == 0.0f && said.volume_after == SI_RESTING,
             "and so with voices off");

    a_line_kept_alive();
    si.call.starts = 0;
    said = si_speak();
    ut_check(said.volume_during == 0.0f && said.volume_after == SI_RESTING,
             "the engine answers that no line started: 2.0 all the same");

    a_line_kept_alive();
    si.eye_pointer       = 0u;
    si.call.has_a_record = false;
    said = si_speak();
    si.eye_pointer = (uint32_t)(uintptr_t)si.eye;
    ut_check(said.volume_during == 0.0f && said.volume_after == SI_RESTING,
             "with the eye unreadable the line goes silent at its own place, and 2.0 after");

    a_line_kept_alive();
    si.call.has_a_record = false;
    si.volume = 1.5f;
    said = si_speak();
    ut_check(said.volume_after == SI_RESTING,
             "the value written back is the engine's own 2.0, not whatever the field held before");

    ut_section("and is not touched where no line is kept alive");

    a_line_kept_alive();
    said = si_say(&si_other_body, NULL);
    ut_check(said.volume_during == SI_RESTING && said.volume_after == SI_RESTING,
             "a line with no place: a flat voice never reads the field, so it is left alone");

    a_line_kept_alive();
    si.call.holds_the_speaker = true;
    si.call.starts            = 0;
    said = si_speak();
    ut_check(said.volume_during == SI_RESTING && said.volume_after == SI_RESTING,
             "a line the engine only holds on is handed nothing");

    a_line_kept_alive();
    si.far_players = 0u;
    said = si_speak();
    ut_check(said.volume_during == SI_RESTING && said.volume_after == SI_RESTING,
             "a line withheld with nobody near is refused by its place, at its volume");

    a_line_kept_alive();
    si.flags |= MP_LOBBY_F_ENDED;
    said = si_speak();
    ut_check(said.volume_during == SI_RESTING && said.volume_after == SI_RESTING,
             "and nothing is lowered after the session ended");

    si_set_the_world(true, MP_LOBBY_F_STARTED, true, true);
    si.body[0] = 0.0f;
    line       = si_next_line();
    replay     = mp_voice_replay_begin(MP_VOICE_FROM_HOST, line, SI_LINE_AT);
    ut_check(replay.say && si.volume == SI_RESTING,
             "a line of the host said again is never silent");
    mp_voice_replay_end(line, 1);
    ut_check(si.volume == SI_RESTING, "and leaves the field at rest");
}

/* ==============================================================================================
 * The subtitle option, held back for one message and written back in it.
 * ============================================================================================ */

static void check_the_subtitle_option_is_written_back(void)
{
    si_spoken_t said;
    int32_t     withheld_line;

    ut_section("the subtitle option is nought for the frame that draws a withheld line, and back");

    si_set_the_world(true, MP_LOBBY_F_STARTED, false, true);
    said          = si_speak();
    withheld_line = said.line;
    si.shown      = withheld_line;

    si.option = 1;
    ut_check(si_message(SI_FRAME_2D) == 0u && si.option == 1,
             "the engine draws the frame with the option at nought, and finds 1 again after it");
    si.option = 2;
    ut_check(si_message(SI_FRAME_2D) == 0u && si.option == 2,
             "what is written back is the value read, not a constant");

    ut_section("and every other path leaves it as the player set it");

    si.option = 1;
    ut_check(si_message(SI_FRAME_2D - 1) == 1u && si.option == 1, "another message is not touched");
    si.shown = withheld_line + 1;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1, "nor is another line on show");
    si.shown  = withheld_line;
    si.option = 0;
    ut_check(si_message(SI_FRAME_2D) == 0u && si.option == 0,
             "nor a player who switched subtitles off");

    si.option       = 1;
    si.block_active = 0u;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1, "a closed block holds nothing back");
    si.block_active = 1u;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1,
             "and its verdict is gone with it, the one exit");

    said     = si_speak();
    si.shown = said.line;
    si.flags |= MP_LOBBY_F_ENDED;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1,
             "an ended session holds nothing back");
    si.flags = MP_LOBBY_F_STARTED;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1,
             "and forgot the verdict when it ended");

    si.body[0] = 0.0f;
    said       = si_speak();
    si.shown   = said.line;
    ut_check(si_message(SI_FRAME_2D) == 1u && si.option == 1,
             "a presented line's subtitle is shown");
}

int main(void)
{
    ut_check(si_start(), "the rule binds against the stand-in engine");
    ut_check(si.line_camera != NULL, "and hands the scene gates its camera question");

    check_the_camera_line_is_always_printed();
    check_the_session_for_host_and_client();
    check_a_host_whose_clients_left();
    check_the_lines_of_a_scene();
    check_the_resting_value_on_every_path();
    check_the_subtitle_option_is_written_back();
    check_the_witnesses_stay_at_nought();
    return ut_summary("mp_voice");
}
