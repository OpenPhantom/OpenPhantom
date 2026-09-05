/* cinematic_gate.h: is a script holding the camera right now.
 *
 * The camera compensation in this DLL retunes the engine's own smoothing so that a camera which
 * FOLLOWS a target behaves at any frame rate the way it did at the thirty the constants were
 * chosen for. That is the right correction for the player's camera and the wrong one for a
 * cutscene camera, which is not following anything: the script places it outright, once per
 * simulation substep, and a follow filter run over a placed camera lags or overshoots the position
 * the script just set. The module's own log line says which way it goes wrong.
 *
 * Measured rather than assumed: on the race opening, with the compensation switched off, the
 * geometry dropping in and out during the camera's flight up the cliff became markedly less. That
 * is what this gate is for. While a script owns the camera the compensation is handed a scale of
 * one, which reproduces the engine's constants exactly, so a cutscene plays as the retail game
 * played it and normal play keeps the fix.
 *
 * The signal is the engine's own cinematic lock. It is set before the substeps of the frame that
 * starts the cutscene and cleared after the last one, so it is never a frame late, which matters:
 * a gate that lagged by a frame would itself introduce the jitter it is meant to remove.
 */
#ifndef COMMON_CINEMATIC_GATE_H
#define COMMON_CINEMATIC_GATE_H

#include <stdbool.h>

/* Resolves the lock. Safe to call once; returns false when the site did not resolve, and the gate
 * then answers false for the rest of the session so the compensation behaves exactly as it did
 * before this existed. */
bool cinematic_gate_install(void);

/* True while the engine holds its cinematic lock, i.e. a script rather than the player owns the
 * camera. False when the lock did not resolve. */
bool cinematic_gate_script_owns_camera(void);

#endif /* COMMON_CINEMATIC_GATE_H */
