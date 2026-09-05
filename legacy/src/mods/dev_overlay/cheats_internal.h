/* cheats_internal.h: what the five OpenPhantom cheat files share, and nothing else.
 *
 * The cheats were one file of over two thousand lines until they were split by responsibility.
 * They still share one state record, because they share one install pass and one panel: the panel
 * reads a row's name and whether it is available and on, and every cheat writes into the same
 * table. That record, the function pointer types it is built from, and the two constants more than
 * one of them needs live here so no cheat file has to include another.
 *
 * Internal to dev_overlay. cheats_openphantom.h is the interface the panel uses; nothing outside
 * this directory should include this file.
 */
#ifndef DEV_OVERLAY_CHEATS_INTERNAL_H
#define DEV_OVERLAY_CHEATS_INTERNAL_H

#include "cheats_openphantom.h"

#include "common/detour.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The player record pointer, read by the jump cheats and by the panel's own rows. */

#define PLAYER_RECORD_PTR_ADDR 0x004B5220u

/* The player's live position, three floats, byte-proven from 0x0044F891's own argument
 * (ECX = pPlayer+0x118). Shared by jump boost's fall handling and by free camera's exit
 * teleport, so the one number cannot drift between them. */
#define PLAYER_POSITION_OFFSET 0x118u

/* The player's DESIRED position, the same three floats one vec3 later, read out of
 * Plr_CommitPose (0x0044C06B), whose first act is:
 *
 *     if (pPlayer+0xA0 != 0) { +0x118 = +0x124; +0x11C = +0x128; +0x120 = +0x12C; }
 *
 * So on any frame the player is moving, desiredPos wins and pos is whatever it says. Anything
 * that puts a player somewhere has to write both, or the movement phase quietly puts them back.
 */
#define PLAYER_DESIRED_POSITION_OFFSET 0x124u

/* Vertical velocity, the one the ground-contact resolver integrates the height by:
 *
 *     if (-40.0 < +0xB4) { +0xB4 -= gravity * dt; }      the -40 is terminal velocity
 *     +0x120 += +0xB4 * dt;                              the height follows from it
 *
 * Zero it and the player falls from rest. Shared with jump boost, which scales it. */
#define PLAYER_VERTICAL_VELOCITY_OFFSET 0xB4u
#define JUMP_BOOST_SCALE_DEFAULT 1.3f
#define JUMP_BOOST_SCALE_MIN     0.5f
#define JUMP_BOOST_SCALE_MAX     5.0f
typedef void (__cdecl *use_ammo_fn_t)(int32_t weapon_id, int32_t amount);
typedef void (__cdecl *damage_fn_t)(int32_t amount);
typedef void (__cdecl *camera_update_fn_t)(void);
typedef int32_t (__cdecl *thing_draw_fn_t)(void *thing, float *matrix);
typedef void (__cdecl *scale_matrix_fn_t)(float *matrix, const float *scale_vec3);
typedef void (__cdecl *mode_enter_fn_t)(void);
typedef void (__cdecl *death_trigger_fn_t)(int32_t cause);
typedef void (__cdecl *camera_type_fn_t)(int32_t type);
typedef void (__cdecl *camera_freeze_fn_t)(const float *position);

typedef struct own_cheat {
    const char *name;
    bool        available;
    bool        on;
} own_cheat_t;

typedef struct cheats_own_state {
    bool                    installed;
    own_cheat_t             cheats[CHEATS_OWN_COUNT];
    detour_t                ammo_detour;
    detour_t                damage_detour;
    detour_t                npc_damage_detour;
    detour_t                thing_draw_detour;
    detour_t                camera_update_detour;
    detour_t                jump_entry_detour;
    detour_t                jedi_jump_entry_detour;
    detour_t                fall_damage_detour;
    detour_t                time_death_detour;
    detour_t                distance_death_detour;
    death_trigger_fn_t      death_trigger;   /* resolved 0x004500B0; NULL until both fall-death
                                              * sites cross-validate against each other */
    detour_t                camera_lock_detour;
    detour_t                camera_freeze_detour;
    camera_type_fn_t        camera_type_select;    /* resolved 0x0041840A */
    camera_freeze_fn_t      camera_freeze_target;   /* resolved 0x0044F891 */
    use_ammo_fn_t           ammo_original;
    damage_fn_t             damage_original;
    thing_draw_fn_t         thing_draw_original;
    camera_update_fn_t      camera_update_original;
    mode_enter_fn_t         jump_entry_original;
    mode_enter_fn_t         jedi_jump_entry_original;
    scale_matrix_fn_t       scale_matrix_compose;      /* the retail matrix-scale composer, called
                                                          * directly rather than detoured; NULL if
                                                          * its own site did not resolve */
    uintptr_t               camera_view_address;       /* address OF the camera object pointer;
                                                          * 0 = unresolved, free camera unavailable */
    float                   jump_boost_scale;           /* see JUMP_BOOST_SCALE_DEFAULT above */
} cheats_own_state_t;


/* The one record every cheat writes into. Defined in cheats_openphantom.c, which also runs the
 * install pass that fills it. */
extern cheats_own_state_t own_state;

/* Resolve one signature and detour it, logging which site was hooked or why it was not. Shared
 * because every cheat installs the same way, and the log line is part of the contract: a fix that
 * did nothing has to say so. */
bool cheats_install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const void *hook, detour_t *detour, size_t prologue_size,
                        const char *what);

/* Each group installs itself; cheats_openphantom_install calls them in order. */
void install_npc_damage(void);
void install_jump_boost(void);
void install_fall_punishment_immunity(void);
bool install_freecam(void);

/* The player damage hook, which unlimited health owns. Jump boost calls it rather than the engine
 * original, so that turning unlimited health on still wins when both cheats are on at once: the
 * landing goes through the same decision every other hit does instead of quietly bypassing it. */
void __cdecl hook_damage(int32_t amount);

#endif /* DEV_OVERLAY_CHEATS_INTERNAL_H */
