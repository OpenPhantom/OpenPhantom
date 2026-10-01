/* far_model.h: a far player's body wears the model that player wears.
 *
 * The multiplayer builds a far player's body as that player's hero and says in a note which body
 * should wear which borrowed model. This answers in a note of its own, once at every scene end: it
 * puts the model on the body with the same rebind, the same translation and the same two sided
 * draw the player's own swap uses, and writes the six skeleton node words into the bank block the
 * wish names. The two notes and the one write are the whole contract; common/model_wear_note.h
 * states it.
 *
 * Every scene end is two passes. The first asks every far body in the table whether it is alive:
 * the serial the note gives the bank is the one it was dressed for, the bank block still names its
 * object, the object still owns the handle, the handle still wears the model and the actor still
 * names the hero. A body that is not goes out of the table without anything of it being touched,
 * and a pair nobody wears any more goes with its last wearer, before anything is dressed. The
 * second pass answers every bank: a body that wears a model keeps it and the answer names it,
 * whatever the bank asks for now, and the multiplayer builds the body again when it wants
 * another; a fresh body is dressed when nothing stands in the way. The note is state, not a queue,
 * so an answer that could not be published is owed and said again at the next scene end.
 *
 * What it costs: the asset a far player asks for is loaded at a scene end, which can be felt once,
 * and it stays resident for the rest of the process, as every asset the player's own swap loads
 * does. Each pair a far body is translated by commits its own clip arena while it is worn.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_FAR_MODEL_H
#define DEV_OVERLAY_FAR_MODEL_H

#include <stdint.h>

/* At load: the answer record says the overlay listens, before any level exists, so the
 * multiplayer knows from its first substep whom it is asking. */
void far_model_install(void);

/* Once a frame at the end of the scene, after the player's own swap had its turn. `epoch` is the
 * world count of the panel's engine node; when it moves a level has ended, and what the far bodies
 * came to in it is said once. */
void far_model_tick(uint32_t epoch);

#endif /* DEV_OVERLAY_FAR_MODEL_H */
