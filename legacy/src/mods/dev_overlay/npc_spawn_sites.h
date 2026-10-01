/* npc_spawn_sites.h: the engine sites the spawned copies' savegame needs, found once.
 *
 * Every pattern, its prologue and every witness was measured against the retail bytes, and each
 * pattern hits once. A head this file hulls or calls is DECLARED rather than matched from its
 * first byte, because another module may have detoured it first. Every cell and routine the
 * savegame depends on is read from at least two places that have to agree; the current menu,
 * which only decides whether the save screen's "saved" line is hidden, is read from one.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_SITES_H
#define DEV_OVERLAY_NPC_SPAWN_SITES_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t (__cdecl *npc_spawn_message_box_fn)(int32_t first, int32_t second,
                                                    const char *line1, const char *line2,
                                                    int32_t flags);
typedef uintptr_t (__cdecl *npc_spawn_find_widget_fn)(int32_t id);
typedef uintptr_t (__cdecl *npc_spawn_module_install_fn)(void *proc, const char *name);
typedef int32_t (__cdecl *npc_spawn_write_chunk_fn)(uintptr_t id, const void *buffer,
                                                    int32_t length, int32_t subversion);
typedef int32_t (__cdecl *npc_spawn_save_read_fn)(void *buffer, int32_t length);
typedef int32_t (__cdecl *npc_spawn_save_seek_fn)(int32_t offset, int32_t whence);

/* The prologues the hulls declare. save_saveGame opens with six bytes, but a foreign detour of
 * seven or eight fits its boundaries as well, and a search that drops only six finds nothing
 * behind one of those. */
#define NPC_SPAWN_ENEMY_SAVE_BLOCK_PROLOGUE 9u
#define NPC_SPAWN_SAVE_GAME_PROLOGUE        8u
#define NPC_SPAWN_SYS_STARTUP_PROLOGUE      9u

typedef struct npc_spawn_sites {
    /* The enemy block and the pool it walks. */
    uintptr_t enemy_save_block;         /* enemy_saveBlock, hulled */
    uintptr_t pool_cell;                /* the cell holding the enemy pool's list */

    /* Saving, and the refusal while the player rides a copy. */
    uintptr_t                save_game; /* save_saveGame, hulled */
    npc_spawn_message_box_fn message_box;
    npc_spawn_find_widget_fn find_widget;
    uintptr_t                current_menu_cell;
    uintptr_t                save_screen;   /* the save menu, whose widget 0x22 says "saved" */
    uintptr_t                tripod_mode;   /* the player mode descriptor of a mounted gun */

    /* The panel's own block. */
    uintptr_t                   sys_startup;   /* hulled, to build the node */
    npc_spawn_module_install_fn module_install;
    uintptr_t                   module_tail_cell;
    npc_spawn_write_chunk_fn    write_chunk;
    npc_spawn_save_read_fn      save_read;
    npc_spawn_save_seek_fn      save_seek;
    uintptr_t                   load_file_cell;   /* non-zero only while a savegame is read */
    uintptr_t                   tag_length_cell;  /* the length of the block being read */
} npc_spawn_sites_t;

/* Resolves everything once and logs what did not. Idempotent. Each consumer asks for the fields it
 * needs to be non-zero: a group that did not resolve is off, the rest stands. */
void npc_spawn_sites_resolve(void);

/* The sites as resolved; fields are 0 where nothing was found. Never NULL. */
const npc_spawn_sites_t *npc_spawn_sites(void);

/* The three groups, each whole or not at all. */
bool npc_spawn_sites_enemy_block(void);
bool npc_spawn_sites_refusal(void);
bool npc_spawn_sites_block(void);

#endif /* DEV_OVERLAY_NPC_SPAWN_SITES_H */
