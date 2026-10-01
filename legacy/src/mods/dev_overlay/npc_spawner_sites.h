/* npc_spawner_sites.h: the two engine routines the spawner raises and removes a copy with, found.
 *
 * Taken out of npc_spawner.c at a real seam when that file came within eight lines of the hard
 * limit: finding a routine and proving it, from two places that have to agree, shares nothing
 * with raising, keeping and removing a copy, and the bytes that prove it are longer than the code
 * that uses it. The same cut as character_prop_sites.c and npc_spawn_sites.c.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWNER_SITES_H
#define DEV_OVERLAY_NPC_SPAWNER_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* spawn_actor: cdecl, the record, its index and a script index or -1 for the record's own; the new
 * actor or NULL. enemy_delete: cdecl, the actor and the reason. */
typedef void *(__cdecl *npc_spawn_fn_t)(void *record, int32_t index, int32_t script_override);
typedef void (__cdecl *npc_delete_fn_t)(void *actor, int32_t reason);

typedef struct npc_spawner_sites {
    npc_spawn_fn_t    spawn;
    uintptr_t         caller;          /* the activation scan's call, the second finding */
    void *volatile   *level_pointer;   /* [g_level], the cell both world operands name */
    npc_delete_fn_t   delete_actor;    /* NULL when the way back did not resolve on its own */
} npc_spawner_sites_t;

/* The spawn routine from its opening and from the activation scan's call, which have to agree, and
 * the world pointer from the two operands in the opening, which have to agree; then the delete,
 * which is not a condition. False, with a log line, when the spawn routine or the world pointer
 * did not resolve; `out` is then not to be read. */
bool npc_spawner_sites_resolve(npc_spawner_sites_t *out);

#endif /* DEV_OVERLAY_NPC_SPAWNER_SITES_H */
