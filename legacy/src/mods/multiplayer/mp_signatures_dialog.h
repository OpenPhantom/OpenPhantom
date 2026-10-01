/* mp_signatures_dialog.h: the fifth site table, the two entry points of the one conversation.
 *
 * The enumeration is mp_dialog_site_t in mp_signatures.h, beside the others, because that is where
 * a reader looks for "which engine entry points does this feature reach for". The patterns and the
 * reason each is cut where it is cut are in mp_signatures_dialog.c.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_DIALOG_H
#define MULTIPLAYER_MP_SIGNATURES_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp_signatures.h"

#include "common/signature.h"

/* Resolves both. Returns how many resolved. Safe to call again. Both are optional and a caller has
 * to check: without either one the conversation does not travel, and it says so rather than
 * hulling one half of itself. */
size_t mp_signatures_dialog_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_dialog_address(mp_dialog_site_t site);

/* The prologue length declared for the site, for a caller that hulls it. */
size_t mp_signatures_dialog_prologue(mp_dialog_site_t site);

/* One site, so that a caller can read a cell out of its matched operands. mp_cells.c does the same
 * thing for the main table; the conversation's cells are few enough and local enough to be read
 * where they are used. */
const signature_t *mp_signatures_dialog_site(mp_dialog_site_t site);

/* The whole table, MP_DIALOG_SITE_COUNT long and indexed by mp_dialog_site_t, so that a test can
 * check its shape without a game. */
const signature_t *mp_signatures_dialog_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_DIALOG_H */
