/* overlay_draw.c: the panel, painted with the engine's own renderer.
 *
 * The entry points it paints through are resolved in overlay_sites.c; what is here is only
 * what the panel looks like. Every proportion is a multiple of the measured text height and
 * lives in overlay_layout.c, so this file holds colour, order and the shapes themselves.
 *
 * SIZE NOTE. Over six hundred lines, and it crossed on the change that made the panel size itself
 * to its widest name and chip rather than to a fixed allowance. What is long is the reasoning
 * attached to the colours and the shapes: why a chip is absent rather than grey, why an inactive
 * tab gets no fill, why the leader is drawn only across the gap it belongs to. Each of those was
 * arrived at by trying the other thing, and deleting the account of it would leave a wall of
 * rectangles nobody can safely change.
 *
 * Two seams have been taken. What the panel has to be WIDE enough for, which is the word in each
 * chip and the widest name and chip on screen, went to overlay_chip.c: a measurement rather than a
 * drawing, three callers had to agree about it, and two of them were once separate copies of the
 * same conditional.
 *
 * The second is the one this note named for a while and is now taken: the title band, the tabs and
 * the search field share nothing with the row list below them but the layout they are measured
 * against, and they went out whole to overlay_frame.c, with the footer that joined them. The
 * palette went with them, to overlay_look.h, because both files paint with it and a second copy of
 * a colour is a colour that drifts. The marks themselves stayed here and are exported: a fill, a
 * framed fill, a string centred in a band, and the width of a string.
 *
 * The seam, if it grows again, is the row painters: the group band, the track, the note and the
 * value row are four blocks that share only the layout and the palette.
 */
#include "overlay_draw.h"

#include "overlay_frame.h"
#include "overlay_look.h"

#include "dev_menu_size_row.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"
#include "overlay_chip.h"
#include "overlay_choice.h"
#include "overlay_input.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_number.h"
#include "overlay_reason.h"
#include "overlay_slider.h"
#include "overlay_sites.h"

#include "common/screen_fill.h"

#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* In text heights. */
#define NAME_X       2.25f
#define GROUP_X      1.50f
#define CHIP_PAD     0.50f
/* The mark a choice carries, and where it sits: inside the indent every row already has, so a
 * list that becomes a choice moves no text. NAME_X is 2.25, so the mark ends half a text height
 * short of the name and begins half a text height inside the panel's own margin. */
#define MARK_X       1.25f
#define MARK_SIZE    0.50f

/* Where each visible row's slider track was drawn, so a click can be turned back into a fraction.
 *
 * Filled while painting and read by overlay_draw_slider_at below, which is the same relationship
 * overlay_draw_row_at already has with the layout: the geometry is settled once, while drawing,
 * and the hit test asks what was settled rather than recomputing it from the label widths and
 * hoping the two agree. They would not for long, since the track's ends depend on the fitted label,
 * which depends on the panel width.
 *
 * Indexed by position down the panel, not by row, and cleared on every paint, so a row that
 * scrolled away cannot be grabbed through a stale entry. */
#define TRACK_SLOTS 128u

static struct {
    bool  present;
    float x0;
    float x1;
    /* The Default button at the end of the same row, when there was room for one. Equal when there
     * was not, which is the same "no box here" a track of no width already says. */
    float def_x0;
    float def_x1;
} tracks[TRACK_SLOTS];

/* And the boxes a row of segments was drawn with, kept the same way and for the same reason: a
 * box ends where its own word ends, so a hit test that worked the ends out a second time would
 * disagree with the paint the moment either changed. */
static struct {
    uint32_t count;
    float    edges[OVERLAY_CHOICE_SEGMENTS_MAX + 1u];
} segments[TRACK_SLOTS];
#define GUTTER_MIN   2.00f
/* The shortest track still worth having. Below about six capitals there is no position on
 * it a value can be put on, and a track that cannot be set is worse than a button that is
 * not drawn, so overlay_number_fit() gives up the Default first and the number second
 * rather than letting the track shrink under this. */
#define TRACK_LEAST  6.00f
#define TRACK_GAP    0.50f

/* The number column is as wide as this string, measured in the panel's own font, and every
 * track row gets the same width whatever its own number says. Seven cells covers what the
 * rows can show: 240, 1.21, 0.030, 10.00x. A value longer than the column is still drawn,
 * right aligned, and runs left into the gap rather than being cut; none does today, and a
 * number that did would be visible at once rather than silently short. */
#define NUMBER_COL   "0000.00"

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

/* Which mode starts a string where it is put is not decidable from the bytes. The three modes
 * write 1, 2 and 4 into the same field, and what that field means is a layer further down. Mode 0
 * was tried and observed to CENTRE a string on the position it is given, so one of the other two
 * starts it there and the third ends it there. Rather than guess twice, it is a setting. */
static int32_t align_mode = 1;

/* Not validated here, and not by the caller either: dev_overlay.c hands the TextAlign ini value
 * through unchanged, so a number outside 0 to 2 reaches the font layer's own setter as it is. What
 * that setter does with one has not been established; the three modes are the whole documented
 * range. */
void overlay_draw_set_align(int32_t mode)
{
    align_mode = mode;
}

/* The font has to be set up before it is measured, and that was the bug.
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

    /* The glyph scale, and why it is this fraction and not one.
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
     * DevMenuSize multiplies both, and is applied nowhere else. Every band, row and
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

/* The engine's own numbers for its pointer, taken from the four lines its menus draw it with. */
#define CURSOR_SIZE   32.0f
#define CURSOR_COLOUR 0xF0FFFFFFu
#define CURSOR_FILL   1.0f


/* Through common/screen_fill.c, which calls the routine's own three calls with vertices every
 * driver draws; the routine's own vertices lost every fill on an Intel UHD laptop. */
void overlay_draw_fill(float x0, float y0, float x1, float y1, uint32_t argb)
{
    screen_fill(x0, y0, x1, y1, argb);
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
 * The engine's text grows upward from the position it is given. That position is the baseline, not
 * the top edge, so treating it as a top left corner put every label at the top of its own band
 * with the rule and the chip sitting under it. Centring a glyph box of one text height in a
 * band therefore puts the baseline at the BOTTOM of that box, not the top. */
void overlay_draw_write_in(const char *what, float x, float y, float height, uint32_t argb)
{
    write(what, x, y + (height + overlay_layout()->text_h) * 0.5f, argb);
}

/* Prepares the font first, and leaving that out was the defect this whole file was written around.
 *
 * The width of a string depends on the font selected and the glyph scale set at the moment it is
 * asked for, exactly as the height does. Only the height path went through the preparation, so the
 * tab widths and the panel width were measured against whatever font the last thing to draw had
 * left behind. At a wide resolution that came back about two and a half times too large, which is
 * why the tabs overflowed their own panel and the panel overflowed its clamp. */
float overlay_draw_width(const char *what)
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
void overlay_draw_outlined(float x0, float y0, float x1, float y1, uint32_t border, uint32_t inner)
{
    const float r = overlay_layout()->rule;

    overlay_draw_fill(x0, y0, x1, y1, border);
    overlay_draw_fill(x0 + r, y0 + r, x1 - r, y1 - r, inner);
}

/* There is no clipping, so a name wider than its room draws straight through the state beside it.
 * Shortened against the font's own measure and finished with two dots. */
const char *overlay_draw_fit(const char *what, float room, char *scratch, size_t scratch_size)
{
    size_t length;

    if (what == NULL || room <= 0.0f || overlay_draw_width(what) <= room) {
        return what;
    }
    for (length = 0; length + 3u < scratch_size && what[length] != 0; ++length) {
        scratch[length] = what[length];
    }
    while (length > 0u) {
        scratch[length] = '.';
        scratch[length + 1u] = '.';
        scratch[length + 2u] = 0;
        if (overlay_draw_width(scratch) <= room) {
            return scratch;
        }
        --length;
    }
    scratch[0] = 0;
    return scratch;
}

/* A group heading: the band, the accent, the expand marker and the label. */
static void paint_group_row(const layout_t *lay, float left, float right, float y,
                            const overlay_row_t *row, bool is_hot)
{
    const float cy = y + lay->row_h * 0.5f;
    const float mx = left + OVERLAY_EDGE_PAD * lay->text_h;

    overlay_draw_fill(left + lay->rule, y, right - lay->rule, y + lay->row_h,
         is_hot ? C_GROUP_HOT : C_GROUP_BAND);
    overlay_draw_fill(left + lay->rule, y, right - lay->rule, y + lay->rule, C_RULE);
    overlay_draw_fill(left + lay->rule, y, left + lay->rule + 0.25f * lay->text_h, y + lay->row_h,
         C_ACCENT);

    /* Two rectangles rather than a plus and a minus: in a fixed bitmap font a hyphen is a
     * smudge, and two fills give a marker at exactly the size and weight wanted. */
    overlay_draw_fill(mx, cy - lay->rule, mx + 0.5f * lay->text_h, cy + lay->rule, C_ACCENT);
    if (!row->expanded) {
        overlay_draw_fill(mx + 0.25f * lay->text_h - lay->rule, cy - 0.25f * lay->text_h,
             mx + 0.25f * lay->text_h + lay->rule, cy + 0.25f * lay->text_h, C_ACCENT);
    }
    overlay_draw_write_in(row->label, left + GROUP_X * lay->text_h, y, lay->row_h, C_GROUP_TEXT);

    /* What the group holds, so a folded band answers for itself. Written as text and not as a
     * filled chip: the band is already a filled rectangle, and a second one inside it reads as a
     * button on a heading that is not one. The colour carries the difference instead, which is the
     * same green a switched-on chip uses and the same grey a taken row's word uses. Green is
     * spent here and on the ON chip and nowhere else, which is the whole of that role. */
    if (row->value[0] != 0) {
        const float x1 = right - OVERLAY_EDGE_PAD * lay->text_h;

        /* A heading's `on` is what its own word claims, set where the word is written. This used
         * to spell the finished text back apart, "O" then "N" or a leading digit, which is a
         * second predicate for a state the writer already knew and would have gone wrong the
         * first time a band said anything else. */
        overlay_draw_write_in(row->value, x1 - overlay_draw_width(row->value), y, lay->row_h,
                 row->on ? C_CHIP_ON : C_STATE_NA);
    }
}

static void paint_slider_row(const layout_t *lay, float left, float right, float y,
                             uint32_t index, uint32_t slot, const overlay_row_t *row,
                             bool is_hot)
{
    /* A line of its own, under the value it drives, and running nearly the panel's width.
     * The first version squeezed the track into the gap between the name and the chip,
     * which put it within a few pixels of both: it was fiddly to grab, and at the panel's
     * smaller sizes it read as though it were striking the name through. A row costs one
     * line and buys a target several times longer.
     *
     * Indented to the same column as the note under the draw distance, so it is plainly
     * attached to the row above rather than a control of its own. */
    const float x0   = left + NAME_X * lay->text_h;
    const float edge = right - OVERLAY_EDGE_PAD * lay->text_h;
    const float gap  = TRACK_GAP * lay->text_h;
    const float mid  = y + lay->row_h * 0.5f;
    const float half = lay->rule * 1.5f;
    const float grip = lay->text_h * 0.30f;
    /* The number reads at the end of the track as well as in the chip on the row above,
     * because the eye that is on a handle should not have to travel a row and the width of
     * the panel to see what it is setting. Both come from one reading: from the file with
     * no hand on the track, and from the hand's own fraction while there is one. */
    const float value_w = overlay_draw_width(row->value);
    const float number_w = overlay_draw_width(NUMBER_COL);
    const float default_w = overlay_draw_width(OVERLAY_DEFAULT_WORD) + lay->text_h;
    overlay_number_t limits;
    const bool  has_default = overlay_model_slider_limits(index, &limits) &&
                              limits.has_standard;
    overlay_number_columns_t col;
    float       x1;
    float       shown = row->fraction;
    int32_t     drag_row = -1;
    float       drag_fraction = 0.0f;
    float       at;

    if (!row->available) {
        return;
    }
    /* The column width and not this row's number, which is the whole of the change: every
     * track on the tab now ends on one line, every number ends on another, and Default
     * stands in the same place on all of them instead of wherever the number left it. */
    if (!overlay_number_columns(x0, edge, TRACK_LEAST * lay->text_h, number_w, default_w,
                                gap, has_default, &col)) {
        return;
    }
    x1 = col.track_x1;
    /* While THIS row is the one being dragged the handle comes from the pointer rather than
     * from the value read back out of the settings file. The write is throttled and the
     * pointer is not, so drawing from the file would move the handle in thirty steps a
     * second against a hand moving in sixty. */
    if (overlay_slider_held(&drag_row, &drag_fraction) && drag_row == (int32_t)index) {
        shown = drag_fraction;
    }
    at = x0 + (x1 - x0) * shown;

    overlay_draw_fill(x0, mid - half, x1, mid + half, C_CHIP_OFF);
    /* The part behind the grip and the grip itself are the accent, because both say the same
     * thing the selection frame says: this is where the value stands. Dimmed while the row is
     * not the current one, so eight tracks down a tab do not all shout at once. The dim is the
     * SAME hue and not a grey, so a track reads as a track either way. */
    overlay_draw_fill(x0, mid - half, at, mid + half, is_hot ? C_ACCENT : C_ACCENT_DIM);
    overlay_draw_fill(at - grip * 0.5f, y + 0.22f * lay->row_h, at + grip * 0.5f,
         y + 0.78f * lay->row_h, is_hot ? C_ACCENT : C_ACCENT_DIM);

    if (col.fit != OVERLAY_NUMBER_FIT_NONE) {
        /* Right aligned in the column, so the last digit of every number falls on one line
         * and the point of every number that has one falls on the next line in. */
        overlay_draw_write_in(row->value, col.number_x1 - value_w, y, lay->row_h, C_ROW_TEXT);
    }
    if (col.fit == OVERLAY_NUMBER_FIT_BOTH) {
        /* An action's own chip, because that is what it is: pressing it runs something
         * once. Drawn the same way the action rows above it are, so a reader needs no new
         * shape to recognise a button. */
        overlay_draw_fill(col.default_x0, y + 0.1875f * lay->row_h, col.default_x1,
             y + 0.8125f * lay->row_h, C_CHIP_ACTION);
        overlay_draw_write_in(OVERLAY_DEFAULT_WORD, col.default_x0 + CHIP_PAD * lay->text_h, y,
             lay->row_h, C_CHIP_ACTION_TEXT);
    }

    if (slot < TRACK_SLOTS) {
        tracks[slot].present = true;
        tracks[slot].x0 = x0;
        tracks[slot].x1 = x1;
        tracks[slot].def_x0 = col.default_x0;
        tracks[slot].def_x1 = col.default_x1;
    }
}

/* The mark left of a choice's name: filled for the chosen entry, an outline for the rest.
 *
 * Not green, and not a chip. Green is this panel's word for a switch that is on, and one entry of
 * a list is not switched on, it is the one picked; five window shapes drawn as five switches, four
 * of them reading OFF, said the window had five settings when it has one with five values. The
 * filled one is the accent for that reason: picked is the same claim the chosen segment makes.
 * The empty ones stay grey, and filled against outline is what carries the state once the
 * colour is taken away. */
static void paint_choice_mark(const layout_t *lay, float left, float y, const overlay_row_t *row)
{
    const float    size = MARK_SIZE * lay->text_h;
    const float    x0   = left + MARK_X * lay->text_h;
    const float    y0   = y + (lay->row_h - size) * 0.5f;
    const uint32_t ink  = row->available ? C_ROW_TEXT_DIM : C_STATE_NA;

    if (row->on) {
        overlay_draw_fill(x0, y0, x0 + size, y0 + size,
             row->available ? C_ACCENT : C_STATE_NA);
    } else {
        /* The dark an OFF chip is filled with, so an empty mark reads as a control that is there
         * and not as a hole in the panel: the body behind it is the one translucent surface here
         * and filling it again would only darken it. */
        overlay_draw_outlined(x0, y0, x0 + size, y0 + size, ink, C_CHIP_OFF);
    }
}

/* The words of a segment row, drawn where the chip would be, and the boxes they were drawn in
 * kept for the hit test. `slot` is the line down the panel, the same index the tracks use. */
static void paint_segments(const layout_t *lay, float chip_x1, float y, uint32_t slot,
                           const overlay_row_t *row)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    float       widths[OVERLAY_CHOICE_SEGMENTS_MAX];
    float       edges[OVERLAY_CHOICE_SEGMENTS_MAX + 1u];
    uint32_t    count = overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX);
    const float y0 = y + 0.1875f * lay->row_h;
    const float y1 = y + 0.8125f * lay->row_h;
    uint32_t    i;

    for (i = 0; i < count; ++i) {
        widths[i] = overlay_draw_width(words[i]);
    }
    if (overlay_choice_edges(widths, count, CHIP_PAD * lay->text_h, chip_x1, edges,
                             OVERLAY_CHOICE_SEGMENTS_MAX + 1u) == 0u) {
        return;
    }
    overlay_draw_fill(edges[0], y0, edges[count], y1, C_CHIP_OFF);
    for (i = 0; i < count; ++i) {
        const bool picked = overlay_choice_is_chosen(row, i);

        if (picked) {
            /* The accent, which is the panel's one word for "this is the one in force", and not
             * green: green says a switch is on, and one word out of five is not switched on, it
             * is picked. The fill is also the only box among the words that has one, so the
             * choice survives the colour being taken away. */
            overlay_draw_fill(edges[i], y0, edges[i + 1u], y1, C_ACCENT);
        } else if (i > 0u) {
            overlay_draw_fill(edges[i] - lay->rule * 0.5f, y0, edges[i] + lay->rule * 0.5f, y1,
                 C_RULE);      /* where one word ends and the next begins */
        }
        overlay_draw_write_in(words[i], edges[i] + CHIP_PAD * lay->text_h, y, lay->row_h,
                 picked ? C_ACCENT_TEXT : C_CHIP_OFF_TEXT);
    }
    if (slot < TRACK_SLOTS) {
        segments[slot].count = count;
        for (i = 0; i <= count; ++i) {
            segments[slot].edges[i] = edges[i];
        }
    }
}

/* What the words of a segment row cost, so the name beside them is fitted against what is left.
 * 0 for any other row, and for one whose words cannot be had. */
static float segment_width(const layout_t *lay, const overlay_row_t *row)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    uint32_t    count = overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX);
    float       total = 0.0f;
    uint32_t    i;

    for (i = 0; i < count; ++i) {
        total += overlay_draw_width(words[i]) + CHIP_PAD * 2.0f * lay->text_h;
    }
    return total;
}

static void paint_info_row(const layout_t *lay, float left, float right, float y,
                           const overlay_row_t *row, char *scratch, size_t scratch_size)
{
    /* Full row width, no chip and no hover fill, because it is a note attached to the row
     * above it, not a control of its own, and dimmed the same way an unavailable row's
     * name is so it reads as secondary at a glance rather than as another cheat to look
     * for. */
    const float name_x = left + NAME_X * lay->text_h;
    const float room = right - OVERLAY_EDGE_PAD * lay->text_h - name_x;
    const char *label = overlay_draw_fit(row->label, room, scratch, scratch_size);

    /* A refusal is the one note that is not secondary: it is the answer to something the player
     * just tried. Every other note stays the dim grey it has always been. */
    overlay_draw_write_in(label, name_x, y, lay->row_h, row->warn ? C_WARN : C_ROW_TEXT_DIM);
}

/* Every other row: the hover, the name, the leader and whatever stands on the right, which is a
 * chip, or the words of a choice, or nothing at all. */
static void paint_value_row(const layout_t *lay, float left, float right, float y, uint32_t slot,
                            const overlay_row_t *row, bool is_hot, char *scratch,
                            size_t scratch_size)
{
    if (is_hot) {
        overlay_draw_fill(left + lay->rule, y, right - lay->rule, y + lay->row_h, C_ROW_HOT);
    }

    const bool  is_action = overlay_chip_is_button(row);
    const bool  is_strip = overlay_choice_is_strip(row);
    const char *word = overlay_chip_word(row);
    /* The room on the right is the strip for a row of segments, a chip for a row with a word, and
     * nothing for a choice that has neither: a chip drawn around an empty word is an empty box. */
    const float chip_w = is_strip          ? segment_width(lay, row)
                       : (word[0] != '\0') ? overlay_draw_width(word) + lay->text_h
                                           : 0.0f;
    const float chip_x1 = right - OVERLAY_EDGE_PAD * lay->text_h;
    const float chip_x0 = chip_x1 - chip_w;
    const float name_x = left + NAME_X * lay->text_h;
    const float room = chip_x0 - GUTTER_MIN * lay->text_h - name_x;
    const char *label = overlay_draw_fit(row->label, room, scratch, scratch_size);
    const float lead_x0 = name_x + overlay_draw_width(label) + 0.5f * lay->text_h;
    const float lead_x1 = chip_x0 - 0.5f * lay->text_h;

    /* The leader is drawn only where the gap actually is, between THIS name and THIS row's
     * state, so it is self evidently that row's connector. Below the minimum gutter the two
     * are already adjacent and a rule between them is clutter; the same test is what keeps
     * an inverted rectangle from ever reaching the engine. A row with nothing on its right,
     * which is a choice whose state is the mark beside its name, gets none: a rule running to
     * the panel's edge and ending at nothing reads as a chip that failed to draw. */
    if (chip_w > 0.0f && lead_x1 - lead_x0 >= GUTTER_MIN * lay->text_h) {
        overlay_draw_fill(lead_x0, y + (lay->row_h - lay->rule) * 0.5f, lead_x1,
             y + (lay->row_h + lay->rule) * 0.5f, is_hot ? C_LEADER_HOT : C_LEADER);
    }

    overlay_draw_write_in(label, name_x, y, lay->row_h,
         row->available ? C_ROW_TEXT : C_ROW_TEXT_DIM);

    if (row->kind == OVERLAY_ROW_CHOICE) {
        paint_choice_mark(lay, left, y, row);
    }
    if (is_strip) {
        paint_segments(lay, chip_x1, y, slot, row);
        return;
    }
    if (word[0] == '\0') {
        return;            /* a choice that can be used says what it is beside its name */
    }
    if (!row->available) {
        overlay_draw_write_in(word, chip_x0 + CHIP_PAD * lay->text_h, y, lay->row_h, C_STATE_NA);
    } else if (row->pending) {
        /* Queued reads as "this will happen", the same claim ON already makes, so it gets
         * ON's own colour rather than a third one; a fourth chip colour buys nothing a
         * different WORD does not already say on its own. */
        overlay_draw_fill(chip_x0, y + 0.1875f * lay->row_h, chip_x1, y + 0.8125f * lay->row_h,
             C_CHIP_ON);
        overlay_draw_write_in(word, chip_x0 + CHIP_PAD * lay->text_h, y, lay->row_h,
             C_CHIP_ON_TEXT);
    } else if (is_action) {
        overlay_draw_fill(chip_x0, y + 0.1875f * lay->row_h, chip_x1, y + 0.8125f * lay->row_h,
             C_CHIP_ACTION);
        overlay_draw_write_in(word, chip_x0 + CHIP_PAD * lay->text_h, y, lay->row_h,
             C_CHIP_ACTION_TEXT);
    } else {
        overlay_draw_fill(chip_x0, y + 0.1875f * lay->row_h, chip_x1, y + 0.8125f * lay->row_h,
             row->on ? C_CHIP_ON : C_CHIP_OFF);
        overlay_draw_write_in(word, chip_x0 + CHIP_PAD * lay->text_h, y, lay->row_h,
                 row->on ? C_CHIP_ON_TEXT : C_CHIP_OFF_TEXT);
    }
}

/* The row the KEYBOARD is on, marked so that it is not the same picture as the row under the
 * pointer. The fill says "this row is current" for both of them; this says which hand put it there,
 * which is the question a player has after touching the mouse and then reaching for Return.
 *
 * A frame round the whole row and NOT the accent bar down its left edge. That bar is the
 * heading's: same colour, same x, and 0.25 text heights against three rule widths, which at the
 * ordinary size is four pixels against three. Two marks that close together in the same place are
 * one mark with two meanings, and a selected row wearing it would read as a heading with an odd
 * fill. The frame is the rule width and the accent, so it is no new measure and no new colour.
 *
 * Drawn after the row rather than before it, because a heading paints its own rule along the top
 * of its band and would otherwise paint over the frame's top edge. */
static void paint_selection(const layout_t *lay, float left, float right, float y)
{
    const float x0 = left + lay->rule;
    const float x1 = right - lay->rule;
    const float y1 = y + lay->row_h;

    overlay_draw_fill(x0, y, x1, y + lay->rule, C_ACCENT);
    overlay_draw_fill(x0, y1 - lay->rule, x1, y1, C_ACCENT);
    overlay_draw_fill(x0, y, x0 + lay->rule, y1, C_ACCENT);
    overlay_draw_fill(x1 - lay->rule, y, x1, y1, C_ACCENT);
}

/* --- the scroll indicator, and only when there is something to indicate -------------------
 * A thumb on the inside of the right border, as long a fraction of the track as the visible
 * rows are of all of them, and as far down it as the list is scrolled. It is drawn rather than
 * clickable on purpose: the wheel and the keys already move the list, and a draggable bar this
 * thin would be a target the game's own pointer is not precise enough to hit.
 *
 * Absent entirely when everything fits, so the ordinary panel is exactly what it always was. */
static void paint_scroll_indicator(const layout_t *lay, float right, uint32_t first)
{
    const uint32_t count = overlay_model_row_count();

    if (count > lay->visible_rows && lay->visible_rows > 0u) {
        const float track_y0 = lay->top + lay->rows_top;
        const float track_y1 = track_y0 + (float)lay->visible_rows * lay->row_h;
        const float track_h  = track_y1 - track_y0;
        const float w        = lay->rule * 3.0f;
        const float x1       = right - lay->rule;
        const float x0       = x1 - w;
        float       thumb_h  = track_h * (float)lay->visible_rows / (float)count;
        float       thumb_y;

        if (thumb_h < lay->text_h) {
            thumb_h = lay->text_h;         /* never so short it reads as a speck */
        }
        thumb_y = track_y0 + (track_h - thumb_h) *
                  ((float)first / (float)(count - lay->visible_rows));

        overlay_draw_fill(x0, track_y0, x1, track_y1, C_RULE);
        overlay_draw_fill(x0, thumb_y, x1, thumb_y + thumb_h, C_ACCENT);
    }
}

static void paint_pointer(const layout_t *lay, float pointer_x, float pointer_y)
{
    if (draw_state.draw_sprite != NULL && draw_state.cursor_texture != NULL &&
        *draw_state.cursor_texture != NULL) {
        draw_state.draw_sprite(*draw_state.cursor_texture,
                               pointer_x, pointer_x + CURSOR_SIZE,
                               pointer_y, pointer_y + CURSOR_SIZE,
                               CURSOR_COLOUR, CURSOR_FILL);
    } else {
        const float arm = 0.4f * lay->text_h;

        overlay_draw_fill(pointer_x - arm, pointer_y - lay->rule, pointer_x + arm,
             pointer_y + lay->rule,
             C_POINTER);
        overlay_draw_fill(pointer_x - lay->rule, pointer_y - arm, pointer_x + lay->rule,
             pointer_y + arm,
             C_POINTER);
    }
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
    int32_t     picked;
    uint32_t    i;
    uint32_t    first;             /* the row drawn at the top, see the list below */
    char        scratch[OVERLAY_LABEL_MAX + 4];

    if (!overlay_draw_screen(NULL, NULL)) {
        return false;                  /* no display mode: nothing to scale to or draw on */
    }

    overlay_frame_tab_widths(tab_word, OVERLAY_LAYOUT_TABS);
    overlay_layout_build(measured_text_height(),
                         overlay_chip_widest_name() + overlay_chip_widest(),
                         overlay_model_row_count(), tab_word,
                         *draw_state.screen_w, *draw_state.screen_h);

    lay = *overlay_layout();
    left = lay.left;
    right = lay.left + lay.width;

    overlay_input_pointer(&pointer_x, &pointer_y);
    hot = overlay_input_pointer_hidden() ? -1 : overlay_draw_row_at(pointer_x, pointer_y);
    /* The keyboard's row, when there is one, is the current row. There can only ever be one of
     * them: moving the mouse clears the selection, so the two never both exist and the panel
     * never shows two rows as the one being acted on.
     *
     * They are TWO answers here and not one. Which row is current decides the fill and is the
     * same question for either hand; which HAND it is decides the mark, and that is what a player
     * who has just touched the mouse needs to know before pressing Return. Nothing new is
     * decided: the selection is the model's and the pointer's row is the layout's, and this reads
     * both rather than working a third one out. */
    picked = overlay_model_selected();
    if (picked >= 0) {
        hot = picked;
    }

    overlay_frame_paint_top(&lay, left, right);

    /* --- the list ------------------------------------------------------------------------------
     * Drawn by position, indexed by row. `i` counts down the panel and `index` counts down the
     * tab, and they differ by wherever the list is scrolled to. Everything below keys off `row`,
     * so only the two lines that turn a position into a row have to know scrolling exists. */
    first = overlay_model_scroll(lay.visible_rows);
    memset(tracks, 0, sizeof tracks);
    memset(segments, 0, sizeof segments);
    for (i = 0; i < lay.visible_rows; ++i) {
        overlay_row_t  row;
        const uint32_t index = first + i;
        const float    y = lay.top + lay.rows_top + (float)i * lay.row_h;
        const bool     is_hot = ((int32_t)index == hot);

        if (!overlay_model_row(index, &row)) {
            break;
        }

        /* What a held track does to the number on the value row above it and on the track
         * row itself: overlay_slider.c's, because that is where the hold is, and one call
         * for every row because leaving one of the two out is the thing that goes wrong. */
        overlay_slider_number_on(index, &row);
        if (row.kind == OVERLAY_ROW_GROUP) {
            paint_group_row(&lay, left, right, y, &row, is_hot);
        } else if (row.kind == OVERLAY_ROW_SLIDER) {
            paint_slider_row(&lay, left, right, y, index, i, &row, is_hot);
        } else if (row.kind == OVERLAY_ROW_INFO) {
            paint_info_row(&lay, left, right, y, &row, scratch, sizeof scratch);
        } else {
            paint_value_row(&lay, left, right, y, i, &row, is_hot, scratch, sizeof scratch);
        }
        if ((int32_t)index == picked) {
            paint_selection(&lay, left, right, y);
        }
    }

    paint_scroll_indicator(&lay, right, first);
    /* After the rows, so nothing of the list can be drawn over it. */
    overlay_frame_paint_foot(&lay, left, right);
    if (!overlay_input_pointer_hidden()) {
        paint_pointer(&lay, pointer_x, pointer_y);
    }
    return true;
}

void overlay_draw_note(const char *text, float x, float y, uint32_t argb)
{
    if (text != NULL && overlay_draw_screen(NULL, NULL)) {
        write(text, x, y, argb);
    }
}

float overlay_draw_note_width(const char *text)
{
    return overlay_draw_screen(NULL, NULL) ? overlay_draw_width(text) : 0.0f;
}

float overlay_draw_note_height(void)
{
    return overlay_draw_screen(NULL, NULL) ? measured_text_height() : 0.0f;
}

/* The same pointer the panel draws, with a layout of the text height alone for the cross the
 * sprite falls back to. */
void overlay_draw_pointer_at(float x, float y)
{
    layout_t lay;

    if (!overlay_draw_screen(NULL, NULL)) {
        return;
    }
    memset(&lay, 0, sizeof lay);
    lay.text_h = measured_text_height();
    lay.rule   = 1.0f;
    paint_pointer(&lay, x, y);
}

/* The track drawn for `index` on the last paint, or false when that row had none or has scrolled
 * out of the panel since. */
static bool track_for_row(int32_t index, float *x0, float *x1)
{
    const layout_t *lay = overlay_layout();
    uint32_t        first;
    uint32_t        slot;

    if (index < 0 || lay == NULL || lay->visible_rows == 0u) {
        return false;
    }
    first = overlay_model_scroll(lay->visible_rows);
    if ((uint32_t)index < first) {
        return false;
    }
    slot = (uint32_t)index - first;
    if (slot >= TRACK_SLOTS || !tracks[slot].present) {
        return false;
    }
    if (!(tracks[slot].x1 > tracks[slot].x0)) {
        return false;
    }
    *x0 = tracks[slot].x0;
    *x1 = tracks[slot].x1;
    return true;
}

/* The row whose Default button is under the pointer, or -1. Kept from the paint the same way
 * the track is, and asked BEFORE the track: the two boxes do not overlap, and the slack a grab
 * is allowed past the end of the track now stops at the number (overlay_number_grabs), but the
 * order is what makes that a second line of defence rather than the only one. */
int32_t overlay_draw_default_at(float x, float y)
{
    const layout_t *lay = overlay_layout();
    const int32_t   index = overlay_draw_row_at(x, y);
    uint32_t        first;
    uint32_t        slot;

    if (lay == NULL || index < 0 || lay->visible_rows == 0u) {
        return -1;
    }
    first = overlay_model_scroll(lay->visible_rows);
    if ((uint32_t)index < first) {
        return -1;
    }
    slot = (uint32_t)index - first;
    if (slot >= TRACK_SLOTS || !tracks[slot].present ||
        !(tracks[slot].def_x1 > tracks[slot].def_x0)) {
        return -1;
    }
    return (x >= tracks[slot].def_x0 && x <= tracks[slot].def_x1) ? index : -1;
}

int32_t overlay_draw_segment_at(float x, float y, int32_t *segment)
{
    const layout_t *lay = overlay_layout();
    const int32_t   index = overlay_draw_row_at(x, y);
    uint32_t        first;
    uint32_t        slot;

    if (segment == NULL || lay == NULL || index < 0 || lay->visible_rows == 0u) {
        return -1;
    }
    first = overlay_model_scroll(lay->visible_rows);
    if ((uint32_t)index < first) {
        return -1;
    }
    slot = (uint32_t)index - first;
    if (slot >= TRACK_SLOTS || segments[slot].count == 0u) {
        return -1;
    }
    *segment = overlay_choice_hit(segments[slot].edges, segments[slot].count, x);
    return (*segment >= 0) ? index : -1;
}

bool overlay_draw_slider_fraction(int32_t index, float x, float *fraction)
{
    float x0;
    float x1;

    if (fraction == NULL || !track_for_row(index, &x0, &x1)) {
        return false;
    }
    *fraction = (x - x0) / (x1 - x0);
    if (*fraction < 0.0f) {
        *fraction = 0.0f;
    }
    if (*fraction > 1.0f) {
        *fraction = 1.0f;
    }
    return true;
}

int32_t overlay_draw_slider_at(float x, float y, float *fraction)
{
    const layout_t *lay = overlay_layout();
    int32_t         index = overlay_draw_row_at(x, y);
    float           x0;
    float           x1;

    if (fraction == NULL || lay == NULL || !track_for_row(index, &x0, &x1)) {
        return -1;
    }
    /* The slack outside each end, and how much of it there is at each: overlay_number.h. It was
     * a text height at both, and the number is drawn TRACK_GAP past the end, so a press on the
     * number's first half height took the track and set the row to its maximum. */
    if (!overlay_number_grabs(x, x0, x1, lay->text_h, TRACK_GAP * lay->text_h)) {
        return -1;
    }
    return overlay_draw_slider_fraction(index, x, fraction) ? index : -1;
}
