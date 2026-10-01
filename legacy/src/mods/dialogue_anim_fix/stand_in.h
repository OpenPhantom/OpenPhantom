/* stand_in.h: a line spoken by an invisible script anchor is gestured by the body it stands for.
 *
 * Some scripted lines are not spoken by the character on screen. The end of the first level has
 * Qui-Gon call "Obi-Wan!" twice and then send him to the transports, and all three lines come
 * from an actor on the inviso.baf template, a script anchor with no model, placed a few units
 * from him; the Qui-Gon in view is a cutscene double whose own script knows nothing of the
 * lines and free-runs a calling gesture and a stance one pass each, round and round, until a
 * flag set with the third line puts him on his talk clip. So the third line is in sync and the
 * two calls land wherever the cycle happens to be: he calls, and the voice comes a second
 * later, or the other way round (reported 2026-09-14, the same with every switch in this DLL
 * off, so the 1999 scripts' own).
 *
 * The census of every level's scripts finds sixty such lines, spoken by eighteen anchors. Most
 * have no face at all: console refusals, the podrace announcers, the crowd. The rest belong to
 * a character who is on screen beside the anchor, and this brings that body in on the voice.
 * When a Statement is issued by an anchor, the line's key in the level's dialogue table names
 * the speaker (its first two characters, QG for Qui-Gon), a small table turns that into a model
 * name, and the nearest live actor on that model within a few units whose script is running a
 * clip one pass at a time is nudged, once, at the moment the voice starts: a gesture in flight
 * is started again from its first frame, so it runs with the line; the stand is ended, so the
 * script moves on to whatever it plays next, the gesture in the case above. The script's own
 * modes and clips are never changed, only where in its own cycle the body is. A body on a
 * looping clip, a walk or a run, or on nothing the script put on, is never touched; a line
 * whose speaker code is not in the table, or whose speaker has no body in range, does nothing.
 * What is done to the body, and how it is followed afterwards, is stand_in_watch.h.
 */
#ifndef STAND_IN_H
#define STAND_IN_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the three sites this needs and reads their operands back. `speaker_lock` is the
 * conversation's speaker cell, already resolved by the caller. Returns true when every site is
 * in place; on false nothing here ever runs and the log says which site failed. */
bool stand_in_install(const volatile uint32_t *speaker_lock);

/* A level is being loaded: the face being followed belongs to the level going away. */
void stand_in_level_changed(void);

/* Opcode 0x504 Statement has just been run for `actor_record`, with `line` the line id it
 * carried. Anything but an anchor returns at once. */
void stand_in_note_line(int32_t actor_record, int32_t line);

#endif /* STAND_IN_H */
