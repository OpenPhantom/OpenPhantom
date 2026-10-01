/* mp_blade.h: a Jedi body's sabre vectors and a puppet's blade, read off the engine and decided by
 * mp_blade_rule.
 *
 * Three moments ask. A far body built as hero 0 or 1 has just read its asset's blade mesh into its
 * bank's block; the player's own spawn or a savegame has just done the same into the hero block;
 * and a puppet's window is about to move the blade's length. The first two keep the longest
 * reading anybody has of that asset, the book's or another standing body's, in place of a reading
 * off a mesh the local player had folded or half grown. The third answers whether the length is
 * stepped in the block, and for which kind of body.
 *
 * This module reads blocks and writes at most the 72 bytes of one block's vectors. The blade mesh
 * is written by the engine's length setter for the local player alone. A far body's blade is
 * drawn from vertices worked out of its own block (mp_blade_draw), so vectors put right here show
 * at the next draw.
 */
#ifndef MULTIPLAYER_MP_BLADE_H
#define MULTIPLAYER_MP_BLADE_H

#include "mp_blade_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Far bank `bank`'s body has just been built, outside every window. The body module calls it
 * after the spawn and before it marks the body standing. The vectors are settled, and the bank's
 * row for drawing its blade from its own vertices is filled. */
void mp_blade_after_far_spawn(size_t bank);

/* The same with the local player's hero block named, which the call above reads from its cell and
 * a test makes up. The others asked are the local player, when his block has a body, and every
 * other far bank whose body stands. Writes the chosen vectors into the bank's block through the
 * bank; answers the source, and the whole choice into `out` when it is not NULL. */
mp_blade_source_t mp_blade_far_spawn_over(size_t bank, uintptr_t local_block,
                                          mp_blade_choice_t *out);

/* The player's own body has just been spawned, or restored from a savegame, with no loan open.
 * Nothing runs without a session's transport, or with a bank window open, where the spawn was a
 * far body's. */
void mp_blade_after_local_spawn(void);
void mp_blade_after_local_restore(void);

/* The same over a hero block that is named, with the far banks whose bodies stand as the others.
 * Writes the vectors into `block` only when they differ from what it holds. */
mp_blade_source_t mp_blade_local_over(uintptr_t block, bool restored, mp_blade_choice_t *out);

/* What the puppet's blade does this substep for the record the window installed, whose body wears
 * a borrowed model when `worn`. The one place the question is asked; the light tick follows the
 * same answer through mp_blade_rule_light_whole. */
mp_blade_tick_t mp_blade_puppet_tick(uintptr_t record, bool worn);

typedef struct mp_blade_counters {
    uint32_t far_jedi;          /* far Jedi bodies whose vectors were settled */
    uint32_t far_book;          /* of them from the book */
    uint32_t far_local;         /* from the local player */
    uint32_t far_bank;          /* from another far bank */
    uint32_t far_own;           /* their own reading */
    uint32_t far_folded;        /* nobody had a blade of the asset */
    uint32_t local_settled;     /* the player's Jedi spawns and restores settled */
    uint32_t local_put_right;   /* of them, the ones whose vectors were replaced */
    uint32_t local_folded;      /* nobody had a blade of the asset */
    uint32_t book_known;        /* asset names the book holds */
    uint32_t book_raised;       /* entries a longer reading replaced */
    uint32_t book_refused;      /* names the full book had no room for */
    uint32_t foreign_hilts;     /* readings with another hilt than the one taken */
    uint32_t write_faults;      /* vectors that could not be written */
} mp_blade_counters_t;

void mp_blade_get_counters(mp_blade_counters_t *out);

#endif /* MULTIPLAYER_MP_BLADE_H */
