/* mover_slots.c: see mover_slots.h. */
#include "mover_slots.h"

#include <string.h>

static size_t slot_index_for(const void *subnode)
{
    /* The pointers are 0x9C apart, so the low bits alone would collide on every neighbour. */
    uintptr_t value = (uintptr_t)subnode;

    return (size_t)(((value >> 2) ^ (value >> 11)) & (MOVER_SLOT_COUNT - 1u));
}

mover_slot_t *mover_slots_find(mover_slot_table_t *table, const void *subnode)
{
    size_t index = slot_index_for(subnode);
    size_t probe;

    if (subnode == NULL) {
        return NULL;                    /* NULL marks an empty slot, so it can never be a key */
    }
    for (probe = 0; probe < MOVER_SLOT_COUNT; ++probe) {
        mover_slot_t *slot = &table->slots[(index + probe) & (MOVER_SLOT_COUNT - 1u)];

        if (slot->subnode == subnode) {
            return slot;
        }
        if (slot->subnode == NULL) {
            return NULL;
        }
    }
    return NULL;
}

static bool slot_is_stale(const mover_slot_t *slot, uint32_t frame_stamp)
{
    return slot->subnode != NULL && (frame_stamp - slot->tick_stamp) > MOVER_SLOT_STALE_FRAMES;
}

mover_slot_t *mover_slots_reserve(mover_slot_table_t *table, const void *subnode,
                                  uint32_t frame_stamp)
{
    size_t        index = slot_index_for(subnode);
    size_t        probe;
    mover_slot_t *stale = NULL;

    if (subnode == NULL) {
        return NULL;
    }
    for (probe = 0; probe < MOVER_SLOT_COUNT; ++probe) {
        mover_slot_t *slot = &table->slots[(index + probe) & (MOVER_SLOT_COUNT - 1u)];

        if (slot->subnode == subnode) {
            return slot;
        }
        if (slot->subnode == NULL) {
            break;
        }
        if (stale == NULL && slot_is_stale(slot, frame_stamp)) {
            /* Remembered but not taken: a slot further along may still hold this very subnode,
             * and taking this one first would put it in the table twice. */
            stale = slot;
        }
    }
    if (stale == NULL) {
        if (probe == MOVER_SLOT_COUNT) {
            return NULL;                            /* full of live movers: not smoothed */
        }
        stale = &table->slots[(index + probe) & (MOVER_SLOT_COUNT - 1u)];
    }
    /* A newcomer starts clean. Whatever the previous owner left is a different mover's history,
     * and one of these is exactly what a freed address reused by the next level would inherit. */
    memset(stale, 0, sizeof *stale);
    stale->subnode = subnode;
    return stale;
}

void mover_slots_forget(mover_slot_table_t *table)
{
    memset(table->slots, 0, sizeof table->slots);
}
