/* character_prop_sites.h: what a borrowed weapon has to FIND in the running executable.
 *
 * Apart from character_prop.c for the reason player_sites.c is apart from the feature it serves:
 * seven patterns with the listings that justify them are longer than the code that uses them, and
 * the file holding the arithmetic is over the review limit without them.
 *
 * The caller's order is part of this file's contract. Every pattern here is cut from UNTOUCHED
 * bytes, and one of them, the render handle dispatch, is also a detour target in
 * character_prop_draw.c. A detour written over that prologue first leaves the pattern invisible to
 * every later resolution over the same bytes, and the weapon is then not drawn at all. So the arm
 * resolves and the hook is installed afterwards, which is the order character_model.c keeps.
 */
#ifndef CHARACTER_PROP_SITES_H
#define CHARACTER_PROP_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* Every prototype carries its return value. The two that answer 0 for failure decide whether a
 * second render handle exists at all, and the dispatcher answers the handle it drew. */
typedef int32_t  (__cdecl *rd_thing_init_fn_t)(void *thing, void *owner);
typedef int32_t  (__cdecl *rd_set_model_fn_t)(void *thing, void *model3);
typedef void     (__cdecl *rd_free_arrays_fn_t)(void *thing);
typedef void     (__cdecl *rd_build_joints_fn_t)(void *thing, const float *root);
typedef void    *(__cdecl *rd_thing_draw_fn_t)(void *thing, const float *root);

/* The engine allocates 0x160 for a render handle. The block the caller owns is larger than any
 * build has asked for, and the pushed immediate is checked against it rather than assumed. */
#define RDTHING_BLOCK_BYTES    0x200u

typedef struct character_prop_sites {
    uintptr_t            frame_counter;   /* the stamp a built pose is compared against      */
    uintptr_t            name_table;      /* 33 node names, indexed by a weapon row's id     */
    uintptr_t            weapon_cfg;      /* twelve rows of 0x18, one per weapon slot        */
    rd_thing_init_fn_t   thing_init;
    rd_set_model_fn_t    set_model;
    rd_free_arrays_fn_t  free_arrays;
    rd_build_joints_fn_t build_joints;
    rd_thing_draw_fn_t   thing_draw;
} character_prop_sites_t;

/* Resolves all eight and logs one line naming every one of them. False means at least one did not
 * resolve, the reason is in the log, and `out` must not be read: a weapon placed against half a
 * resolution is a weapon in the wrong place rather than a weapon that is missing. */
bool character_prop_sites_resolve(character_prop_sites_t *out);

#endif /* CHARACTER_PROP_SITES_H */
