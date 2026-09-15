/* freecam_world.h: the free camera's "world runs while flying" switch.
 *
 * The flight always holds the simulation, since a camera that roams while the player it left
 * behind keeps taking the flight keys as orders is no use; but a world held still is no use
 * either for watching a fight or a crowd from where the camera can go. So the switch: with it
 * on, the flight lets the simulation run under its hold (sim_pause_let_run) and holds the
 * player's input instead (input_freeze_hold), the same hold the panel takes while it is open,
 * so the keys steer the camera alone and the player stands where they were left while everyone
 * else carries on. Off, the flight is the freeze it always was.
 *
 * Kept in the settings file as [dev_overlay] FreeCameraWorldRuns, read once at install, and
 * applied on the next frame of a flight already under way. The row is in the Free camera group.
 */
#ifndef DEV_OVERLAY_FREECAM_WORLD_H
#define DEV_OVERLAY_FREECAM_WORLD_H

#include <stdbool.h>

/* Reads the setting. */
void freecam_world_load(void);

/* Whether the world keeps moving under the camera while it flies. The set writes the file. */
bool freecam_world_runs(void);
void freecam_world_set_runs(bool runs);

/* While a flight is under way, once a frame, and on its first frame: puts the simulation and the
 * player's input in the state the switch asks for. Both calls underneath are idempotent. */
void freecam_world_apply(void);

/* The flight has ended: the simulation is held again for whoever still holds it, and the
 * player's input is let go. */
void freecam_world_release(void);

#endif /* DEV_OVERLAY_FREECAM_WORLD_H */
