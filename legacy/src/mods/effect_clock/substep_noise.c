/* substep_noise.c: the arithmetic effect_clock answers with, with no engine in it. */
#include "substep_noise.h"

#include <stdint.h>

/* The golden ratio odd constant, the usual integer mixer. */
#define MIX_CONSTANT        0x9E3779B1u

/* The halo mixes with constants of its own, so an object that both flickers and carries a halo does
 * not take the two decisions from the same number. */
#define HALO_TICK_CONSTANT  0x85EBCA6Bu
#define HALO_FINAL_CONSTANT 0xC2B2AE35u

uint32_t substep_noise_tick(uint32_t counter, uint32_t substeps_per_roll)
{
    /* A zero cannot come out of the ini, which is clamped into 1 to 32 before it reaches this far.
     * It is read as one anyway: this runs on the drawing path, where there is nobody to report a
     * refusal to and a division by zero would take the game down with it. */
    if (substeps_per_roll == 0u) {
        substeps_per_roll = 1u;
    }
    return counter / substeps_per_roll;
}

uint32_t substep_noise_arc_seed(uint32_t tick)
{
    /* An odd multiplier is invertible over a 32 bit word, so two steps never pin the same seed and
     * never rebuild the same bolt. The seeds themselves lie one constant apart, which does no harm
     * here: 0x0049A580 multiplies whatever it finds by 214013 before returning anything, so what an
     * arc actually draws bears no visible relation to the previous step's. */
    return tick * MIX_CONSTANT;
}

uint32_t substep_noise_flicker(uint32_t tick, uint32_t ordinal)
{
    /* The ordinal is taken plus one so that the first object of a frame contributes something. At
     * plus nothing it would contribute zero, the answer would come from the substep alone, and
     * every flickering object in the room would go dark on the same step.
     *
     * For any one substep there is exactly one ordinal at which the two terms are the same number.
     * They cancel, the mixer maps zero to zero, and zero is under the 0.2 the engine compares
     * against, so that one object is skipped for that one step. Both terms are built with the same
     * constant here, so the ordinal it happens at is the substep number minus one, which is small
     * enough that a frame with a handful of flickering objects meets all of them in the first
     * moments of a level and then never again, since the substep number only grows. Against an
     * original that skipped the same object several times a second that is a far smaller artefact,
     * but it is not nothing, so it is written down rather than discovered later. */
    uint32_t h = (tick * MIX_CONSTANT) ^ ((ordinal + 1u) * MIX_CONSTANT);

    h ^= h >> 15;
    h *= MIX_CONSTANT;
    h ^= h >> 13;
    return h & SUBSTEP_NOISE_MAX;
}

uint32_t substep_noise_halo(uint32_t tick, uint32_t ordinal)
{
    /* The differing constants are also what keeps the flicker's cancellation out of the halo. There
     * is still exactly one ordinal per substep at which the two terms agree, but with two different
     * constants that ordinal is a large and effectively arbitrary number rather than the substep
     * minus one, and no frame holds anywhere near enough objects to reach it. */
    uint32_t h = (tick * HALO_TICK_CONSTANT) ^ ((ordinal + 1u) * MIX_CONSTANT);

    h ^= h >> 16;
    h *= HALO_FINAL_CONSTANT;
    h ^= h >> 13;
    return h & SUBSTEP_NOISE_MAX;
}
