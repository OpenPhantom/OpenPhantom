/* world_clock.h: put movers back on the simulation's own lattice instead of the frame's.
 *
 * THE DEFECT, and it is the cause rather than a symptom. The substep loop sets the world clock to
 * the end of each substep CLAMPED to the frame's target time, so the last substep of a frame
 * leaves the clock short. A mover derives its own step from the difference between two of those
 * values, so its sample pair spans the gap between frames rather than one simulation step: at 60
 * frames a second that is 33.3 ms seven times in eight and 16.7 ms once, from 8 substeps every 15
 * frames. The draw then blends that pair on the substep alpha, which is a phase within a 31.25 ms
 * step. The drawn advance per frame therefore comes out at 1.07 times true speed on the wide
 * intervals and 0.53 on the narrow one: a half-speed step lasting one substep, roughly every
 * 250 ms. Periodic, and small, so it reads as a touch of jitter. At the authored
 * 30 fps the same mismatch is 33.3 against 31.25, seven per cent, so it never showed.
 *
 * Three attempts were made to compensate for the mismatch in the draw and all three failed, two
 * of them because a guard silently refused the weights they produced and the third because the
 * platform stopped agreeing with the character standing on it. The pair and the alpha have to be
 * on the same lattice; nothing applied afterwards fixes them being on different ones.
 *
 * So this removes the clamp instead. Handed the clamped value and the one before it, it answers
 * with a whole substep past the previous one, which is the value the loop would have set had it
 * not clamped. Every mover then integrates exactly one step per substep, its pair spans exactly
 * one step, and the alpha the draw already applies is exactly right for it, for the rider, and at
 * any frame rate. No new weight, no extrapolation.
 *
 * What it costs, and the reason it ships off. The world clock is a value the simulation reads,
 * so this is a change to how movers move rather than to how they are drawn. Un-clamped, it runs
 * up to one substep ahead of the frame's own time, which is where the object simulation already
 * is. The average rate is untouched: the loop runs the same number of substeps and each now
 * advances the clock by exactly one step, so total world time still tracks real time to within a
 * step. What shifts is phase, by less than 31 ms, for every other reader of that clock: the timer
 * and dwell opcodes, the sound scheduler, the light ramps, the surface animation and the save
 * counter.
 *
 * A constant offset also remains, and is deliberately left. The first value after a level opens
 * has no predecessor to measure from and is passed through as it arrives, so the whole lattice
 * sits up to one substep away from the simulation's own. It is bounded by one step, it moves a
 * platform's drawn phase by at most 31 ms, and it moves nothing relative to anything else,
 * because everything drawn is indexed by the same alpha.
 *
 * It is constant because the requested value is never consulted once there is a previous one.
 * That is worth stating, since the first version of this did consult it: it passed the requested
 * value through whenever that was already a step or more ahead, which is true of every substep
 * but the last of a frame. Those values are on the SIMULATION's lattice rather than this one, so
 * the first frame to run two substeps snapped across to it. The offset was therefore not constant
 * at all, and the frame it moved on advanced every mover by 1.4 steps instead of one. Simulated
 * against the decompiled loop rather than reasoned about: at a steady 60 fps the fault never
 * appears, at 25 it is gone by the first frame, and one injected 50 ms frame is enough to show it.
 */
#ifndef WORLD_CLOCK_H
#define WORLD_CLOCK_H

#include <stdbool.h>

/* The substep period. The rate selector in the substep loop chooses between 1/32 and 1/64 on the
 * "60fps" cheat cell, and that cell has no writer anywhere in the image: a four byte scan finds
 * two references, both of them compares, and it sits past the end of the raw data, so it is zero
 * and the 1/64 arm cannot be selected from the shipped image. Were it ever really 1/64 this would
 * advance the clock twice as fast as the loop intended, so the value is named here with
 * its evidence rather than passed in from a caller that would have to guess it too. */
#define WORLD_CLOCK_SUBSTEP_SECONDS 0.03125f

/* The world clock time to hand the engine for a substep.
 *
 * `requested` is what the loop asked for, already clamped to the frame target. `previous` is the
 * value handed over for the substep before, and `have_previous` is false for the first call of a
 * level, where there is nothing to un-clamp against.
 *
 * Answers `requested` unchanged whenever it cannot do better: the first call, a clock that has
 * gone backwards because a level opened, a step that is not positive, and anything not finite.
 * Otherwise it answers `previous + step` and does not consult `requested` at all. An earlier
 * version passed a requested value through whenever it was already a step or more ahead; the
 * reason that was wrong is at the return in world_clock.c, and it was measured rather than
 * argued.
 */
float world_clock_substep_time(float requested, float previous, bool have_previous, float step);

#endif /* WORLD_CLOCK_H */
