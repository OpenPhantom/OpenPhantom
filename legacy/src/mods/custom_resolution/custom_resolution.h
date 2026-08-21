/* custom_resolution.h: a player-typed resolution, applied at startup and offered in the menu.
 *
 * See custom_resolution.c for the byte evidence. The short version: the engine's own mode table
 * has no path that lets a player ask for a resolution nobody enumerated, so this adds one record
 * to that table itself rather than fighting the lookup that already exists.
 */
#ifndef CUSTOM_RESOLUTION_H
#define CUSTOM_RESOLUTION_H

void custom_resolution_install(void);

#endif /* CUSTOM_RESOLUTION_H */
