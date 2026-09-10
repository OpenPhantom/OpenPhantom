/* mover_weight.h: how far along its own last move a mover should be drawn.
 *
 * The engine's objects and the engine's movers are on different clocks, and reading the second one
 * the way the first works is what left a platform a touch jittery after it stopped stepping.
 *
 * A mover moves once per RENDERED frame, from bapmap_tickMovers on the frame broadcast, and it
 * integrates against the world clock, which only the substep loop writes. The substep loop sets
 * that clock to the end of each substep CLAMPED to the frame's own target time, so the last
 * substep of a frame leaves the world clock standing exactly at the moment that frame is meant to
 * represent. Two consequences, and the second is the one that was missed:
 *
 *   a frame that ran at least one substep leaves the mover exactly where the frame wants it, so
 *   there is nothing to interpolate and the alpha should not be applied at all;
 *
 *   and a frame that ran NO substep, which above 32 frames a second is most of them, leaves the
 *   clock untouched, tickMover short-circuits on its own time base, and the mover is drawn where
 *   it was when the last substep ran.
 *
 * So the mover's newest pose is never in the future and usually in the past, by however much
 * render time has passed since the last substep. Blending back toward the older pose on the
 * substep alpha, as shipped, therefore draws it at a time that has nothing to do with
 * either: measured against a 60 fps lattice it lands between 24 and 27 ms behind, and it is the
 * variation rather than the size of that lag that the eye reads as jitter.
 *
 * What is known exactly, without reading any absolute clock. The simulation time only ever moves
 * in whole substeps, and the alpha is the phase of the frame's target between the last two of
 * them, so the render time that has passed since any earlier frame is the substep period times the
 * change in the alpha. Take the alpha when the mover last integrated, subtract it from the alpha
 * now, multiply by the period: that is `elapsed`. The mover's own time base gives `interval`, how
 * much world time its last move covered. Both are exact and neither grows stale with level time,
 * which matters because the level clock is a float32 and its resolution decays as a level runs.
 *
 * Then `elapsed / interval` is the phase, and the two answers differ by exactly one: the phase
 * alone holds the mover one whole interval behind, and one plus the phase asks for the frame's own
 * time.
 *
 * Both were played, both looked worse, and neither had run. That is the part worth carrying.
 *
 * mover_blend_world guarded its weight with the open interval (0, 1), which is right for a weight
 * that is the substep alpha and wrong for anything else. Mode 1 produces [0, 1] and mode 2
 * produces [1, 2], so the guard refused every pose mode 2 ever computed and every pose mode 1
 * computed on an integrating frame. The caller draws the newest pose on a refusal, so mode 2 was
 * the untouched stepping it existed to remove, and mode 1 alternated a refusal with a blend, which
 * is a forward snap and a drop back on every tick. Reported from play as twice as jittery and as
 * worse again, and both descriptions were accurate about the picture and told us nothing about the
 * arithmetic, because the arithmetic had not executed once.
 *
 * Two lessons, and the second is the expensive one. A guard that silently substitutes a fallback
 * cannot be diagnosed from the screen, because a refused feature and a wrong feature look the
 * same. And the instrument built to settle it could not have: it counted refusals in one bucket
 * without recording which guard fired, so it would have reported a sane-looking measurement
 * beside a mover that was never blended.
 *
 * The range is explicit now, at MOVER_BLEND_WEIGHT_MAX, and the arithmetic above is untested in
 * a game rather than refuted by one.
 */
#ifndef MOVER_WEIGHT_H
#define MOVER_WEIGHT_H

#include <stdbool.h>
#include <stdint.h>

/* The substep period. The rate selector inside the substep loop chooses between 1/32 and 1/64 on
 * the "60fps" cheat cell, and that cell has no writer anywhere in the image: a four byte scan
 * finds two references, both of them compares, and it sits past the end of the raw data so it is
 * zero. The 1/64 arm cannot be selected from the shipped image, so this is the period. Were it
 * ever really 1/64, `elapsed` would come out twice too large, the phase would be clamped at its
 * limit below, and a mover would be drawn at its newest pose rather than between two, which is
 * what the game did before any of this existed. */
#define MOVER_SUBSTEP_SECONDS 0.03125f

enum {
    /* The substep alpha, applied as though a mover were an object in the engine's draw list. What
     * shipped, kept so that one play session can hold it against the others rather than needing a
     * rebuilt DLL to go back. */
    MOVER_WEIGHT_ALPHA = 0,
    /* One interval behind. The interval is not constant, so neither is this lag: at 60 frames a
     * second a mover integrates mostly every second frame and occasionally on two consecutive
     * ones, and the drawn moment then advances by those intervals rather than by frame deltas. */
    MOVER_WEIGHT_LAG = 1,
    /* Exactly where the frame wants it, by extrapolating past the newest pose. Needs no
     * assumption about the interval at all, so it was the candidate worth a play
     * session now that the guard lets it run. */
    MOVER_WEIGHT_LEAD = 2,
    /* Draws with the alpha, exactly as mode 0, and reports what the measurement underneath modes
     * 1 and 2 actually comes out as. Both of those were played and both were WORSE than the alpha
     * they replaced, mode 1 roughly twice as jittery and mode 2 worse again, and the arithmetic
     * above says neither should be. So one of the two quantities it rests on is not what it is
     * assumed to be, and the honest way to find out which is to read them out of a running game
     * rather than to derive another frame lattice by hand. This is that instrument. */
    MOVER_WEIGHT_REPORT = 3
};

/* The one withdrawn before it was played, and the engine fact that killed it. The fact is
 * worth more than the mode was.
 *
 * The idea was to stop differencing alphas and state the drawn moment instead: have the snapshot
 * record which simulation step it was taken on, from the engine's own counter, and ask for the
 * moment one step behind the simulation. A snapshot on the current step then wants the alpha, one
 * a step ago wants the alpha plus one, and the same moment comes out either way regardless of
 * whether the mover was ticked before the draw or after it.
 *
 * It does not work, because of where the counter is incremented. The substep loop runs
 * setWorldClock, then the task scheduler, then the module broadcast, and only THEN the counter,
 * before advancing the simulation time. The mover a character stands on is ticked from inside the
 * task scheduler by the carry, so it reads a counter one behind the substep it is actually being
 * ticked for, while every other mover is ticked by the frame sweep after the loop and reads the
 * right one. The correction would therefore have come out one whole step too large for the ridden
 * mover alone, pushing the platform a step in front of the character on it, which is the same
 * fault the extrapolating mode was refused for.
 *
 * Keyed off the world clock instead of the counter, both kinds of mover give the same answer and
 * the correction collapses to zero, which is the substep alpha, which is mode 0. So with
 * MoverSubstepClock on there is nothing here to correct: mode 0 is already the right weight for
 * both, so no mode 4 exists.
 */



/* The phase is clamped here rather than left to the travel guard.
 *
 * A phase above one means more render time has passed since the mover last moved than its last
 * move covered, which is a hitch, a mover that has stopped being ticked, or a level boundary. The
 * honest answer for all three is the newest pose the engine produced, because there is no evidence
 * about where the mover would have gone; continuing to extrapolate would invent it. */
#define MOVER_WEIGHT_PHASE_LIMIT 1.0f

/* There is no stale pose on the draw path, and two modes were built on the belief that there was.
 *
 * bapvrt_transformWorld, the world draw itself, calls bapmap_tickMover(mover, world->worldTime) at
 * 0x419B2C immediately before the bapmap_matMul3 and mat34_invertRigid that this DLL redirects,
 * every time the draw moves to a new mover subnode. Movers are integrated LAZILY, from the draw.
 * So every drawn mover is already at the current world clock at the instant it is drawn, whatever
 * order the modules were installed in, and it is also why a stall census counted thousands of tick
 * calls per frame arriving through this DLL's own trampoline.
 *
 * That kills a line of reasoning three modes came out of. The claim was that the mover a character
 * stands on is ticked inside the substep loop by the carry while every other mover waits for the
 * frame sweep, so some poses on screen predate the current step boundary. They do not. Whoever
 * ticked a mover earlier in the frame, the draw ticks it again before reading it.
 *
 * A fourth mode computed the staleness as (world_now - pose_world) / step, where world_now was the
 * float last handed to bapmap_setWorldClock and pose_world was the argument of bapmap_tickMover,
 * which the engine reads back out of that same cell. Bit-identical, so the staleness was always
 * zero and the mode drew exactly what mode 0 draws. It was played and judged to have improved a
 * measurement from 3.222 to 2.666, which is two windows of identical code disagreeing and is the
 * clearest evidence available that the measurement was noise.
 *
 * The lesson worth keeping is not about movers. Three of the four modes here were corrections for
 * a lag that did not exist, and each was believed because a play session looked different
 * afterwards.
 */

/* The weight to blend a mover's previous pose toward its current one, for `mode`.
 *
 * `elapsed` is the render time since the mover last integrated and `interval` is how much world
 * time that integration covered, both in seconds. False means there is no weight to be had and
 * the caller must draw the engine's own newest pose unblended: an interval of zero or less, and
 * anything not finite. A negative `elapsed` answers with the start of the interval rather than
 * refusing, because the alpha is allowed to leave (0,1] right after a rate change and that is a
 * phase slightly before the pose rather than a broken measurement.
 */
bool mover_weight_for(int mode, float elapsed, float interval, float *out_weight);

#endif /* MOVER_WEIGHT_H */
