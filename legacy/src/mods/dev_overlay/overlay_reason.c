/* overlay_reason.c: see overlay_reason.h. */
#include "overlay_reason.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct reason_words {
    const char *word;        /* the chip, never NULL */
    const char *sentence;    /* the line under the first row that carries it, or NULL */
    bool        final;       /* outranks anything that would take the row away afterwards */
} reason_words_t;

/* One row per code, in the order of the enum.
 *
 * The session code carries no sentence of its own although it plainly needs one: the lock owns
 * those words, writes them once under the first row it took, and a second copy here would be one
 * sentence with two spellings free to drift apart. The spawner's code is the same arrangement with
 * a different owner, and it is final for that reason: its sentence is written by the group, out of
 * the rule the rows, the placement mode and the log all read, so a session must not take the word
 * off the row and put a second sentence beside that one.
 *
 * The three held-back codes are the opposite case. Their word is the same for all three, because
 * "held back" is what a player sees on the row, and the sentence is what separates them; each was
 * established against the running game, and none of them is a failure to resolve.
 *
 * The last three belong to the two buttons under Multiplayer. `MP only` was chosen over the
 * plainer "no session" because that has ten characters, one more than a chip may cost, and over
 * `session`, which is already the word for a row a session TOOK. None of the three is final: the
 * session lock takes nothing from that group, so there is nothing for them to outrank.
 */
static const reason_words_t WORDS[OVERLAY_REASON_COUNT] = {
    { "n/a",       NULL,                                       false },
    { "session",   NULL,                                       false },
    { "needs key", NULL,                                       false },
    { "needs row", NULL,                                       false },
    { "see why",   NULL,                                       true  },
    { "held back", "Held back: it breaks this menu",            true },
    { "held back", "Held back: nothing about it is visible",    true },
    { "held back", "Held back: it misbehaves from this menu",   true },
    { "MP only",   "Only in a multiplayer session",            false },
    { "host",      "You are the host",                         false },
    { "running",   NULL,                                       false }
};

const char *overlay_reason_word(uint32_t reason)
{
    if (reason >= (uint32_t)OVERLAY_REASON_COUNT) {
        return WORDS[OVERLAY_REASON_NONE].word;
    }
    return WORDS[reason].word;
}

const char *overlay_reason_sentence(uint32_t reason)
{
    if (reason >= (uint32_t)OVERLAY_REASON_COUNT) {
        return NULL;
    }
    return WORDS[reason].sentence;
}

bool overlay_reason_is_final(uint32_t reason)
{
    if (reason >= (uint32_t)OVERLAY_REASON_COUNT) {
        return false;
    }
    return WORDS[reason].final;
}
