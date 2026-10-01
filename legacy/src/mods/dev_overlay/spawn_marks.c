/* spawn_marks.c: see spawn_marks.h. */
#include "spawn_marks.h"

#include "overlay_draw.h"

#include "common/screen_fill.h"

#include <math.h>
#include <stddef.h>

/* Opaque, as every mark on the panel is, so a mark reads the same over a dark interior and a sky.
 * The one translucent shape is the band behind the text, for the same reason the panel's body is
 * the only translucent one there: text over a moving picture needs a surface of its own. */
#define C_GHOST_OK       0xFF3EC45Cu
#define C_GHOST_REFUSED  0xFFE04848u
#define C_OWN            0xFFD2D7E4u
#define C_FOREIGN        0xFF7B8195u
#define C_HOVER_OWN      0xFFF0C040u
#define C_HOVER_FOREIGN  0xFFB0B4C0u
#define C_TEXT           0xFFE8EAF2u
#define C_BAND           0xC00D0F16u

#define BOX_MIN_PIXELS   6.0f    /* a copy far away is still a box one can see */
#define BOX_ASPECT       0.5f    /* a standing body is about half as wide as it is tall */

void spawn_marks_box(float feet_sx, float feet_sy, float head_sx, float head_sy,
                     spawn_mark_bracket_t *out)
{
    float tall = fabsf(feet_sy - head_sy);
    float half;
    float mid  = (feet_sx + head_sx) * 0.5f;
    float top  = (head_sy < feet_sy) ? head_sy : feet_sy;

    if (tall < BOX_MIN_PIXELS) {
        tall = BOX_MIN_PIXELS;
    }
    half        = tall * BOX_ASPECT * 0.5f;
    if (half < BOX_MIN_PIXELS * 0.5f) {
        half = BOX_MIN_PIXELS * 0.5f;
    }
    out->left   = mid - half;
    out->right  = mid + half;
    out->top    = top;
    out->bottom = top + tall;
}

static uint32_t colour_of(spawn_mark_kind_t kind)
{
    switch (kind) {
    case SPAWN_MARK_GHOST_OK:      return C_GHOST_OK;
    case SPAWN_MARK_GHOST_REFUSED: return C_GHOST_REFUSED;
    case SPAWN_MARK_OWN:           return C_OWN;
    case SPAWN_MARK_FOREIGN:       return C_FOREIGN;
    case SPAWN_MARK_HOVER_OWN:     return C_HOVER_OWN;
    case SPAWN_MARK_HOVER_FOREIGN:
    default:                       return C_HOVER_FOREIGN;
    }
}

/* Four corners, each two strokes a quarter of the box's side long. The ghost and the copy under the
 * pointer get strokes twice as thick, so they read first. */
static void corners(const spawn_mark_bracket_t *b, float stroke)
{
    uint32_t    argb = colour_of(b->kind);
    const float w    = (b->right - b->left) * 0.25f;
    const float h    = (b->bottom - b->top) * 0.25f;

    screen_fill(b->left, b->top, b->left + w, b->top + stroke, argb);
    screen_fill(b->left, b->top, b->left + stroke, b->top + h, argb);
    screen_fill(b->right - w, b->top, b->right, b->top + stroke, argb);
    screen_fill(b->right - stroke, b->top, b->right, b->top + h, argb);
    screen_fill(b->left, b->bottom - stroke, b->left + w, b->bottom, argb);
    screen_fill(b->left, b->bottom - h, b->left + stroke, b->bottom, argb);
    screen_fill(b->right - w, b->bottom - stroke, b->right, b->bottom, argb);
    screen_fill(b->right - stroke, b->bottom - h, b->right, b->bottom, argb);
}

/* A line of text on a band of its own; `y` is the band's top. */
static void banded(const char *text, float x, float y, float height, uint32_t argb)
{
    float width = overlay_draw_note_width(text);

    if (text == NULL || text[0] == '\0' || !(width > 0.0f)) {
        return;
    }
    screen_fill(x - height * 0.3f, y, x + width + height * 0.3f, y + height * 1.5f, C_BAND);
    overlay_draw_note(text, x, y + height * 1.25f, argb);
}

void spawn_marks_banner(const char *text)
{
    float screen_w = 0.0f;
    float screen_h = 0.0f;
    float text_h;

    if (text == NULL || text[0] == '\0' || !overlay_draw_screen(&screen_w, &screen_h)) {
        return;
    }
    (void)screen_h;
    text_h = overlay_draw_note_height();
    banded(text, (screen_w - overlay_draw_note_width(text)) * 0.5f, text_h * 1.5f, text_h,
           C_TEXT);
}

void spawn_marks_draw(const spawn_marks_scene_t *scene)
{
    static const char HELP[] = "Left click places   Wheel turns, Shift 1, Ctrl 90   "
                               "Middle click faces you   Right click removes   Esc menu";
    float    screen_w = 0.0f;
    float    screen_h = 0.0f;
    float    text_h;
    uint32_t i;

    if (scene == NULL || !overlay_draw_screen(&screen_w, &screen_h)) {
        return;
    }
    text_h = overlay_draw_note_height();
    for (i = 0; i < scene->brackets && i < SPAWN_MARKS_BRACKETS; ++i) {
        const spawn_mark_bracket_t *b = &scene->bracket[i];
        bool thick = b->kind == SPAWN_MARK_GHOST_OK || b->kind == SPAWN_MARK_GHOST_REFUSED ||
                     b->kind == SPAWN_MARK_HOVER_OWN || b->kind == SPAWN_MARK_HOVER_FOREIGN;

        corners(b, thick ? 2.0f : 1.0f);
    }
    if (scene->hover[0] != '\0') {
        banded(scene->hover, scene->hover_x, scene->hover_y - text_h * 1.6f, text_h, C_TEXT);
    }
    overlay_draw_pointer_at(scene->pointer_x, scene->pointer_y);
    banded(scene->first, scene->pointer_x + text_h * 1.5f, scene->pointer_y + text_h * 1.5f,
           text_h, scene->good ? C_GHOST_OK : C_GHOST_REFUSED);
    banded(scene->second, scene->pointer_x + text_h * 1.5f, scene->pointer_y + text_h * 3.1f,
           text_h, C_TEXT);
    banded(HELP, (screen_w - overlay_draw_note_width(HELP)) * 0.5f, screen_h - text_h * 2.5f,
           text_h, C_TEXT);
}
