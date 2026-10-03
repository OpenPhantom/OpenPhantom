/* overlay_reason.h: why a row of the panel cannot be used, in one vocabulary.
 *
 * A row that is not available used to read `n/a`, one word for four different situations: a site
 * that never resolved, a row deliberately held back, another row that has to be switched first,
 * and a running multiplayer session. Only two of those are anything a player can act on, and none
 * of them could be told apart, so the panel answered "no" and never "why not".
 *
 * The answer is a code on the row now. Whoever makes the row unavailable says which of these it
 * is, and this file turns that code into the two things the panel shows: the short word in the
 * chip, which every unavailable row carries, and the sentence written once under the first row
 * that carries it, for the codes whose word cannot be understood on its own.
 *
 * The words are kept under ten characters on purpose. A row costs its label and its chip side by
 * side out of one width, and the longest labels in this panel are already within a few characters
 * of it; a longer word here would be paid for by the name being cut off, which is the one part a
 * player needs to read.
 */
#ifndef DEV_OVERLAY_OVERLAY_REASON_H
#define DEV_OVERLAY_OVERLAY_REASON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum overlay_reason {
    /* Nothing said. Either the row is available, or it is not and nobody named a reason, which is
     * the site that never resolved: the word for it stays the `n/a` the panel always showed. */
    OVERLAY_REASON_NONE = 0,

    /* A running multiplayer session took it. The sentence is the lock's own, so the two cannot
     * say different things; see session_lock.h for which rows and why. */
    OVERLAY_REASON_SESSION,

    /* Another row decides this one: switch that one, and this one comes back. */
    OVERLAY_REASON_NEEDS_KEY,
    OVERLAY_REASON_NEEDS_ROW,

    /* The entity spawner's group, which works its own reason out for the whole group and writes
     * it as a sentence inside itself. The word points at that sentence rather than repeating it:
     * it is longer than a chip and it changes with the world.
     *
     * It outranks the session, which is the one thing here that is not obvious. A session is not
     * what takes that group; a session that does not run the copies is, and that is one of the
     * four states the group's own sentence already distinguishes. While the lock relabelled these
     * rows, the panel carried two spellings of one state a few rows apart: `session` plus "The
     * multiplayer runs no spawned entities" on the rows, and "Why: the session runs no copies"
     * above them, written by the rule the rows, the placement mode and the log all read. */
    OVERLAY_REASON_SPAWNER,

    /* Resolved, runs, and still not offered. Three rows of the shipped console are in this state
     * and each is here for a different measured reason, which is why they are three codes and not
     * one: a player who reads "held back" and nothing else learns no more than from `n/a`. */
    OVERLAY_REASON_HELD_BREAKS_MENU,
    OVERLAY_REASON_HELD_NO_EFFECT,
    OVERLAY_REASON_HELD_MISBEHAVES,

    /* The opposite of the session's own code: a row that exists FOR a multiplayer session and
     * has nothing to act on without one. The two buttons under Multiplayer carry it in single
     * player. In a session whose multiplayer does not answer for a button the row names no
     * reason and reads `n/a`, the word for something this build cannot do. */
    OVERLAY_REASON_NEEDS_SESSION,

    /* A row that takes this machine to the host, on the machine that is the host. */
    OVERLAY_REASON_IS_HOST,

    /* The row was pressed and what it started has not ended yet. The word says so on its own,
     * and the line under the buttons says how it stands. */
    OVERLAY_REASON_UNDER_WAY,

    OVERLAY_REASON_COUNT
} overlay_reason_t;

/* The word in the chip. Never NULL: an unknown code and OVERLAY_REASON_NONE both answer `n/a`,
 * which is what the panel showed before any of this existed. */
const char *overlay_reason_word(uint32_t reason);

/* The sentence written once under the first row carrying this reason, or NULL when the word says
 * enough on its own. The strings are literals of this file and live as long as the process, so a
 * caller may compare two answers by pointer to see whether it has already written one. */
const char *overlay_reason_sentence(uint32_t reason);

/* Whether this reason outranks anything else that would take the row away later.
 *
 * Two things can take one row: what the row is, and what the moment is. A row held back on this
 * build is held back whatever else is true, and a session that arrives afterwards must not
 * relabel it: "View credits" is on the session's list AND on the held-back list, and while the
 * session owned the word a player read `session` on a row that a session has nothing to do with,
 * and the sentence that says why it is really refused was gone. So the more specific answer
 * wins, and which answers are the specific ones is decided here rather than at each writer. */
bool overlay_reason_is_final(uint32_t reason);

#endif /* DEV_OVERLAY_OVERLAY_REASON_H */
