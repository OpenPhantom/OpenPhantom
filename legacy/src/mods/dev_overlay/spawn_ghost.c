/* spawn_ghost.c: see spawn_ghost.h. */
#include "spawn_ghost.h"

#include <math.h>
#include <string.h>

/* The handle's pose stamp, compared against the substep counter before the draw rebuilds the joint
 * matrices; a value the counter cannot hold (it starts at 1000 and counts up) forces the rebuild,
 * so a ghost the mouse moved between two substeps is drawn where it now is. The borrowed weapon
 * writes the same value before each of its draws for the same reason. */
#define POSE_STAMP_WORD    (0x1Cu / 4u)
#define POSE_STAMP_UNBUILT 0x80000000u

#define DEGREES_TO_RADIANS (3.14159265f / 180.0f)

static spawn_ghost_t     ghost_state;
static spawn_ghost_ops_t ghost_ops;
static bool              ghost_ops_set;

spawn_ghost_t *spawn_ghost_state(void)
{
    return &ghost_state;
}

void spawn_ghost_set_ops(const spawn_ghost_ops_t *ops)
{
    if (ops != NULL) {
        ghost_ops     = *ops;
        ghost_ops_set = true;
    }
}

void spawn_ghost_want(spawn_ghost_t *ghost, uintptr_t model3, uint32_t world, const float root[12])
{
    if (ghost == NULL || model3 == 0 || root == NULL) {
        spawn_ghost_want_none(ghost);
        return;
    }
    ghost->wanted     = true;
    ghost->want_model = model3;
    ghost->want_world = world;
    memcpy(ghost->want_root, root, sizeof ghost->want_root);
}

void spawn_ghost_want_none(spawn_ghost_t *ghost)
{
    if (ghost != NULL) {
        ghost->wanted = false;
    }
}

void spawn_ghost_release(spawn_ghost_t *ghost, const spawn_ghost_ops_t *ops)
{
    if (ghost == NULL || !ghost->bound) {
        return;
    }
    /* Flagged unbound first: should the release not return, the handle is never drawn again. */
    ghost->bound       = false;
    ghost->bound_model = 0;
    if (ops != NULL && ops->free_arrays != NULL) {
        ops->free_arrays(ghost->handle);
    }
    ++ghost->counters.releases;
}

static bool bind(spawn_ghost_t *ghost, const spawn_ghost_ops_t *ops)
{
    memset(ghost->handle, 0, sizeof ghost->handle);
    if (ops->init(ghost->handle, NULL) == 0) {
        ++ghost->counters.refused;
        return false;
    }
    if (ops->set_model(ghost->handle, (void *)ghost->want_model) == 0) {
        /* The bind allocates its four arrays one after another and may have taken some before it
         * failed; the release frees whichever it holds. */
        ops->free_arrays(ghost->handle);
        ++ghost->counters.refused;
        return false;
    }
    ghost->bound       = true;
    ghost->bound_model = ghost->want_model;
    ghost->bound_world = ghost->want_world;
    ++ghost->counters.binds;
    return true;
}

bool spawn_ghost_draw(spawn_ghost_t *ghost, const spawn_ghost_ops_t *ops, uint32_t world_now)
{
    if (ghost == NULL || ops == NULL || ops->init == NULL || ops->set_model == NULL ||
        ops->free_arrays == NULL || ops->draw == NULL) {
        return false;
    }
    if (ghost->bound &&
        (ghost->bound_world != world_now || !ghost->wanted ||
         ghost->bound_model != ghost->want_model)) {
        spawn_ghost_release(ghost, ops);
    }
    if (!ghost->wanted || ghost->want_world != world_now) {
        return false;   /* nothing asked, or asked for a world that is not this one */
    }
    if (!ghost->bound && !bind(ghost, ops)) {
        ghost->wanted = false;   /* a model the handle refused is not asked again this frame */
        return false;
    }
    ghost->handle[POSE_STAMP_WORD] = POSE_STAMP_UNBUILT;
    if (ops->draw(ghost->handle, ghost->want_root) == NULL) {
        ++ghost->counters.culled;
        return false;
    }
    ++ghost->counters.draws;
    return true;
}

void spawn_ghost_let_go(void)
{
    spawn_ghost_want_none(&ghost_state);
    spawn_ghost_release(&ghost_state, ghost_ops_set ? &ghost_ops : NULL);
}

void spawn_ghost_matrix(float out[12], const float position[3], float yaw, float scale)
{
    float s = sinf(yaw * DEGREES_TO_RADIANS);
    float c = cosf(yaw * DEGREES_TO_RADIANS);

    out[0]  = c * scale;
    out[1]  = s * scale;
    out[2]  = 0.0f;
    out[3]  = -s * scale;
    out[4]  = c * scale;
    out[5]  = 0.0f;
    out[6]  = 0.0f;
    out[7]  = 0.0f;
    out[8]  = scale;
    out[9]  = position[0];
    out[10] = position[1];
    out[11] = position[2];
}
