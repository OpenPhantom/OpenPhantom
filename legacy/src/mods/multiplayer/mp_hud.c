/* mp_hud.c: the scoreboard panel, painted with the engine's own renderer.
 *
 * What the panel LOOKS like is here; what it SAYS is mp_board.c, and the entry points it is
 * painted through are mp_signatures_hud.c. Nothing is cached and no surface is held: every
 * rectangle and every string goes through the engine on the frame it appears on.
 *
 * ================================ The two shapes of state ====================================
 *
 * The font layer is global and sticky. Colour, alignment and both scales stay whatever the last
 * user of the font left behind, so every one of them is set again in front of every string. They
 * are deliberately NOT put back afterwards: every other consumer of that font sets them for
 * itself, which is what the engine's own heads up display and info print do.
 *
 * The font SLOT is read on every frame rather than once. It holds -1 until the engine builds its
 * font module and goes back to -1 when the module is torn down, and selecting -1 puts the current
 * font at NULL, after which every setter and the drawer itself return having done nothing and
 * without saying so.
 *
 * ================================== The coordinate systems ===================================
 *
 * They are not the same for the two primitives, and this cost a whole class of bug elsewhere.
 *
 * A rectangle takes SCREEN PIXELS. The engine's own letterbox bars are drawn with the screen
 * width and height straight out of their cells.
 *
 * A string does not. The drawer multiplies the x it is given by the canvas width and the y by the
 * canvas height, so what it wants is a FRACTION of the screen. Setting the position scale to one
 * over the screen size turns that back into pixels, which is why both halves of this file can be
 * written in the same units. The glyph scale is the second half of the same arrangement: the
 * layer below multiplies every glyph by the screen size over 640 by 480 before it draws, so
 * passing 640 over the width and 480 over the height cancels it and the text comes out at the
 * size it was drawn for.
 *
 * The y a string is given is its BASELINE and not the top of its box, which is why one function
 * turns the top of a row into the baseline of the line inside it and every string here goes
 * through it. Treating that y as a top edge puts every label at the top of its own band with the
 * rule and the row under it sitting on top of the letters.
 *
 * SIZE NOTE: over 600 lines. The panel and the two notices that take its place, the end of a
 * session and a host gone quiet, are one surface painted from one module message and share every
 * primitive here. The seam, if this grows again, is the notice band: paint_notice and its two
 * callers read nothing of the board's.
 */
#include "mp_hud.h"

#include "mp_board.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_roster.h"
#include "mp_bridge_seats.h"
#include "mp_cells.h"
#include "mp_chat_input.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_roster.h"
#include "mp_round.h"
#include "mp_session_over.h"
#include "mp_rules.h"
#include "mp_score.h"
#include "mp_signatures_hud.h"
#include "mp_text.h"
#include "mp_world_door.h"
#include "multiplayer.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The first statement of the engine's frame end, and the only message this file acts on. The
 * frame end at 0046C139 opens with `push ebp; mov ebp,esp; sub esp,0x3F8`, loads the frame delta,
 * and its first call is `push eax; push 0x15; push 0; call` the module broadcast; the scene is
 * closed about five hundred bytes further down at 0046C341 and the page shown at 0046C346, so a
 * panel drawn on 0x15 lands in the picture about to be presented. Four call sites reach that
 * function: the campaign frame at 0043EAA6, the menu screen pump at 0045F4F7 and 0045F65C, and
 * the debug single step pump at 0046A720. Two of the four are menu screens and the lobby is one
 * of them, which is why the panel gates on a running level rather than on the hook firing. */
#define MODULE_MSG_FRAME_TOP 0x15

/* The campaign loop spins while the level outcome cell reads this and leaves it for any other
 * value, so it is also the engine's own answer to "is a level running". The escape key asks the
 * same question before it will open the pause menu: at 0043F66F it compares the cell with 2,
 * then calls the player's death test, and reaches the pause only when the player is alive,
 * which is the reason this is a panel and not a pause screen. */
#define LEVEL_OUTCOME_RUNNING 2u

/* The sixteen font slots the engine's font module keeps. A number outside them puts the current
 * font at NULL and makes every call below a silent no-op. */
#define FONT_SLOT_COUNT 16

/* The three alignments, in the numbers the setter takes rather than the bits it writes. */
#define ALIGN_CENTRE 0
#define ALIGN_LEFT   1
#define ALIGN_RIGHT  2

/* The sixth argument of the rectangle. Non zero draws it there and then; zero puts it in the
 * engine's deferred, sorted queue, which is emptied before this instant and would show the panel
 * one frame late or not at all. The engine's own fade passes one. */
#define DRAW_NOW 1

/* The font the panel is drawn in, measured from the file the engine loads: the glyph record is
 * (u0, v0, u1, v1, ascent, advance) at 0x108 + code * 24, and over the printable range 32 to
 * 126 no glyph reaches higher than fourteen above the baseline or lower than six below it. The
 * row pitch is that span plus two, which is the smallest leading at which two rows of digits do
 * not touch. `A` has an ascent of 13 and an advance of 13, `M` advances 15 and a space 5, which
 * is how the table is known to be this font's advance field and not something else. */
#define TEXT_ASCENT  14.0f
#define TEXT_SPAN    20.0f
#define ROW_H        22.0f

#define PAD          10.0f
#define RULE          1.0f
#define ACCENT_H      3.0f

/* Where the panel sits: centred across, and a tenth of the way down, which is clear of the heads
 * up display at the top of the screen and of the subtitles at the bottom. */
#define TOP_FRACTION 0.10f

/* The colours, and the one rule that makes them work.
 *
 * Exactly one rectangle is translucent, the body; everything else is opaque. A translucent shape
 * over another translucent shape has a contrast that depends on the scene behind both, so bands
 * drawn that way converge as the game gets brighter. With a single translucent surface every
 * contrast inside the panel is fixed and the alpha has one job. */
#define C_BODY        0xE60D0F16u
#define C_BORDER      0xFF59617Au
#define C_ACCENT      0xFF5AA0F0u
#define C_HEAD_BAND   0xFF171A24u
#define C_RULE        0xFF3A4256u
#define C_HEADING     0xFFE8EAF2u
#define C_ROW         0xFFD2D7E4u
#define C_ROW_SELF    0xFF232838u
#define C_HINT        0xFF7B8195u

#define HUD_KEY_NAME_MAX 8u

typedef struct mp_hud_state {
    bool                    installed;
    const mp_hud_surface_t *surface;
    uintptr_t               level_outcome_cell;

    int32_t key_code;
    char    key_name[HUD_KEY_NAME_MAX];
    bool    key_held;

    /* The frame boundary, and the guard built on it. `pumps` counts drawn frames; the panel
     * refuses to draw twice against the same count. Before the first pump there is no boundary to
     * guard with, and drawing unguarded is the better failure there: a panel that is slightly too
     * dark is visible, one that never appears is not.
     *
     * The field census behind the guard, a host run of 9498 presented frames, counted 9499 of
     * message 0x15 with a maximum of two in one frame. Both are one thing: a present is counted
     * by the frame hook, which runs after the frame end returns, while the module node is
     * installed from the engine's own startup, earlier; one frame is drawn while the node hears
     * and the counter does not yet count, so the first arrival sees zero presents and takes the
     * reset branch and the second the increment. There is no second broadcast, but that is an
     * explanation and not a proof, so the guard stands and `blocked` is the number that would
     * say otherwise. It counts against the frame pump rather than the engine's own clock, which
     * is the substep counter and advanced 2859 times over those 9498 frames. */
    uint32_t pumps;
    uint32_t drawn_at;
    bool     ever_drawn;

    uint32_t offers;
    uint32_t draws;
    uint32_t blocked;
    uint32_t unguarded;
    uint32_t hidden_no_round;
    uint32_t hidden_no_level;
    uint32_t hidden_quiet;
    uint32_t refused_no_font;
    uint32_t refused_no_screen;
    uint32_t notices;   /* frames the end-of-session line was drawn on */
    uint32_t silences;  /* frames the host was said to have gone quiet on */
} mp_hud_state_t;

static mp_hud_state_t hud;

/* ==============================================================================================
 * The primitives.
 * ============================================================================================ */

static void fill(float x0, float y0, float x1, float y1, uint32_t argb)
{
    hud.surface->quad(x0, y0, x1, y1, argb, DRAW_NOW);
}

/* Everything the font layer forgets, set again. It is called once per string on purpose: anything
 * else drawn between two of ours, a subtitle or the engine's own readout, has already changed all
 * four and puts none of them back. */
static void write(const char *text, float x, float baseline, int32_t align, uint32_t argb)
{
    const float width  = *hud.surface->screen_w;
    const float height = *hud.surface->screen_h;

    hud.surface->select(*hud.surface->font_slot);
    hud.surface->align(align);
    hud.surface->pos_scale(1.0f / width, 1.0f / height);
    hud.surface->glyph_scale(640.0f / width, 480.0f / height);
    hud.surface->colour(argb);
    hud.surface->text(text, x, baseline);
}

/* ==============================================================================================
 * What the panel is allowed to know about the game.
 * ============================================================================================ */

static bool level_is_running(void)
{
    uint32_t outcome = 0;

    if (hud.level_outcome_cell == 0u ||
        !memory_try_read_u32(hud.level_outcome_cell, &outcome)) {
        return false;
    }
    return outcome == LEVEL_OUTCOME_RUNNING;
}

static bool font_is_usable(void)
{
    const int32_t slot = *hud.surface->font_slot;

    return slot >= 0 && slot < FONT_SLOT_COUNT;
}

static bool screen_is_known(void)
{
    return *hud.surface->screen_w > 0.0f && *hud.surface->screen_h > 0.0f;
}

/* ==============================================================================================
 * The paint.
 * ============================================================================================ */

typedef struct hud_frame {
    mp_score_board_t   board;
    mp_rules_t         rules;
    mp_board_row_t     row[MP_BOARD_MAX_ROWS];
    size_t             rows;
    mp_board_columns_t columns;
    char               headline[MP_BOARD_LINE_MAX];
    char               result[MP_BOARD_LINE_MAX];
    char               hint[MP_BOARD_LINE_MAX];
} hud_frame_t;

/* The cheap half: the round's own two structures and the three questions that decide whether
 * anything is drawn at all. It runs on every frame of every session, so it copies a hundred bytes
 * and asks two counters, and does no work a hidden panel would throw away. Each refusal has its
 * own counter, so a board that never drew names its reason rather than the fact. */
static bool visible_now(hud_frame_t *frame)
{
    bool round;
    bool level;
    bool decided;

    memset(frame, 0, sizeof *frame);
    round   = mp_round_board(&frame->board) && mp_round_rules(&frame->rules);
    level   = level_is_running();
    decided = frame->board.outcome != (uint8_t)MP_SCORE_RUNNING;

    /* The rule itself is asked with the real conditions rather than with constants, so that the
     * pure function stays the only place it is written down. The counters below only say which of
     * its five arguments was the reason, which is what a report needs and the rule does not. */
    if (mp_board_visible(round, level, hud.key_held, mp_respawn_pending(), decided)) {
        return true;
    }
    if (!round) {
        ++hud.hidden_no_round;
    } else if (!level) {
        ++hud.hidden_no_level;
    } else {
        ++hud.hidden_quiet;
    }
    return false;
}

/* The other half: everything the panel draws, worked out once it is known that it will be drawn,
 * so that the drawing below is layout and nothing else. */
static void compose(hud_frame_t *frame)
{
    mp_roster_t roster;

    if (!mp_bridge_roster_current(&roster)) {
        memset(&roster, 0, sizeof roster);
    }
    frame->rows = mp_board_build(&frame->board, &roster, mp_bridge_drain_my_slot(), frame->row,
                                 MP_BOARD_MAX_ROWS);
    mp_board_columns(frame->row, frame->rows, &frame->columns);
    mp_board_headline(&frame->board, &frame->rules, frame->headline, sizeof frame->headline);
    mp_board_result(&frame->board, frame->row, frame->rows, frame->result, sizeof frame->result);
    text_format(frame->hint, sizeof frame->hint, mp_text(MP_TEXT_HUD_HOLD_HINT), hud.key_name);
}

/* The panel is as wide as the widest thing in it, and three of the four are whole sentences
 * rather than the table. A panel sized from the table alone would have the headline running out
 * through its own border, and there is no clipping anywhere in this renderer. */
static float content_width(const hud_frame_t *frame)
{
    const char *line[3];
    int32_t     widest = frame->columns.total;
    size_t      i;

    line[0] = frame->headline;
    line[1] = frame->result;
    line[2] = frame->hint;
    for (i = 0; i < sizeof line / sizeof line[0]; ++i) {
        const int32_t width = mp_board_text_width(line[i]);

        if (width > widest) {
            widest = width;
        }
    }
    return (float)widest;
}

/* The baseline of a line of text in a row that starts at `top`. The engine's text grows UPWARD
 * from the position it is given, so that position is the baseline and not the top edge; centring
 * a glyph box of one text span in the row therefore puts the baseline near the bottom of the box
 * rather than at the top of it. */
static float row_baseline(float top)
{
    return top + (ROW_H - TEXT_SPAN) * 0.5f + TEXT_ASCENT;
}

/* One row of the table: the name from the left, the three numbers ending on their column edges.
 * The numbers are right aligned by the engine's own alignment rather than by subtracting a
 * measured width, because that alignment steps by the same advance the drawing pass does. */
static void draw_row(const hud_frame_t *frame, const mp_board_row_t *row, float left, float right,
                     float top, uint32_t argb)
{
    const float baseline = row_baseline(top);
    const float team_x   = right;
    const float deaths_x = team_x - (float)(frame->columns.team + MP_BOARD_COLUMN_GAP);
    const float points_x = deaths_x - (float)(frame->columns.deaths + MP_BOARD_COLUMN_GAP);
    char        number[12];

    write(row->name, left, baseline, ALIGN_LEFT, argb);

    text_format(number, sizeof number, "%d", (int)row->points);
    write(number, points_x, baseline, ALIGN_RIGHT, argb);

    text_format(number, sizeof number, "%d", (int)row->deaths);
    write(number, deaths_x, baseline, ALIGN_RIGHT, argb);

    write(mp_board_team_word(row->team), team_x, baseline, ALIGN_RIGHT, argb);
}

/* The heading row carries the same four words at the same four places, so it is drawn by the same
 * arithmetic with a row of headings rather than a row of numbers. */
static void draw_headings(const hud_frame_t *frame, float left, float right, float top)
{
    const float baseline = row_baseline(top);
    const float team_x   = right;
    const float deaths_x = team_x - (float)(frame->columns.team + MP_BOARD_COLUMN_GAP);
    const float points_x = deaths_x - (float)(frame->columns.deaths + MP_BOARD_COLUMN_GAP);

    write(mp_board_heading(0), left, baseline, ALIGN_LEFT, C_HEADING);
    write(mp_board_heading(1), points_x, baseline, ALIGN_RIGHT, C_HEADING);
    write(mp_board_heading(2), deaths_x, baseline, ALIGN_RIGHT, C_HEADING);
    write(mp_board_heading(3), team_x, baseline, ALIGN_RIGHT, C_HEADING);
}

/* The panel is the accent cap, the headline band, the heading row, one row per player, the result
 * and the hint, with half a pad above each of the two rules and a whole one at the foot. Every
 * number in the sum below is a band this function actually draws, which is what keeps the height
 * and the drawing from drifting apart. */
static float panel_height(size_t rows)
{
    return ACCENT_H + 4.0f * ROW_H + ROW_H * (float)rows + 2.0f * PAD;
}

static void paint(const hud_frame_t *frame)
{
    const float content = content_width(frame);
    const float panel_w = content + 2.0f * PAD;
    const float panel_h = panel_height(frame->rows);
    const float slack   = *hud.surface->screen_w - panel_w;
    const float left    = (slack > 0.0f) ? slack * 0.5f : 0.0f;
    const float top     = *hud.surface->screen_h * TOP_FRACTION;
    const float right   = left + panel_w;
    const float text_l  = left + PAD;
    const float text_r  = right - PAD;
    const float centre  = left + panel_w * 0.5f;
    float       y;
    size_t      i;

    fill(left, top, right, top + panel_h, C_BODY);
    fill(left, top, right, top + ACCENT_H, C_ACCENT);
    fill(left, top, left + RULE, top + panel_h, C_BORDER);
    fill(right - RULE, top, right, top + panel_h, C_BORDER);
    fill(left, top + panel_h - RULE, right, top + panel_h, C_BORDER);

    /* The headline, on its own band. The band is what gives a single font at a single size the
     * weight a title needs, and it is also the only thing separating it from the table. */
    y = top + ACCENT_H;
    fill(left + RULE, y, right - RULE, y + ROW_H, C_HEAD_BAND);
    write(frame->headline, centre, row_baseline(y), ALIGN_CENTRE, C_HEADING);

    y += ROW_H + PAD * 0.5f;
    draw_headings(frame, text_l, text_r, y);
    y += ROW_H;
    fill(text_l, y - RULE * 2.0f, text_r, y - RULE, C_RULE);

    for (i = 0; i < frame->rows; ++i) {
        if (frame->row[i].is_self) {
            fill(left + RULE, y, right - RULE, y + ROW_H, C_ROW_SELF);
            fill(left + RULE, y, left + RULE * 4.0f, y + ROW_H, C_ACCENT);
        }
        draw_row(frame, &frame->row[i], text_l, text_r, y, C_ROW);
        y += ROW_H;
    }

    y += PAD * 0.5f;
    fill(text_l, y - RULE, text_r, y, C_RULE);
    write(frame->result, centre, row_baseline(y), ALIGN_CENTRE, C_ACCENT);
    y += ROW_H;
    write(frame->hint, centre, row_baseline(y), ALIGN_CENTRE, C_HINT);
}

/* ==============================================================================================
 * The instant.
 * ============================================================================================ */

/* One line, on its own band, for the end of a session.
 *
 * It is NOT the scoreboard with one row in it, and it does not go through visible_now: that rule
 * requires a round to be running, and the whole point of this line is the moment when there is no
 * longer a session to run one. A client whose host walked away has no score to be shown and no
 * result to be told, what it has is a question, and this is the answer to it.
 *
 * Drawn from the same module message as the board, which is heard after the HUD, the subtitles
 * and the death tint and before the scene closes. That ordering is what puts this line ON TOP of
 * the fade a dying player is looking at, which is exactly where a player who cannot open a menu
 * needs to be able to read it. */
static void paint_notice(const char *text)
{
    const float width   = (float)mp_board_text_width(text) + 4.0f * PAD;
    const float height  = ROW_H + ACCENT_H + PAD;
    const float slack   = *hud.surface->screen_w - width;
    const float left    = (slack > 0.0f) ? slack * 0.5f : 0.0f;
    const float top     = *hud.surface->screen_h * TOP_FRACTION;
    const float right   = left + width;
    const float centre  = left + width * 0.5f;

    fill(left, top, right, top + height, C_BODY);
    fill(left, top, right, top + ACCENT_H, C_ACCENT);
    fill(left, top, left + RULE, top + height, C_BORDER);
    fill(right - RULE, top, right, top + height, C_BORDER);
    fill(left, top + height - RULE, right, top + height, C_BORDER);
    write(text, centre, row_baseline(top + ACCENT_H), ALIGN_CENTRE, C_HEADING);
}

/* How long a client's host may say nothing before the picture says so. Past any hitch a link shows
 * in play and far short of the connected timeout, so a player whose host has gone watches a count
 * rather than a world that has stopped answering. */
#define SILENCE_NOTICE_MS 2000u

/* A world that has not moved for a second is a world somebody stopped: a pause screen, a load or
 * the cheat menu. Shorter than the silence above it on purpose, because this one is the ordinary
 * case and the player is owed the reason before the connection is doubted. */
#define WORLD_STILL_NOTICE_MS 1000u

static void offer_frame(void)
{
    hud_frame_t frame;
    const char *notice;
    uint32_t    silent;

    if (!screen_is_known()) {
        ++hud.refused_no_screen;
        return;
    }
    if (!font_is_usable()) {
        ++hud.refused_no_font;
        return;
    }
    /* Before the board, and instead of it. The board describes a session; once one has ended,
     * describing it is the wrong thing to do, and its own rule would refuse to anyway because the
     * round was ended along with everything else the session owned. */
    /* A moment before a state: a door that was just held shut is what the player is waiting for
     * an answer about, and it is gone in a few seconds either way. The door module keeps its own
     * clock; this only asks. */
    /* And before everything else, because a player looking at a corpse in a world that goes on
     * running is owed the reason first. */
    if (mp_reentry_waiting_for_host()) {
        paint_notice(mp_text(MP_TEXT_HUD_WIPE_WAIT));
        ++hud.notices;
        ++hud.draws;
        hud.ever_drawn = true;
        hud.drawn_at   = hud.pumps;
        return;
    }
    /* And before the silence, because a host that is still answering and not moving is a
     * different thing from a host that has gone: the player needs to be told which. */
    if (mp_bridge_drain_world_still_ms() >= WORLD_STILL_NOTICE_MS &&
        mp_bridge_lobby_host_silent_ms() < WORLD_STILL_NOTICE_MS) {
        paint_notice(mp_text(MP_TEXT_HUD_HOST_STILL));
        ++hud.silences;
        ++hud.draws;
        hud.ever_drawn = true;
        hud.drawn_at   = hud.pumps;
        return;
    }
    notice = mp_world_door_notice();
    if (notice == NULL) {
        notice = mp_bridge_seats_notice();   /* a host that sent a player away says who */
    }
    if (notice != NULL) {
        paint_notice(notice);
        ++hud.notices;
        ++hud.draws;
        hud.ever_drawn = true;
        hud.drawn_at   = hud.pumps;
        return;
    }
    notice = mp_session_over_text(mp_session_over_reason());
    if (notice != NULL) {
        paint_notice(notice);
        ++hud.notices;
        ++hud.draws;
        hud.ever_drawn = true;
        hud.drawn_at   = hud.pumps;
        return;
    }
    /* And before the board as well: a host that says nothing is the first thing a player needs to
     * know, and the count is what tells a dead host from a slow one. */
    silent = mp_bridge_lobby_host_silent_ms();
    if (silent >= SILENCE_NOTICE_MS) {
        char text[64];

        text_format(text, sizeof text, mp_text(MP_TEXT_HUD_HOST_SILENT),
                    (unsigned)(silent / 1000u));
        paint_notice(text);
        ++hud.silences;
        ++hud.draws;
        hud.ever_drawn = true;
        hud.drawn_at   = hud.pumps;
        return;
    }
    if (!visible_now(&frame)) {
        return;
    }
    compose(&frame);
    paint(&frame);
    ++hud.draws;
    hud.ever_drawn = true;
    hud.drawn_at   = hud.pumps;
}

void mp_hud_note_module_message(int message)
{
    if (!hud.installed || message != MODULE_MSG_FRAME_TOP) {
        return;
    }
    ++hud.offers;

    /* No frame boundary has gone past yet, so there is nothing to measure a repeat against. */
    if (hud.pumps == 0u) {
        ++hud.unguarded;
        offer_frame();
        return;
    }
    if (hud.ever_drawn && hud.drawn_at == hud.pumps) {
        ++hud.blocked;
        return;
    }
    offer_frame();
}

/* Held rather than toggled, and refused unless this window is the one in front: two instances of
 * the game on one machine share a keyboard, and a toggle read by both would fight itself. Refused
 * while the player types into the chat too, where a letter of the board's key is a letter. */
static void sample_key(void)
{
    if (hud.key_code == 0 || mp_chat_input_is_typing() ||
        GetForegroundWindow() != GetActiveWindow()) {
        hud.key_held = false;
        return;
    }
    hud.key_held = (GetAsyncKeyState(hud.key_code) & 0x8000) != 0;
}

void mp_hud_pump(void)
{
    if (!hud.installed) {
        return;
    }
    ++hud.pumps;
    sample_key();
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

static void read_key(void)
{
    char configured[HUD_KEY_NAME_MAX];

    (void)ini_read_string(MULTIPLAYER_SECTION, "ScoreboardKey", "TAB", configured,
                          sizeof configured);
    hud.key_code = mp_board_key_code(configured);
    if (hud.key_code == 0) {
        log_warning("ScoreboardKey '%s' is not a key this build knows, so the board falls back to "
                    "TAB. Accepted: TAB, one letter or digit, and F1 to F12", configured);
        hud.key_code = mp_board_key_code("TAB");
        text_format(hud.key_name, sizeof hud.key_name, "TAB");
    } else {
        text_format(hud.key_name, sizeof hud.key_name, "%s", configured);
    }
}

bool mp_hud_install(void)
{
    if (hud.installed) {
        return true;
    }
    (void)mp_signatures_hud_resolve();
    hud.surface = mp_signatures_hud_surface();
    if (hud.surface == NULL) {
        log_warning("the scoreboard has no drawing surface: %s did not resolve, so the score "
                    "stays in this log and nowhere else",
                    mp_signatures_hud_missing() != NULL ? mp_signatures_hud_missing()
                                                        : "one of its sites");
        return false;
    }
    hud.level_outcome_cell = mp_cells_address(MP_CELL_LEVEL_OUTCOME);
    if (hud.level_outcome_cell == 0u) {
        log_warning("the level outcome cell did not resolve, so the board cannot tell a running "
                    "level from a menu and stays off");
        return false;
    }
    read_key();
    hud.installed = true;
    log_info("the scoreboard is armed on the frame's first module message, held open with %s, and "
             "shown to a dead player without one", hud.key_name);
    return true;
}

void mp_hud_report(void)
{
    if (!hud.installed) {
        log_info("the scoreboard: not installed (%s)",
                 mp_signatures_hud_missing() != NULL ? mp_signatures_hud_missing()
                                                     : "the feature was never armed");
        return;
    }
    log_info("the scoreboard: %u offer(s) over %u frame(s), %u drawn, %u refused as a repeat "
             "inside one frame, %u drawn before the first frame boundary; %u frame(s) said the "
             "host had gone quiet",
             (unsigned)hud.offers, (unsigned)hud.pumps, (unsigned)hud.draws,
             (unsigned)hud.blocked, (unsigned)hud.unguarded, (unsigned)hud.silences);
    if (!hud.ever_drawn) {
        log_warning("  the scoreboard NEVER drew: %u offer(s) had no round, %u had no running "
                    "level, %u had nobody asking, %u found no font and %u no screen size",
                    (unsigned)hud.hidden_no_round, (unsigned)hud.hidden_no_level,
                    (unsigned)hud.hidden_quiet, (unsigned)hud.refused_no_font,
                    (unsigned)hud.refused_no_screen);
    } else {
        log_info("  hidden: %u with no round, %u with no running level, %u with nobody asking; "
                 "refused: %u with no font, %u with no screen size",
                 (unsigned)hud.hidden_no_round, (unsigned)hud.hidden_no_level,
                 (unsigned)hud.hidden_quiet, (unsigned)hud.refused_no_font,
                 (unsigned)hud.refused_no_screen);
    }
}
