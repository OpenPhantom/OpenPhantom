/* enhanced_resolution.h: modern resolutions, listed in the options screen as if they belonged there.
 *
 * Produces: enhanced_resolution.dll
 */
#ifndef ENHANCED_RESOLUTION_H
#define ENHANCED_RESOLUTION_H

void enhanced_resolution_install(void);

/* Releases the one piece of process-global OS state this DLL takes: the cursor clip rectangle.
 * Called from DLL_PROCESS_DETACH, so it must stay a single USER32 call. */
void enhanced_resolution_shutdown(void);

#endif /* ENHANCED_RESOLUTION_H */
