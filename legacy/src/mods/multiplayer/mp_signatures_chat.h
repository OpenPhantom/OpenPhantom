/* mp_signatures_chat.h: the place the chat takes its keys from, as a byte pattern.
 *
 * Layer 2. Its own table, as the crates and the voices have theirs, because the main table's file
 * is at its size limit. One site: the engine's own key handler, main_keyHook, the one function of
 * the game every window message of a running level passes through before the default procedure.
 * The chat is hooked on its head. The movie cell its first test names is read out of the same
 * bytes, so the chat asks the engine's own question for "a movie is playing".
 *
 * The pattern is the one the developer menu finds the same head with, sixty nine bytes, so the two
 * cannot disagree about which function it is. Both declare the same six byte prologue: whichever
 * of the two installs second finds a branch there, and the resolver's second stage takes the head
 * by its tail and proves the branch. The pattern ends before the handler's call of the engine's
 * pause, whose operand the pause menu of a session repoints.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_CHAT_H
#define MULTIPLAYER_MP_SIGNATURES_CHAT_H

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

typedef enum mp_chat_site {
    MP_CHAT_SITE_KEY_HOOK,   /* 0x0043F603  main_keyHook, hooked on its head */
    MP_CHAT_SITE_COUNT
} mp_chat_site_t;

/* Where in the key handler's pattern the operand of the movie test stands: `cmp [the movie
 * cell], 0`, the handler's first question. */
#define MP_CHAT_KEY_HOOK_MOVIE_CELL 0x0Fu

/* Resolves the table once and answers the same afterwards. Answers how many resolved. */
size_t mp_signatures_chat_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_chat_address(mp_chat_site_t site);

/* The prologue declared for a function head, for a caller that hulls it. */
size_t mp_signatures_chat_prologue(mp_chat_site_t site);

/* One site, so a caller can read an operand out of it through the shared reader. */
const signature_t *mp_signatures_chat_site(mp_chat_site_t site);

#endif /* MULTIPLAYER_MP_SIGNATURES_CHAT_H */
