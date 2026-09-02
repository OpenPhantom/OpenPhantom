/* overlay_draw.c: the panel, painted with the engine's own renderer.
 *
 * The entry points it paints through are resolved in overlay_sites.c; what is here is only
 * what the panel looks like. Every proportion is a multiple of the measured text height and
 * lives in overlay_layout.c, so this file holds colour, order and the shapes themselves.
 */
#include "overlay_draw.h"

#include "dev_menu_size_row.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"
#include "overlay_input.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_sites.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * THE COLOURS, AND THE ONE RULE THAT MAKES THEM WORK
 *
 * EXACTLY ONE RECTANGLE IS TRANSLUCENT: the body. Everything else is opaque.
 *
 * That is arithmetic, not taste. A translucent rectangle drawn on top of another translucent
 * rectangle has a contrast that depends on the scene behind both. The panel used to draw its tabs
 * that way, and over a dark interior the active and the idle tab were fifteen steps apart while
 * over a sunlit scene they were one and a half: the tabs converged to the same colour as the game
 * got brighter. The rule under the title had it worse and inverted outright, ending up darker than
 * the surface it was supposed to separate.
 *
 * With one translucent surface, every contrast inside the panel is fixed and the alpha has exactly
 * one job. What that job buys: with ten percent transmission the surface behind the text sits at
 * about 13 over a dark interior and 37 over a white sky, which holds the text above a ten to one
 * contrast ratio in the worst case. A quarter transmission would look better through and drops the
 * bright case to under six to one, which is where a bitmap font over a MOVING picture shimmers.
 * ============================================================================================ */
#define C_PANEL_BODY    0xE60D0F16u   /* the only translucent one, and this is its whole budget */
#define C_BORDER        0xFF59617Au   /* mid grey: the only value visible against BOTH sides    */
#define C_ACCENT        0xFF5AA0F0u
#define C_BAND_TITLE    0xFF171A24u
#define C_RULE          0xFF3A4256u

#define C_TAB_ON_FILL   0xFF232838u
#define C_TAB_ON_TEXT   0xFFE8EAF2u
#define C_TAB_OFF_TEXT  0xFF8A90A6u

#define C_FIELD_FILL    0xFF090B11u
#define C_FIELD_BORDER  0xFF3A4256u
#define C_PLACEHOLDER   0xFF6B7186u
#define C_TYPED         0xFFE8EAF2u

#define C_GROUP_BAND    0xFF1C2130u
#define C_GROUP_TEXT    0xFFE8EAF2u
#define C_GROUP_HOT     0xFF272D3Eu
#define C_ROW_TEXT      0xFFD2D7E4u   /* below the heading white, so headings outrank rows */
#define C_ROW_TEXT_DIM  0xFF7B8195u
#define C_ROW_HOT       0xFF232838u   /* the same as the active tab: "current" is one colour */
#define C_LEADER        0xFF262B3Au
#define C_LEADER_HOT    0xFF3E4557u

/* ON and OFF get a chip; n/a does not. That is structural rather than decorative: n/a is not a
 * state the cheat is in, it is the absence of the control, so nothing control shaped goes behind
 * it. Telling it apart by colour alone was the failure, and telling it apart by whether a
 * rectangle is there at all cannot fail. */
#define C_CHIP_ON       0xFF3E9E5Cu
#define C_CHIP_ON_TEXT  0xFF0A140Eu   /* near black on green: inverted reads as "this is on" */
#define C_CHIP_OFF      0xFF262B38u
#define C_CHIP_OFF_TEXT 0xFF98A0B4u
#define C_STATE_NA      0xFF6B7186u

/* An action is not a state, on or off, so it gets neither chip colour: the same accent blue the
 * panel already uses for "this is interactive" everywhere else, on the same dark fill an OFF chip
 * uses. Green here would read as "this is currently on", which a fire-once row never is. */
#define C_CHIP_ACTION      0xFF262B38u
#define C_CHIP_ACTION_TEXT 0xFF5AA0F0u

#define C_POINTER       0xFFFFFFFFu

/* In text heights. */
#define NAME_X       2.25f
#define GROUP_X      1.50f
#define EDGE_PAD     0.75f
#define CHIP_PAD     0.50f
#define GUTTER_MIN   2.00f

static const char *const TAB_TITLE[OVERLAY_TAB_COUNT] = { "Original", "OpenPhantom" };
/* The model counts the tabs and the layout stores their spans. Two constants, and nothing but this
 * keeps them the same number. */
_Static_assert((uint32_t)OVERLAY_TAB_COUNT == OVERLAY_LAYOUT_TABS,
               "the model and the layout disagree about how many tabs there are");

/* ============================================================================================ */

bool overlay_draw_screen(float *out_width, float *out_height)
{
    if (!draw_state.resolved || !(*draw_state.screen_w > 0.0f) || !(*draw_state.screen_h > 0.0f)) {
        return false;
    }
    if (out_width != NULL) {
        *out_width = *draw_state.screen_w;
    }
    if (out_height != NULL) {
        *out_height = *draw_state.screen_h;
    }
    return true;
}

/* WHICH MODE STARTS A STRING WHERE IT IS PUT IS NOT DECIDABLE FROM THE BYTES. The three modes write
 * 1, 2 and 4 into the same field, and what that field means is a layer further down. Mode 0 was
 * tried and observed to CENTRE a string on the position it is given, so one of the other two starts
 * it there and the third ends it there. Rather than guess twice, it is a setting. */
static int32_t align_mode = 1;

/* Clamped rather than refused: a number somebody typed into an ini should move the panel toward
 * what they meant, and a panel that silently kept its old size because the value was out of range
 * would read as the setting not working at all. Below a third the text stops being legible and
 * above four the panel stops fitting anything, so those are the ends. */
void overlay_draw_set_align(int32_t mode)
{
    align_mode = mode;
}

/* THE FONT HAS TO BE SET UP BEFORE IT IS MEASURED, AND THAT WAS THE BUG.
 *
 * font3d_measureChar answers for the font that is selected and the glyph scale that is set right
 * now. The panel measured first and set up afterwards, so it measured somebody else's font at
 * somebody else's scale and then drew with its own, and every band was sized from a number that
 * did not describe the text in it.
 *
 * So both paths go through here. It is called once per string and once per measurement, which is
 * the same thing the engine does for its own text: nothing puts these back, so anything drawn in
 * between has already changed them. */
static void prepare_font(void)
{
    draw_state.select(*draw_state.font_slot);
    draw_state.set_align(align_mode);

    /* Positions in pixels: the font's own space runs 0 to 1 across the screen, so dividing by the
     * screen size is what lets everything else here be written in pixels. */
    draw_state.pos_scale(1.0f / *draw_state.screen_w, 1.0f / *draw_state.screen_h);

    /* THE GLYPH SCALE, AND WHY IT IS THIS FRACTION AND NOT ONE.
     *
     * The glyph layer below this one multiplies every glyph by screenWidth/640 and screenHeight/480
     * before it draws or measures: the font is authored for 640 by 480 and grows with the display
     * by construction. This fraction cancels that, which gives text at its authored size in real
     * pixels, the size the game's own menus are read at.
     *
     * One was tried and is three times that on a 1920 wide display, which is unreadable as a panel.
     * The size was never the defect: the defect was measuring the font BEFORE selecting it and
     * setting these scales, so the measurement described a different font than the drawing did.
     * That is fixed above, and changing this at the same time only hid it.
     *
     * DevMenuSize multiplies both, and that is the only place it is applied. Every band, row and
     * the width clamp are multiples of the height this font measures, and both measurements run
     * through this function first, so scaling here scales the whole panel and its text together
     * rather than growing the boxes around text that stayed put. */
    {
        const float panel_scale = dev_menu_size_row_current();

        draw_state.glyph_scale(panel_scale * OVERLAY_AUTHORED_WIDTH / *draw_state.screen_w,
                               panel_scale * OVERLAY_AUTHORED_HEIGHT / *draw_state.screen_h);
    }
}

/* The height of a capital in the font the panel actually draws with. Every band is a multiple of
 * it, and the engine sizes its own centred text the same way, measuring the letter M. */
static float measured_text_height(void)
{
    float width = 0.0f;
    float height = 0.0f;

    prepare_font();
    if (draw_state.measure_char == NULL ||
        draw_state.measure_char((uint8_t)'M', &width, &height) == 0 ||
        !(height > 0.0f)) {
        return 12.0f;             /* the authored size, for a font that will not answer */
    }
    return height;
}

/* The widest thing the list is about to draw, so the box can be built to hold it. Measured with the
 * font's own metric rather than counted in characters, because the font is proportional. */

/* THE SIXTH ARGUMENT IS A FLAG, NOT A LAYER, and this had it backwards once. Non zero draws the
 * shape there and then; zero puts it into the engine's deferred, sorted queue, which is not where a
 * fixed overlay belongs. The game's own menu backdrop passes one. */
#define OVERLAY_DRAW_NOW 1

/* The engine's own numbers for its pointer, taken from the four lines its menus draw it with. */
#define CURSOR_SIZE   32.0f
#define CURSOR_COLOUR 0xF0FFFFFFu
#define CURSOR_FILL   1.0f


static void fill(float x0, float y0, float x1, float y1, uint32_t argb)
{
    draw_state.quad(x0, y0, x1, y1, argb, OVERLAY_DRAW_NOW);
}

/* The scales are set on every string rather than once per paint. Anything else drawn in the same
 * frame, a subtitle or a heads up readout, sets them for itself and does not put them back. */
static void write(const char *what, float x, float y, uint32_t argb)
{
    if (!(*draw_state.screen_w > 0.0f) || !(*draw_state.screen_h > 0.0f)) {
        return;                        /* before the mode is chosen there is nothing to scale to */
    }
    prepare_font();
    draw_state.colour(argb);
    draw_state.text(what, x, y);
}

/* A string, vertically centred in a band of `height` starting at `y`.
 *
 * THE ENGINE'S TEXT GROWS UPWARD FROM THE POSITION IT IS GIVEN. That position is the baseline, not
 * the top edge, which is why treating it as a top left corner put every label at the top of its own
 * band with the rule and the chip sitting under it. Centring a glyph box of one text height in a
 * band therefore puts the baseline at the BOTTOM of that box, not the top. */
static void write_in(const char *what, float x, float y, float height, uint32_t argb)
{
    write(what, x, y + (height + overlay_layout()->text_h) * 0.5f, argb);
}

/* PREPARES THE FONT FIRST, and leaving that out was the defect this whole file was written around.
 *
 * The width of a string depends on the font selected and the glyph scale set at the moment it is
 * asked for, exactly as the height does. Only the height path went through the preparation, so the
 * tab widths and the panel width were measured against whatever font the last thing to draw had
 * left behind. At a wide resolution that came back about two and a half times too large, which is
 * why the tabs overflowed their own panel and the panel overflowed its clamp. */
static float text_width(const char *what)
{
    float width = 0.0f;

    if (draw_state.measure_string != NULL && what != NULL) {
        prepare_font();
        width = draw_state.measure_string(what);
    }
    return (width > 0.0f) ? width : 0.0f;
}

/* A one pixel outlined box is two fills, and it is what turns a hole into a field. The claim that
 * a frame would be four more fills for no gain was wrong: a translucent body has no edge at all
 * over a bright scene, and four opaque pixels are the cheapest edge there is. */
static void fill_outlined(float x0, float y0, float x1, float y1, uint32_t border, uint32_t inner)
{
    const float r = overlay_layout()->rule;

    fill(x0, y0, x1, y1, border);
    fill(x0 + r, y0 + r, x1 - r, y1 - r, inner);
}

/* THERE IS NO CLIPPING, so a name wider than its room draws straight through the state beside it.
 * Shortened against the font's own measure and finished with two dots. */
static const char *fit(const char *what, float room, char *scratch, size_t scratch_size)
{
    size_t length;

    if (what == NULL || room <= 0.0f || text_width(what) <= room) {
        return what;
    }
    for (length = 0; length + 3u < scratch_size && what[length] != 0; ++length) {
        scratch[length] = what[length];
    }
    while (length > 0u) {
        scratch[length] = '.';
        scratch[length + 1u] = '.';
        scratch[length + 2u] = 0;
        if (text_width(scratch) <= room) {
            return scratch;
        }
        --length;
    }
    scratch[0] = 0;
    return scratch;
}

/* Folds `widest` up to whichever of it and every name in [0, count) is largest. Shared because the
 * Original tab now measures two sources rather than one, and a loop copied twice is two chances to
 * change only one of them. */
static float widen_over(float widest, uint32_t count, const char *(*name_of)(uint32_t))
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        const float width = text_width(name_of(i));

        if (width > widest) {
            widest = width;
        }
    }
    return widest;
}

static const char *original_toggle_name(uint32_t i)
{
    return cheats_original_name(i);
}

static const char *original_action_name(uint32_t i)
{
    return cheats_original_actions_name((cheats_action_id_t)i);
}

static const char *openphantom_name(uint32_t i)
{
    return cheats_openphantom_name((cheats_own_id_t)i);
}

/* The widest NAME the tab can ever show, over ALL its rows rather than the visible ones. Measured
 * over the visible ones, the panel would change width on every keystroke and every fold. The
 * Original tab holds two groups now, so both are measured whether or not either is folded open. */
static float widest_name(void)
{
    float widest = 0.0f;

    if (overlay_model_tab() == OVERLAY_TAB_ORIGINAL) {
        widest = widen_over(widest, cheats_original_count(), original_toggle_name);
        widest = widen_over(widest, (uint32_t)CHEATS_ACTION_COUNT, original_action_name);
        return widest;
    }
    return widen_over(widest, (uint32_t)CHEATS_OWN_COUNT, openphantom_name);
}

bool overlay_draw_paint(void)
{
    layout_t    lay;
    float       left;
    float       right;
    float       tab_word[OVERLAY_LAYOUT_TABS];
    float       pointer_x = 0.0f;
    float       pointer_y = 0.0f;
    int32_t     hot;
    uint32_t    i;
    uint32_t    first;             /* the row drawn at the top, see the list below */
    const char *typed;
    char        scratch[OVERLAY_LABEL_MAX + 4];

    if (!overlay_draw_screen(NULL, NULL)) {
        return false;                  /* no display mode: nothing to scale to or draw on */
    }

    for (i = 0; i < OVERLAY_LAYOUT_TABS; ++i) {
        tab_word[i] = text_width(TAB_TITLE[i]);
    }
    overlay_layout_build(measured_text_height(), widest_name(), overlay_model_row_count(),
                         tab_word, *draw_state.screen_w, *draw_state.screen_h);

    lay = *overlay_layout();
    left = lay.left;
    right = lay.left + lay.width;

    overlay_input_pointer(&pointer_x, &pointer_y);
    hot = overlay_draw_row_at(pointer_x, pointer_y);

    /* --- the body, its frame, and the accent cap ------------------------------------------------
     * The cap is the answer to a title with no weight: with one font at one size weight cannot come
     * from the type, so it comes from a saturated bar directly above it, and it is the top border
     * as well, so it costs nothing. */
    fill(left, lay.top, right, lay.top + lay.height, C_PANEL_BODY);
    fill(left, lay.top, right, lay.top + lay.cap_h, C_ACCENT);
    fill(left, lay.top, left + lay.rule, lay.top + lay.height, C_BORDER);
    fill(right - lay.rule, lay.top, right, lay.top + lay.height, C_BORDER);
    fill(left, lay.top + lay.height - lay.rule, right, lay.top + lay.height, C_BORDER);

    /* --- the title, and the one thing a floating panel must say: how to close it ------- */
    fill(left + lay.rule, lay.top + lay.cap_h, right - lay.rule, lay.top + lay.title_h,
         C_BAND_TITLE);
    fill(left, lay.top + lay.title_h - lay.rule, right, lay.top + lay.title_h, C_RULE);
    write_in("Cheatmenu", left + EDGE_PAD * lay.text_h, lay.top + lay.cap_h,
             lay.title_h - lay.cap_h, C_ACCENT);
    {
        /* A queued swap outranks the usual hint while one is pending: it is the more useful thing
         * to say right now, and the row itself already reads QUEUED, so this is confirming what
         * closing does rather than introducing new information. Kept short rather than naming the
         * character: the panel's width is sized to fit the longest ROW label, not this hint plus
         * "Cheatmenu" both fitting the title band together, and a name-carrying hint here would be
         * the one string in the whole panel that was never checked against that budget. */
        const char *hint = (cheats_original_actions_pending_label() != NULL)
                          ? "Close applies the queued swap" : "Esc closes";

        write_in(hint, right - EDGE_PAD * lay.text_h - text_width(hint), lay.top + lay.cap_h,
                 lay.title_h - lay.cap_h, C_ROW_TEXT_DIM);
    }

    /* --- the tabs. The inactive one gets NO fill, and that is the move that makes them read as
     * tabs: one rectangle among words is unambiguously the selected one at any scene brightness,
     * while a second rectangle both competes with the first and converges on it as the game gets
     * brighter. --- */
    fill(left, lay.top + lay.search_top - lay.rule, right, lay.top + lay.search_top, C_RULE);
    for (i = 0; i < OVERLAY_LAYOUT_TABS; ++i) {
        const bool  active = ((uint32_t)overlay_model_tab() == i);
        const float x0 = left + lay.tab_x0[i];
        const float y0 = lay.top + lay.tabs_top;

        if (active) {
            fill(x0, y0, x0 + lay.tab_w[i], y0 + lay.tab_h, C_TAB_ON_FILL);
            fill(x0, y0 + lay.tab_h - lay.rule * 3.0f, x0 + lay.tab_w[i], y0 + lay.tab_h, C_ACCENT);
        }
        write_in(TAB_TITLE[i], x0 + lay.text_h, y0, lay.tab_h,
                 active ? C_TAB_ON_TEXT : C_TAB_OFF_TEXT);
    }

    /* --- the search field. Opaque with a light border rather than a translucent black rectangle:
     * the same shape, and the difference between a well and a hole.
     *
     * Typing only reaches this box once it has been clicked into - see overlay_input.c's click()
     * and the focus gate in handle(). The border and the caret are what say so: the accent border
     * and the caret both only appear once a click actually landed here, so the box never LOOKS
     * ready to type into before it is. --- */
    {
        const bool  focused = overlay_input_search_focused();
        const float fy0 = lay.top + lay.search_top + (lay.search_h - 1.5f * lay.text_h) * 0.5f;
        const float fy1 = fy0 + 1.5f * lay.text_h;
        const float tx = left + (EDGE_PAD + 0.5f) * lay.text_h;
        float       caret;

        fill_outlined(left + EDGE_PAD * lay.text_h, fy0, right - EDGE_PAD * lay.text_h, fy1,
                      focused ? C_ACCENT : C_FIELD_BORDER, C_FIELD_FILL);
        typed = overlay_model_search();
        if (typed[0] != 0) {
            write_in(typed, tx, fy0, fy1 - fy0, C_TYPED);
            caret = tx + text_width(typed) + lay.rule * 2.0f;
        } else {
            write_in("Search", tx + 0.5f * lay.text_h, fy0, fy1 - fy0, C_PLACEHOLDER);
            caret = tx;
        }
        if (focused) {
            fill(caret, fy0 + 0.25f * lay.text_h, caret + lay.rule * 2.0f,
                 fy0 + 1.25f * lay.text_h, C_ACCENT);
        }
    }
    fill(left, lay.top + lay.rows_top - lay.rule, right, lay.top + lay.rows_top, C_RULE);

    /* --- the list ------------------------------------------------------------------------------
     * DRAWN BY POSITION, INDEXED BY ROW. `i` counts down the panel and `index` counts down the tab,
     * and they differ by wherever the list is scrolled to. Everything below keys off `row`, so only
     * the two lines that turn a position into a row have to know scrolling exists. */
    first = overlay_model_scroll(lay.visible_rows);
    for (i = 0; i < lay.visible_rows; ++i) {
        overlay_row_t  row;
        const uint32_t index = first + i;
        const float    y = lay.top + lay.rows_top + (float)i * lay.row_h;
        const bool     is_hot = ((int32_t)index == hot);

        if (!overlay_model_row(index, &row)) {
            break;
        }

        if (row.kind == OVERLAY_ROW_GROUP) {
            const float cy = y + lay.row_h * 0.5f;
            const float mx = left + EDGE_PAD * lay.text_h;

            fill(left + lay.rule, y, right - lay.rule, y + lay.row_h,
                 is_hot ? C_GROUP_HOT : C_GROUP_BAND);
            fill(left + lay.rule, y, right - lay.rule, y + lay.rule, C_RULE);
            fill(left + lay.rule, y, left + lay.rule + 0.25f * lay.text_h, y + lay.row_h, C_ACCENT);

            /* Two rectangles rather than a plus and a minus: in a fixed bitmap font a hyphen is a
             * smudge, and two fills give a marker at exactly the size and weight wanted. */
            fill(mx, cy - lay.rule, mx + 0.5f * lay.text_h, cy + lay.rule, C_ACCENT);
            if (!row.expanded) {
                fill(mx + 0.25f * lay.text_h - lay.rule, cy - 0.25f * lay.text_h,
                     mx + 0.25f * lay.text_h + lay.rule, cy + 0.25f * lay.text_h, C_ACCENT);
            }
            write_in(row.label, left + GROUP_X * lay.text_h, y, lay.row_h, C_GROUP_TEXT);
            continue;
        }

        if (row.kind == OVERLAY_ROW_INFO) {
            /* Full row width, no chip and no hover fill - it is a note attached to the row above
             * it, not a control of its own, and dimmed the same way an unavailable row's name is
             * so it reads as secondary at a glance rather than as another cheat to look for. */
            const float name_x = left + NAME_X * lay.text_h;
            const float room = right - EDGE_PAD * lay.text_h - name_x;
            const char *label = fit(row.label, room, scratch, sizeof scratch);

            write_in(label, name_x, y, lay.row_h, C_ROW_TEXT_DIM);
            continue;
        }

        if (is_hot) {
            fill(left + lay.rule, y, right - lay.rule, y + lay.row_h, C_ROW_HOT);
            fill(left + lay.rule, y, left + lay.rule * 4.0f, y + lay.row_h, C_ACCENT);
        }

        {
            /* A hotkey row, and now a value row too, get the action's own chip styling - both are
             * buttons that start something rather than a plain switch, the same as ACTION - but
             * never fall through to "RUN": source_row() always populates value for either kind
             * (the bound key's name / "Set" / "...", or the current number / what is being typed),
             * so that arm of the word choice below is dead for both and kept only because ACTION
             * still needs it. */
            const bool  is_action = (row.kind == OVERLAY_ROW_ACTION || row.kind == OVERLAY_ROW_HOTKEY ||
                                     row.kind == OVERLAY_ROW_VALUE);
            const char *word = !row.available    ? "n/a"
                             : row.pending        ? "QUEUED"
                             : row.value[0] != 0  ? row.value    /* e.g. the graphics detail level */
                             : is_action          ? "RUN"
                             : row.on             ? "ON" : "OFF";
            const float chip_w = text_width(word) + lay.text_h;
            const float chip_x1 = right - EDGE_PAD * lay.text_h;
            const float chip_x0 = chip_x1 - chip_w;
            const float name_x = left + NAME_X * lay.text_h;
            const float room = chip_x0 - GUTTER_MIN * lay.text_h - name_x;
            const char *label = fit(row.label, room, scratch, sizeof scratch);
            const float lead_x0 = name_x + text_width(label) + 0.5f * lay.text_h;
            const float lead_x1 = chip_x0 - 0.5f * lay.text_h;

            /* The leader is drawn only where the gap actually is, between THIS name and THIS row's
             * state, so it is self evidently that row's connector. Below the minimum gutter the two
             * are already adjacent and a rule between them is clutter; the same test is what keeps
             * an inverted rectangle from ever reaching the engine. */
            if (lead_x1 - lead_x0 >= GUTTER_MIN * lay.text_h) {
                fill(lead_x0, y + (lay.row_h - lay.rule) * 0.5f, lead_x1,
                     y + (lay.row_h + lay.rule) * 0.5f, is_hot ? C_LEADER_HOT : C_LEADER);
            }

            write_in(label, name_x, y, lay.row_h, row.available ? C_ROW_TEXT : C_ROW_TEXT_DIM);

            if (!row.available) {
                write_in(word, chip_x0 + CHIP_PAD * lay.text_h, y, lay.row_h, C_STATE_NA);
            } else if (row.pending) {
                /* Queued reads as "this will happen", the same claim ON already makes, so it gets
                 * ON's own colour rather than a third one - a fourth chip colour buys nothing a
                 * different WORD does not already say on its own. */
                fill(chip_x0, y + 0.1875f * lay.row_h, chip_x1, y + 0.8125f * lay.row_h, C_CHIP_ON);
                write_in(word, chip_x0 + CHIP_PAD * lay.text_h, y, lay.row_h, C_CHIP_ON_TEXT);
            } else if (is_action) {
                fill(chip_x0, y + 0.1875f * lay.row_h, chip_x1, y + 0.8125f * lay.row_h,
                     C_CHIP_ACTION);
                write_in(word, chip_x0 + CHIP_PAD * lay.text_h, y, lay.row_h, C_CHIP_ACTION_TEXT);
            } else {
                fill(chip_x0, y + 0.1875f * lay.row_h, chip_x1, y + 0.8125f * lay.row_h,
                     row.on ? C_CHIP_ON : C_CHIP_OFF);
                write_in(word, chip_x0 + CHIP_PAD * lay.text_h, y, lay.row_h,
                         row.on ? C_CHIP_ON_TEXT : C_CHIP_OFF_TEXT);
            }
        }
    }

    /* --- the scroll indicator, and only when there is something to indicate -----------------------
     * A thumb on the inside of the right border, as long a fraction of the track as the visible
     * rows are of all of them, and as far down it as the list is scrolled. It is drawn rather than
     * clickable on purpose: the wheel and the keys already move the list, and a draggable bar this
     * thin would be a target the game's own pointer is not precise enough to hit.
     *
     * Absent entirely when everything fits, so the ordinary panel is exactly what it always was. */
    {
        const uint32_t count = overlay_model_row_count();

        if (count > lay.visible_rows && lay.visible_rows > 0u) {
            const float track_y0 = lay.top + lay.rows_top;
            const float track_y1 = track_y0 + (float)lay.visible_rows * lay.row_h;
            const float track_h  = track_y1 - track_y0;
            const float w        = lay.rule * 3.0f;
            const float x1       = right - lay.rule;
            const float x0       = x1 - w;
            float       thumb_h  = track_h * (float)lay.visible_rows / (float)count;
            float       thumb_y;

            if (thumb_h < lay.text_h) {
                thumb_h = lay.text_h;         /* never so short it reads as a speck */
            }
            thumb_y = track_y0 + (track_h - thumb_h) *
                      ((float)first / (float)(count - lay.visible_rows));

            fill(x0, track_y0, x1, track_y1, C_RULE);
            fill(x0, thumb_y, x1, thumb_y + thumb_h, C_ACCENT);
        }
    }

    /* --- last, so nothing covers it: the game's own pointer, in its own texture -------- */
    if (draw_state.draw_sprite != NULL && draw_state.cursor_texture != NULL &&
        *draw_state.cursor_texture != NULL) {
        draw_state.draw_sprite(*draw_state.cursor_texture,
                               pointer_x, pointer_x + CURSOR_SIZE,
                               pointer_y, pointer_y + CURSOR_SIZE,
                               CURSOR_COLOUR, CURSOR_FILL);
    } else {
        const float arm = 0.4f * lay.text_h;

        fill(pointer_x - arm, pointer_y - lay.rule, pointer_x + arm, pointer_y + lay.rule,
             C_POINTER);
        fill(pointer_x - lay.rule, pointer_y - arm, pointer_x + lay.rule, pointer_y + arm,
             C_POINTER);
    }
    return true;
}
