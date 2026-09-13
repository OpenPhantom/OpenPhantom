/* speaker_gesture.h: a speaker keeps animating for the whole of their line.
 *
 * A scene puts a short clip on a speaker's body when their line starts, a stand fidget or a
 * cutscene gesture of two or three seconds authored as one pass, and the body holds the last
 * frame of it for the rest of the line. A long line is spoken standing still from a third of
 * the way in (issue 23). While a body holds the conversation's speaker lock and the voice is
 * still playing, a clip that has played through on it is started again, so the gesture runs
 * for as long as the line does. Whoever is speaking: the player character's lines in a scene
 * are spoken through a scene actor of their own, and the other actors' lines through theirs.
 *
 * A script waiting on that clip's completion is delayed until the voice ends, which is the
 * pacing the scene already has; nothing else the script does is touched.
 */
#ifndef SPEAKER_GESTURE_H
#define SPEAKER_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* `speaker_lock` is the conversation's speaker cell, already resolved by the caller. Returns
 * true when the per-frame check is in place. */
bool speaker_gesture_install(const volatile uint32_t *speaker_lock);

#endif /* SPEAKER_GESTURE_H */
