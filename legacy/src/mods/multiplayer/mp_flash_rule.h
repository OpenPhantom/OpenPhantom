/* mp_flash_rule.h: whether a detonation is one the player would see.
 *
 * Layer 1, pure. The engine has no distance test for the white screen a thermal detonator draws:
 * it fills the picture wherever the detonation is, which in single player is always the player's
 * own throw and in a session is anyone's. This is the rule that replaces "always".
 */
#ifndef MULTIPLAYER_MP_FLASH_RULE_H
#define MULTIPLAYER_MP_FLASH_RULE_H

#include <stdbool.h>

/* The cosine of the half angle that counts as "in the picture". 0.5 is sixty degrees to each
 * side, wider than the field of view the game ships with at any aspect ratio, and that is
 * deliberate: the rule exists to take away the flashes of detonations BEHIND the player and
 * around corners, not to decide what is on screen. Too wide lets through a flash that happens
 * today anyway; too narrow takes away one the player was looking straight at, and that is the
 * failure nobody would report as a bug. */
#define MP_FLASH_IN_VIEW_COS 0.5f

/* Whether a detonation at `at` is one the player at `eye`, facing `heading_deg`, would see. Near
 * enough counts whatever he faces; beyond that it has to be inside the cone.
 *
 * The heading is the engine's own, and the forward axis that goes with it is the second row of a
 * world matrix built from the euler angles, which at heading alone is (-sin, cos, 0): this engine
 * looks along Y and stands Z up. Height does not narrow the cone, because a detonation high over
 * the corridor a player faces is in his picture.
 *
 * Nothing to measure answers yes, which is what the engine does today. */
bool mp_flash_is_seen(const float eye[3], float heading_deg, const float at[3], float near_units);

#endif /* MULTIPLAYER_MP_FLASH_RULE_H */
