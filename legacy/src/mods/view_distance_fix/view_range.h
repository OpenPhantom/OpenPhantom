/* view_range.h: how far the world is drawn, and what is allowed to move that while it runs.
 *
 * THE SEAM. Lifted out of view_distance_fix.c, which was past the hard limit. What came across is
 * one responsibility and all of the state it decides over: the field of view this DLL observes
 * for itself, the radius cap that pays for a widened picture, where the cut edge lands, the
 * bapmat_viewDistance detour that puts it there, and the per frame tick that arbitrates between
 * the frame governor, the level opening window, a scripted camera and the cell watchdog.
 *
 * The query the fog asks, view_distance_fix_cut_for, is defined in view_range.c and declared in
 * view_distance_fix.h, because that is the header the fog already includes.
 */
#ifndef VIEW_RANGE_H
#define VIEW_RANGE_H

#include <stdint.h>

#include "view_settings.h"

/* rdCamera_BuildProjection's prologue, `55 / 8B EC / 83 EC 24`, six bytes. Declared here rather
 * than beside the pattern because the detour that overwrites those bytes is this file's, and the
 * site table needs the same number to search in the detour form. */
#define BUILD_PROJECTION_PROLOGUE_SIZE 6u

/* Binds the DLL's one configuration record and sets the scale the session starts at. Called
 * before the per frame tick is registered, so nothing here can run against an unbound pointer. */
void view_range_configure(view_distance_config_t *config, float scale);

/* Pins the scale. The install sequence uses it when the per frame hook or the cell watchdog is
 * missing, because with neither of them nothing would be watching the draw table overflow. */
void view_range_set_scale(float scale);

/* The DLL's per frame callback. */
void view_range_on_frame(void);

/* Both take an already resolved site, or 0, which declines that half and says so in the log. */
void view_range_install_fov_observer(uintptr_t site);
void view_range_install_draw_distance(uintptr_t site);

#endif /* VIEW_RANGE_H */
