/* world_probe.h: the two questions about a point that are not "is there a floor".
 *
 * The floor is floor_probe.h and is older than this file. These two are what the placement mode
 * asks of a spot before an entity is put down on it:
 *
 *   headroom     is there an authored crawl space over this point
 *   line hit     where does a ray from here to there first strike something solid
 *
 * Read the first one twice. The engine's clearance probe counts only faces carrying the mask it is
 * handed, and the low ceiling flag is the only mask the shipped game ever passes it. So it answers
 * "is there an AUTHORED crawl space over this point", not "is there geometry". It is kept because
 * the engine's own respawn asks it of every seat it considers, and a spot the engine would not
 * seat a player at is not a spot to put a body at either; it is not kept because it finds walls.
 * Finding walls is what the ray is for.
 *
 * Both are optional, and each is optional on its own: a build where one pattern did not resolve
 * asks the other and says once which is missing. Neither is hulled. They are read through the same
 * detour aware search the rest of this tree uses, so another module's jump on the same function is
 * followed rather than tripped over.
 *
 * Neither may be asked from a window message. Both walk the engine's single global polygon list.
 * Ask one while the simulation is between two of its own probes and the answer the engine was in
 * the middle of computing is gone, and a window message may be dispatched inside a substep. The
 * game's own flow is safe: a substep, or the end of the scene, where the substeps of the frame are
 * over and no probe of the engine is half done, which is where the placement mode asks them. The
 * engine's own respawn is careful in exactly this way, and this file cannot enforce it, so its
 * callers have to.
 */
#ifndef OPENPHANTOM_WORLD_PROBE_H
#define OPENPHANTOM_WORLD_PROBE_H

#include <stdbool.h>

/* Resolves what has not been resolved yet and says once what it found. Cheap to call again. */
void world_probe_resolve(void);

/* Whether the headroom question can be put on this executable at all. A caller hands a null probe
 * on where the answer is false, so that the rule skips the question instead of guessing at it. The
 * ray says the same through world_probe_line_hit, which answers false when it never resolved. */
bool world_probe_has_headroom(void);

/* True when a body fits over `at`. False also when the probe never resolved, so ask first. */
bool world_probe_headroom(const float *at);

/* Where along the line from `from` to `to` the ray first strikes something solid: true and the
 * distance from `from` in `distance`, or false when it strikes nothing, the probe never resolved or
 * the world is NULL. `world` is the engine's own world record, which the census holds. The ray is
 * the engine's own, a 0.15 unit sphere cast one grid cell at a time
 * for up to sixty four cells, so a line that crosses more than that is looked at only as far as
 * the cells reach, and a point outside every cell answers "nothing". */
bool world_probe_line_hit(const void *world, const float *from, const float *to, float *distance);

#endif /* OPENPHANTOM_WORLD_PROBE_H */
