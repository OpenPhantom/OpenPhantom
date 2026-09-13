/* speaker_gesture.h: a speaker keeps animating for the whole of their line.
 *
 * A scene puts a short clip on a speaker's body when their line starts, a stand fidget or a
 * cutscene gesture of two or three seconds authored as one pass, and the body holds the last
 * frame of it for the rest of the line. A long line is spoken standing still from a third of
 * the way in (issue 23). While a body holds the conversation's speaker lock and the voice has
 * at least the clip's length still to play, a clip that has played through on it is started
 * again, so the gesture runs for as long as the line does and never past it. Whoever is
 * speaking: the player character's lines in a scene are spoken through a scene actor of their
 * own, and the other actors' lines through theirs.
 *
 * Each pass ends as the scene's own did, held on its last frame, and the last one ends before
 * the voice does; a script waiting on the clip's completion sees it at most a pass later than
 * it would have. Nothing else the script does is touched.
 */
#ifndef SPEAKER_GESTURE_H
#define SPEAKER_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* `speaker_lock` is the conversation's speaker cell, already resolved by the caller. `repeat`
 * is the gesture repeat above, `rest` the stand in a cutscene (speaker_rest.h); both ride on
 * the same per-frame check. Returns true when that check is in place. */
bool speaker_gesture_install(const volatile uint32_t *speaker_lock, bool repeat, bool rest);

/* A level is being loaded: every body followed here belongs to the level going away, so the
 * line in progress and the rest's watch list are dropped before their memory is reused. */
void speaker_gesture_level_changed(void);

#endif /* SPEAKER_GESTURE_H */
