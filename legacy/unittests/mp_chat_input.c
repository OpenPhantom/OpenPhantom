/* mp_chat_input.c: the chat's key, input line and one way out, as the module ships.
 *
 * The module is the real one, and so are the key table, the typing rule, the holders and the
 * session note the holders publish; the engine and the modules around the chat are the stand-in's.
 * Every way out of an open chat is walked, and after each the same fields are read: typing, the
 * line, the hold, the session note, and the one close counter that grew. Then the opening press
 * against the handlers further in, the input split and the transport; the hold a closing key keeps
 * until its own release; a line that could not be sent; the typing counters; the key read again.
 *
 * With the argument no-movie-cell the operand of the movie cell is refused, which is the one way
 * the cell can be missing while the hook stands. The module arms once a process, so that case is a
 * run of its own.
 */
#include "unittest.h"

#include "mp_chat_input_stand_in.h"

#include "mp_armed.h"
#include "mp_chat.h"
#include "mp_chat_input.h"
#include "mp_chat_key_rule.h"

#include "common/host_image.h"
#include "common/session_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MSG_MOUSE_MOVE 0x0200u
#define TAKEN          1

/* Scan codes and virtual keys of the keys the fingers below press. */
#define SCAN_T      0x14u
#define SCAN_U      0x16u
#define SCAN_H      0x23u
#define SCAN_I      0x17u
#define SCAN_A      0x1Eu
#define SCAN_ENTER  0x1Cu
#define SCAN_ESCAPE 0x01u
#define SCAN_BACK   0x0Eu
#define SCAN_DOWN   0x50u
#define SCAN_SPACE  0x39u
#define VK_T_       0x54u
#define VK_U_       0x55u
#define VK_H_       0x48u
#define VK_I_       0x49u
#define VK_DOWN_    0x28u

/* Whether a line written since the report was last printed contains `text`. */
static bool log_says(const char *text)
{
    const size_t count = engine.log_count < STAND_IN_LINES ? engine.log_count : STAND_IN_LINES;
    size_t       i;

    for (i = 0; i < count; ++i) {
        if (strstr(engine.log[i], text) != NULL) {
            return true;
        }
    }
    return false;
}

/* Prints the report and answers its numbers; the lines kept before it are forgotten. */
static counts_t counts(void)
{
    memset(&engine.report, 0, sizeof engine.report);
    engine.log_count = 0;
    mp_chat_input_report();
    return engine.report;
}

static unsigned long count_of(count_t counter)
{
    const counts_t c = counts();

    return c.value[counter];
}

/* Whether `counter` grew by one from `before` to `after`, and no close counter but it moved. */
static bool only_closed_by(const counts_t *before, const counts_t *after, count_t counter)
{
    int reason;

    if (!before->read || !after->read) {
        return false;
    }
    for (reason = C_ENTER; reason <= C_SESSION; ++reason) {
        const unsigned long grew = reason == (int)counter ? 1ul : 0ul;

        if (after->value[reason] != before->value[reason] + grew) {
            return false;
        }
    }
    return true;
}

/* ==============================================================================================
 * The player's fingers.
 * ============================================================================================ */

static uint32_t press(uint32_t scan)
{
    return (scan << 16) | 1u;
}

static uint32_t repeat(uint32_t scan)
{
    return (scan << 16) | 0x40000000u | 1u;
}

static uint32_t release(uint32_t scan)
{
    return (scan << 16) | 0xC0000000u | 1u;
}

static int32_t send(uint32_t message, uint32_t wparam, uint32_t lparam)
{
    return engine.hook(0u, (int32_t)message, (int32_t)wparam, lparam);
}

/* A message the chat has to hand on, and does: it reached the handlers further in. */
static bool handed_on(uint32_t message, uint32_t wparam, uint32_t lparam)
{
    const uint32_t calls = engine.further_in_calls;

    (void)send(message, wparam, lparam);
    return engine.further_in_calls == calls + 1u && engine.further_in_message == message;
}

/* A message the chat takes, and nothing further in saw it. */
static bool taken(uint32_t message, uint32_t wparam, uint32_t lparam)
{
    const uint32_t calls = engine.further_in_calls;

    return send(message, wparam, lparam) == TAKEN && engine.further_in_calls == calls;
}

static bool line_is(const char *text)
{
    size_t      length  = 0u;
    bool        refused = false;
    const char *line    = mp_chat_input_line(engine.now_ms, &length, &refused);

    return line != NULL && length == strlen(text) && strcmp(line, text) == 0;
}

static bool refused_now(void)
{
    bool refused = false;

    return mp_chat_input_line(engine.now_ms, NULL, &refused) != NULL && refused;
}

static bool shut_and_empty(void)
{
    size_t length = 0u;

    return !mp_chat_input_is_typing() && mp_chat_input_line(engine.now_ms, &length, NULL) == NULL;
}

/* The hold, as the input readers and the other mods see it. */
static bool held(void)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    return mp_armed_input_held() && session_note_read(&note) && note.input_held;
}

static bool free_again(void)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    return !mp_armed_input_held() && session_note_read(&note) && !note.input_held;
}

/* T opens the chat and "hi" is typed; `what` names the case in the lines. */
static void open_and_type(const char *what)
{
    const uint32_t calls = engine.further_in_calls;

    ut_checkf(send(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) == TAKEN &&
                  engine.further_in_calls == calls + 1u && mp_chat_input_is_typing() && held() &&
                  line_is(""),
              "%s: a fresh T asks the handlers further in once, opens the chat and holds the input",
              what);
    ut_checkf(taken(MP_CHAT_MSG_CHAR, 't', press(SCAN_T)) && line_is(""),
              "%s: the character of T is dropped, neither typed nor handed on", what);
    ut_checkf(taken(MP_CHAT_MSG_KEY_DOWN, VK_H_, press(SCAN_H)) &&
                  taken(MP_CHAT_MSG_CHAR, 'h', press(SCAN_H)) &&
                  taken(MP_CHAT_MSG_KEY_DOWN, VK_I_, press(SCAN_I)) &&
                  taken(MP_CHAT_MSG_CHAR, 'i', press(SCAN_I)) && line_is("hi"),
              "%s: h and i are typed, and nothing of them is handed on", what);
}

/* The chat closed by a key keeps the input held until that key is up. */
static void let_go(uint32_t vk, uint32_t scan, const char *what)
{
    mp_chat_input_frame();
    ut_checkf(held(), "%s: a frame later the input is still held, the key being down", what);
    ut_checkf(handed_on(MP_CHAT_MSG_KEY_UP, vk, release(scan)), "%s: its release is handed on",
              what);
    mp_chat_input_frame();
    ut_checkf(free_again(), "%s: and the next frame lets the input go", what);
}

static void back_to_the_level(void)
{
    engine.outcome = STAND_IN_LEVEL_RUNNING;
    engine.menu    = 0u;
    engine.movie   = 0u;
    engine.gate    = 0u;
    engine.say     = MP_CHAT_SAY_SENT;
    if (!mp_armed_transport()) {
        mp_armed_set_transport(true, false);
    }
    engine.now_ms += 5000u;
    mp_chat_input_frame();
}

/* ==============================================================================================
 * The cases.
 * ============================================================================================ */

static void check_the_arming(void)
{
    counts_t c;

    ut_section("arming: the hook on the key handler's head, the cells, and the key");
    ut_check(engine.hook != NULL && engine.prologue == 6u,
             "the hook is handed to the detour with the declared prologue of six");
    ut_check(log_says("the chat is hooked on the key handler at 0043F603") &&
                 !log_says("did not resolve"),
             "the arming says where the hook sits, and no cell is missing");
    c = counts();
    ut_check(c.read && strcmp(c.key, "T") == 0,
             "the report reads, and the chat opens on T as ChatKey says");
    ut_check(mp_chat_input_in_play() && shut_and_empty() && free_again(),
             "a running level with no menu, movie or load is in play, the chat "
             "shut and nothing held");
}

typedef enum way {
    WAY_ENTER,
    WAY_ESCAPE,
    WAY_MENU,
    WAY_MOVIE,
    WAY_OUTCOME,
    WAY_LOAD,
    WAY_SESSION_EXIT,
    WAY_NO_TRANSPORT,
    WAY_COUNT
} way_t;

static const char *const WAY_NAME[WAY_COUNT] = {
    "enter", "escape", "a menu", "a movie", "the level outcome", "a load", "the session's exit",
    "the transport gone"
};

static const count_t WAY_COUNTER[WAY_COUNT] = {
    C_ENTER, C_ESCAPE, C_MENU, C_MOVIE, C_LEVEL, C_LEVEL, C_SESSION, C_SESSION
};

/* The movie is seen by the thread timer's look, since the engine draws no frame while one plays;
 * the other reasons no key brings are seen by the frame. */
static void take(way_t way)
{
    switch (way) {
    case WAY_ENTER:
        (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER));
        return;
    case WAY_ESCAPE:
        (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE));
        return;
    case WAY_MOVIE:
        engine.movie = 1u;
        mp_chat_input_look();
        return;
    case WAY_SESSION_EXIT:
        mp_chat_input_close(MP_CHAT_CLOSE_SESSION);
        return;
    case WAY_NO_TRANSPORT:
        mp_armed_set_transport(false, false);
        ut_check(handed_on(MSG_MOUSE_MOVE, 0u, 0u),
                 "the transport gone: the next message of any kind is handed on");
        return;
    default:
        break;
    }
    engine.menu    = way == WAY_MENU ? 1u : 0u;
    engine.outcome = way == WAY_OUTCOME ? 4u : STAND_IN_LEVEL_RUNNING;
    engine.gate    = way == WAY_LOAD ? 1u : 0u;
    mp_chat_input_frame();
}

/* Each way out, one after the other, with the same fields read after each. */
static void check_every_way_out(void)
{
    int way;

    ut_section("every way out of an open chat, and the same fields after each");
    for (way = 0; way < WAY_COUNT; ++way) {
        const char    *name   = WAY_NAME[way];
        const bool     by_key = way == WAY_ENTER || way == WAY_ESCAPE;
        const uint32_t says   = engine.says;
        const counts_t before = counts();
        counts_t       after;

        open_and_type(name);
        take((way_t)way);
        after = counts();
        ut_checkf(shut_and_empty(), "%s: the chat is shut and its line gone", name);
        ut_checkf(only_closed_by(&before, &after, WAY_COUNTER[way]),
                  "%s: one close counted, under its own reason and no other", name);
        ut_checkf(way == WAY_ENTER ? engine.says == says + 1u && strcmp(engine.said, "hi") == 0
                                   : engine.says == says,
                  "%s: the line is said by Enter and by nothing else", name);
        if (!by_key) {
            ut_checkf(free_again(), "%s: the input is let go at once", name);
            back_to_the_level();
            continue;
        }
        ut_checkf(held(), "%s: the input stays held while the closing key is down", name);
        if (way == WAY_ENTER) {
            let_go(MP_CHAT_VK_RETURN, SCAN_ENTER, name);
        } else {
            let_go(MP_CHAT_VK_ESCAPE, SCAN_ESCAPE, name);
        }
        ut_checkf(count_of(C_HELD_AFTER_CLOSE) == before.value[C_HELD_AFTER_CLOSE] + 1u,
                  "%s: counted once as held after the close", name);
        back_to_the_level();
    }
}

static void check_the_opening_press(void)
{
    counts_t before = counts();
    counts_t after;

    ut_section("the opening press: the handlers further in, the input split, the transport");

    engine.further_in_answer = 1;
    ut_check(send(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) == 1 && shut_and_empty() &&
                 free_again(),
             "a handler further in that takes T keeps it: its answer goes "
             "back, the chat stays shut");
    ut_check(handed_on(MP_CHAT_MSG_CHAR, 't', press(SCAN_T)),
             "and the character of T is handed on too, nothing being dropped for a shut chat");
    engine.further_in_answer = 0;
    after = counts();
    ut_check(after.value[C_LEFT_TO_CHAIN] == before.value[C_LEFT_TO_CHAIN] + 1u &&
                 after.value[C_OPENS] == before.value[C_OPENS],
             "counted as left to a mod further in, and not as an open");

    before             = after;
    engine.input_split = false;
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) && shut_and_empty() &&
                 free_again(),
             "without the input split T stays the game's, and the chat does not open");
    engine.input_split = true;
    after              = counts();
    ut_check(after.value[C_NO_HOLD] == before.value[C_NO_HOLD] + 1u &&
                 after.value[C_OPENS] == before.value[C_OPENS],
             "counted as refused: no input hold");

    before = after;
    mp_armed_set_transport(false, false);
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) && shut_and_empty() &&
                 free_again(),
             "with no transport T is handed on and nothing opens");
    mp_armed_set_transport(true, false);
    engine.menu = 1u;
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) && shut_and_empty(),
             "with a menu of the engine's up T is handed on and nothing opens");
    engine.menu = 0u;
    after       = counts();
    ut_check(after.value[C_OPENS] == before.value[C_OPENS] &&
                 after.value[C_NO_HOLD] == before.value[C_NO_HOLD] &&
                 after.value[C_LEFT_TO_CHAIN] == before.value[C_LEFT_TO_CHAIN],
             "and neither is counted as anything");
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_T_, repeat(SCAN_T)) && shut_and_empty(),
             "a repeat of T is handed on: holding the key does not open the chat");
}

/* Enter still down under another key: a dialogue's choice would read its release. */
static void check_the_closing_key(void)
{
    ut_section("the hold after a close lasts until the closing key's own "
               "release, a second at most");

    open_and_type("Enter, then an arrow");
    (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER));
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_DOWN_, press(SCAN_DOWN)),
             "an arrow pressed after Enter is handed on to the game");
    mp_chat_input_frame();
    ut_check(held(), "and a frame later the input is still held, Enter being down");
    ut_check(handed_on(MP_CHAT_MSG_KEY_UP, VK_DOWN_, release(SCAN_DOWN)), "the arrow let go");
    mp_chat_input_frame();
    ut_check(held(), "still held: the arrow's release says nothing about Enter");
    let_go(MP_CHAT_VK_RETURN, SCAN_ENTER, "then Enter let go");
    back_to_the_level();

    open_and_type("a lost release");
    (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER));
    mp_chat_input_frame();
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER)),
             "Enter pressed afresh, its release having gone to another window, is handed on");
    mp_chat_input_frame();
    ut_check(free_again(), "and the fresh press of the closing key lets the input go");
    back_to_the_level();

    open_and_type("Escape held");
    (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE));
    ut_check(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, repeat(SCAN_ESCAPE)),
             "the repeats of Escape are swallowed, so the pause menu does not open under them");
    engine.now_ms += 999u;
    mp_chat_input_frame();
    ut_check(held(), "999 ms later the input is still held");
    engine.now_ms += 1u;
    mp_chat_input_frame();
    ut_check(free_again(), "a second after the close it is let go, the key or not");
    ut_check(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, repeat(SCAN_ESCAPE)),
             "and the repeats are still swallowed until Escape is up");
    ut_check(handed_on(MP_CHAT_MSG_KEY_UP, MP_CHAT_VK_ESCAPE, release(SCAN_ESCAPE)) &&
                 handed_on(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE)),
             "after its release a fresh Escape goes to the pause menu again");
    back_to_the_level();
}

static void check_a_line_not_sent(void)
{
    static const mp_chat_say_result_t unsent[] = { MP_CHAT_SAY_TOO_FAST, MP_CHAT_SAY_UNSENT,
                                                   MP_CHAT_SAY_NO_SESSION };
    size_t   i;
    counts_t before;

    ut_section("a line the chat could not send stays, and an empty one only closes");
    for (i = 0; i < sizeof unsent / sizeof unsent[0]; ++i) {
        open_and_type("not sent");
        engine.say = unsent[i];
        ut_checkf(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER)) &&
                      mp_chat_input_is_typing() && line_is("hi") && refused_now() && held(),
                  "answer %u: the chat stays open with the line as typed, the prefix refused",
                  (unsigned)unsent[i]);
        engine.now_ms += 999u;
        ut_check(refused_now(), "for a second");
        engine.now_ms += 1u;
        ut_check(!refused_now() && line_is("hi"), "and then no longer, the line still there");
        engine.say = MP_CHAT_SAY_SENT;
        ut_check(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER)) &&
                     shut_and_empty() && strcmp(engine.said, "hi") == 0,
                 "the next Enter sends it and closes");
        let_go(MP_CHAT_VK_RETURN, SCAN_ENTER, "sent at the second try");
        back_to_the_level();
    }

    for (i = 0; i < 2u; ++i) {
        const uint32_t says = engine.says;
        counts_t       after;

        before = counts();
        (void)send(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T));
        if (i == 1u) {
            (void)send(MP_CHAT_MSG_CHAR, ' ', press(SCAN_SPACE));
            (void)send(MP_CHAT_MSG_CHAR, ' ', press(SCAN_SPACE));
        }
        ut_checkf(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_RETURN, press(SCAN_ENTER)) &&
                      shut_and_empty() && engine.says == says,
                  "Enter on %s closes the chat and says nothing",
                  i == 0u ? "an empty line" : "a line of spaces");
        after = counts();
        ut_check(only_closed_by(&before, &after, C_ENTER), "counted as closed by enter");
        let_go(MP_CHAT_VK_RETURN, SCAN_ENTER, "nothing said");
        back_to_the_level();
    }
}

static void check_what_is_typed(void)
{
    counts_t before;
    counts_t after;
    int      i;

    ut_section("the typing counters: no room, a control character, a transliteration");

    open_and_type("a full line");
    before = counts();
    for (i = 2; i < (int)MP_CHAT_TEXT_MAX; ++i) {
        (void)send(MP_CHAT_MSG_CHAR, 'a', press(SCAN_A));
    }
    ut_check(taken(MP_CHAT_MSG_CHAR, 'a', press(SCAN_A)), "a character past the end is taken");
    ut_check(taken(MP_CHAT_MSG_CHAR, 0x01u, press(SCAN_A)), "and so is Control+A's character");
    after = counts();
    ut_checkf(after.value[C_TYPED] == before.value[C_TYPED] + MP_CHAT_TEXT_MAX - 2u &&
                  after.value[C_NO_ROOM] == before.value[C_NO_ROOM] + 1u,
              "%u typed to the full line, the one past it refused for no room, the control "
              "character not counted",
              (unsigned)(MP_CHAT_TEXT_MAX - 2u));
    ut_check(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_BACK, press(SCAN_BACK)) &&
                 taken(MP_CHAT_MSG_CHAR, 0x08u, press(SCAN_BACK)),
             "Backspace erases, and its character is swallowed");
    before = counts();
    ut_check(taken(MP_CHAT_MSG_CHAR, 0xE9u, press(SCAN_A)), "an e with an acute accent is taken");
    after = counts();
    ut_check(after.value[C_TYPED] == before.value[C_TYPED] + 1u &&
                 after.value[C_TRANSLITERATED] == before.value[C_TRANSLITERATED] + 1u,
             "typed as what the wire carries, and counted as a transliteration");
    (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE));
    let_go(MP_CHAT_VK_ESCAPE, SCAN_ESCAPE, "the full line cancelled");
    back_to_the_level();
}

/* The key is read again once a second, from the frame. */
static void check_the_key_read_again(void)
{
    counts_t before = counts();
    counts_t after;

    ut_section("the chat key read again in a running session");

    engine.chat_key = "U";
    engine.now_ms += 1000u;
    mp_chat_input_frame();
    after = counts();
    ut_check(strcmp(after.key, "U") == 0 &&
                 after.value[C_CHANGES] == before.value[C_CHANGES] + 1u,
             "ChatKey set to U: a second later the chat opens on U, one change counted");
    ut_check(handed_on(MP_CHAT_MSG_KEY_DOWN, VK_T_, press(SCAN_T)) && shut_and_empty(),
             "T is the game's again");
    ut_check(send(MP_CHAT_MSG_KEY_DOWN, VK_U_, press(SCAN_U)) == TAKEN &&
                 mp_chat_input_is_typing() && taken(MP_CHAT_MSG_CHAR, 'u', press(SCAN_U)) &&
                 line_is(""),
             "U opens it, and its own character is dropped");
    (void)taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE));
    let_go(MP_CHAT_VK_ESCAPE, SCAN_ESCAPE, "opened on U");

    before          = counts();
    engine.chat_key = "M";
    engine.now_ms += 1000u;
    mp_chat_input_frame();
    after = counts();
    ut_check(strcmp(after.key, "T") == 0 &&
                 after.value[C_REFUSED_NAMES] == before.value[C_REFUSED_NAMES] + 1u,
             "M is refused, counted once, and the chat falls back to T");
    engine.now_ms += 1000u;
    mp_chat_input_frame();
    ut_check(count_of(C_REFUSED_NAMES) == after.value[C_REFUSED_NAMES],
             "the same refused name read again a second later is not counted again");
    engine.chat_key = NULL;
    back_to_the_level();
}

/* The operand of the movie cell refused at the arming: the chat still works, and says so. */
static int run_without_the_movie_cell(void)
{
    ut_section("the movie cell missing while the hook stands");

    engine.refuse_operand = true;
    ut_check(mp_chat_input_arm() && engine.hook != NULL, "the hook is placed");
    ut_check(log_says("the movie cell did not resolve"),
             "the missing movie cell has a warning of its own");
    ut_check(!log_says("draws nothing"), "and the chat is not said to stay shut");
    ut_check(mp_chat_input_in_play(),
             "a running level is in play: a missing movie cell reads as no movie");
    open_and_type("no movie cell");
    ut_check(taken(MP_CHAT_MSG_KEY_DOWN, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE)) &&
                 shut_and_empty(),
             "and Escape closes it");
    let_go(MP_CHAT_VK_ESCAPE, SCAN_ESCAPE, "no movie cell");
    return ut_summary("mp_chat_input without the movie cell");
}

int main(int argc, char **argv)
{
    memset(&engine, 0, sizeof engine);
    engine.outcome     = STAND_IN_LEVEL_RUNNING;
    engine.input_split = true;
    engine.now_ms      = 100000u;
    engine.say         = MP_CHAT_SAY_SENT;
    if (!host_image_resolve()) {
        ut_check(false, "the test's own image resolves, so the cells count as inside it");
        return ut_summary("mp_chat_input");
    }
    mp_armed_set_transport(true, false);
    if (argc > 1 && strcmp(argv[1], "no-movie-cell") == 0) {
        return run_without_the_movie_cell();
    }
    ut_check(mp_chat_input_arm(), "the chat arms and hooks the key handler");
    check_the_arming();
    check_every_way_out();
    check_the_opening_press();
    check_the_closing_key();
    check_a_line_not_sent();
    check_what_is_typed();
    check_the_key_read_again();
    return ut_summary("mp_chat_input");
}
