/* mp_level_state_journal_rule.h: the level's journal, the host's window of the changes that took
 * effect, and what a client plays of it.
 *
 * Layer 1, pure. The level's note is a state, and a state says only where things ended: the green
 * a script turned on and off again between two notes, and a line of text that crawled across the
 * screen, are in no state at all. So the host keeps the last sixteen changes that took effect,
 * each with a number of its own, and every note carries all sixteen. A client plays the entries
 * whose numbers come after the last one it played, in the host's order. Sixteen and not "since
 * the last note" because a client keeps only the newest note it was given and the channel may
 * replace one note by the next before it leaves: with the window, a note lost that way costs
 * nothing as long as fewer than sixteen changes followed it.
 *
 * When a client finds the numbers jumped past the window, what it missed is gone. It takes the
 * level from the note's state instead and counts the jump; the text lines inside the window that
 * it has not seen are still shown, because a line is a moment and no state carries it. A client's
 * first note of a level and generation shows no line at all: a late joiner is not handed the text
 * the others read a minute ago (Quake 3's server commands work the same way, the gamestate for the
 * newcomer and the commands only from there on).
 *
 * The numbers run 1..65535 and never 0, which means none, and wrap; "after" is the nearer way
 * round the circle.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_JOURNAL_RULE_H
#define MULTIPLAYER_MP_LEVEL_STATE_JOURNAL_RULE_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_level_journal {
    uint16_t                 newest;   /* 0 before any */
    uint8_t                  count;
    mp_level_journal_entry_t entry[MP_LEVEL_STATE_JOURNAL_MAX];   /* oldest first */
} mp_level_journal_t;

/* The number after `sequence`, never 0. */
uint16_t mp_level_journal_next(uint16_t sequence);

/* Whether `a` came after `b`. A `b` of 0 is none, and every number comes after it. */
bool mp_level_journal_after(uint16_t a, uint16_t b);

/* How many numbers lie strictly between `from` and `to` going forward, for a jump's count. */
uint32_t mp_level_journal_between(uint16_t from, uint16_t to);

/* Appends one change that took effect, dropping the oldest when the window is full. Returns the
 * number it was given, 0 for a kind the codec does not know. */
uint16_t mp_level_journal_push(mp_level_journal_t *journal, uint8_t kind, uint8_t a, uint16_t b,
                               uint32_t c);

/* The window into a note, and the part bit with it once there is anything to say. */
void mp_level_journal_to_note(const mp_level_journal_t *journal, mp_level_state_note_t *note);

typedef struct mp_level_journal_plan {
    size_t   first;           /* the first entry of the note's window to look at */
    size_t   count;           /* how many from there, in order */
    bool     first_snapshot;  /* the first note of a level and generation: nothing is played */
    bool     jump;            /* numbers were lost: only lines are played, the rest is healed */
    uint32_t lost;            /* how many numbers the jump lost */
    uint16_t newest;          /* the number to remember as played afterwards */
} mp_level_journal_plan_t;

/* What a client does with a note's journal, given whether it has taken a note of this level and
 * generation before and the last number it played. */
mp_level_journal_plan_t mp_level_journal_plan(const mp_level_state_note_t *note, bool known,
                                              uint16_t last);

/* The kinds that are moments rather than a change of state: only these are played after a jump. */
bool mp_level_journal_is_moment(uint8_t kind);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_JOURNAL_RULE_H */
