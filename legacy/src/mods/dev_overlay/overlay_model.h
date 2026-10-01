/* overlay_model.h: what the panel shows, with no window, no drawing and no engine in it.
 *
 * All of the panel's behaviour lives here: which tab is open, what has been typed into the
 * search box, which groups are folded, and therefore which rows are on screen and in what order.
 * Drawing reads this and paints it; the mouse reads this and asks it to act. Neither owns any of
 * it, so the interesting half is testable without the game.
 *
 * Rows are produced fresh on every rebuild rather than cached, because the state they show belongs
 * to the engine: the game's own console can flip a cheat behind us, and a panel showing a value it
 * remembered from a second ago would be lying about the thing it exists to display.
 *
 * The search matches anywhere in the label and ignores case, as people expect of a search
 * box. A group with a match is shown expanded even if it is folded, because a search that
 * hides its own hits is a search nobody can use; folding it again is remembered separately from
 * that, so clearing the box puts everything back where it was.
 */
#ifndef OVERLAY_MODEL_H
#define OVERLAY_MODEL_H

#include "overlay_number.h"

#include <stdbool.h>
#include <stdint.h>

#define OVERLAY_SEARCH_MAX   32u
#define OVERLAY_LABEL_MAX    48u

/* Every group on the open tab is built at once, headings included, and the OpenPhantom tab shows
 * twelve headings. With all of them open, every fold open and a display offering a full size
 * list, that tab passes 550 rows. This was 64 when the tab had four groups, so 27 of the 91 it
 * then had were built and dropped by a bounds test with no log and no way to scroll to what
 * went missing. overlay_row_ids.h asserts this against the parts it is made of, so a group that
 * grows past it stops the build and does not quietly lose its last rows, which is how the third
 * fold's lines raised it to 160, the spawner's list of a level's actor files, 122 in Mos Espa,
 * to 384, and the same list widened to the archive's creatures to 768. */
#define OVERLAY_ROWS_MAX     1152u

typedef enum overlay_tab {
    OVERLAY_TAB_ORIGINAL = 0,
    OVERLAY_TAB_OPENPHANTOM,
    OVERLAY_TAB_COUNT
} overlay_tab_t;

/* One entry per group, and a group belongs to exactly one tab. The Original tab holds two: the
 * eleven codes that are real toggles, and the sixteen that run once. Splitting them is what lets a
 * fire-once row and a switched row sit in the same tab without either pretending to be the
 * other.
 *
 * A group here is a SOURCE of rows, not necessarily a heading on screen. Two of them are drawn
 * inside another group's body rather than under a heading of their own, so the OpenPhantom tab
 * reads as twelve subjects while the row ids, the slot numbers and the session lock's lists all
 * stay exactly where they were. overlay_headings.c holds the drawn order and says which those two
 * are.
 *
 * A new group goes at the END, before the count, whatever its place on screen: a row id and the
 * session lock's lists are derived from these numbers, and one moved hands a player, in the
 * middle of a session, a row the session takes. Where it is drawn is overlay_headings.c's. */
typedef enum overlay_group {
    OVERLAY_GROUP_ORIGINAL_TOGGLES = 0,
    OVERLAY_GROUP_ORIGINAL_ACTIONS,
    OVERLAY_GROUP_OPENPHANTOM,
    OVERLAY_GROUP_OPENPHANTOM_LEVELS,
    OVERLAY_GROUP_OPENPHANTOM_SPAWN,
    OVERLAY_GROUP_OPENPHANTOM_FREECAM,
    OVERLAY_GROUP_OPENPHANTOM_DISMEMBER,
    OVERLAY_GROUP_OPENPHANTOM_MODELSWAP,
    OVERLAY_GROUP_OPENPHANTOM_UTILITIES,
    OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS,
    OVERLAY_GROUP_OPENPHANTOM_PICTURE,
    OVERLAY_GROUP_OPENPHANTOM_FOG,
    OVERLAY_GROUP_OPENPHANTOM_CONTROLS,
    OVERLAY_GROUP_OPENPHANTOM_WINDOW,
    OVERLAY_GROUP_OPENPHANTOM_FRAMERATE,
    OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER,
    OVERLAY_GROUP_COUNT
} overlay_group_t;

typedef enum overlay_row_kind {
    OVERLAY_ROW_GROUP = 0,      /* a foldable heading, with what is under it in its chip */
    OVERLAY_ROW_CHEAT,          /* something that can be switched, shown ON / OFF */
    OVERLAY_ROW_ACTION,         /* something that runs once, shown as a plain button */
    OVERLAY_ROW_HOTKEY,         /* a key binding, shown as a button that captures the next
                                  * keypress; see overlay_model_is_capturing_hotkey() */
    OVERLAY_ROW_VALUE,          /* a typed-in number, shown as a chip that starts free text entry on
                                  * click; see overlay_model_is_editing_value() */
    OVERLAY_ROW_INFO,           /* plain text, no chip, not clickable, a note attached to the row
                                  * above it rather than a cheat of its own */
    OVERLAY_ROW_SLIDER,         /* just a track, on its own line under the value row it
                                  * belongs to. It gets a line of its own rather than sharing one so
                                  * that it can run the width of the panel: a track squeezed into
                                  * the gap between a name and its chip is both hard to hit and
                                  * close enough to the text to read as though it were striking it
                                  * through. `fraction` is where the handle sits. */
    OVERLAY_ROW_CHOICE,         /* one entry of a list, where exactly one is the chosen one. It
                                  * carries a MARK left of its name and no chip: a chip reading ON
                                  * says the entry is switched on, which is the wrong claim to make
                                  * about a list. `on` is true on the chosen entry and no other. */
    OVERLAY_ROW_SEGMENT         /* a short choice drawn whole, as a row of words where the chip
                                  * would be, the chosen one filled. `chosen` is which of them, and
                                  * the words come from the row's own source rather than from
                                  * `value`: four of them do not fit sixteen characters. It carries
                                  * no chip, because a chip beside the words would be the same
                                  * state spelled twice. */
} overlay_row_kind_t;

/* Whether a player can act on a row of this kind at all: press it, switch it, type into it or
 * drag it. The panel's own summary counts by it and session_lock.c decides by it, and it used to
 * be two lists in those two files whose comments both claimed to be complete: the lock had the
 * slider on it and the model did not.
 *
 * Written as what is NOT acted on, so a kind added later counts as acted on until somebody says
 * otherwise. The cost of being wrong is not symmetric, the same way session_lock.h has it: a row
 * locked that did not need to be is a setting somebody cannot reach while they play together, and
 * a row left open that needed locking ends the session at the next level. */
static inline bool overlay_row_kind_is_acted_on(overlay_row_kind_t kind)
{
    return kind != OVERLAY_ROW_GROUP && kind != OVERLAY_ROW_INFO;
}

typedef struct overlay_row {
    overlay_row_kind_t kind;
    char               label[OVERLAY_LABEL_MAX];
    bool               expanded;    /* groups: whether their children follow */
    bool               on;          /* cheats: whether it is active right now */
    bool               available;   /* cheats and actions: false when its site never resolved,
                                      * or when it is gated safe and running it now would not be */
    bool               pending;     /* actions only: queued to run when the panel closes, not yet
                                      * run; see cheats_original_actions.h for why the four
                                      * play-as codes work this way and no other action does */
    /* Notes only: this note is a REFUSAL and is drawn in the warning colour rather than the dim
     * grey every other note has. The entity spawner's "Refused: no room in front of you" is the
     * one that sets it: it describes a state of the world and stays where it is, which is why it
     * is a row and not the band above the footer (overlay_notice.h). Left false everywhere
     * else, so a count, a reading or an explanation stays grey. */
    bool               warn;
    /* Sixteen, not eight. Eight fitted "2.50x" and every state word, and then the dev menu
     * size row began reporting "auto 1.33x" and a player read "auto 1.", a truncation with no
     * ellipsis, in the one place a number was the whole point of the row. Nothing here is a fixed
     * width in the file format sense, so the cost of the slack is a few bytes per row. */
    char               value[16];   /* actions: only for the one that has a number worth showing on
                                      * its own chip instead of RUN. hotkeys: the bound key's short
                                      * name, "..." while capturing, or empty when unbound. groups:
                                      * what is under the heading in one word, "2 on" or "ON" or
                                      * the reason a session has taken the whole of it, so a folded
                                      * band answers for itself; empty when there is nothing to
                                      * say. Empty otherwise. */
    uint32_t           group;       /* an overlay_group_t value, groups included */
    uint32_t           id;          /* index within that group's own source */
    /* Why the row is not available, an overlay_reason_t value. Read only when `available` is
     * false; whoever takes the row away sets it, and overlay_reason.c turns it into the word in
     * the chip. Left at OVERLAY_REASON_NONE the row reads `n/a`, which is what an unresolved site
     * has always said. The type is plain here so that overlay_model.h stays free of it: every
     * group source includes the one and the model includes the other. */
    uint32_t           reason;

    /* True when `value` holds the HOST's value of the setting this row edits, in the words its
     * chip shows, "host 1.50x" or "host ON". Set only on a client of a running session whose host
     * named that setting, which is when the session has taken the row: the chip of a taken row
     * then reads the host's value instead of the word `session`, because the host's value is what
     * this machine runs and the one in its own ini is not. False everywhere else, and there the
     * row reads as it always did. */
    bool               host_value;

    /* Where a SLIDER row's handle sits: 0 at the row's minimum and 1 at its maximum. Read only for
     * that kind. */
    float              fraction;

    /* Which segment of a SEGMENT row is the chosen one. Read only for that kind.
     *
     * An index and not the word, because the words belong to the source: `value` holds sixteen
     * characters and the one row with segments today needs twenty four for its four. The source
     * answers both, through overlay_choice.h, so the row and the drawing cannot end up holding two
     * lists of one choice. */
    uint32_t           chosen;
} overlay_row_t;

/* Puts the panel back to the picture it has when the game starts: the Original tab, nothing
 * typed, every group folded, every list and fold shut, and no edit in progress. Called once, when
 * the panel is installed.
 *
 * It is NOT called when the panel closes any more, and that is the whole of the navigation
 * memory. Closing forgets what is half done (below) and keeps where the player was, so a setting
 * that has to be looked at in the game costs one keypress each way instead of four. Nothing that
 * is kept can outlive what it describes: every group counts its own rows again on each rebuild,
 * every row reads its state from the engine, and the scroll is clamped where it is read, so a
 * level change or a session beginning between two openings cannot leave any of it stale. What a
 * session takes away is decided on the rebuild as well, so a remembered fold shows the rows and
 * the lock still greys them. */
void overlay_model_reset(void);

/* Ends whatever was half finished: a hotkey row waiting for a keypress, a number being typed into,
 * and the refusal band. Called every time the panel closes, because a capture that survived it
 * would swallow the next key the player pressed in the game, a half typed number would come back
 * with digits in it that belong to a minute ago, and a standing refusal would answer a keystroke
 * from before the close. */
void overlay_model_forget_edits(void);

/* Drags a slider row to `fraction`, 0 to 1, clamped. False when the row is not a slider or the
 * write failed, which the caller shows by leaving the handle where it was rather than reporting a
 * value the game is not in. */
bool overlay_model_slider_set(uint32_t index, float fraction);

/* What the row ABOVE the slider at `index` reads with the handle at `fraction`, the inverse of
 * the reading that row takes to place the handle in the first place. Written by the row's own
 * group, through the one arithmetic overlay_model_slider_set() writes with.
 *
 * While a track is held, the handle comes from the hand and the file is only written four times a
 * second, so the number beside it has to come from the same fraction the handle does or the two
 * are two spellings of one drag. False when the row is no slider, is not available, or its group
 * has nothing to show. */
bool overlay_model_slider_value(uint32_t index, float fraction, char *out, size_t size);

/* The numbers behind the track at `index`: its two ends, the size of one sideways press and
 * of one with the modifier held, and what Default puts back. False for a row that is no track
 * or cannot be used, and `has_standard` is false for a track that has no Default at all.
 *
 * It is the one place the keys and Default read a row's numbers from, so the step a key takes
 * and the grid a drag rounds to are the same number and not two that agree today. */
bool overlay_model_slider_limits(uint32_t index, overlay_number_t *out);

/* Whether a drag on the slider at `index` writes at the full rate; see the utilities page. */
bool overlay_model_slider_wants_full_rate(uint32_t index);

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
 * Cheap enough to call once per drawn frame, as the overlay does. */
void overlay_model_rebuild(void);

uint32_t overlay_model_row_count(void);

/* How many of those rows are headings, for the tally the footer shows.
 *
 * Counted while the list is built, not worked out from the group tables. The number a player is
 * told has to be the number of bands in front of them, and those are not the same thing: two
 * sources are drawn under another group's heading, and a count from the enum would be right today
 * and wrong on the first change to either. */
uint32_t overlay_model_heading_count(void);

/* Which row is drawn first, so a list taller than the screen can still be reached.
 *
 * `visible` is what the layout worked out fits. The answer is clamped against it on every ask
 * rather than corrected when the list changes: the row count moves with every keystroke in the
 * search field and every fold, and a stored index would be stale between the change and the next
 * correction. Clamping where it is read means it can never point past the end.
 *
 * Scrolling is by whole rows because the rows are what the hit test divides by; a smooth offset
 * would put half a row under the pointer and give the click nowhere honest to land. */
uint32_t overlay_model_scroll(uint32_t visible);

/* Moves the first drawn row by `rows`, negative toward the top. Clamped at the top here and at the
 * bottom by the reader above, which is the only place the row count and the visible count are both
 * known. */
void overlay_model_scroll_by(int32_t rows);

/* The row the keyboard is on, or -1 for none.
 *
 * The pad has stepped from row to row since it was added and the keyboard could only scroll, so a
 * machine with no mouse and no pad could open this panel and change nothing in it. The selection
 * is the keyboard's pointer: the arrows move it, Return acts on it, and it is drawn with the same
 * highlight the mouse gives the row under it. Moving the mouse clears it again, so the panel never
 * shows two rows as the current one.
 *
 * Clamped against the row count where it is read, the same way the scroll is, because the count
 * moves with every keystroke in the search box and every fold. */
int32_t overlay_model_selected(void);

/* Puts the selection on `index`, clamped into the list; a negative index clears it. */
void overlay_model_set_selected(int32_t index);

/* Moves the selection by `rows`. From no selection it starts at `from`, which the caller passes
 * as the first row on screen so that the first arrow press lands where the player is looking. */
void overlay_model_move_selection(int32_t rows, uint32_t from);

/* Copies one row out. False for an index past the end, and then `out` is untouched. */
bool overlay_model_row(uint32_t index, overlay_row_t *out);

/* Acts on a row: folds a group, switches a cheat, starts capturing a hotkey. Answers false when the
 * row cannot act, which is a cheat whose site never resolved. The visible list is rebuilt by the
 * caller afterwards. */
bool overlay_model_activate(uint32_t index);

/* Case insensitive substring test, exposed because it is the one piece of the search worth testing
 * on its own. True when `needle` is empty. */
bool overlay_model_matches(const char *label, const char *needle);

/* The five below and the two above them act on the one row that is mid-something, and they live
 * in overlay_edit.c beside that state rather than here: at most one row of this panel is ever
 * being typed into or waiting for a key, and both of those are ended by starting the other.
 *
 * Whether a hotkey row is waiting for its next keypress. While true, overlay_input.c routes the
 * very next key-down here instead of its usual handling (Escape, typing, and so on), including
 * Escape itself and system key combinations; the capture is unconditional by design, so binding
 * is predictable rather than needing its own list of exceptions. */
bool overlay_model_is_capturing_hotkey(void);

/* Ends a capture in progress with this key. A no-op if nothing is capturing. */
void overlay_model_capture_hotkey(int32_t virtual_key);

/* Whether the jump-boost scale row is waiting for typed digits. While true, overlay_input.c routes
 * WM_CHAR here (via overlay_model_value_append()) instead of the search box, and gives Enter/
 * Escape/Backspace their own meaning (commit/cancel/delete) instead of their usual one, the same
 * kind of unconditional redirect overlay_model_is_capturing_hotkey() above already gets, for the
 * same reason: predictable is better than a second list of keys this refuses. */
bool overlay_model_is_editing_value(void);

/* Appends one character if it could plausibly be part of a positive decimal number (a digit, or a
 * single '.'); anything else, and a second '.', are silently refused rather than accepted and
 * later failing to parse. No-op unless a capture is in progress. */
void overlay_model_value_append(char digit);
void overlay_model_value_backspace(void);

/* Parses what has been typed and, if it is a usable positive number, hands it to
 * cheats_openphantom_jump_boost_set_scale(), which clamps it, and ends the capture either way.
 * An empty field commits nothing, leaving whatever scale was already set untouched rather than
 * zeroing it out. */
void overlay_model_value_commit(void);

/* Ends a capture in progress, discarding what has been typed. A no-op if nothing is capturing;
 * also called on every click that does not land back on the value row, so at most one field in
 * this panel is ever mid-edit at a time. */
void overlay_model_value_cancel(void);

#endif /* OVERLAY_MODEL_H */
