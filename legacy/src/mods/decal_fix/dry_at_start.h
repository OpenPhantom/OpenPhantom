/* dry_at_start.h: a freshly spawned or restored body starts dry.
 *
 * The engine lays a wet footprint behind a body for eight seconds after its last footstep on
 * water, measured on the wall clock since the process started against a stamp kept at
 * thing+0x108. A new body's stamp is written as zero, which the print pass cannot tell from
 * "wet at time zero", so for the first eight seconds of the process every body that has never
 * touched water leaves wet prints on dry ground. In 1999 no machine reached a level inside eight
 * seconds; a modern machine loading a save from the menu does, and the prints follow the player
 * across the first room. The two stores of that zero are given a time far in the past instead.
 */
#ifndef DRY_AT_START_H
#define DRY_AT_START_H

#include <stdbool.h>

/* Installs both stores or neither. Returns true when the repair is in place. */
bool dry_at_start_install(void);

#endif /* DRY_AT_START_H */
