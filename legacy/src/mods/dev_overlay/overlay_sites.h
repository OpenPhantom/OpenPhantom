/* overlay_sites.h: every engine entry point the panel draws through, and nothing else.
 *
 * Split from the painting because they are two jobs and the file had grown past what this project
 * allows: one half is byte evidence and pattern matching, the other is layout and colour. Neither
 * is easier to read next to the other.
 *
 * Everything here is resolved by signature, never by address, and every address bearing field in
 * every pattern is masked and read back out of the match.
 */
#ifndef OVERLAY_SITES_H
#define OVERLAY_SITES_H

#include <stdbool.h>
#include <stdint.h>

typedef void (__cdecl *draw_quad_fn_t)(float x0, float y0, float x1, float y1,
                                       uint32_t argb, int32_t layer);
typedef void (__cdecl *draw_sprite_fn_t)(void *texture, float left, float right, float top,
                                         float bottom, uint32_t argb, float fill);
typedef void (__cdecl *set_align_fn_t)(int32_t mode);
typedef int32_t (__cdecl *measure_char_fn_t)(uint8_t ch, float *width, float *height);
typedef float (__cdecl *measure_string_fn_t)(const char *text);
typedef void (__cdecl *scale_fn_t)(float sx, float sy);
typedef void (__cdecl *select_fn_t)(int32_t slot);
typedef void (__cdecl *set_colour_fn_t)(uint32_t argb);
typedef void (__cdecl *draw_text_fn_t)(const char *text, float x, float y);

typedef struct overlay_draw_state {
    bool                    resolved;
    draw_quad_fn_t          quad;
    set_align_fn_t          set_align;
    draw_sprite_fn_t        draw_sprite;
    void *const volatile   *cursor_texture;
    measure_char_fn_t       measure_char;
    measure_string_fn_t     measure_string;
    scale_fn_t              glyph_scale;
    scale_fn_t              pos_scale;
    select_fn_t             select;
    set_colour_fn_t         colour;
    draw_text_fn_t          text;
    const volatile int32_t *font_slot;
    const volatile float   *screen_w;
    const volatile float   *screen_h;
} overlay_draw_state_t;

extern overlay_draw_state_t draw_state;

#endif /* OVERLAY_SITES_H */
