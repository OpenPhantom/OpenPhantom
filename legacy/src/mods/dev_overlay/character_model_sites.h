/* character_model_sites.h: what a model swap has to FIND in the running executable.
 *
 * Apart from character_model.c for the reason player_sites.c is apart from the feature it serves:
 * five patterns with the listings that justify them were a third of that file, and the file was at
 * the hard limit with them in it.
 *
 * The caller may not detour any of the five, and that is part of this file's contract. Every one of
 * them is resolved as a CALL TARGET, anchored on its own prologue and used as a function.
 * signature_find_unique has no second stage onto the body the way signature_find_detour_target has,
 * so a prologue somebody has already written a jump over is invisible to it, and this resolution
 * would then find nothing. That is not hypothetical in this feature: it is what stopped the
 * borrowed weapon being drawn at all, one module over, the first time it shipped.
 *
 * Three heads are found through that second stage all the same, with their prologues declared:
 * the object scale, whose head the multiplayer declares for its own calls, and the glow card
 * release and the lightning arc release beside the five. A head another module has declared is
 * bytes it may one day write over.
 */
#ifndef CHARACTER_MODEL_SITES_H
#define CHARACTER_MODEL_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* The prototypes carry their return values: rdThing_SetModel answers whether its four allocations
 * succeeded, and a prototype that threw that away would turn an out of memory into a body drawn
 * from arrays that were never allocated. */
typedef int32_t (__cdecl *rd_set_model_fn_t)(void *thing, void *model3);
typedef void    (__cdecl *rd_free_arrays_fn_t)(void *thing);
typedef void    (__cdecl *bap_set_scale_fn_t)(void *obj, float sx, float sy, float sz);
typedef void    (__cdecl *plr_rebind_weapon_fn_t)(void);
typedef void   *(__cdecl *res_alloc_fn_t)(uint32_t tag, const char *name);

typedef struct character_model_sites {
    uintptr_t              player_record;   /* the cell holding the player block pointer */
    uintptr_t              name_table;      /* the table of four asset names             */
    rd_set_model_fn_t      set_model;
    rd_free_arrays_fn_t    free_arrays;
    bap_set_scale_fn_t     set_scale;
    plr_rebind_weapon_fn_t rebind_weapon;
    res_alloc_fn_t         res_alloc;
} character_model_sites_t;

/* Resolves all five and reads the three operands the last of them carries. False means at least one
 * did not resolve, the reason is in the log, and `out` must not be read: a swap carried out against
 * half a resolution is a body left wearing arrays nobody sized rather than a swap that is missing.
 *
 * Nothing is logged on success. The line that says the swap has its entry points belongs to the
 * caller, because it is only true once the caller's own precondition, the clip translation, stands
 * as well. */
bool character_model_sites_resolve(character_model_sites_t *out);

/* halo_freeForThing, which takes every glow card the engine hung on an object off it. Apart from
 * the five because only a far body's dressing calls it, so a build where it does not resolve still
 * offers the player's own swap. It is the one site here found through the two stage rule with its
 * prologue declared: a hull another module places on it later does not hide it. NULL when it did
 * not resolve; resolved once, and the answer is kept. */
typedef void (__cdecl *halo_free_fn_t)(void *obj);

halo_free_fn_t character_model_sites_halo_free(void);

/* fxzappo_detachThing, which lets go of every lightning arc end the engine hung on an object: an
 * end keeps a node index of the rig its object wore, and a rebind gives the object another rig.
 * Called before every rebind, the player's own and a far body's. Not a condition of either: a
 * build where it does not resolve says so once and swaps without it. NULL when it did not
 * resolve; resolved once, and the answer is kept. */
typedef void (__cdecl *arc_release_fn_t)(void *obj);

arc_release_fn_t character_model_sites_arc_release(void);

/* The release on `obj`, when it resolved; nothing otherwise. */
void character_model_sites_let_go_of_arcs(uintptr_t obj);

#endif /* CHARACTER_MODEL_SITES_H */
