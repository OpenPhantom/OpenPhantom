/* character_clipcopy.h: the private clips a borrowed skeleton is driven by.
 *
 * This is the half of the name translation that touches keyframe data. `character_nodemap.c` works
 * out WHICH reference joint drives which target joint and by how much the pose has to be re-seated;
 * this module turns that answer into clips the engine's own pose compositor can play, and hands
 * them out one at a time for the length of a single call.
 *
 * A copy is the clip's header verbatim, a node table rebuilt in the TARGET's ordinal space, and,
 * for every record whose shift is not zero, a private keyframe pool holding the asset's own entries
 * with that shift applied. A record whose shift is zero keeps the pointer the engine fixed up into
 * the asset's block, so a swap between two identical rigs costs no pool memory at all.
 *
 * The map is passed, not shared, and that is the whole reason this can be a translation unit of its
 * own. A table built in one ordinal space and filled in another is how the two halves would come to
 * disagree; here the map, the reference part words and the shift arrive together in one call and
 * are copied, so there is no second opinion about which space anything is in.
 *
 * The pools belong to the engine. A copy points into, or copies out of, keyframe entries owned by
 * the asset the reference rig comes from, and the resource layer is free to release that asset
 * when its body is taken down. So a pair's copies live exactly as long as the pair has a wearer:
 * they are built for the first, kept for every further one, and dropped with the last.
 *
 * One arena per pair, and the address space for all of them is reserved at install: pair 0's share
 * on its own, the far pairs' shares in one piece after it. A pair commits its own share when it is
 * armed and gives it back when it is released, so the player's own swap fails where and at the
 * size it always did, a refusal of the far shares takes only the far bodies, and a far body's pair
 * costs memory only while it is worn.
 */
#ifndef CHARACTER_CLIPCOPY_H
#define CHARACTER_CLIPCOPY_H

#include "character_bodies.h"
#include "character_nodemap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reserves the address space the copies of every pair live in: pair 0's share, then the others'.
 * Idempotent, and false when pair 0's share was refused, in which case no swap may be offered: a
 * clip that cannot be translated drives the wrong joints. A refusal of the others' shares is said
 * once and leaves the answer true; those pairs then cannot be armed, and the far bodies' path asks
 * character_clipcopy_far_reserved before it dresses anybody, so the player's own body is then the
 * only one filed and takes pair 0. */
bool character_clipcopy_reserve(void);

/* Whether the far pairs' shares are reserved. */
bool character_clipcopy_far_reserved(void);

/* Takes the translation for one pair, below PAIR_MAX, and commits its arena. `map` and `rebase`
 * carry one entry per target node and `reference_type` one per reference node, all bounded by
 * NODEMAP_MAX_NODES. A pair that is armed already is refused rather than started again, because
 * its wearers are posing out of the copies it holds. False leaves the pair unarmed. `say` is
 * whether a commit the system refuses is said in the log, which a far body asked again at every
 * scene end says once. */
bool character_clipcopy_arm(uint32_t pair, const int32_t *map, const uint32_t *reference_type,
                            const nodemap_rest_t *rebase, uint32_t target_nodes, bool say);

/* Drops every copy of one pair, gives its arena back and forgets it. Answers how many bytes were
 * decommitted, zero when nothing was armed. */
size_t character_clipcopy_release(uint32_t pair);

/* The copy of one clip for one pair, built once and then kept. Answers 0 when the clip cannot be
 * translated, and the caller must then keep that clip from playing rather than let it drive the
 * target's joints by the reference rig's ordinals. */
uintptr_t character_clipcopy_for(uint32_t pair, uintptr_t original);

#endif /* CHARACTER_CLIPCOPY_H */
