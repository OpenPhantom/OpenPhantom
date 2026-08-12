/* substep_noise.h: the numbers effect_clock answers the engine with in place of a random draw.
 *
 * Two of the three sites this DLL repairs cannot be bracketed, so their single call to the engine's
 * generator is redirected into a replacement instead. What comes back has to pass for a draw from
 * that generator, and that fixes the range. 0x0049A580 ends
 *
 *   0049A5A1  C1 F8 10        sar eax,16
 *   0049A5A4  25 FF 7F 00 00  and eax,0x7FFF
 *
 * so it answers between 0 and 32767, and both redirected call sites scale what they get by 1/32767
 * before using it: the flicker test at 0x00411449 through [0x004A812C], the halo at 0x00439FCE
 * through [0x004A8528]. An answer outside that band would mean patching the engine's own scaling as
 * well, which is exactly what this avoids.
 *
 * Everything here is a pure function of its arguments. The hook state, the engine cells it reads
 * and the sites it patches stay behind in effect_clock.c, because the half that can be checked
 * without the game is worth separating from the half that cannot.
 */
#ifndef SUBSTEP_NOISE_H
#define SUBSTEP_NOISE_H

#include <stdint.h>

/* The largest answer any of these gives, and the range the engine's own scaling expects. It is one
 * less than a power of two, so the bound is also the mask that produces it. */
#define SUBSTEP_NOISE_MAX 32767u

/* Which simulation step the effects are currently allowed to see: the engine's substep counter
 * divided by how many steps an effect keeps its value. The divisor is the ini setting, already
 * clamped into 1 to 32 by the installer, and a zero is read as one rather than divided by. */
uint32_t substep_noise_tick(uint32_t counter, uint32_t substeps_per_roll);

/* The value pinned into the generator's one word of state at 0x004BA61C for the duration of the arc
 * pool call. The property it must have is that no two steps share one, because a shared seed
 * rebuilds the same bolt. */
uint32_t substep_noise_arc_seed(uint32_t tick);

/* The flicker test's answer for the object at `ordinal` in this frame's draw order. The engine
 * divides it by 32767 and, when the result is under the 0.2 at [0x004A8130], skips the object
 * entirely, animation advance included. */
uint32_t substep_noise_flicker(uint32_t tick, uint32_t ordinal);

/* The halo jitter's answer for the halo at `ordinal` in this frame's draw order. The engine divides
 * it by 32767, multiplies by the 0.4 at [0x004A852C] and adds that to the halo's intensity before
 * clamping into 0 to 1. */
uint32_t substep_noise_halo(uint32_t tick, uint32_t ordinal);

#endif /* SUBSTEP_NOISE_H */
