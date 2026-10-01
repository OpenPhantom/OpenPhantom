/* overlay_slider.h: the one way a dragged track reaches the settings file, and the one throttle
 * on it.
 *
 * There were five writers into overlay_model_slider_set() across two files, two of them throttled
 * with a timestamp and a "last written" of their own, and the two throttle intervals were the same
 * pair of numbers written out twice. A drag is one state (which track, where the hand has it,
 * what the file last got), so it is held once here and every hand says what it did rather than
 * keeping its own copy. The sideways keys and the Default button are the sixth and seventh, and
 * they go through the bottom of this file rather than writing a track of their own.
 *
 * Why a throttle at all. The only channel between this DLL and the one that owns the setting is
 * the settings file, and writing a key rewrites the file, which here is around ninety kilobytes.
 * At sixty frames a second that is five megabytes a second of file traffic for one dragged handle,
 * and the stutter it causes would be blamed on the setting being changed rather than on the
 * changing of it.
 *
 * WHY TWO RATES. Thirty a second was the one figure for every slider, and it was sized on Windows,
 * where a write costs a few hundred microseconds. Under Wine the profile layer parses and rewrites
 * the whole file on every write and every other DLL's next read parses it again, and a Steam Deck
 * dragging any slider fell to seven frames a second on that. The field of view keeps thirty a
 * second, because its whole effect is the picture zooming under the hand and at four a second that
 * zoom is a series of steps, which is worse than the frame cost; every other track writes four
 * times a second, and letting go always writes the final position, so nothing the hand settled on
 * is lost. The handle itself follows the hand at the full frame rate whatever the write rate: what
 * is throttled is the write, not the drawing.
 *
 * One hold, and a hand only commands the hold it has. Two hands can reach a track (the pointer,
 * which the pad's A button also stands in for, and the pad's triggers), and they used to run past
 * each other with two states. A hand that lets go of a track it is not holding does nothing, and a
 * hand that asks for a track another hand is DRIVING is refused. Without that the triggers, which
 * pick their track from whatever is under the pointer, would end a mouse drag the moment the hand
 * wandered off the row it grabbed.
 *
 * DRIVING, and not merely holding, because the refusal was written against every hold there was,
 * and one of the two hands takes a track without changing it. With a pad plugged in the triggers
 * held whatever track the pointer was over, so the pointer's own click, the sideways keys and the
 * Default button were all refused the track they were pointing at, and no slider in the panel
 * could be moved by anything (field, 2026-09-24). A hold that has been grabbed or moved is driving
 * its track and is guarded; a hold that has only noticed one gives way. The hand that gave way is
 * told nothing, because the track it had was the track it was going to read again next frame.
 *
 * The time is handed in rather than read here, the way spawn_place.c takes its click: it is the
 * one thing in a throttle a test cannot otherwise drive.
 */
#ifndef DEV_OVERLAY_OVERLAY_SLIDER_H
#define DEV_OVERLAY_OVERLAY_SLIDER_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum overlay_slider_hand {
    OVERLAY_SLIDER_POINTER = 0,   /* the mouse, and the pad's A button standing in for it */
    OVERLAY_SLIDER_TRIGGER,       /* the pad's triggers */
    OVERLAY_SLIDER_KEYS           /* the sideways keys and the Default button */
} overlay_slider_hand_t;

/* Takes hold of the track at `index` and puts its handle at `fraction` at once. What a click on a
 * track does: the handle jumps to where it was clicked, so that jump reaches the file without
 * waiting for the throttle. */
void overlay_slider_grab(overlay_slider_hand_t hand, int32_t index, float fraction, uint32_t now);

/* Takes hold of the track at `index` with its handle where it already is, and writes nothing:
 * noticing a track is not changing it. What the pad's triggers do when the pointer lands on one.
 * An `index` of -1 is this hand on no track at all, and lets go of whatever it had: a hand that
 * is on nothing holds nothing. */
void overlay_slider_take(overlay_slider_hand_t hand, int32_t index, float fraction);

/* Where this hand has the handle now, clamped to the track. Recorded every time, so the drawing
 * follows at the frame rate; written at this track's own rate. Does nothing when this hand holds
 * nothing. */
void overlay_slider_move(overlay_slider_hand_t hand, float fraction, uint32_t now);

/* Lets go: whatever the hand settled on reaches the file now, throttle or not. Does nothing when
 * this hand holds nothing. */
void overlay_slider_let_go(overlay_slider_hand_t hand);

/* The track this hand is holding, or -1 when it holds none. */
int32_t overlay_slider_row(overlay_slider_hand_t hand);

/* The track being held and where its handle is, whichever hand has it, for the drawing and for
 * the number the row above it shows. False when nothing is held; `index` and `fraction` may be
 * NULL. */
bool overlay_slider_held(int32_t *index, float *fraction);

/* The number a row SHOWS, which is not always the number in the settings file. While a track is
 * held, the value row above it and the track row itself both read the fraction the hand has,
 * through one inversion; with nothing held both read the file, through one reading. Either way
 * the two rows agree, which they did not when the file was written four times a second and the
 * handle moved sixty.
 *
 * Called for every drawn row, by the index the model built it at, and it answers for the two
 * kinds it is about and leaves the rest alone. It is one function rather than a test in the
 * painter because there are TWO rows showing that number and what goes wrong is one of them
 * being left out, which looks like nothing at all until somebody drags a handle.
 *
 * It cannot be done in the model instead: the model is built when something changes, and what
 * changes here is the hand, every frame, against a file written four times a second. */
void overlay_slider_number_on(uint32_t index, overlay_row_t *on_it);

/* ONE DISCRETE CHANGE, which is what a key press and a pressed button are, as against the sixty
 * fractions a second a drag makes.
 *
 * Both of these take the track, write, and let go inside the call, and both write AT ONCE. That
 * is a decision rather than an oversight, and it is two decisions:
 *
 * Not held between presses, because a hold kept here would refuse the pointer and the triggers
 * the track under the rule above, and a burst of presses would end a drag somebody had started
 * with the mouse. A track another hand IS holding is moved in place instead, since "the value
 * is now this" and "the handle is now there" are the same sentence; a DIFFERENT track being
 * held is left alone.
 *
 * Not throttled, because the throttle exists to collapse the fractions of a motion, of which
 * only the last one is a value anybody asked for, and every press here is one. There is no key
 * release to write the last of a burst on, so a throttle would have to drop it or keep a hold
 * to flush it later, and the first loses a setting the player made. A held key repeats about
 * thirty times a second, which is the rate this file already writes the field of view at by
 * design, and it stops the moment the key comes up.
 *
 * One press of a sideways key: `by` is -1 or 1 and `coarse` is the modifier, and the step is
 * the row's own, from wherever the handle is now. False for a track with no press size, for a
 * different track being held, and for a write the model turned down. */
bool overlay_slider_nudge(int32_t index, int32_t by, bool coarse);

/* Default: the track back to the standard its row named. False for a track that named none,
 * which is what a row nobody wrote a standard for gets instead of a Default that puts it at
 * zero. */
bool overlay_slider_to_standard(int32_t index);

#endif /* DEV_OVERLAY_OVERLAY_SLIDER_H */
