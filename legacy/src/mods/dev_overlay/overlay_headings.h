/* overlay_headings.h: which headings the panel draws, in what order, and which sources of rows
 * stand under each of them.
 *
 * A heading and a source of rows are not the same thing in this panel. The sources are the group
 * enum in overlay_model.h and there are more of them than there are headings, because two of them
 * are drawn inside another group's body: the fog under the engine's settings, the game's own
 * screens under this panel's. Merging them this way rather than by moving rows between sources is
 * what lets the headings read as subjects while every row id, every slot number and the whole of
 * session_lock.c stay exactly where they were, and a slot number moved there would hand a player,
 * in the middle of a session, a row the session takes.
 *
 * Lifted out of overlay_model.c when that file stood within fifteen lines of the hard limit, and
 * it was the seam its own SIZE NOTE named: three tables and the titles, read once per rebuild and
 * deciding nothing else. What is folded, what is searched and which row the keyboard is on stayed
 * in the model, because those are the panel's state and none of them is here.
 */
#ifndef DEV_OVERLAY_OVERLAY_HEADINGS_H
#define DEV_OVERLAY_OVERLAY_HEADINGS_H

#include "overlay_model.h"

#include <stdint.h>

/* The tab a source belongs to. A group cannot change tabs at runtime, so this is a table and not
 * state. A value that is no group answers OVERLAY_TAB_COUNT, which is no tab at all, so a caller
 * comparing it with the open tab finds nothing to draw. */
overlay_tab_t overlay_headings_tab(overlay_group_t group);

/* How many headings both tabs hold together, and the one drawn at `index` in the order they are
 * drawn. Past the end answers OVERLAY_GROUP_COUNT, which is no group. */
uint32_t        overlay_headings_count(void);
overlay_group_t overlay_headings_at(uint32_t index);

/* The heading a source's rows are drawn under: the source itself when it has a heading of its own.
 * A value that is no group answers itself, so the caller's own range test still refuses it. */
overlay_group_t overlay_headings_drawn_under(overlay_group_t group);

/* The sources drawn under one heading: the heading's own group first, then every source drawn
 * under it, in enum order. Writes at most `max` and answers how many it wrote. */
uint32_t overlay_headings_bodies(overlay_group_t heading, overlay_group_t *out, uint32_t max);

/* The words on a heading's band. NULL for a source that has no heading of its own and for a value
 * that is no group. */
const char *overlay_headings_title(overlay_group_t heading);

#endif /* DEV_OVERLAY_OVERLAY_HEADINGS_H */
