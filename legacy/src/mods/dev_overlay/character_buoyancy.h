/* character_buoyancy.h: how deep the player floats, for a body that is not the height his was.
 *
 * The one absolute body measurement in the player module. Every substep the swimmer is on the
 * water, the swim tick pins him with `pos.z += water_zDelta(pos) - 0.72`. That 0.72 is a world
 * constant with no reference to the body it is pinning, and the player's origin is at his feet, so
 * it says "put the feet 0.72 of a unit under the surface" whoever is wearing the body.
 *
 * On the rig the constant was chosen for, the crown of the head stands 0.929 of a unit over the
 * origin, so the water sits at the neck and about a fifth of the body is above the line. Put a
 * shorter body on the same origin and the water climbs it: on a child rig the crown stands 0.745
 * and the water closes over the top of the skull. Nothing refuses, nothing is out of range and the
 * animation is correct; the swimmer is simply under the surface, and that is what a player calls
 * not being able to swim.
 *
 * So the pin is corrected by the ratio of the two bodies, and the correction is expressed as the
 * height the swimmer is lifted after the engine has pinned him:
 *
 *     lift = depth * (1 - wornHeight / ownHeight)
 *
 * At that lift the same FRACTION of the worn body is above the line as of the player's own, which
 * is the only reading of the constant that survives a change of body. A body of the player's own
 * height gets a lift of zero, so wearing your own model changes nothing at all.
 *
 * Why the lift is added after the tick and not subtracted from the constant. The swim tick asks,
 * in this order, whether the floor throws the swimmer off, then pins him, then whether he is still
 * in water, then whether there is a bank in front of him he may climb out on. The climb-out test
 * accepts a lip between 0.25 and 1.0 of a unit above the swimmer and it measures from the position
 * the pin has just written. Lowering the constant would raise the swimmer before that test and take
 * a quarter of a unit off every lip in the level, which can leave a water body with no exit at all.
 * Adding the lift after the tick leaves every test in it standing on the engine's own geometry: the
 * pin is absolute and idempotent, so the next substep overwrites the lifted value before the test
 * reads it. What the lift reaches is the drawn body, the camera and the collision push, which is
 * exactly what was wrong.
 */
#ifndef CHARACTER_BUOYANCY_H
#define CHARACTER_BUOYANCY_H

#include <stdbool.h>
#include <stdint.h>

/* THE PURE HALF, and the half a test can drive. Heights are in world units, measured from the
 * body's own origin to the crown of its head, and `depth` is the engine's own pin.
 *
 * Zero for anything it will not act on: a body of the player's own height, a height or a depth that
 * is not a positive finite number, and a ratio outside a tenth to ten, which is not a body but a
 * misread. Zero always means "leave the engine's pin alone", so every refusal is the safe one. */
float character_buoyancy_lift_for(float depth, float own_height, float worn_height);

/* Whether the ratio of the two heights is one this module will act on at all. The caller reports
 * the case it declines, because a body it cannot measure is worth a line in the log. */
bool character_buoyancy_ratio_is_plausible(float own_height, float worn_height);

/* ============================================================================================ */

/* Resolves the swim tick and puts the correction behind it. Idempotent. False when the site did not
 * resolve, and then a worn body floats at the player's own depth, which is what it did before this
 * module existed. A model swap is still offered in that case: floating wrong is not a crash. */
bool character_buoyancy_install(void);

/* The cell that holds the pointer to the player record. Without it nothing is corrected, because
 * the swimmer's position and the body he is wearing are both reached through it. */
void character_buoyancy_bind_player_record(uintptr_t player_record_cell);

/* Measures the two bodies and arms the correction. `thing` is the render handle the swap is being
 * done on, and it must still be wearing `own_model` at this point: that is the only moment the
 * scale the player's own body is drawn at can be read off the object. A borrow made on top of an
 * earlier borrow arrives here with the earlier body's scale already written, and then the reading
 * taken by the first borrow is kept.
 *
 * False leaves nothing armed, and the caller carries on: the swap is not conditional on this. */
bool character_buoyancy_arm(uintptr_t thing, uintptr_t own_model, uintptr_t worn_model);

void character_buoyancy_disarm(void);

bool character_buoyancy_is_armed(void);

/* The crown of a live model, in the model's own units, measured off the bounding box the loader
 * computed for the mesh that hangs on the node named `head`. Zero when the model, the node or the
 * mesh cannot be read, and a zero declines the correction rather than guessing at it. */
float character_buoyancy_crown(uintptr_t model);

#endif /* CHARACTER_BUOYANCY_H */
