/* dev_menu_size_row.h: the dev menu's own size, as a row inside the dev menu.
 *
 * DevMenuSize exists because the engine knows the resolution and not the screen: a fixed pixel size
 * reads the same at 1080 and at 4K, and on a high density laptop panel those same pixels are tiny.
 *
 * IT SIZES ITSELF BY DEFAULT. DevMenuSize=0, which is what ships, holds the size the panel reads
 * at on a 1080 screen as the resolution changes: 1.0x at 1080 and below, 2.0x at 2160. That is a
 * READING size and deliberately not the largest that fits, which at 4K fills the screen. An
 * explicit number still overrides it, because only the person looking at the screen knows how far
 * away it is.
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

/* Below a third the text stops being legible, so that is the floor.
 *
 * FOUR IS THE CEILING ONLY ON A LARGE ENOUGH SCREEN. The panel is a fixed number of pixels times
 * this scale, so four is right for a high density display and far too big for a small one: on a
 * 1280x800 Steam Deck it puts the panel past the edge of the screen, taking the row that would set
 * it back with it. dev_menu_size_row_clamp therefore lowers this ceiling in proportion to the
 * display, never below 1.0, and the row reports whatever it actually allowed. See screen_ceiling()
 * in the .c for the reasoning. */
/* Zero asks the panel to size itself from the resolution, and it is the shipped default. */
#define DEV_MENU_SIZE_AUTOMATIC 0.00f
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
