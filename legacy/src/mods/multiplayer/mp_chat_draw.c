/* mp_chat_draw.c: the chat box, painted with the engine's own renderer. See the header.
 *
 * The two primitives below are the scoreboard's, written again rather than shared, because the
 * scoreboard's are its file's own and the rule behind them is two lines long: the font layer is
 * global and sticky, so every piece of its state is set again in front of every string, and never
 * put back. The coordinates are the scoreboard's as well: a rectangle in screen pixels, a string at
 * a position scale of one over the screen, which makes it pixels too, and at a glyph scale that
 * gives the font its authored size, so the layout's widths are pixels. The y of a string is its
 * baseline.
 *
 * Nothing is kept between frames but the frame count the once a frame guard counts against, and
 * the newest line already counted for the report.
 */
#include "mp_chat_draw.h"

#include "mp_chat.h"
#include "mp_chat_input.h"
#include "mp_chat_layout.h"
#include "mp_chat_rule.h"
#include "mp_signatures_hud.h"
#include "mp_text.h"
#include "mp_wallclock.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The first statement of the engine's frame end, and the only message the box acts on. */
#define MODULE_MSG_FRAME_TOP 0x15

/* The sixteen font slots of the engine's font module; outside them every font call is a no-op. */
#define FONT_SLOT_COUNT 16

/* The left alignment, in the number the setter takes. */
#define ALIGN_LEFT 1

/* The rectangle's sixth argument: draw it now rather than in the queue that is already emptied. */
#define DRAW_NOW 1

/* The built in font reaches fourteen above its baseline and six below it for every character a
 * line can carry. A row is that span and four pixels, which keeps two rows of a fading box apart
 * at a glance. */
#define TEXT_ASCENT 14.0f
#define TEXT_SPAN   20.0f
#define ROW_H       24.0f

/* Where the box stands, in fractions of the screen and in pixels. */
#define BOTTOM_FRACTION 0.72f
#define LEFT_PX         16.0f
#define WIDTH_FRACTION  0.40f
#define WIDTH_MAX_PX    520.0f
#define PAD             8.0f
#define RULE            1.0f

/* How far the dark copy under every string of a line is set off. The lines stand on the level
 * itself while nobody types, and white on a bright wall is otherwise unreadable. While the player
 * types the box stands behind them and the copy is left out: every glyph is a draw call of its own,
 * and the copy doubles them. */
#define SHADOW_PX 1.0f

/* How long the cursor shows and hides. */
#define CURSOR_BLINK_MS 500u

/* The one translucent surface is the box behind the input; everything on it is opaque, so its
 * contrast does not depend on the scene behind. The names and the prefix take the amber of this
 * project's panels, the prefix turns red for a second when Enter sent nothing. */
#define C_BOX        0xB40D0F16u
#define C_RULE       0xFFF2C76Bu
#define C_PREFIX     0xFFF2C76Bu
#define C_REFUSED    0xFFE8584Au
#define C_TYPED      0xFFFFFFFFu
#define RGB_NAME     0x00F2C76Bu
#define RGB_TEXT     0x00FFFFFFu
#define ALPHA_SHIFT  24u

typedef struct chat_draw_state {
    uint32_t              pumps;
    uint32_t              drawn_at;
    bool                  ever_drawn;
    bool                  no_surface;
    const mp_chat_line_t *last_counted;       /* the newest line the report has counted */
    uint32_t              last_counted_ms;
    uint32_t              draws;
    uint32_t              repeats;
    uint32_t              no_font;
    uint32_t              no_screen;
    uint32_t              wrapped_lines;
    uint32_t              wrapped_rows;
} chat_draw_state_t;

static chat_draw_state_t draw;

/* A frame's worth of what the box shows. */
typedef struct chat_frame {
    const mp_hud_surface_t *surface;
    const mp_chat_line_t   *newest[MP_CHAT_LAYOUT_ROWS];
    mp_chat_layout_line_t   lines[MP_CHAT_LAYOUT_ROWS];
    mp_chat_layout_row_t    rows[MP_CHAT_LAYOUT_ROWS];
    size_t                  line_count;
    size_t                  row_count;
    int32_t                 inner;
    uint32_t                now_ms;
    bool                    typing;
} chat_frame_t;

static void fill(const mp_hud_surface_t *s, float x0, float y0, float x1, float y1, uint32_t argb)
{
    s->quad(x0, y0, x1, y1, argb, DRAW_NOW);
}

static void put_text(const mp_hud_surface_t *s, const char *text, float x, float baseline,
                     uint32_t argb)
{
    const float width  = *s->screen_w;
    const float height = *s->screen_h;

    s->select(*s->font_slot);
    s->align(ALIGN_LEFT);
    s->pos_scale(1.0f / width, 1.0f / height);
    s->glyph_scale(640.0f / width, 480.0f / height);
    s->colour(argb);
    s->text(text, x, baseline);
}

/* A string of a line at the line's alpha, on its dark copy unless the box is behind it. */
static void put_shadowed(const mp_hud_surface_t *s, const char *text, float x, float baseline,
                         uint32_t rgb, uint8_t alpha, bool typing)
{
    const uint32_t a = (uint32_t)alpha << ALPHA_SHIFT;

    if (!typing) {
        put_text(s, text, x + SHADOW_PX, baseline + SHADOW_PX, a);
    }
    put_text(s, text, x, baseline, a | rgb);
}

static float baseline_of(float top)
{
    return top + (ROW_H - TEXT_SPAN) * 0.5f + TEXT_ASCENT;
}

/* One row: the name and its colon when the row carries them, then its piece of the text. */
static void draw_row(const chat_frame_t *f, const mp_chat_layout_row_t *row, float left,
                     float top)
{
    const mp_chat_layout_line_t *line     = &f->lines[row->line];
    const float                  baseline = baseline_of(top);
    char                         piece[MP_CHAT_TEXT_MAX + 2u];

    if (row->named) {
        memcpy(piece, line->name, row->name_length);
        piece[row->name_length]      = ':';
        piece[row->name_length + 1u] = '\0';
        put_shadowed(f->surface, piece, left, baseline, RGB_NAME, row->alpha, f->typing);
    }
    if (row->text_length > 0u) {
        memcpy(piece, line->text + row->text_start, row->text_length);
        piece[row->text_length] = '\0';
        put_shadowed(f->surface, piece, left + (float)row->text_x, baseline, RGB_TEXT,
                     row->alpha, f->typing);
    }
}

/* The prefix, and as much of the end of the typed text as fits behind it, and the cursor. */
static void draw_input(const chat_frame_t *f, float left, float top)
{
    const char *prefix   = mp_text(MP_TEXT_CHAT_SAY);
    const float baseline = baseline_of(top);
    bool        refused  = false;
    size_t      length   = 0u;
    const char *typed    = mp_chat_input_line(f->now_ms, &length, &refused);
    char        piece[MP_CHAT_TEXT_MAX + 2u];
    size_t      start;

    if (typed == NULL) {
        return;
    }
    start = mp_chat_layout_input_start(prefix, typed, length, f->inner);
    memcpy(piece, typed + start, length - start);
    piece[length - start]      = (f->now_ms / CURSOR_BLINK_MS) % 2u == 0u ? '_' : '\0';
    piece[length - start + 1u] = '\0';
    put_text(f->surface, prefix, left, baseline, refused ? C_REFUSED : C_PREFIX);
    put_text(f->surface, piece,
             left + (float)(mp_chat_layout_width(prefix, strlen(prefix)) +
                            mp_chat_layout_width(" ", 1u)),
             baseline, C_TYPED);
}

/* Lines that came since the last frame the box was drawn, counted once each for the report. */
static void count_new_lines(const chat_frame_t *f)
{
    size_t i = f->line_count;

    while (i > 0u && !(f->newest[i - 1u] == draw.last_counted &&
                       f->newest[i - 1u]->shown_ms == draw.last_counted_ms)) {
        const size_t rows = mp_chat_layout_row_count(&f->lines[i - 1u], f->inner);

        if (rows > 1u) {
            ++draw.wrapped_lines;
            draw.wrapped_rows += (uint32_t)rows;
        }
        --i;
    }
    if (f->line_count > 0u) {
        draw.last_counted    = f->newest[f->line_count - 1u];
        draw.last_counted_ms = draw.last_counted->shown_ms;
    }
}

/* Everything the box shows this frame, worked out before anything is drawn. */
static void compose(chat_frame_t *f, bool typing)
{
    size_t i;

    f->now_ms     = mp_wallclock_ms();
    f->line_count = mp_chat_newest(f->newest, MP_CHAT_LAYOUT_ROWS);
    for (i = 0; i < f->line_count; ++i) {
        const uint32_t age = f->now_ms - f->newest[i]->shown_ms;

        f->lines[i].name   = f->newest[i]->name;
        f->lines[i].text   = f->newest[i]->text;
        f->lines[i].age_ms = (int32_t)age < 0 ? 0u : age;
    }
    f->row_count = mp_chat_layout_rows(f->lines, f->line_count, f->inner, typing, f->rows,
                                       MP_CHAT_LAYOUT_ROWS);
}

/* The box: the rows from the bottom up over the place of the input row, which stays where it is
 * whether or not the player types, so nothing moves when the box opens. */
static bool paint(chat_frame_t *f)
{
    const bool  typing   = mp_chat_input_is_typing();
    const float screen_w = *f->surface->screen_w;
    const float box_w    = screen_w * WIDTH_FRACTION < WIDTH_MAX_PX ? screen_w * WIDTH_FRACTION
                                                                   : WIDTH_MAX_PX;
    const float bottom   = *f->surface->screen_h * BOTTOM_FRACTION;
    const float input_y  = bottom - PAD - ROW_H;
    const float left     = LEFT_PX + PAD;
    size_t      k;

    f->inner  = (int32_t)(box_w - 2.0f * PAD);
    f->typing = typing;
    compose(f, typing);
    count_new_lines(f);
    if (f->row_count == 0u && !typing) {
        return false;
    }
    if (typing) {
        fill(f->surface, LEFT_PX, input_y - (float)MP_CHAT_LAYOUT_ROWS * ROW_H - PAD,
             LEFT_PX + box_w, bottom, C_BOX);
        fill(f->surface, LEFT_PX, input_y - RULE, LEFT_PX + box_w, input_y, C_RULE);
    }
    for (k = 0; k < f->row_count; ++k) {
        draw_row(f, &f->rows[k], left, input_y - (float)(f->row_count - k) * ROW_H);
    }
    if (typing) {
        draw_input(f, left, input_y);
    }
    return true;
}

void mp_chat_draw_note_module_message(int message)
{
    chat_frame_t f;

    if (message != MODULE_MSG_FRAME_TOP || !mp_chat_input_in_play()) {
        return;
    }
    if (draw.ever_drawn && draw.pumps != 0u && draw.drawn_at == draw.pumps) {
        ++draw.repeats;
        return;
    }
    memset(&f, 0, sizeof f);
    f.surface = mp_signatures_hud_surface();
    if (f.surface == NULL) {
        draw.no_surface = true;
        return;
    }
    if (!(*f.surface->screen_w > 0.0f && *f.surface->screen_h > 0.0f)) {
        ++draw.no_screen;
        return;
    }
    if (*f.surface->font_slot < 0 || *f.surface->font_slot >= FONT_SLOT_COUNT) {
        ++draw.no_font;
        return;
    }
    if (paint(&f)) {
        ++draw.draws;
        draw.ever_drawn = true;
        draw.drawn_at   = draw.pumps;
    }
}

void mp_chat_draw_frame(void)
{
    ++draw.pumps;
}

void mp_chat_draw_report(void)
{
    log_info("the chat box: drawn on %u frame(s), %u refused as a repeat inside one frame, %u with "
             "no font, %u with no screen size, %u line(s) wrapped into %u row(s)",
             (unsigned)draw.draws, (unsigned)draw.repeats, (unsigned)draw.no_font,
             (unsigned)draw.no_screen, (unsigned)draw.wrapped_lines,
             (unsigned)draw.wrapped_rows);
    if (draw.no_surface) {
        log_warning("  the chat box has no drawing surface: %s did not resolve",
                    mp_signatures_hud_missing() != NULL ? mp_signatures_hud_missing()
                                                        : "one of its sites");
    }
}
