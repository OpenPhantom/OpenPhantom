/* view_distance_fix.h: draw distance, fog, NPC activation and two-sided severed bodies.
 *
 * Produces: view_distance_fix.dll
 */
#ifndef VIEW_DISTANCE_FIX_H
#define VIEW_DISTANCE_FIX_H

void view_distance_fix_install(void);

/* Called at DLL_PROCESS_DETACH: the draw-table relocation has to be undone before the process
 * lets go of our buffer. */
void view_distance_fix_shutdown(void);

#endif /* VIEW_DISTANCE_FIX_H */
