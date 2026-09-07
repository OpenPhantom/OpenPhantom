/* input_mode.h: which of the engine's binding sets is live, and whether the stick may drive.
 *
 * ============================ Why this is needed at all ========================================
 *
 * A conversation with a choice menu is supposed to stop the player moving, so the stick can pick
 * an answer without walking off. It stopped working the moment the pad stick started reading
 * XInput directly.
 *
 * The engine does NOT gate movement on a flag. `input_setMode` is a state machine that swaps the
 * whole binding set: entering a dialogue calls it with 4, leaving calls it with 0, and the mode
 * cell it keeps is read nowhere else in the image. So the engine disables movement by unbinding
 * it, and a reader that goes to XInput itself is not unbound by anything and walks straight past
 * that. Nothing was wrong with the bindings; the stick simply stopped going through them.
 *
 * pad_stick.h already sets out why the engine's own read of that stick cannot be used for a
 * direction, so going around it is not optional. What was missing is the other half of the deal:
 * having taken the stick, this has to honour the thing the bindings were doing for us.
 *
 * ============================ What is asked, and what is assumed ===============================
 *
 * The mode cell is the engine's own statement of which binding set is live, so it is the honest
 * thing to ask. Only mode 0 drives: it is the one the dialogue returns to when it lets go, and
 * every other value is some set of bindings that is not gameplay.
 *
 * When the site does not resolve this answers YES rather than no. The cost of guessing wrong that
 * way is the fault this repairs, which is a nuisance during one conversation; the cost of guessing
 * the other way is a player who cannot move at all, on a build where nothing is known to be wrong.
 */
#ifndef ENHANCED_INPUT_INPUT_MODE_H
#define ENHANCED_INPUT_INPUT_MODE_H

#include <stdbool.h>
#include <stdint.h>

/* The engine's mode for ordinary play, the one a dialogue hands back to on its way out. */
#define INPUT_MODE_GAMEPLAY 0

/* Resolves the site and derives the cell. Says in the log what it found, once. Safe to call more
 * than once; only the first does the work. */
void input_mode_resolve(void);

/* True when the stick is allowed to drive the player. `resolved` false always answers true, for
 * the reason in the header above; this is the whole decision and it is pure so it can be tested. */
bool input_mode_allows_movement(bool resolved, int32_t mode);

/* The same question against the live cell. */
bool input_mode_is_gameplay(void);

#endif /* ENHANCED_INPUT_INPUT_MODE_H */
