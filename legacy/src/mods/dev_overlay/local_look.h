/* local_look.h: what this machine's player looks like, said once for whoever is listening.
 *
 * Two of this mod's rows change how the player is drawn and neither leaves anything a second
 * machine could read. The two player scales compose a factor into the player's own draw matrix,
 * which is rebuilt from scratch every frame; a model swap replaces the mesh on the body and
 * leaves the actor alone, and the geometry block then carries the name of the source MESH rather
 * than of the actor file, which several actors share. So what a far machine needs has to come
 * from whoever set it, and that is this file: it keeps the two, and publishes them together
 * through `common/appearance_note`.
 *
 * TOGETHER is the point. The note is one record. A caller that changed the scale and published
 * only the scale would put a swapped model back to its own on the far machine, and the other way
 * round. So nothing here publishes a field; it publishes the look.
 *
 * Nobody has to be listening. With no multiplayer in the installation the note is written and
 * never read, which costs one small write per press.
 */
#ifndef LOCAL_LOOK_H
#define LOCAL_LOOK_H

#include <stdbool.h>

/* How big the player is drawn, 1.0 being the size its own asset asks for. Out of range is
 * ignored, because the two rows that call it pass their own two constants. */
void local_look_set_scale(float scale);

/* The model on the body, NULL or empty for its own. The model swap says it here rather than
 * publishing the note itself: publishing one field would put the other back, and a swap that
 * reset the player's size on the far machine is exactly that mistake.
 *
 * True when the note carries this model, including when it already did. False when the name does
 * not fit or the note refused the write; the model kept is then the one before, so the same call
 * next frame tries again. */
bool local_look_set_model(const char *model);

#endif /* LOCAL_LOOK_H */
