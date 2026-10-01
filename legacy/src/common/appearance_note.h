/* appearance_note.h: what the local player looks like, said by whoever changed it.
 *
 * This is one record filed through shared_note, and it exists because of a gap that cannot be
 * closed on either side alone.
 *
 * A fix that describes the local player to a second machine can read the player's actor out
 * of the engine and send its name: the hero block points at the loaded asset and the asset carries
 * its own file name. That covers the four shipped heroes and any swap that replaces the actor.
 *
 * It does not cover a model swap. That one replaces the mesh on the body and leaves the actor
 * alone, so the far machine sees no change at all. And it cannot be recovered from the engine
 * either: the geometry block carries the name of the source mesh rather than of the actor file,
 * and several actors share one mesh name. A reader that guessed from it would build the wrong
 * character in the colliding cases. The developer overlay publishes it and the multiplayer
 * reads it.
 *
 * So the name has to come from whoever set it. It cannot come as a call, because feature DLLs do
 * not depend on each other at run time; it comes as a record under a name, through code both of
 * them link statically.
 *
 * An empty model name means "the body wears the model its own actor came with", which is what a
 * player who has not swapped anything wears, and what a player who swapped back wears. It is not
 * the same as no note at all: no note means nothing has ever published one, which is the ordinary
 * state of an installation without any model swapping in it.
 */
#ifndef COMMON_APPEARANCE_NOTE_H
#define COMMON_APPEARANCE_NOTE_H

#include <stdbool.h>
#include <stddef.h>

/* The name the record is filed under. Both sides use this literal and nothing else. */
#define APPEARANCE_NOTE_NAME "appearance"

/* An asset file name and its terminator. The engine's own resource layer refuses anything longer
 * than 8.3, so this is generous on purpose rather than exact. */
#define APPEARANCE_MODEL_MAX 32u

/* And how big the body is drawn, 1.0 being the size its own asset asks for.
 *
 * It is here for the same reason the model is. The two player scales of the developer overlay are
 * a factor composed into the player's own draw matrix every frame; nothing is written down that a
 * second machine could read, and the far side would see an ordinary player doing whatever a giant
 * or a doll does. The bounds are the panel's own two values with room around them, and they are
 * bounds rather than a clamp: a factor outside them is refused, because a body drawn at a hundred
 * times its size is not a thing anybody asked for.
 */
#define APPEARANCE_SCALE_MIN 0.1f
#define APPEARANCE_SCALE_MAX 6.5f

/* Says what the local body looks like now: the model it wears, NULL or empty for its own, and the
 * factor it is drawn at, 1.0 for its own size. The two travel together in one record, so a caller
 * that changes one says both; publishing a model with the scale forgotten would put a swapped
 * body back to ordinary size on the far machine.
 *
 * False when the name does not fit, the scale is outside the bounds, or the channel refused;
 * nothing is published then and a reader goes on seeing what was there before. */
bool appearance_note_publish(const char *model, float scale);

/* Reads it back. False when nobody has published one, which is the normal answer in an
 * installation that has no model swapping in it; `out` and `scale` are then left alone. `scale`
 * may be NULL for a caller that only wants the name. */
bool appearance_note_read(char *out, size_t out_size, float *scale);

#endif /* COMMON_APPEARANCE_NOTE_H */
