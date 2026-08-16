/* cheats_original_actions.h: the sixteen one-shot effects the shipped console also understands,
 * beside the eleven toggles cheats_original.c already covers.
 *
 * These are not toggles. Typing one into the retail console runs it once and it is done; there is
 * no state to show ON or OFF for "kill self" the way there is for a wireframe view. The panel
 * offers each as something you press, not something you switch.
 *
 * Two of them are gated, the same way the retail console itself gates them: full health and
 * all-weapons-full-ammo both raise a hidden counter, capped under ten, past which the retail
 * effect stops giving anything either. That counter has a second reader nowhere near where it is
 * written - it also pins the player's effective difficulty at its hardest row, from the FIRST
 * nonzero value it ever sees, not the tenth. See cheats_original_actions.c for the read that proved
 * this, and for why the gate still matches retail's own cap rather than trying to prevent a cost
 * that the effect itself cannot be used without paying at least once.
 */
#ifndef CHEATS_ORIGINAL_ACTIONS_H
#define CHEATS_ORIGINAL_ACTIONS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum cheats_action_id {
    CHEATS_ACTION_KILL_SELF = 0,
    CHEATS_ACTION_FULL_HEALTH,           /* gated: see the header comment above */
    CHEATS_ACTION_ALL_WEAPONS_AMMO,      /* gated: see the header comment above */
    CHEATS_ACTION_PLAY_OBI,
    CHEATS_ACTION_PLAY_QUIGON,
    CHEATS_ACTION_PLAY_PANAKA,
    CHEATS_ACTION_PLAY_QUEEN,
    CHEATS_ACTION_LOWER_DIFFICULTY_A,
    CHEATS_ACTION_LOWER_DIFFICULTY_B,
    CHEATS_ACTION_INCREASE_DIFFICULTY,
    CHEATS_ACTION_VIEW_CREDITS,
    CHEATS_ACTION_WAVERING_GRAPHICS,
    CHEATS_ACTION_DEBUG_MODE,
    CHEATS_ACTION_GRAPHICS_DETAIL,   /* cycles a 1-4 detail level, not a force power colour */
    CHEATS_ACTION_RED_HIGHLIGHT,     /* toggles a red icon highlight; which icon is unconfirmed */
    CHEATS_ACTION_TECH_BONUS,
    CHEATS_ACTION_COUNT
} cheats_action_id_t;

/* Resolves the one shared anchor every action's address is read relative to. Answers false and
 * logs why when it does not, leaving every action unavailable rather than guessed at. */
bool cheats_original_actions_resolve(void);

/* The label to show. Never NULL for a valid id: the three whose retail code text is too short for
 * the image's own string analysis to have named are read live from the image instead of guessed,
 * so the label still names them; only the id is fixed at compile time. */
const char *cheats_original_actions_name(cheats_action_id_t id);

/* False when the site never resolved, OR - for the two gated actions only - when the shared
 * counter has already reached the retail console's own cap, the same point retail's effect stops
 * giving anything too. Both reasons show as the same unavailable row: the panel does not need to
 * say which, only that pressing it will not work. */
bool cheats_original_actions_is_available(cheats_action_id_t id);

/* Runs it once - except the four play-as codes, which this only QUEUES. Answers false when it
 * could not, for either reason above.
 *
 * The swap has its own precondition, read out of the retail function itself: a pointer in the
 * player's own state block that reads as "no active controller" whenever dev_overlay is holding the
 * player suspended, which is the engine's own idle state and is true on every frame the panel is
 * open. Running the swap here could silently do nothing depending on the exact frame pressed, with
 * nothing to show for it. Queuing it and applying it once the panel closes and the player is
 * un-suspended again is what makes it reliable. cheats_original_actions_apply_pending() is what
 * actually runs it, and the caller is responsible for calling that after un-suspending the player,
 * not before. */
bool cheats_original_actions_invoke(cheats_action_id_t id);

/* Whether this is the play-as code currently queued. Always false for anything that is not one of
 * the four play-as codes. */
bool cheats_original_actions_is_pending(cheats_action_id_t id);

/* The label of whichever play-as code is queued, or NULL when none is. For the panel to say what
 * closing it will do. */
const char *cheats_original_actions_pending_label(void);

/* Runs the queued play-as swap, if any, and clears the queue either way. Safe to call with nothing
 * queued. Call this after the player has been un-suspended, not before - see the note on
 * cheats_original_actions_invoke(). */
void cheats_original_actions_apply_pending(void);

/* The graphics detail level right now, 1 to 4, read fresh every call so the panel can show it
 * living rather than as a one-off confirmation that scrolls away. Zero when the site never
 * resolved - not one of the four real levels, so a caller cannot mistake "unknown" for "level 1"
 * by forgetting to check first. */
int32_t cheats_original_actions_graphics_level(void);

#endif /* CHEATS_ORIGINAL_ACTIONS_H */
