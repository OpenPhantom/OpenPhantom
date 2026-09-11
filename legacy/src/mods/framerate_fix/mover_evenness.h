/* mover_evenness.h: whether what a mover draws actually advances evenly.
 *
 * The question this answers keeps coming back, and answering it by eye costs a play session and
 * still leaves an argument. It is also easy to measure badly. An earlier version divided the
 * largest consecutive-frame step a subnode made by the smallest, which is a speed range rather
 * than jitter: a lift easing into its stop, a door decelerating and a platform reversing all score
 * three, six or twenty while moving perfectly smoothly. It reported a fault where there was none
 * and credited a change that provably drew identical output.
 *
 * Jitter is a frame whose step disagrees with its NEIGHBOURS, not a window whose steps span a
 * range. So each frame's drawn step is compared against the mean of the step before it and the
 * step after it, and a frame counts as uneven when it differs from that mean by more than a few
 * per cent. Smooth motion at any speed, accelerating or decelerating, passes. A frame that jumps
 * and the frame that holds after it both fail, which is the pair a stepped mover produces.
 *
 * What it cannot tell you, and it matters. A drawn object correctly moves further on a longer
 * frame, so this conflates an arithmetic fault with a genuinely uneven frame time. At a steady
 * frame rate it is trustworthy: measured at a matched 144 Hz where the frame time held to two
 * microseconds, it read single figures per thousand. Uncapped at 300 frames a second, where the
 * frame time swings by a factor of three, the same measurement reads 35 to 40 per cent uneven
 * with nothing wrong at all. Read it only against a steady rate.
 *
 * Off unless LogMoverEvenness asks for it, because it costs a square root per drawn pose per
 * frame and nobody needs it while nothing is being investigated.
 */
#ifndef MOVER_EVENNESS_H
#define MOVER_EVENNESS_H

#include <stdbool.h>
#include <stdint.h>

/* One subnode's own history. Three consecutive frames are needed before anything is judged. */
typedef struct mover_evenness_state {
    float    last[3];
    uint32_t stamp;
    float    step_before;
    float    step_middle;
    bool     have_before;
    bool     have_middle;
    bool     seen;
} mover_evenness_state_t;

/* Switches the whole thing on or off and forgets the window. */
void mover_evenness_enable(bool enabled);
bool mover_evenness_enabled(void);

/* One drawn pose. `translation` is what is about to be handed to the engine, and `frame_stamp`
 * counts rendered frames. Called twice per subnode per frame by the two redirected consumers, so
 * a repeat of the same stamp is ignored rather than treated as a break in the chain: doing the
 * latter reset the history on every second call and reported nothing judged at all. */
void mover_evenness_note(mover_evenness_state_t *state, const float *translation,
                         uint32_t frame_stamp);

/* Writes the window to the log and starts a new one. Silent when nothing was judged. */
void mover_evenness_report(void);

#endif /* MOVER_EVENNESS_H */
