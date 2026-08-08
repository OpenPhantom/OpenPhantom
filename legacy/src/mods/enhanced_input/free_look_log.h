#ifndef FREE_LOOK_LOG_H
#define FREE_LOOK_LOG_H

#include "camera_sites.h"
#include "free_look_math.h"

#include <stdbool.h>
#include <stdint.h>

/* One line per CHANGE of free look's arming gate, and nothing else.
 *
 * A release is the one event in this feature that a player can see and nobody can name. It writes
 * no byte, changes no setting and produces no message: the feature simply stops writing, the
 * engine's own damper takes the camera back toward the region's authored yaw, and what the player
 * sees is the camera turning on its own. Ten conditions can cause it.
 *
 * So this module exists to answer "why did the camera do that". It is deliberately a separate
 * responsibility from the feature: it owns four state fields of its own, it is the only place that
 * reads the camera object for diagnosis rather than for control, and it is bounded, a player
 * standing exactly on a region boundary can flip the gate every frame, and a per-frame log is both
 * forbidden and useless.
 */

/* Arms the module. `camera` must outlive it; only `view` and `region_yaw` are read, and only for
 * the diagnostic line. `enabled` false makes every function below a no-op that costs one test. */
void free_look_log_init(const camera_sites_t *camera, bool enabled);

/* True when this (armed, reason) pair differs from the one last logged, in which case it is
 * recorded as the new state. False when the log is switched off. Call it before doing the thing
 * that is being logged, so the line can still quote what is being given up. */
bool free_look_log_is_new(bool armed, free_look_release_t reason);

/* Writes the line. `note` is never NULL, pass "" for none. `wanted_yaw` is what free look is
 * asking for and is ignored when `wanted_valid` is false, which is what a dropped yaw looks like.
 * `region` may be NULL. */
void free_look_log_transition(bool armed, free_look_release_t reason, const char *note,
                              const free_look_gate_t *gate, const uint8_t *region,
                              bool wanted_valid, float wanted_yaw);

/* How many lines this module will write in a session before it goes quiet, for the install log to
 * quote rather than duplicate. */
int free_look_log_line_budget(void);

#endif /* FREE_LOOK_LOG_H */
