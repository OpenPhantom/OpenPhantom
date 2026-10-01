/* mp_enemy_block.c: an NPC's blade clang, on the machine that only watches. */
#include "mp_enemy_block.h"

#include "mp_enemy_sync.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

/* What the engine's clang reads of the actor before it plays: the body at +0x34, whose thing at
 * +0x9C it asserts on in retail when it looks up the weapon node, and the actor's own position at
 * +0xD0, where the sound is anchored. */
#define ACTOR_BODY  0x34u
#define BODY_THING  0x9Cu
#define ACTOR_POS   0xD0u

typedef struct block_state {
    mp_enemy_block_ready_fn_t  ready;
    mp_enemy_block_replay_fn_t replay;

    uint32_t let_through;  /* passes this side kept because it describes */
    uint32_t posted;       /* of them, taken by the world events */
    uint32_t too_large;    /* a kind the event cannot carry */
    uint32_t played;       /* clangs played on a replica here */
    uint32_t unresolved;   /* nothing to play it through */
    uint32_t unreadable;   /* the replica does not read as far as the engine will */
} block_state_t;

static block_state_t block;

/* Everything the engine's clang dereferences before it plays, read the faulting way: a body with
 * no thing stops retail in an assert, and a position that does not read would leave the clang
 * without a place, audible everywhere at full volume. */
static bool actor_readable(uintptr_t actor)
{
    uint32_t body  = 0;
    uint32_t thing = 0;

    return memory_try_read(actor + ACTOR_BODY, &body, sizeof body) && body != 0u &&
           memory_try_read((uintptr_t)body + BODY_THING, &thing, sizeof thing) && thing != 0u &&
           memory_try_readable(actor + ACTOR_POS, 3u * sizeof(float));
}

/* On a client, one clang the host's cooldown let through, on the replica. */
static bool play_clang(const mp_world_event_t *event, uintptr_t replica)
{
    int32_t kind = 0;

    if (block.ready == NULL || block.replay == NULL || !block.ready()) {
        ++block.unresolved;
        return false;
    }
    if (replica == 0u || !actor_readable(replica) || !mp_enemy_block_unpack(event->a, &kind) ||
        !block.replay(replica, kind)) {
        ++block.unreadable;
        return false;
    }
    ++block.played;
    return true;
}

void mp_enemy_block_set_replay(mp_enemy_block_ready_fn_t ready, mp_enemy_block_replay_fn_t replay)
{
    block.ready  = ready;
    block.replay = replay;
    (void)mp_world_event_set_player(MP_WORLD_EVENT_NPC_CLANG, &play_clang);
}

void mp_enemy_block_note(uint32_t actor, int32_t kind, mp_enemy_block_gate_t verdict)
{
    uint32_t packed;

    /* A call the cooldown held played nothing on this machine, so it is no event: a client that
     * played it would hear a clang the host never made. */
    if (verdict != MP_ENEMY_BLOCK_PASSED || actor == 0u || !mp_enemy_sync_describing()) {
        return;
    }
    ++block.let_through;
    packed = mp_enemy_block_pack(kind);
    if (packed == 0u) {
        ++block.too_large;
        return;
    }
    if (mp_world_event_post_at_actor(MP_WORLD_EVENT_NPC_CLANG, (uintptr_t)actor, (uint16_t)packed,
                                     NULL, 0u)) {
        ++block.posted;
    }
}

/* Printed on both sides and never replaced by a shorter sentence: the module has no hull of its
 * own, so a line that went missing would say nothing about why. What arrived, what was seen again
 * and what found no replica is the world events' line now. */
void mp_enemy_block_report(void)
{
    log_info("  a blade clang on an NPC: %u let through here, %u posted to the world events, %u "
             "for a kind the event cannot carry | on a replica: %u played, %u with no site, %u "
             "unreadable",
             (unsigned)block.let_through, (unsigned)block.posted, (unsigned)block.too_large,
             (unsigned)block.played, (unsigned)block.unresolved, (unsigned)block.unreadable);
}
