#ifndef MOUSE_CONFIG_H
#define MOUSE_CONFIG_H

#include <stdbool.h>

/* Everything the mouse path reads out of the ini, and nothing it does with it.
 *
 * This half was lifted out of mouse_look.c along the seam that file's own size note had been
 * naming: it touches the engine at no point. It reads keys, clamps them, reports the renamed ones
 * and answers questions about what the player asked for. Everything that talks to a device or to
 * the game stayed on the other side.
 *
 * The values are handed out as a read-only struct rather than through one getter each, because they
 * are read on the per-frame path and because a dozen one-line getters is not an interface, it is a
 * struct with extra steps. The two that can change while the game runs have setters, and only those
 * two.
 */

typedef struct mouse_config {
    /* Degrees of view turn per mouse count, and the same divided by the engine's own axis scale
     * once, because everything upstream of the setting is in axis units. */
    float degrees_per_count;
    float degrees_per_axis_unit;

    /* The door bolt's slope, in degrees per second. A bound on a broken device, not on a hand. */
    float max_turn_rate;

    /* The most delay the delivery filter may spend. Its length is the device's own report interval,
     * six of them; this only bounds it. */
    float smooth_ceiling_seconds;

    /* The player left the key out altogether, so the control mode may choose. A written value,
     * including a written zero, is a decision and is never overridden. */
    bool  smoothing_is_automatic;

    /* Collect once per rendered frame rather than once per substep. Switching it off is not a
     * degraded version of this feature, it is the engine's own behaviour restored: one live read
     * per substep with nothing banked, which is what makes the two comparable in one build. */
    bool  accumulate_requested;

    bool  raw_requested;          /* read the device directly rather than through the engine */

    /* The menu pointer, which is a different consumer of the same device. The engine moves it by a
     * whole-pixel difference from the system pointer, which loses everything under one pixel and
     * loses travel at the screen edge; this drives it from the device instead. It needs the raw
     * reader, so it is ignored when that is off or silent. */
    bool  menu_cursor_raw;

    bool  log_samples;            /* one line per sample the door bolt had to cut */
} mouse_config_t;

/* Reads every key, clamps every value and reports each renamed key exactly once. Safe to call
 * before anything is resolved; it touches no engine memory. */
void mouse_config_load(void);

/* The values in force. Never NULL, and zero-filled until the load above has run, so a caller that
 * runs too early gets a harmless nothing rather than a wild pointer. */
const mouse_config_t *mouse_config(void);

/* Applies a new sensitivity and saves it. Clamped to the accepted range; a value that is not a
 * number is refused rather than clamped, because turning it into the minimum would silently make
 * the mouse the slowest it can be. Returns false when the ini could not be written, in which case
 * the value is in force for this session and the caller is expected to say so. */
bool mouse_config_set_degrees_per_count(float degrees_per_count);

/* Raises the smoothing ceiling to this build's default for the per-frame view path, unless the
 * player wrote the key themselves.
 *
 * It exists because the right ceiling is not a property of the mouse alone but of what consumes it.
 * A simulation step is three times as long as a frame, so the same device puts three times as many
 * reports into it and the sampling boundary matters three times less; a feature that draws the view
 * per frame therefore needs the filter that the per-step path can do without. Called once, by
 * whatever arms that path, and only after it knows it is really running. */
void mouse_config_use_frame_clock_smoothing(void);

#endif /* MOUSE_CONFIG_H */
