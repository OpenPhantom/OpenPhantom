/* view_distance_fix.h: draw distance, fog, NPC activation and two-sided severed bodies.
 *
 * Produces: view_distance_fix.dll
 */
#ifndef VIEW_DISTANCE_FIX_H
#define VIEW_DISTANCE_FIX_H

#include <stdint.h>

void view_distance_fix_install(void);

/* Called at DLL_PROCESS_DETACH: the draw-table relocation has to be undone before the process
 * lets go of our buffer. */
void view_distance_fix_shutdown(void);

/* Where the cut edge lands for an engine range of `engine_range`, under the scale and the radius
 * cap in force right now. The fog asks this at a level load, when nothing has walked the world yet
 * and the reported cut does not exist, so that a level opens on the band it is going to keep
 * instead of easing to it over the first seconds. */
int32_t view_distance_fix_cut_for(int32_t engine_range);

#endif /* VIEW_DISTANCE_FIX_H */
