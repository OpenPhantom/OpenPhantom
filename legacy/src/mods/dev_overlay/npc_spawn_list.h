/* npc_spawn_list.h: taking nodes out of one of the engine's lists for a moment, and back.
 *
 * The engine's lists are a header and a block of nodes. The header holds the first node of the
 * chain at +0x04 and a cursor at +0x08; every node is a link word and the element behind it, so an
 * actor sits at its node + 4. The link word names the next node, 0 ends the chain, and -1 marks a
 * node as free: the allocator searches every node for -1, never the chain.
 *
 * The enemy block of a savegame is written by walking that chain, and the panel's copies must not
 * be in it. So for the length of that one call each copy's node is unlinked from the chain with its
 * own link word left as it was, which keeps it out of the walk and out of the allocator both, and
 * afterwards put back exactly where it was. Nothing else here knows what a copy is: a caller's
 * function picks the nodes.
 *
 * Every read and write goes through the guarded memory calls, so a chain that is not what it
 * should be ends in a refusal rather than a fault. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_LIST_H
#define DEV_OVERLAY_NPC_SPAWN_LIST_H

#include <stdbool.h>
#include <stdint.h>

#define NPC_SPAWN_LIST_HEAD   0x04u
#define NPC_SPAWN_LIST_CURSOR 0x08u
#define NPC_SPAWN_LIST_FREE   0xFFFFFFFFu

/* One unlinked node and the node in front of it when it was taken out; 0 when it was the head. */
typedef struct npc_spawn_unlinked {
    uintptr_t node;
    uintptr_t previous;
} npc_spawn_unlinked_t;

/* Whether the element at `element` (its node + 4) is to be taken out. */
typedef bool (*npc_spawn_list_pick_fn)(uintptr_t element, void *user);

/* Called with each element of a chain in turn. */
typedef void (*npc_spawn_list_visit_fn)(uintptr_t element, void *user);

/* Visits every element of the chain of `list` from its head, at most `limit`. False when the
 * chain could not be walked to its end, after visiting what came before. */
bool npc_spawn_list_each(uintptr_t list, uint32_t limit, npc_spawn_list_visit_fn visit,
                         void *user);

/* Walks the chain of `list` from its head, at most `limit` nodes, and unlinks every node `pick`
 * names, into `out` in the order they were taken, at most `max`. False, with every node already
 * taken put back, when the chain could not be read, ran past `limit`, met a free mark, or held
 * more picks than `max`. */
bool npc_spawn_list_detach(uintptr_t list, uint32_t limit, npc_spawn_list_pick_fn pick,
                           void *user, npc_spawn_unlinked_t *out, uint32_t max, uint32_t *count);

/* Puts the nodes back, the last taken first, each after the node it followed, and leaves the
 * cursor on the head, which is where the engine's own walk leaves it. A node whose place is no
 * longer what it was is put at the head instead, since a live element outside the chain would never
 * be ticked or deleted again; those are counted in `repaired`. Answers how many went back. */
uint32_t npc_spawn_list_relink(uintptr_t list, const npc_spawn_unlinked_t *unlinked,
                               uint32_t count, uint32_t *repaired);

#endif /* DEV_OVERLAY_NPC_SPAWN_LIST_H */
