/* dev_menu_size_row.h: the dev menu's own size, as a row inside the dev menu.
 *
 * DevMenuSize exists because the engine knows the resolution and not the screen: a fixed pixel size
 * reads the same at 1080 and at 4K, and on a high density laptop panel those same pixels are tiny.
 * Only the person looking at it knows, so it is theirs to set.
 *
 * Putting it in the panel rather than only in the ini is the difference between guessing a number,
 * restarting, and guessing again, and just seeing it. The setter applies it immediately and writes
 * it to the ini, so what is on screen and what is on disk never disagree.
 *
 * The same shape as view_range_row.h, and for the same reasons: parsed here rather than by atof so
 * the ini and the row agree about what counts as a number, and clamped rather than refused so a
 * typed value moves the panel toward what was meant.
 */
#ifndef DEV_OVERLAY_DEV_MENU_SIZE_ROW_H
#define DEV_OVERLAY_DEV_MENU_SIZE_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* Below a third the text stops being legible and above four the panel stops fitting anything, so
 * those are the ends. They are the same numbers overlay_draw.c clamps to, deliberately: a row that
 * offered a value the drawing would silently refuse would be lying about what it does. */
#define DEV_MENU_SIZE_MIN  0.33f
#define DEV_MENU_SIZE_MAX  4.00f
#define DEV_MENU_SIZE_STEP 0.10f

float dev_menu_size_row_clamp(float scale);
bool  dev_menu_size_row_parse(const char *text, float *out);
void  dev_menu_size_row_format(float scale, char *out, size_t size);

/* The value in the ini, parsed and clamped. Reads the file every call. */
float dev_menu_size_row_get(void);

/* The value the panel is being drawn at: taken from the ini on the first ask and from the row
 * after that. The drawing layer calls this rather than being told, which is what lets this file be
 * linked into a test with no renderer behind it. */
float dev_menu_size_row_current(void);

/* Clamp, apply to the drawing immediately, and write it to the ini. Applying is what view_range_row
 * does not have to do: view_distance_fix re-reads its own key once a second, and nothing re-reads
 * this one, so the row is the only thing that can make a typed value take effect. */
bool dev_menu_size_row_set(float scale);

#endif /* DEV_OVERLAY_DEV_MENU_SIZE_ROW_H */
