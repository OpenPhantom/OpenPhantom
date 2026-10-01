/* mp_chat_draw.c: what the chat box hands the engine's renderer in one frame, counted.
 *
 * The module is the real one and so is the layout; the surface, the chat's lines, its input line,
 * the prefix and the clock are played here. The engine draws a string one glyph at a time, a space
 * included, and each glyph is a draw call of its own, so the strings and the characters handed over
 * are the cost of a frame. With six full rows, every string of a line stands on a dark copy while
 * nobody types; while the player types the box behind the rows stands in for the copy, and the
 * copies are left out.
 */
#include "unittest.h"

#include "mp_chat.h"
#include "mp_chat_draw.h"
#include "mp_chat_input.h"
#include "mp_chat_layout.h"
#include "mp_signatures_hud.h"
#include "mp_text.h"
#include "mp_wallclock.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAME_TOP 0x15

/* The words the lines are made of. Each line takes as many of them as fill one row of the box at
 * 1920 pixels, so the six rows the box shows are six full rows. */
#define SENTENCE "we hold the landing pad until the droids are gone, then everyone meets " \
                 "at the hangar door by the second lift on the left"

/* The inner width of the box at 1920 pixels: 520 wide, eight pixels of padding either side. */
#define INNER_1920 504

typedef struct painted {
    uint32_t strings;        /* calls of the text drawer */
    uint32_t characters;     /* one draw call each */
    uint32_t dark_strings;   /* strings in black: the copies under the lines */
    uint32_t dark_characters;
    uint32_t boxes;          /* filled rectangles */
} painted_t;

typedef struct scene {
    painted_t      painted;
    uint32_t       colour;
    bool           typing;
    uint32_t       now_ms;
    size_t         line_count;
    mp_chat_line_t lines[MP_CHAT_LAYOUT_ROWS];
    char           typed[MP_CHAT_TEXT_MAX + 1u];
} scene_t;

static scene_t scene;

/* ==============================================================================================
 * The engine's renderer and the modules around the box.
 * ============================================================================================ */

static void __cdecl fill(float x0, float y0, float x1, float y1, uint32_t argb, int32_t now)
{
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)argb;
    (void)now;
    ++scene.painted.boxes;
}

static void __cdecl draw_text(const char *text, float x, float y)
{
    const uint32_t length = (uint32_t)strlen(text);

    (void)x;
    (void)y;
    ++scene.painted.strings;
    scene.painted.characters += length;
    if ((scene.colour & 0x00FFFFFFu) == 0u) {
        ++scene.painted.dark_strings;
        scene.painted.dark_characters += length;
    }
}

static void __cdecl choose_font(int32_t slot)
{
    (void)slot;
}

static void __cdecl align(int32_t alignment)
{
    (void)alignment;
}

static void __cdecl colour(uint32_t argb)
{
    scene.colour = argb;
}

static void __cdecl scale(float sx, float sy)
{
    (void)sx;
    (void)sy;
}

static int32_t font_slot = 0;
static float   screen_w  = 1920.0f;
static float   screen_h  = 1080.0f;

static const mp_hud_surface_t surface = {
    fill, draw_text, choose_font, colour, align, scale, scale, &font_slot, &screen_w, &screen_h
};

const mp_hud_surface_t *mp_signatures_hud_surface(void)
{
    return &surface;
}

const char *mp_signatures_hud_missing(void)
{
    return NULL;
}

size_t mp_chat_newest(const mp_chat_line_t **out, size_t max)
{
    size_t i;

    for (i = 0; i < scene.line_count && i < max; ++i) {
        out[i] = &scene.lines[i];
    }
    return i;
}

bool mp_chat_input_in_play(void)
{
    return true;
}

bool mp_chat_input_is_typing(void)
{
    return scene.typing;
}

const char *mp_chat_input_line(uint32_t now_ms, size_t *length, bool *refused)
{
    (void)now_ms;
    if (!scene.typing) {
        return NULL;
    }
    if (length != NULL) {
        *length = strlen(scene.typed);
    }
    if (refused != NULL) {
        *refused = false;
    }
    return scene.typed;
}

const char *mp_text(mp_text_id_t id)
{
    return id == MP_TEXT_CHAT_SAY ? "Say:" : "";
}

uint32_t mp_wallclock_ms(void)
{
    return scene.now_ms;
}

/* ============================================================================================== */

static void one_frame(void)
{
    memset(&scene.painted, 0, sizeof scene.painted);
    mp_chat_draw_frame();
    mp_chat_draw_note_module_message(FRAME_TOP);
}

/* As many whole words of the sentence as fill the line's one row. */
static void fill_one_row(mp_chat_line_t *line)
{
    mp_chat_layout_line_t probe;
    size_t                end;
    size_t                best = 0u;

    probe.name   = line->name;
    probe.text   = line->text;
    probe.age_ms = 0u;
    for (end = 1u; end <= strlen(SENTENCE); ++end) {
        if (SENTENCE[end] != ' ' && SENTENCE[end] != '\0') {
            continue;
        }
        memcpy(line->text, SENTENCE, end);
        line->text[end] = '\0';
        if (mp_chat_layout_row_count(&probe, INNER_1920) == 1u) {
            best = end;
        }
    }
    memcpy(line->text, SENTENCE, best);
    line->text[best] = '\0';
}

static void six_lines(void)
{
    size_t i;

    scene.line_count = MP_CHAT_LAYOUT_ROWS;
    for (i = 0; i < scene.line_count; ++i) {
        memset(&scene.lines[i], 0, sizeof scene.lines[i]);
        text_format(scene.lines[i].name, sizeof scene.lines[i].name, "Player%u", (unsigned)i + 1u);
        fill_one_row(&scene.lines[i]);
        scene.lines[i].shown_ms = scene.now_ms;
    }
}

static void check_six_full_rows(void)
{
    painted_t idle;
    painted_t typing;

    ut_section("six full rows: the dark copies while nobody types, none behind the box");
    scene.now_ms = 100000u;
    six_lines();
    ut_checkf(strlen(scene.lines[0].text) > 40u &&
                  mp_chat_layout_width(scene.lines[0].text, strlen(scene.lines[0].text)) >
                      INNER_1920 - 120,
              "each line fills its row: %u characters of text behind the name",
              (unsigned)strlen(scene.lines[0].text));

    scene.typing = false;
    one_frame();
    idle = scene.painted;
    ut_checkf(idle.boxes == 0u && idle.strings > 0u && idle.dark_strings * 2u == idle.strings &&
                  idle.dark_characters * 2u == idle.characters,
              "nobody typing: %u strings and %u glyph draw calls, half of them the dark copy",
              (unsigned)idle.strings, (unsigned)idle.characters);

    scene.typing = true;
    memcpy(scene.typed, "on my way", sizeof "on my way");
    scene.now_ms += 1000u;
    one_frame();
    typing = scene.painted;
    ut_checkf(typing.boxes == 2u && typing.dark_strings == 0u,
              "typing: the box and its rule are drawn, and no dark copy (%u)",
              (unsigned)typing.dark_strings);
    ut_checkf(typing.strings == idle.strings / 2u + 2u &&
                  typing.characters == idle.characters / 2u + (uint32_t)strlen("Say:") +
                                           (uint32_t)strlen("on my way") + 1u,
              "typing: %u strings and %u glyph draw calls, the rows once and the input row",
              (unsigned)typing.strings, (unsigned)typing.characters);

    scene.typing = false;
    scene.now_ms += 1000u;
    one_frame();
    ut_check(scene.painted.dark_strings * 2u == scene.painted.strings,
             "and once the box is shut the copies are back");
}

int main(void)
{
    check_six_full_rows();
    return ut_summary("mp_chat_draw");
}
