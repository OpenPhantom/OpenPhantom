/* mp_enemy_limb.c: a limb an enemy lost, on the machine that only watches. See the header. */
#include "mp_enemy_limb.h"

#include "mp_cells.h"
#include "mp_enemy_limb_rule.h"
#include "mp_enemy_sync.h"
#include "mp_signatures.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

/* The chain from an actor to its hidden nodes. Every offset here is already used elsewhere in this
 * feature: mp_node_map walks the same nodes and mp_enemy_nodes reads the same array. The engine's
 * cut indexes the array by the node's own number. */
#define ACTOR_BODY        0x34u
#define ACTOR_HEALTH      0x38u
#define OBJECT_THING      0x9Cu
#define THING_MODEL3      0x04u
#define THING_NODE_HIDDEN 0x28u
#define MODEL_NUM_NODES   0x54u

/* The engine's task table: 64 records of 0x2C bytes, a record in use while its function word at
 * +0x14 is not 0. */
#define TASK_SLOTS    64u
#define TASK_STRIDE   0x2Cu
#define TASK_FUNCTION 0x14u

/* `int enemy_detachPiece(character *actor, u32 part)`. The part is a VALUE. It is written here
 * because getting it wrong once cost a crash: a hull that took a pointer dereferenced the node
 * number 20 as the address 0x14, the faulting push never pushed, the `add esp, 8` after the call
 * cleaned up eight bytes anyway, and from there the epilogue read one slot too high. `pop ebp`
 * took the return address and `ret` took the actor pointer, so the processor went on executing
 * inside the object heap. */
typedef int32_t(__cdecl *detach_piece_fn_t)(uint32_t actor, uint32_t part);

typedef struct limb_state {
    detour_t          detour;
    detach_piece_fn_t original;
    bool              installed;

    uint32_t taken_off;      /* the engine took a limb off here */
    uint32_t posted;         /* of them, taken by the world events on a host that describes */
    uint32_t too_large;      /* an ordinal the event cannot carry */
    uint32_t thrown;         /* pieces thrown from a replica here */
    uint32_t hidden_only;    /* nodes hidden on a replica without a piece */
    uint32_t want_of_task;   /* of them, the task table was nearly full */
    uint32_t no_object;      /* of them, the engine had no object for the piece */
    uint32_t already;        /* of them, the node was already hidden */
    uint32_t mask_first;     /* the host's mask had hidden the node before the cut came */
    uint32_t health_kept;    /* the cut wrote the replica's health, and it was put back */
    uint32_t out_of_range;   /* the root, or past this model's node count */
    uint32_t unreadable;     /* the body, the thing, the model or the array did not read */
} limb_state_t;

static limb_state_t limb;

/* The drawn thing's node count and hidden array; a pointer that does not read refuses them. */
static bool read_body(uintptr_t actor, uint32_t *count, uint32_t *hidden)
{
    uint32_t body  = 0;
    uint32_t thing = 0;
    uint32_t model = 0;

    return memory_read_u32(actor + ACTOR_BODY, &body) && body != 0u &&
           memory_read_u32((uintptr_t)body + OBJECT_THING, &thing) && thing != 0u &&
           memory_read_u32((uintptr_t)thing + THING_MODEL3, &model) && model != 0u &&
           memory_read_u32((uintptr_t)model + MODEL_NUM_NODES, count) &&
           memory_read_u32((uintptr_t)thing + THING_NODE_HIDDEN, hidden) && *hidden != 0u;
}

/* Whether the node was hidden on this body before the cut: the piece takes that state with it. */
static bool hidden_before(uint32_t actor, uint32_t part)
{
    uint32_t count  = 0;
    uint32_t hidden = 0;
    int32_t  entry  = 0;

    return read_body((uintptr_t)actor, &count, &hidden) && part < count &&
           memory_try_read((uintptr_t)(hidden + part * 4u), &entry, sizeof entry) && entry != 0;
}

/* The hull. The write-back that the site's own notes describe happens one level DOWN: the engine
 * passes the address of its own argument slot to bapobj_detachNode, which writes a body part mask
 * into it. That slot belongs to enemy_detachPiece, not to this hull, and this hull pushes its own
 * copy for the chained call, so what arrives here is what was asked for and stays that way.
 *
 * engine: int enemy_detachPiece(character *actor, u32 part) */
static int32_t __cdecl hook_detach_piece(uint32_t actor, uint32_t part)
{
    if (actor != 0u && part != 0u) {
        ++limb.taken_off;
        if (mp_enemy_sync_describing()) {
            uint32_t packed = mp_enemy_limb_pack(part, hidden_before(actor, part));

            if (packed == 0u) {
                ++limb.too_large;
            } else if (mp_world_event_post_at_actor(MP_WORLD_EVENT_LIMB_FLY, (uintptr_t)actor,
                                                    (uint16_t)packed, NULL, 0u)) {
                ++limb.posted;
            }
        }
    }
    return limb.original != NULL ? limb.original(actor, part) : 0;
}

/* How many of the engine's task slots are free, 0 when the table is not known here. */
static uint32_t free_tasks(void)
{
    uintptr_t table = mp_cells_address(MP_CELL_TASK_ARRAY);
    uint32_t  free_slots = 0;
    uint32_t  slot;

    for (slot = 0; table != 0u && slot < TASK_SLOTS; ++slot) {
        uint32_t function = 1u;

        if (memory_try_read(table + slot * TASK_STRIDE + TASK_FUNCTION, &function,
                            sizeof function) &&
            function == 0u) {
            ++free_slots;
        }
    }
    return free_slots;
}

/* What the cut does to the body when no piece can fly: one store of 1. */
static bool hide_only(uint32_t hidden, uint32_t ordinal)
{
    int32_t entry = 0;
    int32_t one   = 1;

    ++limb.hidden_only;
    if (!memory_try_read((uintptr_t)(hidden + ordinal * 4u), &entry, sizeof entry)) {
        ++limb.unreadable;
        return false;
    }
    if (entry != 0) {
        ++limb.already;
        return true;
    }
    if (!memory_try_write((uintptr_t)(hidden + ordinal * 4u), &one, sizeof one)) {
        ++limb.unreadable;
        return false;
    }
    return true;
}

/* The engine's own cut on the replica, with the node first put back to what the host's was before
 * its cut, and the replica's health kept: a cut that the model marks fatal zeroes the actor's
 * health, and on a replica the host's record is the one writer of that. */
static bool throw_piece(uintptr_t replica, uint32_t hidden, uint32_t ordinal, bool before)
{
    int32_t entry  = 0;
    int32_t wanted = before ? 1 : 0;
    int32_t health = 0;
    int32_t after  = 0;
    bool    kept;
    bool    thrown;

    if (!memory_try_read((uintptr_t)(hidden + ordinal * 4u), &entry, sizeof entry)) {
        ++limb.unreadable;
        return false;
    }
    limb.mask_first += (entry != 0 && !before) ? 1u : 0u;
    if ((entry != 0) != before &&
        !memory_try_write((uintptr_t)(hidden + ordinal * 4u), &wanted, sizeof wanted)) {
        ++limb.unreadable;
        return false;
    }
    kept   = memory_try_read(replica + ACTOR_HEALTH, &health, sizeof health);
    thrown = limb.original((uint32_t)replica, ordinal) != 0;
    if (kept && memory_try_read(replica + ACTOR_HEALTH, &after, sizeof after) && after != health &&
        memory_try_write(replica + ACTOR_HEALTH, &health, sizeof health)) {
        ++limb.health_kept;
    }
    if (!thrown) {
        ++limb.no_object;
        return hide_only(hidden, ordinal);
    }
    ++limb.thrown;
    return true;
}

/* On a client, the limb the host took off, cut from the replica. */
static bool play_limb(const mp_world_event_t *event, uintptr_t replica)
{
    uint32_t ordinal = 0;
    uint32_t count   = 0;
    uint32_t hidden  = 0;
    bool     before  = false;

    if (replica == 0u || limb.original == NULL ||
        !mp_enemy_limb_unpack(event->a, &ordinal, &before) ||
        !read_body(replica, &count, &hidden)) {
        ++limb.unreadable;
        return false;
    }
    switch (mp_enemy_limb_verdict(ordinal, count, free_tasks())) {
    case MP_ENEMY_LIMB_OUT_OF_RANGE:
        ++limb.out_of_range;
        return false;
    case MP_ENEMY_LIMB_HIDE_ONLY:
        ++limb.want_of_task;
        return hide_only(hidden, ordinal);
    case MP_ENEMY_LIMB_THROW:
    default:
        return throw_piece(replica, hidden, ordinal, before);
    }
}

bool mp_enemy_limb_install(void)
{
    uintptr_t site = mp_signatures_address(MP_SITE_ENEMY_DETACH_PIECE);

    if (limb.installed) {
        return true;
    }
    if (site == 0) {
        log_warning("a limb an enemy loses stays on the machine that took it off: the site did "
                    "not resolve");
        return false;
    }
    if (!detour_install(&limb.detour, site, (const void *)&hook_detach_piece,
                        mp_signatures_prologue(MP_SITE_ENEMY_DETACH_PIECE))) {
        log_error("the limb site at %08X refused the detour", (unsigned)site);
        return false;
    }
    limb.original  = (detach_piece_fn_t)limb.detour.original;
    limb.installed = true;
    (void)mp_world_event_set_player(MP_WORLD_EVENT_LIMB_FLY, &play_limb);
    return true;
}

/* Printed whenever the hull stands, on both sides, so that the nodes only hidden and the ones past
 * a model's count are seen even when nothing was thrown. What arrived, what was seen again and
 * what found no replica is the world events' line. */
void mp_enemy_limb_report(void)
{
    if (!limb.installed) {
        log_info("  a limb an enemy lost: the hull never stood on this side");
        return;
    }
    log_info("  a limb an enemy lost: %u taken off here, %u posted to the world events, %u with an "
             "ordinal the event cannot carry | on a replica: %u thrown as a piece, %u hidden only "
             "(%u for want of a task slot, %u with no object for the piece), %u already hidden, "
             "%u whose node the mask had hidden first, %u whose health the cut would have "
             "written, %u past this model's node count, %u unreadable",
             (unsigned)limb.taken_off, (unsigned)limb.posted, (unsigned)limb.too_large,
             (unsigned)limb.thrown, (unsigned)limb.hidden_only, (unsigned)limb.want_of_task,
             (unsigned)limb.no_object, (unsigned)limb.already, (unsigned)limb.mask_first,
             (unsigned)limb.health_kept, (unsigned)limb.out_of_range,
             (unsigned)limb.unreadable);
}
