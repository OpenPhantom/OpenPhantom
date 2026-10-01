/* character_clipcopy.c: rebuilding a clip in another rig's ordinal space and on its own build. */
#include "character_clipcopy.h"

#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The clip layout, confirmed by the engine's own text writer (0x0047A27D) and by its parser
 * (0x00479C7B), which allocates numJoints * 0x2c and strides the entry pool by 0x38. */
#define CLIP_HEADER_BYTES     0x84u
#define CLIP_NUM_JOINTS       0x28u
#define CLIP_TYPE             0x2Cu
#define CLIP_NUM_NODES        0x38u
#define CLIP_NODES            0x3Cu
#define CLIPNODE_BYTES        0x2Cu
#define CLIPNODE_JOINT        0x20u   /* read by the cursor walk, so it has to follow the ordinal */
#define CLIPNODE_ENTRIES      0x24u   /* zero means this node contributes nothing and holds still */
#define CLIPNODE_POOL         0x28u   /* the keyframe entries, fixed up into the asset's block */

#define KEYENTRY_BYTES        0x38u

/* The widest clip the six rigs a hero slot can carry holds 966 entries. The cap is orders above
 * that and exists so a count read out of memory cannot overflow the byte arithmetic below it. */
#define CLIP_MAX_ENTRIES      0x10000u

/* Every part bit, so a node that survived the reference side mask wins its node on the target
 * whatever the target authored at node+0x48. The shipped clips use 0xffff for a full body clip. */
#define CLIP_TYPE_EVERY_PART  0xFFFFu

/* One copy per clip the hero plays, and the player's own asset carries 114. The table half is
 * sized for the widest rig at the cache's capacity, so a full cache cannot run it dry.
 *
 * The pool half holds the shifted keyframe entries. Measured over the six rigs a hero slot can
 * carry, the widest whole set of clips an asset owns is obiwan's 18254 entries, 1022224 bytes, and
 * that is the ceiling: each clip is copied at most once and a copy takes only the records the
 * target actually uses. The budget sits a quarter above it. Running out is not fatal, it is the
 * warning below and a track that contributes nothing for that one call. */
#define CACHE_MAX             192u
#define TABLE_ARENA_BYTES    (CACHE_MAX * (CLIP_HEADER_BYTES + NODEMAP_MAX_NODES * CLIPNODE_BYTES))
#define POOL_ARENA_BYTES     (1280u * 1024u)
#define ARENA_BYTES          (TABLE_ARENA_BYTES + POOL_ARENA_BYTES)

/* Each pair's share of the reservation starts on an allocation granule of its own, so committing
 * and decommitting one share never touches a page of the next. */
#define ARENA_GRANULE         0x10000u
#define ARENA_STRIDE         ((ARENA_BYTES + ARENA_GRANULE - 1u) & ~(ARENA_GRANULE - 1u))

typedef struct clip_entry {
    uintptr_t original;
    uintptr_t copy;
} clip_entry_t;

typedef struct clip_source {
    uint32_t nodes;
    uint32_t type;
    uint32_t base;
} clip_source_t;

typedef struct clip_pair {
    bool           armed;
    bool           arena_reported;

    int32_t        map[NODEMAP_MAX_NODES];
    uint32_t       reference_type[NODEMAP_MAX_NODES];
    nodemap_rest_t rebase[NODEMAP_MAX_NODES];
    uint32_t       target_nodes;

    uint8_t       *arena;          /* this pair's share, committed while it is armed */
    size_t         arena_used;
    clip_entry_t   cache[CACHE_MAX];
    uint32_t       cache_count;
} clip_pair_t;

typedef struct clipcopy_state {
    uint8_t     *reserve;          /* pair 0's share of address space, none of it committed */
    uint8_t     *far_reserve;      /* the shares of the pairs after it, NULL when refused */
    bool         far_tried;
    clip_pair_t  pair[PAIR_MAX];
} clipcopy_state_t;

static clipcopy_state_t clips;

/* The share a pair commits into. Pair 0's is reserved on its own, the size the player's own swap
 * has always needed; the far bodies' shares are one reservation after it, and a refusal of that
 * one takes no share of his. NULL for a share that was not reserved. */
static uint8_t *share_of(uint32_t pair)
{
    if (pair == 0u) {
        return clips.reserve;
    }
    return clips.far_reserve != NULL ? clips.far_reserve + (size_t)(pair - 1u) * ARENA_STRIDE
                                     : NULL;
}

/* ============================================================================================ */

static void *arena_take(clip_pair_t *p, size_t bytes)
{
    void *block;

    if (p->arena == NULL || p->arena_used + bytes > (size_t)ARENA_BYTES) {
        return NULL;
    }
    block = p->arena + p->arena_used;
    p->arena_used += bytes;
    return block;
}

static uintptr_t cache_lookup(const clip_pair_t *p, uintptr_t original)
{
    uint32_t i;

    for (i = 0; i < p->cache_count; ++i) {
        if (p->cache[i].original == original) {
            return p->cache[i].copy;
        }
    }
    return 0u;
}

/* ============================================================================================ */

/* Which reference record a target node takes, or false when it takes none. An empty record is the
 * answer to three different questions: this node has no counterpart, the counterpart is outside the
 * clip's authored node table, and the clip's own part mask does not claim it. */
static bool record_source(const clip_pair_t *p, const clip_source_t *source, uint32_t j,
                          uint32_t *from)
{
    int32_t at = p->map[j];

    if (at < 0 || (uint32_t)at >= source->nodes) {
        return false;
    }
    if ((p->reference_type[at] & source->type) == 0u) {
        return false;
    }
    *from = (uint32_t)at;
    return true;
}

/* Whether a record needs a private keyframe pool, and how many entries it has. Only a record whose
 * shift is not zero does; every other record keeps the asset's own pool. */
static bool record_needs_pool(const clip_pair_t *p, const clip_source_t *source, uint32_t j,
                              uint32_t from, uint32_t *entries)
{
    uintptr_t record = (uintptr_t)source->base + (uintptr_t)from * CLIPNODE_BYTES;

    *entries = *(const uint32_t *)(record + CLIPNODE_ENTRIES);
    if (character_nodemap_shift_is_zero(&p->rebase[j])) {
        return false;
    }
    /* Zero entries is the compositor's own skip, so nothing is ever read out of that pool. */
    return *entries != 0u;
}

/* The private pool bytes a copy needs, measured before anything is taken. False when a record's
 * entry count or its pool cannot be trusted, and then no copy may be built at all: a half shifted
 * clip would drive some joints from the target's rest and others from the reference's. */
static bool pool_bytes(const clip_pair_t *p, const clip_source_t *source, size_t *out)
{
    size_t   total = 0;
    uint32_t j;

    for (j = 0; j < p->target_nodes; ++j) {
        uintptr_t record;
        uint32_t  from = 0;
        uint32_t  entries = 0;
        uint32_t  pool = 0;

        if (!record_source(p, source, j, &from) ||
            !record_needs_pool(p, source, j, from, &entries)) {
            continue;
        }
        record = (uintptr_t)source->base + (uintptr_t)from * CLIPNODE_BYTES;
        pool = *(const uint32_t *)(record + CLIPNODE_POOL);
        if (entries > CLIP_MAX_ENTRIES || pool == 0u ||
            !memory_try_readable((uintptr_t)pool, (size_t)entries * KEYENTRY_BYTES)) {
            return false;
        }
        total += (size_t)entries * KEYENTRY_BYTES;
    }
    *out = total;
    return true;
}

/* THE COPY. The header is taken verbatim, so the clock, the frame count and the fade behaviour are
 * the clip's own; only the node table is rebuilt, and it is rebuilt in the TARGET's ordinal space
 * so that node+0x44, which the compositor uses as a clip index, keeps meaning the node it means on
 * every other model. `jointIdx` is rewritten to the record's new ordinal, because the cursor walk
 * reads the cursor at kfCursor[jointIdx] and writes it at kfCursor[n]. */
static uintptr_t clip_copy_of(clip_pair_t *p, uintptr_t original)
{
    clip_source_t source;
    size_t        table = (size_t)p->target_nodes * CLIPNODE_BYTES;
    size_t        pool_total = 0;
    uint8_t      *copy;
    uint8_t      *records;
    uint8_t      *pool;
    uint8_t      *pool_end;
    uint32_t      j;

    if (p->cache_count >= CACHE_MAX) {
        return 0u;
    }
    if (!memory_try_read(original + CLIP_NUM_NODES, &source.nodes, sizeof source.nodes) ||
        !memory_try_read(original + CLIP_TYPE, &source.type, sizeof source.type) ||
        !memory_try_read(original + CLIP_NODES, &source.base, sizeof source.base) ||
        source.base == 0u || source.nodes == 0u || source.nodes > NODEMAP_MAX_NODES) {
        return 0u;
    }
    if (!memory_try_readable((uintptr_t)source.base, (size_t)source.nodes * CLIPNODE_BYTES) ||
        !memory_try_readable(original, CLIP_HEADER_BYTES)) {
        return 0u;
    }
    if (!pool_bytes(p, &source, &pool_total)) {
        return 0u;
    }

    copy = (uint8_t *)arena_take(p, CLIP_HEADER_BYTES + table + pool_total);
    if (copy == NULL) {
        return 0u;
    }
    memcpy(copy, (const void *)original, CLIP_HEADER_BYTES);
    records = copy + CLIP_HEADER_BYTES;
    memset(records, 0, table);
    pool = records + table;
    pool_end = pool + pool_total;

    for (j = 0; j < p->target_nodes; ++j) {
        uint8_t *record = records + (size_t)j * CLIPNODE_BYTES;
        uint32_t from = 0;
        uint32_t entries = 0;
        uint32_t e;

        if (!record_source(p, &source, j, &from)) {
            continue;
        }
        memcpy(record, (const void *)((uintptr_t)source.base + (uintptr_t)from * CLIPNODE_BYTES),
               CLIPNODE_BYTES);
        *(uint32_t *)(record + CLIPNODE_JOINT) = j;

        if (!record_needs_pool(p, &source, j, from, &entries)) {
            continue;
        }
        /* The measuring pass above and this one read the same fields of the same memory with
         * nothing in between, so they cannot disagree. The bound is checked anyway, because the
         * cost of being wrong here is a write past the block. */
        if ((size_t)(pool_end - pool) < (size_t)entries * KEYENTRY_BYTES) {
            return 0u;
        }
        memcpy(pool, (const void *)(uintptr_t)*(const uint32_t *)(record + CLIPNODE_POOL),
               (size_t)entries * KEYENTRY_BYTES);
        for (e = 0; e < entries; ++e) {
            character_nodemap_shift_entry(pool + (size_t)e * KEYENTRY_BYTES, &p->rebase[j]);
        }
        *(uint32_t *)(record + CLIPNODE_POOL) = (uint32_t)(uintptr_t)pool;
        pool += (size_t)entries * KEYENTRY_BYTES;
    }

    *(uint32_t *)(copy + CLIP_NUM_NODES) = p->target_nodes;
    *(uint32_t *)(copy + CLIP_NUM_JOINTS) = p->target_nodes;
    *(uint32_t *)(copy + CLIP_NODES) = (uint32_t)(uintptr_t)records;
    *(uint32_t *)(copy + CLIP_TYPE) = CLIP_TYPE_EVERY_PART;

    p->cache[p->cache_count].original = original;
    p->cache[p->cache_count].copy = (uintptr_t)copy;
    p->cache_count++;
    return (uintptr_t)copy;
}

/* ============================================================================================ */

bool character_clipcopy_reserve(void)
{
    if (clips.reserve == NULL) {
        clips.reserve = (uint8_t *)VirtualAlloc(NULL, (SIZE_T)ARENA_STRIDE, MEM_RESERVE,
                                                PAGE_NOACCESS);
        if (clips.reserve == NULL) {
            log_warning("the %u byte clip translation arena could not be reserved (%lu), so no "
                        "model swap is offered", (unsigned)ARENA_STRIDE,
                        (unsigned long)GetLastError());
            return false;
        }
    }
    /* Asked once, like pair 0's. */
    if (!clips.far_tried) {
        clips.far_tried = true;
        clips.far_reserve = (uint8_t *)VirtualAlloc(NULL, (SIZE_T)ARENA_STRIDE * (PAIR_MAX - 1u),
                                                    MEM_RESERVE, PAGE_NOACCESS);
        if (clips.far_reserve == NULL) {
            log_warning("the %u byte clip arena of the far bodies could not be reserved (%lu), so "
                        "the player's own model swap is offered and no far body is dressed",
                        (unsigned)(ARENA_STRIDE * (PAIR_MAX - 1u)),
                        (unsigned long)GetLastError());
        }
    }
    return true;
}

bool character_clipcopy_far_reserved(void)
{
    return clips.far_reserve != NULL;
}

size_t character_clipcopy_release(uint32_t pair)
{
    clip_pair_t *p;
    size_t       released = 0;

    if (pair >= PAIR_MAX) {
        return 0u;
    }
    p = &clips.pair[pair];
    if (p->arena != NULL) {
        /* Decommitted rather than kept: the pages a far body's pair filled are given back to the
         * system the moment nobody wears it, and a later pair commits zeroed pages again. */
        if (VirtualFree(p->arena, (SIZE_T)ARENA_BYTES, MEM_DECOMMIT)) {
            released = ARENA_BYTES;
        }
    }
    memset(p, 0, sizeof *p);
    return released;
}

bool character_clipcopy_arm(uint32_t pair, const int32_t *map, const uint32_t *reference_type,
                            const nodemap_rest_t *rebase, uint32_t target_nodes, bool say)
{
    clip_pair_t *p;
    uint8_t     *share;
    void        *arena;

    if (pair >= PAIR_MAX || map == NULL || reference_type == NULL || rebase == NULL ||
        target_nodes == 0u || target_nodes > NODEMAP_MAX_NODES) {
        return false;
    }
    share = share_of(pair);
    if (share == NULL) {
        return false;
    }
    p = &clips.pair[pair];
    if (p->armed) {
        return false;
    }
    arena = VirtualAlloc(share, (SIZE_T)ARENA_BYTES, MEM_COMMIT, PAGE_READWRITE);
    if (arena == NULL) {
        if (say) {
            log_warning("the %u byte clip arena of pair %u could not be committed (%lu), so the "
                        "body is not translated", (unsigned)ARENA_BYTES, (unsigned)pair,
                        (unsigned long)GetLastError());
        }
        return false;
    }
    memset(p, 0, sizeof *p);
    p->arena = (uint8_t *)arena;
    memcpy(&p->map[0], map, sizeof p->map);
    memcpy(&p->reference_type[0], reference_type, sizeof p->reference_type);
    memcpy(&p->rebase[0], rebase, sizeof p->rebase);
    p->target_nodes = target_nodes;
    p->armed = true;
    return true;
}

uintptr_t character_clipcopy_for(uint32_t pair, uintptr_t original)
{
    clip_pair_t *p;
    uintptr_t    copy;

    if (pair >= PAIR_MAX || original == 0u) {
        return 0u;
    }
    p = &clips.pair[pair];
    if (!p->armed) {
        return 0u;
    }
    copy = cache_lookup(p, original);
    if (copy != 0u) {
        return copy;
    }
    copy = clip_copy_of(p, original);
    if (copy == 0u && !p->arena_reported) {
        p->arena_reported = true;
        log_warning("a clip could not be translated, so it contributes nothing this frame: the "
                    "copy arena or the copy table is full at %u entries", p->cache_count);
    }
    return copy;
}
