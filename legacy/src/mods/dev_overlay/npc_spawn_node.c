/* npc_spawn_node.c: see npc_spawn_node.h. */
#include "npc_spawn_node.h"

#include "npc_spawn_block.h"
#include "npc_spawn_link.h"
#include "npc_spawn_list.h"
#include "npc_spawn_rules.h"
#include "npc_spawn_save.h"
#include "npc_spawn_sites.h"
#include "npc_spawner.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/session_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The node's name. The loader finds a block's node by the first eight characters of its name, so
 * these differ there from the engine's 27 and from the multiplayer's three. */
#define NODE_NAME       "OPNpcCopies"
#define NODE_SUBVERSION 1

#define NODE_ID         0x08u   /* a node's id is its own address */
#define NODE_PROC       0x10u

#define MSG_INSTALL     0x01
#define MSG_LEVEL_END   0x06
#define MSG_SAVE        0x0A
#define MSG_RESTORE     0x0B
#define MSG_RESTORED    0x18
#define MSG_WORLD_BUILT 0x19
/* The end of every substep: the substep loop broadcasts it once a step, at 0x004757D3, right
 * before it counts the step at 0x004757DB (mp_bridge.h). It reaches every node, since only
 * messages 2, 3, 4, 8 and 9 skip a node that has not started (mp_module.c). */
#define MSG_SUBSTEP_END 0x0E
#define ANSWER_DONE     0   /* 0 to a restore: the loader goes on from where the node stopped */
#define ANSWER_NOT_MINE 2

#define SEEK_FROM_HERE  1

#define ACTOR_HEALTH    0x38u
#define ACTOR_YAW       0xBCu   /* the yaw it faces, degrees, as the record's start yaw */
#define ACTOR_POSITION  0xD0u

typedef int32_t (__cdecl *sys_startup_fn)(void);

static struct {
    detour_t          startup;
    bool              hulled;
    bool              tried;     /* the node was asked for, whatever the answer */
    uintptr_t         id;
    uint32_t          substeps;  /* MSG_SUBSTEP_END seen, a count that only ever goes up */
    npc_spawn_hold_t  hold;
    npc_spawn_saved_t held[NPC_SPAWN_COPIES_MAX];
} node;

uint32_t npc_spawn_node_epoch(void)
{
    return node.hold.epoch;
}

uint32_t npc_spawn_node_substeps(void)
{
    return node.substeps;
}

bool npc_spawn_node_standing(void)
{
    return node.id != 0;
}

bool npc_spawn_node_loading(void)
{
    uint32_t file = 0;

    return npc_spawn_sites()->load_file_cell != 0 &&
           memory_try_read_u32(npc_spawn_sites()->load_file_cell, &file) && file != 0u;
}

/* ==============================================================================================
 * Saving.
 * ============================================================================================ */

typedef struct collected {
    npc_spawn_saved_t copies[NPC_SPAWN_COPIES_MAX];
    uint32_t          count;
    uint32_t          dying;       /* health gone: left out, since a load would raise them whole */
    uint32_t          unreadable;
} collected_t;

static void collect(uintptr_t actor, void *user)
{
    collected_t      *c = (collected_t *)user;
    npc_spawn_saved_t copy;

    memset(&copy, 0, sizeof copy);
    if (c->count >= NPC_SPAWN_COPIES_MAX || !npc_spawner_description(actor, &copy.desc)) {
        return;
    }
    if (!memory_try_read(actor + ACTOR_HEALTH, &copy.health, sizeof copy.health) ||
        !memory_try_read(actor + ACTOR_POSITION, copy.position, sizeof copy.position) ||
        !memory_try_read(actor + ACTOR_YAW, &copy.yaw, sizeof copy.yaw) ||
        !npc_spawn_block_copy_is_valid(&copy)) {
        ++c->unreadable;
        return;
    }
    if (copy.health <= 0) {
        ++c->dying;
        return;
    }
    c->copies[c->count++] = copy;
}

/* The block is laid out on this call's stack, since a message box inside the save can run the
 * whole save again, nested, and this with it. */
static void write_block(uintptr_t id)
{
    collected_t found;
    uint8_t     bytes[NPC_SPAWN_BLOCK_MAX_BYTES];
    size_t      size = 0;
    uintptr_t   list = npc_spawn_save_pool_list();

    memset(&found, 0, sizeof found);
    if (id != node.id || list == 0) {
        return;
    }
    (void)npc_spawn_list_each(list, NPC_SPAWN_POOL_CAPACITY, &collect, &found);
    if (found.count == 0u && found.dying == 0u && found.unreadable == 0u) {
        return;   /* nothing of the panel's in this world, and no block */
    }
    if (!npc_spawn_block_encode(found.copies, found.count, bytes, sizeof bytes, &size) ||
        npc_spawn_sites()->write_chunk(id, bytes, (int32_t)size, NODE_SUBVERSION) != 0) {
        log_warning("npc spawner: the panel's block could not be written, so this save holds none "
                    "of the %u spawned copies", found.count);
        return;
    }
    log_info("npc spawner: the panel's block: %u written (%u dying left out, %u unreadable), %u "
             "bytes", found.count, found.dying, found.unreadable, (unsigned)size);
}

/* ==============================================================================================
 * Loading.
 * ============================================================================================ */

/* Reads exactly the block's length and answers the loader 0 whatever it found: any other answer
 * opens a box whose Escape abandons the whole load. The length is the one in the block's tag,
 * which the loader holds while it hands the block over. */
static void read_block(uint32_t subversion)
{
    const npc_spawn_sites_t *sites  = npc_spawn_sites();
    int32_t                  length = -1;
    uint8_t                  bytes[NPC_SPAWN_BLOCK_MAX_BYTES];
    npc_spawn_block_read_t   read;

    /* The loader holds the block's tag in a static cell while it hands the block over, so this
     * cannot fail while it does; were it to, nothing could say how far to step. */
    if (!memory_try_read(sites->tag_length_cell, &length, sizeof length) || length < 0) {
        log_warning("npc spawner: the panel's block has no length to read by");
        return;
    }
    if (npc_spawn_block_plan(subversion, length, sizeof bytes) == NPC_SPAWN_BLOCK_STEP_OVER) {
        (void)sites->save_seek(length, SEEK_FROM_HERE);
        log_warning("npc spawner: the panel's block is version %u and %d bytes, which this build "
                    "does not read; it was stepped over", subversion, (int)length);
        return;
    }
    if (sites->save_read(bytes, length) != 1) {
        log_warning("npc spawner: the panel's block could not be read to its end");
        return;
    }
    if (!npc_spawn_block_decode(bytes, (size_t)length, node.held, NPC_SPAWN_COPIES_MAX, &read)) {
        log_warning("npc spawner: the panel's block is torn, so none of its copies is raised");
        return;
    }
    npc_spawn_hold_read(&node.hold, read.copies);
    log_info("npc spawner: the panel's block: %u read (%u of a kind this build does not know, %u "
             "no build could raise, %u past the ring), held until the load has finished",
             read.copies, read.unknown, read.invalid, read.over);
}

static void next_epoch(void)
{
    uint32_t dropped = npc_spawn_hold_new_world(&node.hold);

    if (dropped != 0u) {
        log_info("npc spawner: the panel's block: %u read and dropped, because a new world came "
                 "before the load finished", dropped);
    }
}

static int32_t __cdecl node_proc(int32_t message, int32_t argument, int32_t unused)
{
    (void)unused;
    switch (message) {
    case MSG_INSTALL:
        return ANSWER_DONE;
    case MSG_SAVE:
        write_block((uintptr_t)(uint32_t)argument);
        return ANSWER_DONE;
    case MSG_RESTORE:
        read_block((uint32_t)argument);
        return ANSWER_DONE;
    case MSG_LEVEL_END:
    case MSG_WORLD_BUILT:
        next_epoch();
        return ANSWER_NOT_MINE;
    case MSG_RESTORED:
        npc_spawn_hold_load_over(&node.hold);
        return ANSWER_NOT_MINE;
    case MSG_SUBSTEP_END:
        ++node.substeps;
        return ANSWER_NOT_MINE;
    default:
        return ANSWER_NOT_MINE;
    }
}

/* ==============================================================================================
 * The node itself.
 * ============================================================================================ */

static void build_node(const char *when)
{
    const npc_spawn_sites_t *sites = npc_spawn_sites();
    uintptr_t                id;
    uint32_t                 self = 0;
    uint32_t                 proc = 0;
    uint32_t                 tail = 0;

    node.tried = true;
    id = sites->module_install((void *)&node_proc, NODE_NAME);
    if (id == 0 || !memory_try_read_u32(id + NODE_ID, &self) || self != (uint32_t)id ||
        !memory_try_read_u32(id + NODE_PROC, &proc) || proc != (uint32_t)(uintptr_t)&node_proc ||
        !memory_try_read_u32(sites->module_tail_cell, &tail) || tail != (uint32_t)id) {
        log_warning("npc spawner: the module node %s was not built %s (id %08X, its own id %08X, "
                    "the list's tail %08X), so spawned copies are left out of a savegame",
                    NODE_NAME, when, (unsigned)id, (unsigned)self, (unsigned)tail);
        return;
    }
    node.id = id;
    log_info("npc spawner: the module node %s is in the engine's list %s, at %08X", NODE_NAME,
             when, (unsigned)id);
}

/* The prototype keeps the original's answer: the multiplayer's hull around this one reads it, and
 * a nonzero answer is what says the engine was built. */
static int32_t __cdecl hook_sys_startup(void)
{
    int32_t result = ((sys_startup_fn)node.startup.original)();

    if (result != 0 && !node.tried) {
        build_node("at startup");
    }
    return result;
}

bool npc_spawn_node_install(void)
{
    npc_spawn_sites_resolve();
    if (!npc_spawn_sites_block()) {
        return false;
    }
    node.hulled = detour_install(&node.startup, npc_spawn_sites()->sys_startup,
                                 (const void *)&hook_sys_startup, NPC_SPAWN_SYS_STARTUP_PROLOGUE);
    if (!node.hulled) {
        log_warning("npc spawner: sys_startup at %08X could not be hulled, so the module node is "
                    "built at the first frame instead", (unsigned)npc_spawn_sites()->sys_startup);
    }
    return true;
}

/* The copies of a finished load, raised once the load is over and a player stands in the world.
 * In a running multiplayer session the copies of a load belong to the host: its panel asks the
 * multiplayer for each again, and a client's panel raises none. The host's wishes are stamped with
 * the epoch of the last grant record read, so they wait for a frame whose reading succeeded: one
 * stamped with the world before the load is thrown away unanswered, every copy of the save with
 * it. */
static void raise_held(void)
{
    session_note_t note;
    uint32_t       count   = node.hold.held;
    uint32_t       raised  = 0;
    uint32_t       refused = 0;
    uint32_t       i;

    if (!npc_spawn_hold_due(&node.hold)) {
        return;
    }
    if (npc_spawn_node_loading() || npc_spawn_save_window_open() || !npc_spawner_is_available()) {
        return;   /* not yet */
    }
    if (npc_spawn_link_active() && !npc_spawn_link_is_client() && !npc_spawn_link_fresh()) {
        return;   /* not this frame */
    }
    npc_spawn_hold_clear(&node.hold);
    if (npc_spawn_link_active() || (session_note_read(&note) && note.running)) {
        log_info("npc spawner: the panel's block: %u read, %u asked of the session's multiplayer "
                 "and none raised here, since a session is running", count,
                 npc_spawn_link_restore(node.held, count));
        return;
    }
    for (i = 0; i < count; ++i) {
        if (npc_spawner_raise_saved(&node.held[i])) {
            ++raised;
        } else {
            ++refused;
        }
    }
    log_info("npc spawner: the panel's block: %u read, %u raised, %u refused", count, raised,
             refused);
}

void npc_spawn_node_tick(void)
{
    if (!node.tried && npc_spawn_sites_block()) {
        build_node("at the first frame, since sys_startup ran without the hull");
    }
    raise_held();
}
