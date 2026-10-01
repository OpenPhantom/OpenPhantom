/* npc_spawn_rules.c: see npc_spawn_rules.h. */
#include "npc_spawn_rules.h"

#include "npc_spawn_block.h"
#include "npc_spawn_desc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The block this build writes: its version. */
#define BLOCK_SUBVERSION 1u

bool npc_spawn_rides(uint32_t mode, uint32_t mounted, uint32_t gun, uint32_t key)
{
    if (mode != mounted || mounted == 0u) {
        return false;
    }
    return key == 0u ? npc_spawn_key_is_copy(gun) : gun == key;
}

npc_spawn_block_plan_t npc_spawn_block_plan(uint32_t subversion, int32_t length, size_t room)
{
    if (subversion != BLOCK_SUBVERSION || length < (int32_t)NPC_SPAWN_BLOCK_LENGTH_BYTES ||
        (size_t)length > room) {
        return NPC_SPAWN_BLOCK_STEP_OVER;
    }
    return NPC_SPAWN_BLOCK_READ;
}

uint32_t npc_spawn_hold_new_world(npc_spawn_hold_t *hold)
{
    uint32_t dropped = hold->held;

    ++hold->epoch;
    hold->held     = 0;
    hold->finished = false;
    return dropped;
}

void npc_spawn_hold_read(npc_spawn_hold_t *hold, uint32_t count)
{
    hold->held     = count;
    hold->finished = false;
}

void npc_spawn_hold_load_over(npc_spawn_hold_t *hold)
{
    if (hold->held != 0u) {
        hold->finished = true;
    }
}

bool npc_spawn_hold_due(const npc_spawn_hold_t *hold)
{
    return hold->held != 0u && hold->finished;
}

void npc_spawn_hold_clear(npc_spawn_hold_t *hold)
{
    hold->held     = 0;
    hold->finished = false;
}
