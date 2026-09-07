/* enhanced_input_internal.h: the two phase thunks' shared state, and nothing else.
 *
 * The engine calls this DLL at exactly two points in the player pipeline, phase 2 to steer and
 * phase 7 to integrate, and they are one mechanism split across two moments: phase 2 decides what
 * the substep's input means and phase 7 spends it. Eleven fields pass between them, so they cannot
 * each own their own copy, and until this header existed they were kept together by living in the
 * same file. That file reached the size limit, which is a poor reason to move something but a fine
 * moment to notice that "the steer" and "everything else" were always two responsibilities.
 *
 * So the seam is the phase, not the state: input_steer.c owns phase 2, enhanced_input.c owns phase
 * 7, the install and the queries, and this declares the one struct they both reach for. Modelled on
 * free_look_internal.h, which answers the same question for that feature's four files.
 */
#ifndef ENHANCED_INPUT_INTERNAL_H
#define ENHANCED_INPUT_INTERNAL_H

#include "player_sites.h"

#include <stdbool.h>
#include <stdint.h>

typedef void(__cdecl *phase_fn_t)(void);

typedef struct enhanced_input_state {
    bool                installed;

    player_sites_t      sites;
    phase_fn_t          original_steer;
    phase_fn_t          original_integrate;

    /* What phase 2 saw and phase 7 is to consume. One frame of lifetime, not state. */
    bool               turn_wheel_is_ours;
    bool  pending_valid;
    float pending_yaw_degrees;
    float pending_travel_degrees;   /* how far the walk is turned off the body's heading */

    /* Set by phase 2, read and cleared by phase 7. It is how phase 7 knows whether the body angle
     * has already been stepped this substep: both phases run in Stand, but only phase 7 runs while
     * swimming or in a launched sidestep, and the angle must be stepped exactly once either way. */
    bool  steer_ran_this_substep;

    bool  logged_steer;
    bool  logged_integrate;
    bool  logged_first_input;
    bool  warned_about_mode;
} enhanced_input_state_t;

/* The one instance. Defined in enhanced_input.c, which owns the install that fills it in. */
extern enhanced_input_state_t input_state;

/* Phase 2. Defined in input_steer.c; named here so the install can put it in the phase table. */
void __cdecl enhanced_input_steer_thunk(void);

/* The three the two phases share. Small enough that a call is not worth the reach, and identical
 * in both, so they are declared here rather than copied. */
float enhanced_input_clamp(float value, float minimum, float maximum);
float enhanced_input_read_field(const uint8_t *record, int offset);
void  enhanced_input_write_field(uint8_t *record, int offset, float value);

#endif /* ENHANCED_INPUT_INTERNAL_H */
