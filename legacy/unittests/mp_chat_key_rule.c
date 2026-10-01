/* mp_chat_key_rule.c: the chat's key table, row by row, and the choice of its key against the
 * developer menu's row.
 *
 * The messages are built the way the platform builds them: a key down carries the scan code in
 * bits 16 to 23 and sets bit 30 on a repeat, and the character a key types follows its key down
 * with the same scan code. The sequences below are what a player's fingers make: open with T and
 * type, open with F5, hold a key, close with Escape and keep it held.
 */
#include "unittest.h"

#include "mp_board.h"
#include "mp_chat_key_rule.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WM_KEYDOWN_    0x0100u
#define WM_KEYUP_      0x0101u
#define WM_CHAR_       0x0102u
#define WM_DEADCHAR_   0x0103u
#define WM_SYSKEYDOWN_ 0x0104u
#define WM_SYSCHAR_    0x0106u
#define WM_MOUSEMOVE_  0x0200u

/* Scan codes of a US and German board, which agree on these keys. */
#define SCAN_T      0x14u
#define SCAN_E      0x12u
#define SCAN_A      0x1Eu
#define SCAN_F5     0x3Fu
#define SCAN_ENTER  0x1Cu
#define SCAN_ESCAPE 0x01u
#define SCAN_BACK   0x0Eu
#define SCAN_F10    0x44u
#define SCAN_DOWN   0x50u

#define VK_T    0x54u
#define VK_E    0x45u
#define VK_A    0x41u
#define VK_F5   0x74u
#define VK_DOWN 0x28u

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

static mp_chat_key_state_t opened_with(uint32_t scan)
{
    mp_chat_key_state_t state;

    memset(&state, 0, sizeof state);
    (void)mp_chat_key_chain_answered(&state, press(scan), 0);
    return state;
}

static void check_the_shut_rows(void)
{
    mp_chat_key_state_t state;

    ut_section("a shut chat hands everything on but a fresh press of its own key");

    memset(&state, 0, sizeof state);
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_ASK_CHAIN,
             "a fresh T asks the mods further in first");
    ut_check(!state.open, "and asking opens nothing yet");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, repeat(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "a repeat of T is handed on: holding the key does not open the chat again");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, press(SCAN_T), VK_T, true) ==
                 MP_CHAT_KEY_PASS,
             "T with Control held is handed on, AltGr included");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_E, press(SCAN_E), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "another key is handed on");
    ut_check(mp_chat_key_decide(&state, WM_SYSKEYDOWN_, VK_T, press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "T as a system key, with Alt, is handed on");
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "the character t is handed on");
    ut_check(mp_chat_key_decide(&state, WM_KEYUP_, VK_T, release(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "the release of T is handed on");
    ut_check(mp_chat_key_decide(&state, WM_MOUSEMOVE_, 0u, 0u, VK_T, false) == MP_CHAT_KEY_PASS,
             "the pointer is handed on");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, press(SCAN_T), 0, false) ==
                 MP_CHAT_KEY_PASS,
             "with no chat key nothing opens");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_PASS,
             "Escape is handed on, to the pause menu, while the chat is shut");
    ut_check(mp_chat_key_decide(NULL, WM_KEYDOWN_, VK_T, press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "and a rule with no state hands everything on");
}

static void check_the_chain(void)
{
    mp_chat_key_state_t state;

    ut_section("the mods further in are asked about the opening press, and their answer counts");

    memset(&state, 0, sizeof state);
    ut_check(mp_chat_key_chain_answered(&state, press(SCAN_T), 1) == MP_CHAT_KEY_LEFT &&
                 !state.open,
             "a mod further in that took the press keeps it, and the chat stays shut");
    ut_check(mp_chat_key_chain_answered(&state, press(SCAN_T), -1) == MP_CHAT_KEY_LEFT &&
                 !state.open,
             "any answer but nought is taking it");
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "and the press's character goes on to that mod as well");
    ut_check(mp_chat_key_chain_answered(&state, press(SCAN_T), 0) == MP_CHAT_KEY_OPEN &&
                 state.open,
             "nought from the chain opens the chat");
    ut_check(state.drop_pending && state.drop_scan == SCAN_T,
             "with the opening key's own character to be dropped");
}

static void check_the_open_rows(void)
{
    static const uint32_t swallowed_keys[] = { VK_T, VK_E, 0x25u, 0x26u, 0x09u, 0x4Du, VK_F5,
                                               0x75u, 0x20u, 0x10u, 0x11u, 0xDCu, 0xC0u };
    static const uint32_t control_chars[] = { 0x00u, 0x08u, 0x09u, 0x0Au, 0x0Du, 0x1Bu, 0x1Fu,
                                              0x7Fu };
    mp_chat_key_state_t   state = opened_with(SCAN_T);
    size_t                i;
    uint32_t              c;
    uint32_t              typed = 0u;

    ut_section("an open chat takes every key and every character, and hands on what is not text");

    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_RETURN, press(SCAN_ENTER), VK_T,
                                false) == MP_CHAT_KEY_SEND,
             "Enter sends");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_RETURN, repeat(SCAN_ENTER), VK_T,
                                false) == MP_CHAT_KEY_SWALLOW,
             "a repeat of Enter is swallowed, so a held Enter says a refused line once");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_CANCEL,
             "Escape cancels, and never reaches the pause menu");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_ESCAPE, repeat(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_SWALLOW,
             "a repeat of Escape is swallowed");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_BACK, press(SCAN_BACK), VK_T,
                                false) == MP_CHAT_KEY_ERASE,
             "Backspace erases, and never opens the cheat console");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_BACK, repeat(SCAN_BACK), VK_T,
                                false) == MP_CHAT_KEY_ERASE,
             "and a held Backspace goes on erasing");
    for (i = 0; i < sizeof swallowed_keys / sizeof swallowed_keys[0]; ++i) {
        ut_checkf(mp_chat_key_decide(&state, WM_KEYDOWN_, swallowed_keys[i], press(0x30u), VK_T,
                                     false) == MP_CHAT_KEY_SWALLOW,
                  "the key %02X is swallowed while the chat is open",
                  (unsigned)swallowed_keys[i]);
    }
    for (c = 0x20u; c <= 0xFFu; ++c) {
        if (mp_chat_key_decide(&state, WM_CHAR_, c, press(SCAN_A), VK_T, false) ==
            MP_CHAT_KEY_TYPE) {
            ++typed;
        } else if (c != 0x7Fu) {
            ut_checkf(false, "the character %02X is typed", (unsigned)c);
        }
    }
    ut_checkf(typed == 223u, "every character from 20 to FF but DEL is typed (%u)",
              (unsigned)typed);
    for (i = 0; i < sizeof control_chars / sizeof control_chars[0]; ++i) {
        ut_checkf(mp_chat_key_decide(&state, WM_CHAR_, control_chars[i], press(SCAN_A), VK_T,
                                     false) == MP_CHAT_KEY_SWALLOW,
                  "the control character %02X is swallowed, not typed",
                  (unsigned)control_chars[i]);
    }
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 0x100u, press(SCAN_A), VK_T, false) ==
                 MP_CHAT_KEY_SWALLOW,
             "a character past one byte, which an ANSI window never sends, is swallowed");
    ut_check(mp_chat_key_decide(&state, WM_DEADCHAR_, '^', press(0x29u), VK_T, false) ==
                 MP_CHAT_KEY_SWALLOW,
             "a dead key is swallowed; the letter it makes arrives as a character after it");
    ut_check(mp_chat_key_decide(&state, WM_SYSKEYDOWN_, 0x73u, press(0x3Eu), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "Alt+F4 is handed on");
    ut_check(mp_chat_key_decide(&state, WM_SYSKEYDOWN_, 0x79u, press(SCAN_F10), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "F10, a system key, is handed on");
    ut_check(mp_chat_key_decide(&state, WM_SYSCHAR_, 'x', press(0x2Du), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "a system character is handed on");
    ut_check(mp_chat_key_decide(&state, WM_KEYUP_, VK_E, release(SCAN_E), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "a release is handed on");
    ut_check(mp_chat_key_decide(&state, WM_MOUSEMOVE_, 0u, 0u, VK_T, false) == MP_CHAT_KEY_PASS,
             "the pointer is handed on");
    ut_check(state.open, "and deciding closed nothing");
}

static void check_the_opening_character(void)
{
    mp_chat_key_state_t state;

    ut_section("the character of the key that opened the chat is dropped, and only that one");

    state = opened_with(SCAN_T);
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_DROP,
             "the t behind the opening T is dropped");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, press(SCAN_T), VK_T, false) ==
                     MP_CHAT_KEY_SWALLOW &&
                 mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                     MP_CHAT_KEY_TYPE,
             "and a t typed after it is typed");

    state = opened_with(SCAN_T);
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 'x', press(0x2Du), VK_T, false) ==
                 MP_CHAT_KEY_TYPE,
             "a character with another scan code is typed while the drop still waits");
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_DROP,
             "and the one with the opening key's scan code is still dropped after it");

    state = opened_with(SCAN_T);
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_E, press(SCAN_E), VK_T, false) ==
                     MP_CHAT_KEY_SWALLOW &&
                 !state.drop_pending,
             "the next key down forgets the drop");
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 'e', press(SCAN_E), VK_T, false) ==
                 MP_CHAT_KEY_TYPE,
             "so its character is typed");

    state = opened_with(SCAN_F5);
    mp_chat_key_frame_end(&state);
    ut_check(!state.drop_pending, "F5 types nothing, and the end of the frame forgets its drop");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_A, press(SCAN_A), VK_F5, false) ==
                     MP_CHAT_KEY_SWALLOW &&
                 mp_chat_key_decide(&state, WM_CHAR_, 'a', press(SCAN_A), VK_F5, false) ==
                     MP_CHAT_KEY_TYPE,
             "so the first letter after F5 appears");

    state = opened_with(SCAN_F5);
    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 'a', press(SCAN_A), VK_F5, false) ==
                 MP_CHAT_KEY_TYPE,
             "and appears even inside the same frame, since its scan code is not F5's");
}

static void check_holding_keys(void)
{
    mp_chat_key_state_t state = opened_with(SCAN_T);
    int                 i;

    ut_section("a held key: the chat key types once the chat is open, and the closing key stays");

    ut_check(mp_chat_key_decide(&state, WM_CHAR_, 't', press(SCAN_T), VK_T, false) ==
                 MP_CHAT_KEY_DROP,
             "T held: its first character is the dropped one");
    for (i = 0; i < 3; ++i) {
        ut_checkf(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, repeat(SCAN_T), VK_T, false) ==
                          MP_CHAT_KEY_SWALLOW &&
                      mp_chat_key_decide(&state, WM_CHAR_, 't', repeat(SCAN_T), VK_T, false) ==
                          MP_CHAT_KEY_TYPE,
                  "repeat %d of T is typed as a t, as in any field", i + 1);
    }

    mp_chat_key_closed(&state, MP_CHAT_VK_ESCAPE);
    ut_check(!state.open && state.closing_key == MP_CHAT_VK_ESCAPE,
             "Escape closed it and is still down");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_ESCAPE, repeat(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_SWALLOW,
             "its repeats are swallowed, so holding Escape does not open the pause menu");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_E, repeat(SCAN_E), VK_T, false) ==
                 MP_CHAT_KEY_PASS,
             "a repeat of another key is handed on");
    ut_check(mp_chat_key_decide(&state, WM_KEYUP_, MP_CHAT_VK_ESCAPE, release(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_PASS &&
                 state.closing_key == 0,
             "its release is handed on and ends it");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_ESCAPE, press(SCAN_ESCAPE), VK_T,
                                false) == MP_CHAT_KEY_PASS,
             "and a fresh Escape after it goes to the pause menu as usual");

    mp_chat_key_closed(&state, MP_CHAT_VK_RETURN);
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_RETURN, repeat(SCAN_ENTER), VK_T,
                                false) == MP_CHAT_KEY_SWALLOW,
             "Enter closed it: its repeats are swallowed");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_DOWN, press(SCAN_DOWN), VK_T, false) ==
                     MP_CHAT_KEY_PASS &&
                 state.closing_key == MP_CHAT_VK_RETURN,
             "a fresh arrow is handed on and leaves Enter down: in a dialogue's choice Enter can "
             "still be held under it");
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_E, press(SCAN_E), VK_T, false) ==
                     MP_CHAT_KEY_PASS &&
                 mp_chat_key_decide(&state, WM_KEYUP_, VK_E, release(SCAN_E), VK_T, false) ==
                     MP_CHAT_KEY_PASS &&
                 state.closing_key == MP_CHAT_VK_RETURN,
             "so does a letter, pressed and let go: another key says nothing about Enter");
    ut_check(mp_chat_key_decide(&state, WM_KEYUP_, MP_CHAT_VK_RETURN, release(SCAN_ENTER), VK_T,
                                false) == MP_CHAT_KEY_PASS &&
                 state.closing_key == 0,
             "Enter's own release is handed on and ends it");
    mp_chat_key_closed(&state, MP_CHAT_VK_RETURN);
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, MP_CHAT_VK_RETURN, press(SCAN_ENTER), VK_T,
                                false) == MP_CHAT_KEY_PASS &&
                 state.closing_key == 0,
             "a fresh Enter of its own ends it too, when its release went to another window");

    mp_chat_key_closed(&state, 0);
    ut_check(state.closing_key == 0 && !state.open && !state.drop_pending,
             "a close by anything but a key leaves nothing held");
    state = opened_with(SCAN_T);
    mp_chat_key_closed(&state, MP_CHAT_VK_RETURN);
    ut_check(mp_chat_key_decide(&state, WM_KEYDOWN_, VK_T, press(SCAN_T), VK_T, false) ==
                     MP_CHAT_KEY_ASK_CHAIN &&
                 state.closing_key == MP_CHAT_VK_RETURN,
             "the chat key asks to open it again at once, Enter still down");
    ut_check(mp_chat_key_chain_answered(&state, press(SCAN_T), 0) == MP_CHAT_KEY_OPEN &&
                 state.closing_key == 0,
             "and the chat that opens forgets the key that closed it");
}

/* The developer menu's row writes these names and shows the key the multiplayer answers for them;
 * the pick has to agree with that row on every one of them. */
static void check_the_key_names(void)
{
    static const char *const reserved[] = { "M", "m", "TAB", "tab", "F4", "F6", "F7", "F8", "F10",
                                            "F11", "f12" };
    static const char *const unknown[] = { "", "84", "Ctrl", "F13", "F0", "TT", "Enter", "Space",
                                           "^", "`", "F1x", "F123" };
    mp_chat_key_verdict_t    verdict;
    char                     name[4];
    int                      accepted = 0;
    int                      c;
    size_t                   i;

    ut_section("the chat key: the scoreboard's grammar, nine keys refused and the board's own");

    ut_check(mp_chat_key_pick("T", "TAB", &verdict) == 0x54 &&
                 verdict == MP_CHAT_KEY_NAME_ACCEPTED,
             "T is T");
    ut_check(mp_chat_key_pick("U", "TAB", &verdict) == 0x55, "U is U, not 85");
    ut_check(mp_chat_key_pick("5", "TAB", &verdict) == 0x35, "5 is the 5 key, not key code 5");
    ut_check(mp_chat_key_pick("f9", "TAB", &verdict) == 0x78, "f9 is F9");
    ut_check(mp_chat_key_pick("F01", "TAB", &verdict) == 0x70,
             "F01 is F1, the scoreboard's own oddity, which the menu row copies");
    for (i = 0; i < sizeof reserved / sizeof reserved[0]; ++i) {
        ut_checkf(mp_chat_key_pick(reserved[i], "TAB", &verdict) == MP_CHAT_KEY_DEFAULT &&
                      verdict == MP_CHAT_KEY_NAME_RESERVED,
                  "%s is refused and the chat falls back to T", reserved[i]);
    }
    for (i = 0; i < sizeof unknown / sizeof unknown[0]; ++i) {
        ut_checkf(mp_chat_key_pick(unknown[i], "TAB", &verdict) == MP_CHAT_KEY_DEFAULT &&
                      verdict == MP_CHAT_KEY_NAME_UNKNOWN,
                  "'%s' is no key and the chat falls back to T", unknown[i]);
    }
    ut_check(mp_chat_key_pick(NULL, "TAB", &verdict) == MP_CHAT_KEY_DEFAULT &&
                 verdict == MP_CHAT_KEY_NAME_UNKNOWN,
             "no name at all is T");

    ut_check(mp_chat_key_pick("Q", "q", &verdict) == MP_CHAT_KEY_DEFAULT &&
                 verdict == MP_CHAT_KEY_NAME_SCOREBOARD,
             "with ScoreboardKey=q the chat may not be Q");
    ut_check(mp_chat_key_pick("q", "Q", &verdict) == MP_CHAT_KEY_DEFAULT,
             "case is ignored on both sides");
    ut_check(mp_chat_key_pick("Q", "xyz", &verdict) == 0x51,
             "an unreadable ScoreboardKey is TAB, so Q is free again");
    ut_check(mp_chat_key_pick("Q", NULL, &verdict) == 0x51, "and so is a missing one");
    ut_check(mp_chat_key_pick("T", "T", &verdict) == MP_CHAT_KEY_SECOND &&
                 verdict == MP_CHAT_KEY_NAME_SCOREBOARD,
             "T while the scoreboard is T is refused, and the chat falls back to U, not to T");
    ut_check(mp_chat_key_pick("M", "t", &verdict) == 0x55 && verdict == MP_CHAT_KEY_NAME_RESERVED,
             "any refused name falls back to U while the scoreboard is T");
    ut_check(mp_chat_key_pick("84", "T", &verdict) == 0x55, "and so does a name that is no key");
    ut_check(mp_chat_key_pick("U", "T", &verdict) == 0x55 && verdict == MP_CHAT_KEY_NAME_ACCEPTED,
             "U itself is taken beside a scoreboard on T");
    ut_check(mp_chat_key_pick("U", "U", &verdict) == 0x54,
             "and a chat key refused for a scoreboard on U falls back to T");
    ut_check(mp_chat_key_pick("F5", "F5", &verdict) == MP_CHAT_KEY_DEFAULT,
             "F5 while the scoreboard is F5 is refused and falls back to T");
    ut_check(mp_chat_key_fallback(0x54) == 0x55 && mp_chat_key_fallback(0x09) == 0x54 &&
                 mp_chat_key_fallback(0x55) == 0x54 && mp_chat_key_fallback(0) == 0x54,
             "the fallback is T, and U only beside a scoreboard on T");

    /* Every name the menu row can write: 26 letters, 10 digits, F1 to F12. */
    for (c = 0; c < 48; ++c) {
        int32_t expected;

        if (c < 26) {
            text_format(name, sizeof name, "%c", 'A' + c);
        } else if (c < 36) {
            text_format(name, sizeof name, "%c", '0' + (c - 26));
        } else {
            text_format(name, sizeof name, "F%d", c - 35);
        }
        expected = mp_board_key_code(name);
        if (mp_chat_key_pick(name, "TAB", &verdict) == expected) {
            ++accepted;
        } else if (verdict != MP_CHAT_KEY_NAME_RESERVED) {
            ut_checkf(false, "%s was refused for a reason that is not the reserved list", name);
        }
    }
    ut_checkf(accepted == 40, "40 of the 48 names are chat keys (%d): all but M and seven F keys",
              accepted);
    ut_check(strcmp(mp_chat_key_verdict_text(MP_CHAT_KEY_NAME_SCOREBOARD), "") != 0 &&
                 strcmp(mp_chat_key_verdict_text(MP_CHAT_KEY_NAME_RESERVED), "") != 0,
             "every refusal has words for the log");
}

int main(void)
{
    check_the_shut_rows();
    check_the_chain();
    check_the_open_rows();
    check_the_opening_character();
    check_holding_keys();
    check_the_key_names();
    return ut_summary("mp_chat_key_rule");
}
