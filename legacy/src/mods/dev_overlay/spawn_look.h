/* spawn_look.h: what is under the pointer in the placement mode, looked at once a frame.
 *
 * Taken out of spawn_mode.c at the seam between looking and acting: this reads the camera and the
 * pointer, casts the ray, asks the world where the entity would stand and whether it may, finds
 * the copy under the pointer, turns the entity to face the player, says where the ghost is to be
 * drawn and fills the marks; spawn_mode.c decides from what was found whether a click places and a
 * right click removes. Every probe here walks the engine's one polygon list, so this is only ever
 * called from the frame, the engine's own moment.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_LOOK_H
#define DEV_OVERLAY_SPAWN_LOOK_H

#include "entity_names.h"
#include "npc_spawner.h"
#include "spawn_marks.h"
#include "spawn_place.h"

#include <stdbool.h>
#include <stdint.h>

/* The copy under the pointer, when there is one. */
typedef struct spawn_hover {
    uintptr_t actor;
    uint32_t  key;
    bool      mine;     /* this player's, or no session */
    bool      ridden;   /* the player sits on it */
    char      name[ENTITY_NAME_MAX + 1u];
} spawn_hover_t;

typedef struct spawn_look {
    bool                 camera;         /* the camera read, and the ray was cast */
    spawn_spot_verdict_t verdict;
    float                spot[3];        /* where the entity would stand, at its feet */
    float                strike;         /* how far along the ray it struck */
    float                moved;          /* how far the place was pushed off a wall */
    float                facing;
    float                ghost_height;
    spawn_hover_t        hover;
    spawn_marks_scene_t  scene;
    uintptr_t            player_thing;   /* the body the ghost is drawn beside */
} spawn_look_t;

/* Looks, for the chosen `kind` at list index `chosen`, and asks the ghost to be drawn where the
 * entity would stand. */
void spawn_look_at(spawn_look_t *out, const npc_spawner_kind_t *kind, int32_t chosen);

/* The name a person reads for a kind, or its stem. */
void spawn_look_label(const npc_spawner_kind_t *kind, char *out, uint32_t out_size);

#endif /* DEV_OVERLAY_SPAWN_LOOK_H */
