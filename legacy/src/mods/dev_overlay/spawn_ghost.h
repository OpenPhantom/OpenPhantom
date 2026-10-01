/* spawn_ghost.h: the entity the placement mode would place, drawn where it would stand.
 *
 * Nothing is spawned for it. The engine draws a model through a render handle, 0x160 bytes it
 * initialises, binds a model to and draws with a matrix, and the borrowed weapon of the model swap
 * has drawn a second one of those for weeks. This is a third: a handle of this module's own, bound
 * to the chosen kind's model, drawn once a frame in its rest pose at the place and the facing the
 * mode has found. Being no actor, it collides with nothing, is hit by nothing, and exists on this
 * machine only, which in a session is what a preview should be.
 *
 * The engine has no per-object transparency, so the ghost is solid; whether it can be placed is
 * said by the marks around it (spawn_marks.c), not by the model.
 *
 * The one way this can crash the game: a handle drawn after its model is gone. A handle keeps a
 * pointer to the model it was bound to; the level's models go with the level and the archive's with
 * the loader at the next world. So the handle is let go whenever the world it was bound in is not
 * the world now, before anything else is done with it, and the panel's spawner lets go of it first
 * of all when it notices a new world, before the loader gives a single model back. Letting go only
 * releases the handle's own arrays (rdThing_freeArrays reads nothing of the model), and a handle
 * let go twice is let go once.
 *
 * Pure: the four engine calls come in through spawn_ghost_ops_t, so the lifecycle is tested
 * without the game. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_GHOST_H
#define DEV_OVERLAY_SPAWN_GHOST_H

#include <stdbool.h>
#include <stdint.h>

/* The engine allocates 0x160 for a handle; the block is larger, as character_prop_sites.c checks
 * against the build's own number before any handle is made. */
#define SPAWN_GHOST_HANDLE_BYTES 0x200u

/* The four engine calls, the same prototypes the borrowed weapon draws with. */
typedef struct spawn_ghost_ops {
    int32_t (__cdecl *init)(void *thing, void *owner);
    int32_t (__cdecl *set_model)(void *thing, void *model3);
    void    (__cdecl *free_arrays)(void *thing);
    void   *(__cdecl *draw)(void *thing, const float *root);
} spawn_ghost_ops_t;

typedef struct spawn_ghost_counters {
    uint32_t binds;
    uint32_t releases;
    uint32_t draws;      /* the engine drew it */
    uint32_t culled;     /* asked, and the engine's own visibility test said no */
    uint32_t refused;    /* a model the handle would not take */
} spawn_ghost_counters_t;

typedef struct spawn_ghost {
    uint32_t               handle[SPAWN_GHOST_HANDLE_BYTES / 4u];
    bool                   bound;
    uintptr_t              bound_model;
    uint32_t               bound_world;
    bool                   wanted;
    uintptr_t              want_model;
    uint32_t               want_world;
    float                  want_root[12];
    spawn_ghost_counters_t counters;
} spawn_ghost_t;

/* The panel's ghost, and the engine calls it draws with once the draw path is armed. */
spawn_ghost_t *spawn_ghost_state(void);
void           spawn_ghost_set_ops(const spawn_ghost_ops_t *ops);

/* What the next draw is to show: the model (an rdModel3), the world it belongs to and the root
 * matrix. Set every frame the mode shows one; spawn_ghost_want_none for a frame that shows none. */
void spawn_ghost_want(spawn_ghost_t *ghost, uintptr_t model3, uint32_t world, const float root[12]);
void spawn_ghost_want_none(spawn_ghost_t *ghost);

/* From the draw pass, once a frame. In this order: a handle bound in another world than `world_now`
 * or to another model than the one wanted is let go; the wanted model is bound when it is of this
 * world; it is drawn. Answers whether the engine drew it. */
bool spawn_ghost_draw(spawn_ghost_t *ghost, const spawn_ghost_ops_t *ops, uint32_t world_now);

/* Lets the handle go. Nothing to do for one that is not bound, so twice is once. */
void spawn_ghost_release(spawn_ghost_t *ghost, const spawn_ghost_ops_t *ops);

/* The panel's ghost let go with the calls it was bound with: for the spawner, which calls it first
 * when the world changes, and for the mode, when it goes off. */
void spawn_ghost_let_go(void);

/* The root matrix of a body standing at `position` and turned `yaw` degrees, at `scale`: the
 * engine's own euler to matrix with pitch and roll zero, the scale in the rows as the engine puts
 * an object's scale into the matrix it draws with. */
void spawn_ghost_matrix(float out[12], const float position[3], float yaw, float scale);

#endif /* DEV_OVERLAY_SPAWN_GHOST_H */
