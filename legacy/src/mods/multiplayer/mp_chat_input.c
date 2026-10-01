/* mp_chat_input.c: the chat's key, its input line, and the one way it closes. See the header.
 *
 * Every function here runs on the game's own thread: the hook from the engine's message pump, the
 * frame and the look from the frame hook and the thread timer, which the same pump dispatches. So
 * nothing here needs a lock, and the hook sees the state the last frame left.
 */
#include "mp_chat_input.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_chat.h"
#include "mp_chat_key_rule.h"
#include "mp_chat_rule.h"
#include "mp_input.h"
#include "mp_signatures_chat.h"
#include "mp_signatures_pause.h"
#include "mp_wallclock.h"
#include "multiplayer.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The engine's key handler: cdecl, four arguments, and non zero for a message it took. */
typedef int32_t(__cdecl *key_hook_fn_t)(uint32_t window, int32_t message, int32_t wparam,
                                        uint32_t lparam);

/* What the hook answers for a message the chat took. The window procedure then returns a value
 * the pump throws away, so nothing else is owed. */
#define TAKEN 1

/* The level outcome while a level runs; any other value is a level ending or not yet begun. */
#define LEVEL_OUTCOME_RUNNING 2u

/* Room for whatever a person types into the file. A key's name is three characters at most, so a
 * longer value stays no key as long as it is not cut below four. */
#define KEY_NAME_MAX 32u

/* How often the key is read again while a transport stands, so the developer menu's row takes
 * effect in a session without a restart. */
#define KEY_READ_EVERY_MS 1000u

/* How long the prefix shows that Enter sent nothing, and how long a closing key may keep the
 * input held at most. */
#define REFUSED_SHOWN_MS  1000u
#define CLOSING_HOLD_MS   1000u

typedef struct chat_cells {
    bool      ready;
    uintptr_t outcome;   /* the level outcome */
    uintptr_t menu;      /* the menu screen on show, nought for none */
    uintptr_t movie;     /* set while the engine plays a movie */
    uintptr_t gate;      /* the simulation gate a load holds; 0 when its table did not resolve */
} chat_cells_t;

typedef struct chat_counts {
    uint32_t reads;
    uint32_t changes;
    uint32_t refused_names;
    uint32_t opens;
    uint32_t left_to_chain;
    uint32_t no_hold;
    uint32_t typed;
    uint32_t transliterated;
    uint32_t no_room;
    uint32_t closed[MP_CHAT_CLOSE_COUNT];
    uint32_t held_frames;
    uint32_t held_after_close;
} chat_counts_t;

typedef struct chat_input_state {
    bool                tried;
    bool                installed;
    detour_t            detour;
    chat_cells_t        cells;
    mp_chat_key_state_t keys;
    uint32_t            code_page;

    int32_t             key_code;
    char                key_name[KEY_NAME_MAX];     /* the key the chat opens on */
    char                key_read[KEY_NAME_MAX];     /* ChatKey as the file said it last */
    char                board_read[KEY_NAME_MAX];   /* ScoreboardKey as the file said it last */
    bool                key_ever_read;
    uint32_t            key_read_ms;

    char                line[MP_CHAT_TEXT_MAX + 1u];
    size_t              length;
    bool                refused;
    uint32_t            refused_ms;

    bool                lingering;       /* closed by a key that is still down: the hold stays */
    bool                linger_counted;
    uint32_t            closed_ms;

    chat_counts_t       counts;
} chat_input_state_t;

static chat_input_state_t input;

/* ==============================================================================================
 * The key the chat opens on.
 * ============================================================================================ */

/* A key as the file spells it: the letter or the digit, or F and the number. */
static void name_of(int32_t vk, char *out, size_t size)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        text_format(out, size, "%c", (char)vk);
    } else if (vk >= 0x70 && vk <= 0x7B) {
        text_format(out, size, "F%d", (int)(vk - 0x70 + 1));
    } else {
        text_format(out, size, "key %d", (int)vk);
    }
}

/* The two settings the choice depends on, read again; a change is said once, and so is a name
 * that is refused, when it first appears. Two reads of one file a second are the whole cost. */
static void read_the_key(uint32_t now_ms)
{
    char                  chat[KEY_NAME_MAX];
    char                  board[KEY_NAME_MAX];
    char                  name[KEY_NAME_MAX];
    int32_t               code;
    mp_chat_key_verdict_t verdict = MP_CHAT_KEY_NAME_ACCEPTED;

    input.key_read_ms = now_ms;
    ++input.counts.reads;
    (void)ini_read_string(MULTIPLAYER_SECTION, "ChatKey", "T", chat, sizeof chat);
    (void)ini_read_string(MULTIPLAYER_SECTION, "ScoreboardKey", "TAB", board, sizeof board);
    if (input.key_ever_read && strcmp(chat, input.key_read) == 0 &&
        strcmp(board, input.board_read) == 0) {
        return;
    }
    memcpy(input.key_read, chat, sizeof input.key_read);
    memcpy(input.board_read, board, sizeof input.board_read);
    code = mp_chat_key_pick(chat, board, &verdict);
    name_of(code, name, sizeof name);
    if (verdict != MP_CHAT_KEY_NAME_ACCEPTED) {
        ++input.counts.refused_names;
        log_warning("ChatKey '%s' is %s, so the chat opens on %s", chat,
                    mp_chat_key_verdict_text(verdict), name);
    }
    if (input.key_ever_read && code != input.key_code) {
        ++input.counts.changes;
        log_info("the chat key is now %s, it was %s", name, input.key_name);
    }
    input.key_code      = code;
    input.key_ever_read = true;
    memcpy(input.key_name, name, sizeof input.key_name);
}

/* ==============================================================================================
 * Where the chat may be: the engine's own answers, read every time they are asked.
 * ============================================================================================ */

static bool cell_set(uintptr_t cell)
{
    uint32_t value = 0u;

    return cell != 0u && memory_try_read_u32(cell, &value) && value != 0u;
}

/* Why the chat may not be open or drawn now, or false when it may. The order is the order the
 * reasons are counted in: a session that is gone first, then what covers the level. */
static bool not_in_play(mp_chat_close_t *reason)
{
    uint32_t outcome = 0u;

    if (!mp_armed_transport()) {
        *reason = MP_CHAT_CLOSE_SESSION;
        return true;
    }
    if (!input.cells.ready) {
        *reason = MP_CHAT_CLOSE_LEVEL;
        return true;
    }
    if (cell_set(input.cells.movie)) {
        *reason = MP_CHAT_CLOSE_MOVIE;
        return true;
    }
    if (cell_set(input.cells.menu)) {
        *reason = MP_CHAT_CLOSE_MENU;
        return true;
    }
    if (!memory_try_read_u32(input.cells.outcome, &outcome) || outcome != LEVEL_OUTCOME_RUNNING ||
        cell_set(input.cells.gate)) {
        *reason = MP_CHAT_CLOSE_LEVEL;
        return true;
    }
    return false;
}

bool mp_chat_input_in_play(void)
{
    mp_chat_close_t reason;

    return !not_in_play(&reason);
}

/* ==============================================================================================
 * The input line.
 * ============================================================================================ */

static int32_t pass(uint32_t window, int32_t message, int32_t wparam, uint32_t lparam)
{
    return ((key_hook_fn_t)input.detour.original)(window, message, wparam, lparam);
}

static void release_the_hold(void)
{
    input.lingering = false;
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
}

/* The opening press. The mods further in are asked first and keep it when they take it; the
 * chat opens only on a press nobody else wanted, with the input held from that moment. */
static int32_t open_on(uint32_t window, int32_t message, int32_t wparam, uint32_t lparam)
{
    int32_t answer;

    if (!mp_chat_input_in_play()) {
        return pass(window, message, wparam, lparam);
    }
    /* Without the input split nothing could hold the body, and it would walk, jump and fire on
     * the letters typed. The key stays the game's then. */
    if (!mp_input_installed()) {
        ++input.counts.no_hold;
        return pass(window, message, wparam, lparam);
    }
    answer = pass(window, message, wparam, lparam);
    if (mp_chat_key_chain_answered(&input.keys, lparam, answer) != MP_CHAT_KEY_OPEN) {
        ++input.counts.left_to_chain;
        return answer;
    }
    input.length    = 0u;
    input.line[0]   = '\0';
    input.refused   = false;
    input.lingering = false;
    ++input.counts.opens;
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    return TAKEN;
}

/* One typed byte, written the way the wire carries it. A byte with no room left is refused rather
 * than cut in half, and only a byte that has a place in a line counts as refused for room. */
static void type_one(uint8_t typed)
{
    const size_t added = mp_chat_rule_append_typed(input.line, input.length, MP_CHAT_TEXT_MAX,
                                                   typed, input.code_page);

    if (added == 0u) {
        if (typed >= 0x20u && typed != 0x7Fu) {
            ++input.counts.no_room;
        }
        return;
    }
    ++input.counts.typed;
    if (added != 1u || input.line[input.length] != (char)typed) {
        ++input.counts.transliterated;
    }
    input.length += added;
}

static void erase_one(void)
{
    if (input.length > 0u) {
        --input.length;
        input.line[input.length] = '\0';
    }
}

/* Enter. An empty line only closes. A line the chat did not send stays in the field as typed, the
 * prefix says so for a second, and the next Enter tries again. */
static void say_the_line(void)
{
    char   text[MP_CHAT_TEXT_MAX + 1u];
    size_t length;

    memcpy(text, input.line, input.length + 1u);
    length = mp_chat_rule_trim(text, input.length);
    if (length == 0u) {
        mp_chat_input_close(MP_CHAT_CLOSE_ENTER);
        return;
    }
    switch (mp_chat_say(text, length, mp_wallclock_ms())) {
    case MP_CHAT_SAY_TOO_FAST:
    case MP_CHAT_SAY_UNSENT:
    case MP_CHAT_SAY_NO_SESSION:
        input.refused    = true;
        input.refused_ms = mp_wallclock_ms();
        return;
    case MP_CHAT_SAY_SENT:
    case MP_CHAT_SAY_EMPTY:
    default:
        mp_chat_input_close(MP_CHAT_CLOSE_ENTER);
        return;
    }
}

/* engine: int main_keyHook(u32 hwnd, i32 msg, i32 wParam, u32 lParam) */
static int32_t __cdecl hook_chat_key(uint32_t window, int32_t message, int32_t wparam,
                                     uint32_t lparam)
{
    bool control = false;

    if (!mp_armed_transport()) {
        mp_chat_input_close(MP_CHAT_CLOSE_SESSION);
        return pass(window, message, wparam, lparam);
    }
    /* Only a key press can open the chat, so only a key press asks for Control. */
    if ((uint32_t)message == MP_CHAT_MSG_KEY_DOWN) {
        control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    }
    switch (mp_chat_key_decide(&input.keys, (uint32_t)message, (uint32_t)wparam, lparam,
                               input.key_code, control)) {
    case MP_CHAT_KEY_ASK_CHAIN:
        return open_on(window, message, wparam, lparam);
    case MP_CHAT_KEY_TYPE:
        type_one((uint8_t)wparam);
        return TAKEN;
    case MP_CHAT_KEY_ERASE:
        erase_one();
        return TAKEN;
    case MP_CHAT_KEY_SEND:
        say_the_line();
        return TAKEN;
    case MP_CHAT_KEY_CANCEL:
        mp_chat_input_close(MP_CHAT_CLOSE_ESCAPE);
        return TAKEN;
    case MP_CHAT_KEY_SWALLOW:
    case MP_CHAT_KEY_DROP:
        return TAKEN;
    case MP_CHAT_KEY_PASS:
    case MP_CHAT_KEY_OPEN:
    case MP_CHAT_KEY_LEFT:
    default:
        return pass(window, message, wparam, lparam);
    }
}

/* ==============================================================================================
 * The one way out, and the frame.
 * ============================================================================================ */

void mp_chat_input_close(mp_chat_close_t reason)
{
    const int32_t key = reason == MP_CHAT_CLOSE_ENTER    ? MP_CHAT_VK_RETURN
                        : reason == MP_CHAT_CLOSE_ESCAPE ? MP_CHAT_VK_ESCAPE
                                                         : 0;

    if (!input.keys.open) {
        if (input.lingering) {
            release_the_hold();
        }
        return;
    }
    ++input.counts.closed[(size_t)reason < (size_t)MP_CHAT_CLOSE_COUNT ? reason
                                                                        : MP_CHAT_CLOSE_SESSION];
    mp_chat_key_closed(&input.keys, key);
    memset(input.line, 0, sizeof input.line);
    input.length  = 0u;
    input.refused = false;
    if (key != 0 && mp_armed_transport()) {
        input.lingering      = true;
        input.linger_counted = false;
        input.closed_ms      = mp_wallclock_ms();
        return;
    }
    release_the_hold();
}

void mp_chat_input_look(void)
{
    mp_chat_close_t reason;

    if ((input.keys.open || input.lingering) && not_in_play(&reason)) {
        mp_chat_input_close(reason);
    }
}

void mp_chat_input_frame(void)
{
    const uint32_t now = mp_wallclock_ms();

    if (!input.tried) {
        return;
    }
    if (!input.key_ever_read || now - input.key_read_ms >= KEY_READ_EVERY_MS) {
        read_the_key(now);
    }
    mp_chat_input_look();
    mp_chat_key_frame_end(&input.keys);
    if (input.lingering) {
        if (input.keys.closing_key != 0 && !input.linger_counted) {
            input.linger_counted = true;
            ++input.counts.held_after_close;
        }
        if (input.keys.closing_key == 0 || now - input.closed_ms >= CLOSING_HOLD_MS) {
            release_the_hold();
        }
    }
    if (input.keys.open || input.lingering) {
        ++input.counts.held_frames;
    }
}

bool mp_chat_input_is_typing(void)
{
    return input.keys.open;
}

const char *mp_chat_input_line(uint32_t now_ms, size_t *length, bool *refused)
{
    if (!input.keys.open) {
        return NULL;
    }
    if (length != NULL) {
        *length = input.length;
    }
    if (refused != NULL) {
        *refused = input.refused && now_ms - input.refused_ms < REFUSED_SHOWN_MS;
    }
    return input.line;
}

/* ==============================================================================================
 * Arming and the report.
 * ============================================================================================ */

static void bind_the_cells(void)
{
    const signature_t *site  = mp_signatures_chat_site(MP_CHAT_SITE_KEY_HOOK);
    uintptr_t          movie = 0u;

    input.cells.outcome = mp_cells_address(MP_CELL_LEVEL_OUTCOME);
    input.cells.menu    = mp_cells_address(MP_CELL_CURRENT_MENU);
    (void)mp_signatures_pause_resolve();
    input.cells.gate = mp_signatures_pause_cell(MP_PAUSE_CELL_SIM_GATE);
    if (site != NULL && site->address != 0u &&
        signature_read_address_operand(site, MP_CHAT_KEY_HOOK_MOVIE_CELL, &movie) &&
        memory_is_inside_image(movie, sizeof(int32_t))) {
        input.cells.movie = movie;
    }
    /* A missing movie cell reads as no movie. The engine draws no frame while a movie plays, so
     * the box is not drawn over one either way. The cell comes out of the bytes the hook is placed
     * on: without them there is no hook, and a hook with no cell, whose operand could not be read,
     * is said below. */
    input.cells.ready = input.cells.outcome != 0u && input.cells.menu != 0u;
    if (!input.cells.ready) {
        log_warning("the chat cannot tell a running level: the level outcome or the menu on show "
                    "did not resolve, so the chat stays shut and draws nothing");
        return;
    }
    if (input.cells.movie == 0u) {
        log_warning("the movie cell did not resolve, so the chat cannot tell a movie from the "
                    "level: a chat open when a movie starts stays open, holding the input, and its "
                    "key may open it during one, with nothing drawn");
    }
    if (input.cells.gate == 0u) {
        log_warning("the simulation gate did not resolve, so a load is seen by the level outcome "
                    "alone");
    }
}

static void hook_the_keys(void)
{
    const uintptr_t site = mp_signatures_chat_address(MP_CHAT_SITE_KEY_HOOK);

    if (site == 0u) {
        log_warning("the engine's key handler did not resolve, so the chat never opens%s",
                    input.cells.ready ? "; the lines of the other players are still shown" : "");
        return;
    }
    if (!detour_install(&input.detour, site, (const void *)&hook_chat_key,
                        mp_signatures_chat_prologue(MP_CHAT_SITE_KEY_HOOK))) {
        log_warning("the engine's key handler at %08X could not be hooked, so the chat never "
                    "opens", (unsigned)site);
        return;
    }
    input.installed = true;
    log_info("the chat is hooked on the key handler at %08X%s and opens on %s: while a line is "
             "typed the player's input is held and the world goes on; with no transport every key "
             "is handed on", (unsigned)site,
             input.detour.chained ? " in front of a hook already there" : "", input.key_name);
}

bool mp_chat_input_arm(void)
{
    read_the_key(mp_wallclock_ms());
    if (!input.tried) {
        input.tried     = true;
        input.code_page = (uint32_t)GetACP();
        (void)mp_signatures_chat_resolve();
        bind_the_cells();
        hook_the_keys();
    }
    return input.installed;
}

void mp_chat_input_report(void)
{
    const chat_counts_t *c = &input.counts;

    log_info("the chat keys: the key is %s (ChatKey), %u read(s) of the setting, %u change(s), %u "
             "refused name(s); %u open(s), %u key press(es) left to a mod further in that took "
             "it, %u refused: no input hold, %u character(s) typed, %u replaced by a "
             "transliteration, %u refused for no room; closed %u by enter, %u by escape, %u by a "
             "menu, %u by a movie, %u by the level, %u by the session; the input held for %u "
             "frame(s), %u held after the close",
             input.key_ever_read ? input.key_name : "T", (unsigned)c->reads, (unsigned)c->changes,
             (unsigned)c->refused_names, (unsigned)c->opens, (unsigned)c->left_to_chain,
             (unsigned)c->no_hold, (unsigned)c->typed, (unsigned)c->transliterated,
             (unsigned)c->no_room, (unsigned)c->closed[MP_CHAT_CLOSE_ENTER],
             (unsigned)c->closed[MP_CHAT_CLOSE_ESCAPE], (unsigned)c->closed[MP_CHAT_CLOSE_MENU],
             (unsigned)c->closed[MP_CHAT_CLOSE_MOVIE], (unsigned)c->closed[MP_CHAT_CLOSE_LEVEL],
             (unsigned)c->closed[MP_CHAT_CLOSE_SESSION], (unsigned)c->held_frames,
             (unsigned)c->held_after_close);
    if (input.tried && !input.installed) {
        log_warning("  the chat keys: the key handler was never hooked, so the chat could "
                    "not open");
    }
}
