/* steer_lean.h: give mouse look back the upper-body twist the engine builds from its own turn.
 *
 * Plr_Steer ends by twisting two nodes about the model's up axis, from the turn rate it has just
 * clamped to +-120 deg/s:
 *
 *     setNodeYaw(hActor, chestNode, turnWheel / 12.0)
 *     setNodeYaw(hActor, headNode,  turnWheel / 10.0)
 *
 * How big that really is, and the trap that cost two rounds: the keyboard ramp has a ceiling of
 * 40 deg/s, and that is the ceiling of the INCREMENT, not of the cell. `turnWheel += ramp * axis`
 * ACCUMULATES, so a held key climbs 12, 26, 42, 60, 80, 102, 120 and saturates the +-120 clamp in
 * seven substeps, about a fifth of a second. Chest 1.0 -> 10.0 deg, head 1.2 -> 12.0 deg.
 *
 * That climb is the engine's own ease-in, which is why nothing here damps the value a second time.
 *
 * That is the body leading into a corner before the feet follow. Mouse look has to clear the turn
 * cell; it carries the mouse as well as the keys, so leaving it standing integrates the mouse a
 * second time and the engine's turn penalty then brakes the player, and by the time the cell is
 * cleared the twist has already been computed from the engine's own number. Hence: centred body.
 *
 * The fix does not fight over that cell. The setter is an ABSOLUTE store, so the two calls are
 * simply re-issued afterwards with the turn the player actually made, through the engine's own
 * divisors and the engine's own clamp.
 *
 * ---- Why the chest is not cosmetic, and the head is ------------------------------------------
 * The weapon and the sabre blade hang off the chest node, and the auto-aim measures its bearing
 * from that node's world position. A chest twist therefore moves the muzzle. This is not a new
 * effect, the shipped engine twists the same node by the same formula every substep, but it is
 * why the chest write is skipped while the aim claims the node, and why the amplitude is kept at
 * the engine's own clamp rather than being made adjustable.
 *
 * The head node has exactly one writer in the whole image: the line this file re-issues.
 */
#ifndef STEER_LEAN_H
#define STEER_LEAN_H

#include "player_record.h"

#include <stdbool.h>
#include <stdint.h>

void steer_lean_bind(set_node_yaw_fn_t set_node_yaw, bool enabled);

/* Re-issues both node writes and reports whether anything was written. Must be called AFTER the
 * original phase 2 and BEFORE the turn cell is overwritten, because `engine_rate` has to be the
 * value the ENGINE just computed.
 *
 *   engine_rate   [pPlr+0x2A4] as the original left it: the ramped keyboard turn (0..40 deg/s) or
 *                 the clamped mouse turn (up to 120), whichever the engine saw. AUTHENTIC.
 *   fallback_rate our own view rate for this substep, used only where the engine saw nothing,
 *                 the substeps its once-per-frame device poll missed.
 *
 * Refuses silently on a substep length it cannot believe; the engine's own twist then stands. */
bool steer_lean_apply(const uint8_t *record, float engine_rate, float fallback_rate,
                      float substep_seconds);

/* Forgets the damper state. No node is written: whichever substep stops calling apply() has
 * already taken the engine's own twist from the original, so there is nothing to hand back. */
void steer_lean_release(void);

/* Writes the chest to an explicit angle and reports whether it did. For the free-look aim, where
 * the number is not a turn rate at all but the angle between the shot and the body. Must be called
 * AFTER the original phase 2: Plr_Steer writes that node unconditionally every substep, with no
 * gate, so it is the last writer before the draw and anything written earlier is lost. */
bool steer_lean_aim(const uint8_t *record, float degrees);

/* For the log line, so it can name what is actually in force. */
bool steer_lean_is_active(void);

/* What the last call did, and, the part that matters when nothing is visible. WHICH GUARD it
 * left through. Every early return in steer_lean_apply is silent by design, because a warning per
 * substep would be worse than the defect; this is how those silences are read back. */
typedef struct steer_lean_report {
    const char *status;        /* "engine"  the engine's own value was reissued
                                * "filled"  ours was used because the cell held zero
                                * "FORCED"  the diagnostic angle
                                * "released" / "off" / "no-record" / "bad-dt" / "bad-angle" /
                                * "no-body" / "no-nodes" / "not-called"                    */
    uint32_t    chest_node;
    uint32_t    head_node;
    float       raw_rate;      /* degrees per second, as chosen */
    float       applied_rate;  /* what was actually divided by 10 and 12 */
    float       chest_claim;   /* [pPlr+0x178]; non-zero means the aim owns the chest */
    bool        wrote_head;
    bool        wrote_chest;
} steer_lean_report_t;

void steer_lean_last_report(steer_lean_report_t *out);

/* Diagnostic: force a fixed twist on both nodes every substep, ignoring turn, damper and clamp.
 * 0 disables. It answers one question only, does a yaw on these nodes reach the screen. */
void steer_lean_set_test_degrees(float degrees);

#endif /* STEER_LEAN_H */
