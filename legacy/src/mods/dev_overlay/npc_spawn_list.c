/* npc_spawn_list.c: see npc_spawn_list.h. */
#include "npc_spawn_list.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

static bool read_word(uintptr_t at, uintptr_t *out)
{
    uint32_t word = 0;

    if (!memory_try_read_u32(at, &word)) {
        return false;
    }
    *out = (uintptr_t)word;
    return true;
}

static bool write_word(uintptr_t at, uintptr_t value)
{
    uint32_t word = (uint32_t)value;

    return memory_try_write(at, &word, sizeof word);
}

/* The word that points at a node's place in the chain: its predecessor's link, or the head. */
static uintptr_t place_of(uintptr_t list, uintptr_t previous)
{
    return previous != 0 ? previous : list + NPC_SPAWN_LIST_HEAD;
}

bool npc_spawn_list_each(uintptr_t list, uint32_t limit, npc_spawn_list_visit_fn visit,
                         void *user)
{
    uintptr_t node  = 0;
    uint32_t  steps = 0;

    if (list == 0 || visit == NULL || !read_word(list + NPC_SPAWN_LIST_HEAD, &node)) {
        return false;
    }
    while (node != 0) {
        uintptr_t next = 0;

        if (steps++ >= limit || node == NPC_SPAWN_LIST_FREE || !read_word(node, &next) ||
            next == NPC_SPAWN_LIST_FREE) {
            return false;
        }
        visit(node + 4u, user);
        node = next;
    }
    return true;
}

bool npc_spawn_list_detach(uintptr_t list, uint32_t limit, npc_spawn_list_pick_fn pick,
                           void *user, npc_spawn_unlinked_t *out, uint32_t max, uint32_t *count)
{
    uintptr_t previous = 0;
    uintptr_t node     = 0;
    uint32_t  steps    = 0;
    uint32_t  taken    = 0;
    uint32_t  repaired = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (list == 0 || pick == NULL || (out == NULL && max != 0u) || count == NULL ||
        !read_word(list + NPC_SPAWN_LIST_HEAD, &node)) {
        return false;
    }
    while (node != 0) {
        uintptr_t next = 0;

        if (steps++ >= limit || node == NPC_SPAWN_LIST_FREE || !read_word(node, &next) ||
            next == NPC_SPAWN_LIST_FREE) {
            break;
        }
        if (!pick(node + 4u, user)) {
            previous = node;
            node     = next;
            continue;
        }
        if (taken >= max || !write_word(place_of(list, previous), next)) {
            break;
        }
        out[taken].node     = node;
        out[taken].previous = previous;
        ++taken;
        node = next;   /* `previous` stays: the node behind it now follows it */
    }
    if (node != 0) {
        (void)npc_spawn_list_relink(list, out, taken, &repaired);
        return false;
    }
    *count = taken;
    return true;
}

uint32_t npc_spawn_list_relink(uintptr_t list, const npc_spawn_unlinked_t *unlinked,
                               uint32_t count, uint32_t *repaired)
{
    uint32_t  back = 0;
    uint32_t  i;
    uintptr_t head = 0;

    if (repaired != NULL) {
        *repaired = 0;
    }
    if (list == 0 || (unlinked == NULL && count != 0u)) {
        return 0;
    }
    for (i = count; i-- > 0u;) {
        const npc_spawn_unlinked_t *u     = &unlinked[i];
        uintptr_t                   place = place_of(list, u->previous);
        uintptr_t                   there = 0;
        uintptr_t                   after = 0;

        /* The node's own link still names what followed it, and its place must name that too. */
        if (read_word(place, &there) && read_word(u->node, &after) && there == after &&
            write_word(place, u->node)) {
            ++back;
            continue;
        }
        /* Its place moved under it. At the head it is at least in the chain again. */
        if (read_word(list + NPC_SPAWN_LIST_HEAD, &head) && write_word(u->node, head) &&
            write_word(list + NPC_SPAWN_LIST_HEAD, u->node)) {
            ++back;
            if (repaired != NULL) {
                ++*repaired;
            }
        }
    }
    if (read_word(list + NPC_SPAWN_LIST_HEAD, &head)) {
        (void)write_word(list + NPC_SPAWN_LIST_CURSOR, head);
    }
    return back;
}
