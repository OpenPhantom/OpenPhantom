/* overlay_multiplayer.h: what this machine's own player has to do with the multiplayer, drawn as
 * the group "Multiplayer".
 *
 * The first row is the key that opens the chat. It was the third row under Menus, beside the key
 * that opens this panel, on the grounds that both are keys that open something. In the field it
 * was not found there: the heading says neither chat nor key, it starts folded and it is second
 * from the bottom. The row is the same row, written by chat_key_row.c; what moved is where it
 * stands, under a heading with the word a player looks for, directly under the controls. It is
 * offered with no multiplayer loaded and in a session alike: it writes a file the multiplayer
 * reads, and which key opens the chat is this machine's own business.
 *
 * Under it stand two buttons, "Repair lock" and "Teleport to host", and a line that says what the
 * last press of either came to. They are for the player who is stuck in a session: held by what
 * a scene left behind, or somewhere the host is not. The panel carries neither of them out. It
 * files an ask for the multiplayer and reads the answer (player_help_row.c), so the buttons are
 * offered only in a session whose multiplayer answers for them, and are greyed with a word that
 * says which of those is missing everywhere else. The line is there only once a button has been
 * pressed, so the group draws three rows until then and four after.
 *
 * The session lock leaves the whole group alone. The key is this machine's own, and the two
 * buttons act for the player who presses them and for nobody else.
 *
 * The rows here are DESCRIBED and ACTED ON here and NUMBERED by the caller, the same split the
 * utilities group has: a slot is just its position in the list.
 */
#ifndef DEV_OVERLAY_OVERLAY_MULTIPLAYER_H
#define DEV_OVERLAY_OVERLAY_MULTIPLAYER_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots, in drawn order. The key stays the first: its id is the group's base, and the key
 * capture remembers the row it waits on by that id. The line is the last, so that its coming and
 * going moves no other row. */
typedef enum overlay_multiplayer_slot {
    OVERLAY_MULTIPLAYER_CHAT_KEY = 0,
    OVERLAY_MULTIPLAYER_REPAIR,
    OVERLAY_MULTIPLAYER_TELEPORT,
    OVERLAY_MULTIPLAYER_LAST
} overlay_multiplayer_slot_t;

/* The most rows the group draws, which is what its ids are budgeted against in overlay_row_ids.h:
 * the key, the two buttons and the line. overlay_multiplayer_row_count() answers how many it
 * draws now. */
#define OVERLAY_MULTIPLAYER_ROW_COUNT 4u

/* How many rows the group has in this picture: three, and the line once a button was pressed. */
uint32_t overlay_multiplayer_row_count(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `capturing` is whether this row is the key row waiting for its key. A slot past the end answers
 * as an empty unavailable row. */
void overlay_multiplayer_row(uint32_t slot, bool capturing, overlay_row_t *out);

/* Binds a key. False when the slot is not a key row or the key was refused; a refusal leaves the
 * file alone and puts its sentence in the band. */
bool overlay_multiplayer_bind(uint32_t slot, int32_t virtual_key);

/* Presses one of the two buttons. False for every other slot, and for a button that is not
 * offered or whose ask could not be filed; the key row is not pressed through here, the model
 * starts its capture itself. */
bool overlay_multiplayer_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_MULTIPLAYER_H */
