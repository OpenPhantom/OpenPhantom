/* mp_chat_key_rule.c: the chat's key table and the choice of its key. See the header. */
#include "mp_chat_key_rule.h"

#include "mp_board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* lParam of a key message: bit 30 is set when the key was already down, which is how the platform
 * marks the messages its auto repeat makes, and bits 16 to 23 are the scan code of the key. */
#define KEY_WAS_DOWN   0x40000000u
#define SCAN_SHIFT     16u
#define SCAN_MASK      0xFFu

/* The first character that is text; everything below is a control character. */
#define FIRST_PRINTABLE 0x20u
#define DELETE_CHAR     0x7Fu
#define LAST_BYTE       0xFFu

/* The virtual keys a chat key may not be, beside the names the scoreboard's grammar does not read.
 * M opens the multiplayer menu; F4 is half of Alt+F4 and the free camera's way out; F6 opens the
 * developer menu by default; the engine's graphics handler sees every key before the handler this
 * chat sits on and keeps F7 and F8 for the gamma and F11 and F12 for the resolution; F10 arrives
 * as a system key, which the chat does not open on; TAB is the scoreboard's own default. */
static const int32_t RESERVED[] = {
    0x09,   /* TAB */
    0x4D,   /* M */
    0x73,   /* F4 */
    0x75,   /* F6 */
    0x76,   /* F7 */
    0x77,   /* F8 */
    0x79,   /* F10 */
    0x7A,   /* F11 */
    0x7B    /* F12 */
};

static bool is_repeat(uint32_t lparam)
{
    return (lparam & KEY_WAS_DOWN) != 0u;
}

static uint8_t scan_of(uint32_t lparam)
{
    return (uint8_t)((lparam >> SCAN_SHIFT) & SCAN_MASK);
}

/* A shut chat hands everything on, but for its own key and for the repeats of the key that closed
 * it. Those repeats are what a held Escape makes after the chat has gone, and handed on they would
 * reach the engine's pause as though the player had pressed it again.
 *
 * The closing key counts as down until its own release, or its own next fresh press when that
 * release went to another window. Another key pressed meanwhile says nothing about it: Enter can
 * still be down under an arrow in a dialogue's choice, and the input stays held on this same state
 * so that the dialogue does not read Enter's release as a choice. */
static mp_chat_key_action_t while_shut(mp_chat_key_state_t *state, uint32_t message,
                                       uint32_t wparam, uint32_t lparam, int32_t chat_key,
                                       bool control_held)
{
    if (message == MP_CHAT_MSG_KEY_UP && state->closing_key != 0 &&
        (int32_t)wparam == state->closing_key) {
        state->closing_key = 0;
        return MP_CHAT_KEY_PASS;
    }
    if (message != MP_CHAT_MSG_KEY_DOWN) {
        return MP_CHAT_KEY_PASS;
    }
    if (is_repeat(lparam)) {
        return state->closing_key != 0 && (int32_t)wparam == state->closing_key
                   ? MP_CHAT_KEY_SWALLOW
                   : MP_CHAT_KEY_PASS;
    }
    if ((int32_t)wparam == state->closing_key) {
        state->closing_key = 0;
    }
    if (chat_key == 0 || (int32_t)wparam != chat_key || control_held) {
        return MP_CHAT_KEY_PASS;
    }
    return MP_CHAT_KEY_ASK_CHAIN;
}

/* An open chat takes every key and every character, and hands on only what is not text: the
 * system keys, the releases and the pointer. */
static mp_chat_key_action_t while_open(mp_chat_key_state_t *state, uint32_t message,
                                       uint32_t wparam, uint32_t lparam)
{
    switch (message) {
    case MP_CHAT_MSG_KEY_DOWN:
        state->drop_pending = false;
        if (wparam == (uint32_t)MP_CHAT_VK_BACK) {
            return MP_CHAT_KEY_ERASE;   /* a held Backspace goes on erasing, as in any field */
        }
        if (is_repeat(lparam)) {
            return MP_CHAT_KEY_SWALLOW;
        }
        if (wparam == (uint32_t)MP_CHAT_VK_RETURN) {
            return MP_CHAT_KEY_SEND;
        }
        return wparam == (uint32_t)MP_CHAT_VK_ESCAPE ? MP_CHAT_KEY_CANCEL : MP_CHAT_KEY_SWALLOW;
    case MP_CHAT_MSG_CHAR:
        if (state->drop_pending && scan_of(lparam) == state->drop_scan) {
            state->drop_pending = false;
            return MP_CHAT_KEY_DROP;
        }
        if (wparam < FIRST_PRINTABLE || wparam == DELETE_CHAR || wparam > LAST_BYTE) {
            return MP_CHAT_KEY_SWALLOW;
        }
        return MP_CHAT_KEY_TYPE;
    case MP_CHAT_MSG_DEAD_CHAR:
        return MP_CHAT_KEY_SWALLOW;
    default:
        return MP_CHAT_KEY_PASS;
    }
}

mp_chat_key_action_t mp_chat_key_decide(mp_chat_key_state_t *state, uint32_t message,
                                        uint32_t wparam, uint32_t lparam, int32_t chat_key,
                                        bool control_held)
{
    if (state == NULL) {
        return MP_CHAT_KEY_PASS;
    }
    return state->open ? while_open(state, message, wparam, lparam)
                       : while_shut(state, message, wparam, lparam, chat_key, control_held);
}

mp_chat_key_action_t mp_chat_key_chain_answered(mp_chat_key_state_t *state, uint32_t lparam,
                                                int32_t answer)
{
    if (state == NULL || answer != 0) {
        return MP_CHAT_KEY_LEFT;
    }
    state->open         = true;
    state->drop_pending = true;
    state->drop_scan    = scan_of(lparam);
    state->closing_key  = 0;
    return MP_CHAT_KEY_OPEN;
}

void mp_chat_key_closed(mp_chat_key_state_t *state, int32_t closing_key)
{
    if (state == NULL) {
        return;
    }
    state->open         = false;
    state->drop_pending = false;
    state->drop_scan    = 0u;
    state->closing_key  = closing_key;
}

void mp_chat_key_frame_end(mp_chat_key_state_t *state)
{
    if (state != NULL) {
        state->drop_pending = false;
    }
}

int32_t mp_chat_key_fallback(int32_t scoreboard_vk)
{
    return scoreboard_vk == MP_CHAT_KEY_DEFAULT ? MP_CHAT_KEY_SECOND : MP_CHAT_KEY_DEFAULT;
}

int32_t mp_chat_key_pick(const char *name, const char *scoreboard_name,
                         mp_chat_key_verdict_t *verdict)
{
    int32_t chat = mp_board_key_code(name);
    int32_t board = mp_board_key_code(scoreboard_name);
    size_t  i;
    mp_chat_key_verdict_t found = MP_CHAT_KEY_NAME_ACCEPTED;

    if (board == 0) {
        board = mp_board_key_code("TAB");   /* what the scoreboard falls back to itself */
    }
    if (chat == 0) {
        found = MP_CHAT_KEY_NAME_UNKNOWN;
    }
    for (i = 0; found == MP_CHAT_KEY_NAME_ACCEPTED && i < sizeof RESERVED / sizeof RESERVED[0];
         ++i) {
        if (chat == RESERVED[i]) {
            found = MP_CHAT_KEY_NAME_RESERVED;
        }
    }
    if (found == MP_CHAT_KEY_NAME_ACCEPTED && chat == board) {
        found = MP_CHAT_KEY_NAME_SCOREBOARD;
    }
    if (verdict != NULL) {
        *verdict = found;
    }
    return found == MP_CHAT_KEY_NAME_ACCEPTED ? chat : mp_chat_key_fallback(board);
}

const char *mp_chat_key_verdict_text(mp_chat_key_verdict_t verdict)
{
    switch (verdict) {
    case MP_CHAT_KEY_NAME_ACCEPTED:
        return "taken";
    case MP_CHAT_KEY_NAME_UNKNOWN:
        return "not a key this build reads: one letter or digit, or F1 to F12";
    case MP_CHAT_KEY_NAME_RESERVED:
        return "a key the game or a menu already keeps (TAB, M, F4, F6, F7, F8, F10, F11, F12)";
    case MP_CHAT_KEY_NAME_SCOREBOARD:
        return "the key ScoreboardKey holds the board with";
    default:
        break;
    }
    return "refused";
}
