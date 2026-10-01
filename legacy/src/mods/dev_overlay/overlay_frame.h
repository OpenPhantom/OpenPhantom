/* overlay_frame.h: the panel's chrome, which is everything but the list.
 *
 * The title band, the two tabs, the search field and the footer. They share nothing with the rows
 * below them except the layout they are measured against and the palette in overlay_look.h, which
 * is what made them a file of their own: overlay_draw.c had named this seam in its own note for
 * long enough, and the footer would have been the change that pushed it past its limit.
 *
 * What the footer says is overlay_legend.c's, as text with no drawing in it. This file puts it on
 * the screen and decides nothing about the words.
 */
#ifndef DEV_OVERLAY_OVERLAY_FRAME_H
#define DEV_OVERLAY_OVERLAY_FRAME_H

#include "overlay_layout.h"

#include <stdint.h>

/* The width of each tab's own word, measured in the font the panel draws with, for the layout:
 * a tab is as wide as its word, because a four character word inside a twelve character box has
 * no visible relationship to the word that names it. Writes `count` of them. */
void overlay_frame_tab_widths(float *out, uint32_t count);

/* The body, its border, the accent cap, the title, the tabs and the search field. Painted before
 * the list, since the list is drawn inside the shape this leaves. */
void overlay_frame_paint_top(const layout_t *lay, float left, float right);

/* The two bands along the bottom: what the panel last turned down, whenever it has turned
 * something down, and under it what the keys do and what the open tab holds or which end of a
 * session this machine is. Painted after the list, so nothing of the list can land on either. */
void overlay_frame_paint_foot(const layout_t *lay, float left, float right);

#endif /* DEV_OVERLAY_OVERLAY_FRAME_H */
