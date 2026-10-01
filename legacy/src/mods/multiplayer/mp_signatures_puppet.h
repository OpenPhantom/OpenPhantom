/* mp_signatures_puppet.h: the second site table, the body and sabre cluster the puppet is dressed
 * with.
 *
 * The main pattern file was ten entries short of the hard size limit and two features were about to
 * add six more. The seam chosen is by engine subsystem: everything here is a function of the body
 * object, the animation puppet or the sabre swing, and everything there is the scheduler, the
 * player lifecycle, the input readers and the data anchors. Both tables share one enumeration,
 * mp_site_t, so no caller knows or cares which file a site lives in: the resolver merges this table
 * behind the main one before anything is looked up.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_PUPPET_H
#define MULTIPLAYER_MP_SIGNATURES_PUPPET_H

#include <stddef.h>

#include "common/signature.h"

/* The three things read out of MP_SITE_HERO_ASSET_TABLE, as offsets into its pattern. They live
 * here rather than at the reader because they are properties of the pattern: moving a byte in the
 * pattern moves them, and a reader carrying its own copy would go on reading the old place and
 * answer with something. A wrong offset into a matched pattern does not fail, it answers, which is
 * why the numbers and the bytes have to be maintained in one file. */
#define MP_HERO_ASSET_SITE_RECORD    1u    /* the player record cell, an absolute operand */
#define MP_HERO_ASSET_SITE_TABLE     11u   /* the four entry name table, an absolute operand */
#define MP_HERO_ASSET_SITE_RES_ALLOC 21u   /* the call whose target loads an actor by name */

/* The template rows, in the order of the puppet block of mp_site_t. Const on purpose: the
 * resolver copies them into its own merged table and writes the resolved addresses there, so the
 * template stays what the file says and a test can check its shape without a game. */
const signature_t *mp_signatures_puppet_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_PUPPET_H */
