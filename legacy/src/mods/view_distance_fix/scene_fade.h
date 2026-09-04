/* scene_fade.h: how long a level takes to fade in from black.
 *
 * A level load arms a four second fade with FUN_004393D0(1, 4.0f, 0,0,0,0): a full-screen BLACK
 * quad, drawn with SRCALPHA and INVSRCALPHA. For a black source that blend collapses to
 * out = dst * (1 - alpha/255), a plain multiply over the whole picture. The alpha is recomputed
 * every frame from a performance-counter clock, so it slides smoothly and is never held.
 *
 * The frame buffer is 16 bit, because the mode enumeration only accepts 16-bit RGB. As the
 * multiplier slides, a stored 5-bit channel changes only when it crosses a quantisation boundary.
 * An area of ONE flat colour crosses everywhere on the same frame and steps as a solid block, while
 * lit geometry crosses pixel by pixel and does not, so the boundary between them jumps. That is a
 * flashing line and it lasts as long as the fade. The sky is such an area by construction, because
 * graphics_clearFrame clears the frame to the fog colour.
 *
 * WHY THIS IS A DURATION AND NOT A SWITCH. The steps are a property of the buffer and cannot be
 * removed here; how long they are on screen is a property of the fade and can be. fmv_player's own
 * post-movie curtain is the control: it fades an alpha quad out through the SAME primitive into the
 * same 16-bit buffer, so it quantises identically, and it has never once been reported as flashing.
 * It fades in 300 ms rather than 4000, and the steps are gone before the eye settles on one.
 *
 * So a short fade keeps what the fade is for and loses what it costs. It also matters more than it
 * looks, because the curtain only covers a level that FOLLOWS A MOVIE. A save load, a retry or a
 * death gets no curtain at all, and on those the four second fade is both the only cover and the
 * sole cause of the flashing.
 */
#ifndef VIEW_DISTANCE_FIX_SCENE_FADE_H
#define VIEW_DISTANCE_FIX_SCENE_FADE_H

/* Seconds. 4.0 is the engine's own value and patches nothing. 0 finishes the fade before the first
 * frame is drawn, which is as close to no fade as the engine can be asked for without risking a
 * division by the duration. Anything else is written over the immediate at the call site. */
void scene_fade_install(float seconds);

#endif /* VIEW_DISTANCE_FIX_SCENE_FADE_H */
