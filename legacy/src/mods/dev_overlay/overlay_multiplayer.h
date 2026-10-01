/* overlay_multiplayer.h: the multiplayer's settings that belong to this machine alone, drawn as
 * the group "Multiplayer".
 *
 * One row today, the key that opens the chat. It was the third row under Menus, beside the key
 * that opens this panel, on the grounds that both are keys that open something. In the field it
 * was not found there: the heading says neither chat nor key, it starts folded and it is second
 * from the bottom. The row is the same row, written by chat_key_row.c; what moved is where it
 * stands, under a heading with the word a player looks for, directly under the controls.
 *
 * Offered with no multiplayer loaded and in a session alike. It writes a file the multiplayer
 * reads, never calls into it, and which key opens the chat is this machine's own business, so the
 * session lock leaves the whole group alone.
 *
 * The rows here are DESCRIBED and ACTED ON here and NUMBERED by the caller, the same split the
 * utilities group has: a slot is just its position in the list.
 */
#ifndef DEV_OVERLAY_OVERLAY_MULTIPLAYER_H
#define DEV_OVERLAY_OVERLAY_MULTIPLAYER_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The key that opens the chat. */
#define OVERLAY_MULTIPLAYER_ROW_COUNT 1u

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `capturing` is whether this row is the key row waiting for its key. A slot past the end answers
 * as an empty unavailable row. */
void overlay_multiplayer_row(uint32_t slot, bool capturing, overlay_row_t *out);

/* Binds a key. False when the slot is not a key row or the key was refused; a refusal leaves the
 * file alone and puts its sentence in the band. */
bool overlay_multiplayer_bind(uint32_t slot, int32_t virtual_key);

#endif /* DEV_OVERLAY_OVERLAY_MULTIPLAYER_H */
