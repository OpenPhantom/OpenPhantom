/* diag_world_path_census.h: the draw path and trace censuses, trigger levels 4 to 6.
 *
 * Every hook that feeds them stays in diag_world.c with the detour it belongs to. What crosses
 * this boundary is a record call per hook and an install call per level.
 */
#ifndef DIAG_WORLD_PATH_CENSUS_H
#define DIAG_WORLD_PATH_CENSUS_H

#include <stdint.h>

/* common/frame_hook.c caps a single DLL at 8 callbacks (MAX_FRAME_CALLBACKS), shared with
 * diag_frame.c's own per-second summary and diag_present.c's hook. Five censuses each registering
 * their own callback blew straight through that and silently starved the other two. This one is
 * defined in diag_world_path_census.c after the report functions it ticks, and is registered from
 * every census's own install function; frame_hook_add is idempotent per callback, so the five
 * install call sites collapse to exactly one slot no matter which trigger sub-levels are actually
 * armed. */
void diag_world_census_tick(void);

/* Level 4: how often the two render-path functions are entered at all. */
void render_census_install(void);
void render_census_count_poly_to_world(void);
void render_census_count_transform_world(void);

/* Level 5: which call site reaches bapmap_polyToWorld. A `poly_address` of 0 arms nothing. */
void poly_census_install(uintptr_t poly_address);
void poly_census_record(const void *return_address);

/* Level 6: which call site reaches each of the two traces. Either address is 0 when that
 * observer did not install, and only the other census is armed. */
void trace_census_install(uintptr_t trace_general_address, uintptr_t trace_floor_address);
void trace_general_census_record(const void *return_address);
void trace_floor_census_record(const void *return_address);

#endif /* DIAG_WORLD_PATH_CENSUS_H */
