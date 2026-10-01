/* npc_spawn_save.c: see npc_spawn_save.h. */
#include "npc_spawn_save.h"

#include "npc_spawn_desc.h"
#include "npc_spawn_list.h"
#include "npc_spawn_rules.h"
#include "npc_spawn_sites.h"
#include "npc_spawner.h"
#include "player_slot.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

_Static_assert(NPC_SPAWN_POOL_CAPACITY == NPC_SPAWN_COPIES_MAX, "a copy takes a pool slot");

#define POOL_ELEMENT       0x204u
#define LIST_ELEMENT_SIZE  0x0Cu
#define LIST_CAPACITY      0x10u

#define ACTOR_KEY          0x18u   /* the engine index spawn_actor writes */
#define PLAYER_MODE        0x60u   /* the player's mode descriptor */
#define PLAYER_GUN_KEY     0x38Cu  /* the index of the gun it last mounted, never cleared */

/* The "Save successful" line of the save screen: a text widget whose start is text 0x83. */
#define SAVED_WIDGET       0x22
#define WIDGET_TYPE        0x00u
#define WIDGET_VISIBLE     0x0Cu
#define WIDGET_START       0x10u
#define WIDGET_TYPE_TEXT   1u
#define SAVED_WIDGET_START 0x83u

/* One button, Ok, and Escape does nothing. The lines are drawn as written, and kept short, since
 * the box is 260 pixels wide and a line's wrapping was not read. */
#define MESSAGE_BOX_OK     0xF00
#define REFUSAL_LINE_1     "Not saved"
#define RIDING_LINE_2      "Leave the spawned tripod first"
#define UNWALKABLE_LINE_2  "The spawned NPCs cannot be kept out"

typedef int32_t (__cdecl *enemy_save_block_fn)(int32_t module_id);
typedef int32_t (__cdecl *save_game_fn)(const char *path);

static struct {
    detour_t block;
    detour_t game;
    bool     keeps_out;
    bool     refuses;
    uint32_t window;        /* save_saveGame calls running, nested ones counted */
    uint32_t block_depth;   /* enemy_saveBlock calls running */
    bool     refusing;      /* the refusal's box is open */
    uint32_t refused;
    uint32_t pool_logged;
} save;

bool npc_spawn_save_keeps_copies_out(void)
{
    return save.keeps_out;
}

bool npc_spawn_save_can_refuse(void)
{
    return save.refuses;
}

bool npc_spawn_save_window_open(void)
{
    return save.window != 0u || save.block_depth != 0u;
}

bool npc_spawn_save_rides(uint32_t key)
{
    uintptr_t player = (uintptr_t)player_slot_current();
    uint32_t  mode   = 0;
    uint32_t  gun    = 0;

    if (!save.refuses || player == 0 || !memory_try_read_u32(player + PLAYER_MODE, &mode) ||
        !memory_try_read_u32(player + PLAYER_GUN_KEY, &gun)) {
        return false;
    }
    return npc_spawn_rides(mode, (uint32_t)npc_spawn_sites()->tripod_mode, gun, key);
}

/* ==============================================================================================
 * The enemy block.
 * ============================================================================================ */

typedef struct pick_count {
    uint32_t outside;   /* actors under a copy's key that are not in the ring: must stay 0 */
} pick_count_t;

/* Every copy of the panel's, and every other actor whose index is past the level's placements
 * too: the enemy block would write that index, and a load would read the directory with it. One
 * that is not the panel's is counted, since nothing raises one, and not saved anywhere. */
static bool pick_copy(uintptr_t actor, void *user)
{
    pick_count_t *count = (pick_count_t *)user;
    uint32_t      key   = 0;

    if (npc_spawner_owns(actor)) {
        return true;
    }
    if (memory_try_read_u32(actor + ACTOR_KEY, &key) && key >= NPC_SPAWN_KEY_FIRST) {
        ++count->outside;
        return true;
    }
    return false;
}

static void count_one(uintptr_t actor, void *user)
{
    (void)actor;
    ++*(uint32_t *)user;
}

/* Whether the enemy block's hull will be able to take the copies out: the pool has its shape and
 * its chain walks to its end. */
static bool pool_walkable(void)
{
    uintptr_t list  = npc_spawn_save_pool_list();
    uint32_t  count = 0;

    return list != 0 && npc_spawn_list_each(list, NPC_SPAWN_POOL_CAPACITY, &count_one, &count);
}

uintptr_t npc_spawn_save_pool_list(void)
{
    uint32_t list     = 0;
    uint32_t element  = 0;
    uint32_t capacity = 0;

    if (!memory_try_read_u32(npc_spawn_sites()->pool_cell, &list) || list == 0 ||
        !memory_try_read_u32(list + LIST_ELEMENT_SIZE, &element) ||
        !memory_try_read_u32(list + LIST_CAPACITY, &capacity) || element != POOL_ELEMENT ||
        capacity != NPC_SPAWN_POOL_CAPACITY) {
        if (save.pool_logged++ == 0u) {
            log_error("npc spawner: the enemy pool at %08X is not the 0x80 by 0x204 list it was "
                      "(element %X, capacity %X), so the copies cannot be kept out of a save",
                      (unsigned)list, (unsigned)element, (unsigned)capacity);
        }
        return 0;
    }
    return (uintptr_t)list;
}

/* The copies out of the pool's chain for the length of the original, and back. The state lives on
 * this call's stack: a nested call, from a message box the original opened, finds the copies
 * already out and takes and puts back nothing. */
static int32_t __cdecl hook_enemy_save_block(int32_t module_id)
{
    npc_spawn_unlinked_t unlinked[NPC_SPAWN_POOL_CAPACITY];
    pick_count_t         count    = { 0 };
    uintptr_t            list     = 0;
    uint32_t             taken    = 0;
    uint32_t             back     = 0;
    uint32_t             repaired = 0;
    bool                 out      = false;
    int32_t              answer;

    if (++save.block_depth == 1u) {
        list = npc_spawn_save_pool_list();
        if (list != 0) {
            out = npc_spawn_list_detach(list, NPC_SPAWN_POOL_CAPACITY, &pick_copy, &count,
                                        unlinked, NPC_SPAWN_POOL_CAPACITY, &taken);
            if (!out) {
                log_error("npc spawner: the enemy pool's chain could not be walked, so the "
                          "copies stay in this enemy block and a load of this save may fail");
            }
        }
    }
    answer = ((enemy_save_block_fn)save.block.original)(module_id);
    if (out) {
        back = npc_spawn_list_relink(list, unlinked, taken, &repaired);
    }
    --save.block_depth;
    if (taken != 0u || count.outside != 0u || repaired != 0u) {
        log_info("npc spawner: the copies were left out of the enemy block: %u detached and %u put "
                 "back, %u outside the ring (must be 0)%s", taken, back, count.outside,
                 repaired != 0u ? ", some put back at the head of the chain" : "");
    }
    return answer;
}

/* ==============================================================================================
 * The refusal.
 * ============================================================================================ */

/* The save screen set its "saved" line before the commit, and it is drawn after it in the same
 * frame; it is hidden here so a refused save does not read as a saved one. */
static void hide_saved_line(void)
{
    const npc_spawn_sites_t *sites = npc_spawn_sites();
    uint32_t                 menu  = 0;
    uintptr_t                widget;
    uint32_t                 type  = 0;
    uint32_t                 start = 0;
    uint32_t                 hidden = 0;

    if (sites->find_widget == NULL || !memory_try_read_u32(sites->current_menu_cell, &menu) ||
        menu != (uint32_t)sites->save_screen) {
        return;
    }
    widget = sites->find_widget(SAVED_WIDGET);
    if (widget != 0 && memory_try_read_u32(widget + WIDGET_TYPE, &type) &&
        type == WIDGET_TYPE_TEXT && memory_try_read_u32(widget + WIDGET_START, &start) &&
        start == SAVED_WIDGET_START) {
        (void)memory_try_write(widget + WIDGET_VISIBLE, &hidden, sizeof hidden);
    }
}

/* The refusal, with the engine's own box: the answer save_saveGame gives for a save not made,
 * before the slot's file was opened. */
static int32_t refuse(const char *line2, const char *why)
{
    save.refusing = true;
    ++save.window;
    hide_saved_line();
    (void)npc_spawn_sites()->message_box(0, 0, REFUSAL_LINE_1, line2, MESSAGE_BOX_OK);
    --save.window;
    save.refusing = false;
    ++save.refused;
    log_info("npc spawner: a save was refused: %s (%u so far)", why, save.refused);
    return 1;
}

static int32_t __cdecl hook_save_game(const char *path)
{
    int32_t answer;

    /* A nested commit from the refusal's own box: refused again, with no second box. */
    if (save.refusing) {
        return 1;
    }
    if (npc_spawn_save_rides(0u)) {
        return refuse(RIDING_LINE_2, "the player sits on a spawned tripod");
    }
    /* A save that would carry a copy's index in its enemy block could not be loaded again. */
    if (save.keeps_out && npc_spawner_names_any() && !pool_walkable()) {
        return refuse(UNWALKABLE_LINE_2, "the enemy pool cannot be walked to keep the copies out");
    }
    ++save.window;
    answer = ((save_game_fn)save.game.original)(path);
    --save.window;
    return answer;
}

bool npc_spawn_save_install(void)
{
    const npc_spawn_sites_t *sites = npc_spawn_sites();

    npc_spawn_sites_resolve();
    if (npc_spawn_sites_enemy_block()) {
        save.keeps_out = detour_install(&save.block, sites->enemy_save_block,
                                        (const void *)&hook_enemy_save_block,
                                        NPC_SPAWN_ENEMY_SAVE_BLOCK_PROLOGUE);
        if (!save.keeps_out) {
            log_warning("npc spawner: enemy_saveBlock at %08X could not be hulled, so no copy is "
                        "raised", (unsigned)sites->enemy_save_block);
        }
    }
    if (npc_spawn_sites_refusal()) {
        save.refuses = detour_install(&save.game, sites->save_game,
                                      (const void *)&hook_save_game,
                                      NPC_SPAWN_SAVE_GAME_PROLOGUE);
        if (!save.refuses) {
            log_warning("npc spawner: save_saveGame at %08X could not be hulled, so no tripod gun "
                        "is raised", (unsigned)sites->save_game);
        }
    }
    if (save.keeps_out && save.refuses) {
        log_info("npc spawner: a savegame keeps the copies out of its enemy block, and saving is "
                 "refused while the player sits on a spawned tripod");
    }
    return save.keeps_out;
}
