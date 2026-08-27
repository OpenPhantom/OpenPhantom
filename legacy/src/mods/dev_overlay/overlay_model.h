/* overlay_model.h: what the panel shows, with no window, no drawing and no engine in it.
 *
 * The whole of the panel's behaviour lives here: which tab is open, what has been typed into the
 * search box, which groups are folded, and therefore which rows are on screen and in what order.
 * Drawing reads this and paints it; the mouse reads this and asks it to act. Neither owns any of
 * it, which is what makes the interesting half testable without the game.
 *
 * Rows are produced fresh on every rebuild rather than cached, because the state they show belongs
 * to the engine: the game's own console can flip a cheat behind us, and a panel showing a value it
 * remembered from a second ago would be lying about the thing it exists to display.
 *
 * The search matches anywhere in the label and ignores case, which is what people expect of a
 * search box. A group with a match is shown expanded even if it is folded, because a search that
 * hides its own hits is a search nobody can use; folding it again is remembered separately from
 * that, so clearing the box puts everything back where it was.
 */
#ifndef OVERLAY_MODEL_H
#define OVERLAY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define OVERLAY_SEARCH_MAX   32u
#define OVERLAY_ROWS_MAX     64u
#define OVERLAY_LABEL_MAX    48u

typedef enum overlay_tab {
    OVERLAY_TAB_ORIGINAL = 0,
    OVERLAY_TAB_OPENPHANTOM,
    OVERLAY_TAB_COUNT
} overlay_tab_t;

/* One entry per group, and a group belongs to exactly one tab. The Original tab holds two: the
 * eleven codes that are real toggles, and the sixteen that run once. Splitting them is what lets a
 * fire-once row and a switched row sit in the same tab without either pretending to be the other. */
typedef enum overlay_group {
    OVERLAY_GROUP_ORIGINAL_TOGGLES = 0,
    OVERLAY_GROUP_ORIGINAL_ACTIONS,
    OVERLAY_GROUP_OPENPHANTOM,
    OVERLAY_GROUP_COUNT
} overlay_group_t;

typedef enum overlay_row_kind {
    OVERLAY_ROW_GROUP = 0,      /* a foldable heading */
    OVERLAY_ROW_CHEAT,          /* something that can be switched, shown ON / OFF */
    OVERLAY_ROW_ACTION,         /* something that runs once, shown as a plain button */
    OVERLAY_ROW_HOTKEY,         /* a key binding, shown as a button that captures the next keypress -
                                  * see overlay_model_is_capturing_hotkey() */
    OVERLAY_ROW_VALUE,          /* a typed-in number, shown as a chip that starts free text entry on
                                  * click; see overlay_model_is_editing_value() */
    OVERLAY_ROW_INFO            /* plain text, no chip, not clickable - a note attached to the row
                                  * above it rather than a cheat of its own */
} overlay_row_kind_t;

typedef struct overlay_row {
    overlay_row_kind_t kind;
    char               label[OVERLAY_LABEL_MAX];
    bool               expanded;    /* groups: whether their children follow */
    bool               on;          /* cheats: whether it is active right now */
    bool               available;   /* cheats and actions: false when its site never resolved,
                                      * or when it is gated safe and running it now would not be */
    bool               pending;     /* actions only: queued to run when the panel closes, not yet
                                      * run - see cheats_original_actions.h for why the four
                                      * play-as codes work this way and nothing else does */
    char               value[8];    /* actions: only for the one that has a number worth showing on
                                      * its own chip instead of RUN. hotkeys: the bound key's short
                                      * name, "..." while capturing, or empty when unbound. Empty
                                      * otherwise. */
    uint32_t           group;       /* an overlay_group_t value, groups included */
    uint32_t           id;          /* index within that group's own source */
} overlay_row_t;

/* Forgets the typed text and folds every group. Called when the panel closes, so that opening it
 * again is always the same picture rather than wherever the last session was left. */
void overlay_model_reset(void);

overlay_tab_t overlay_model_tab(void);
void overlay_model_set_tab(overlay_tab_t tab);

/* The text is copied and truncated, never referenced. NULL clears it. */
const char *overlay_model_search(void);
void overlay_model_set_search(const char *text);

/* Appends one character to the search, or removes the last one. Both are no-ops at the ends. */
void overlay_model_search_append(char letter);
void overlay_model_search_backspace(void);

/* Folds or unfolds one group of the current tab. */
void overlay_model_toggle_group(uint32_t group);

/* Rebuilds the visible list from the engine's current state and the current tab, search and folds.
 * Cheap enough to call once per drawn frame, which is what the overlay does. */
void overlay_model_rebuild(void);

uint32_t overlay_model_row_count(void);

/* Copies one row out. False for an index past the end, and then `out` is untouched. */
bool overlay_model_row(uint32_t index, overlay_row_t *out);

/* Acts on a row: folds a group, switches a cheat, starts capturing a hotkey. Answers false when the
 * row cannot act, which is a cheat whose site never resolved. The visible list is rebuilt by the
 * caller afterwards. */
bool overlay_model_activate(uint32_t index);

/* Case insensitive substring test, exposed because it is the one piece of the search worth testing
 * on its own. True when `needle` is empty. */
bool overlay_model_matches(const char *label, const char *needle);

/* Whether a hotkey row is waiting for its next keypress. While true, overlay_input.c routes the
 * very next key-down here instead of its usual handling (Escape, typing, and so on), including
 * Escape itself and system key combinations - the capture is unconditional by design, so binding
 * is predictable rather than needing its own list of exceptions. */
bool overlay_model_is_capturing_hotkey(void);

/* Ends a capture in progress with this key. A no-op if nothing is capturing. */
void overlay_model_capture_hotkey(int32_t virtual_key);

/* Whether the jump-boost scale row is waiting for typed digits. While true, overlay_input.c routes
 * WM_CHAR here (via overlay_model_value_append()) instead of the search box, and gives Enter/
 * Escape/Backspace their own meaning (commit/cancel/delete) instead of their usual one - the same
 * kind of unconditional redirect overlay_model_is_capturing_hotkey() above already gets, for the
 * same reason: predictable is better than a second list of keys this refuses. */
bool overlay_model_is_editing_value(void);

/* Appends one character if it could plausibly be part of a positive decimal number (a digit, or a
 * single '.'); anything else, and a second '.', are silently refused rather than accepted and
 * later failing to parse. No-op unless a capture is in progress. */
void overlay_model_value_append(char digit);
void overlay_model_value_backspace(void);

/* Parses what has been typed and, if it is a usable positive number, hands it to
 * cheats_openphantom_jump_boost_set_scale() - which clamps it - and ends the capture either way.
 * An empty field commits nothing, leaving whatever scale was already set untouched rather than
 * zeroing it out. */
void overlay_model_value_commit(void);

/* Ends a capture in progress, discarding what has been typed. A no-op if nothing is capturing;
 * also called on every click that does not land back on the value row, so at most one field in
 * this panel is ever mid-edit at a time. */
void overlay_model_value_cancel(void);

#endif /* OVERLAY_MODEL_H */
