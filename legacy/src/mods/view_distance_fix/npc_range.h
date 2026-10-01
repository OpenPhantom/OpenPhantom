/* npc_range.h: the NPC activation radius a scale may reach.
 *
 * The engine wakes a placement when the player is inside its activation radius (rec+0x28) and
 * removes the actor again when the player is outside its removal radius (rec+0x2C). Both tests go
 * through within_range 0x00428EB3, a strict three dimensional comparison, and the actor starts
 * exactly on the placement's position. Only the activation test is scaled, so a scaled radius at or
 * past the removal radius creates an actor that the next substep removes, which writes the spawn
 * state back to 0 and lets the scan wake it again: an actor made and deleted on every substep.
 *
 * Pure, so a test drives it without the engine.
 */
#ifndef VIEW_DISTANCE_FIX_NPC_RANGE_H
#define VIEW_DISTANCE_FIX_NPC_RANGE_H

#include <stdbool.h>

/* The share of the removal radius a scaled activation radius may reach. The eleven shipped levels
 * give 2205 placements a radius; the tightest gap between the two radii that the level authors used
 * on more than a handful of them is removal = 1.12 x activation (62 placements), which wakes at
 * 0.89 of the removal radius. A scaled radius gets no tighter gap than the authors gave theirs. */
#define NPC_RANGE_REMOVAL_SHARE 0.89f

/* The activation radius to test with: `active` scaled, but never as far as the removal radius
 * allows, and never below `active` itself. An `active` of 0 or less stays as it is (0 is "always
 * active"), and a `removal` of 0 or less means the engine never removes the actor, so nothing
 * caps the scale. `capped` receives whether the result is less than the scaled radius. */
float npc_range_scaled(float active, float removal, float scale, bool *capped);

#endif /* VIEW_DISTANCE_FIX_NPC_RANGE_H */
