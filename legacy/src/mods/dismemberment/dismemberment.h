/* dismemberment.h: lightsaber dismemberment: the RIGHT node, and on the killing blow.
 *
 * Produces: dismemberment.dll
 */
#ifndef DISMEMBERMENT_H
#define DISMEMBERMENT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum limb_mode {
    LIMB_MODE_OFF = 0,
    LIMB_MODE_NODE_ONLY,     /* only correct which node is severed */
    LIMB_MODE_ON_DEATH       /* also sever on the killing blow */
} limb_mode_t;

typedef struct limb_config {
    limb_mode_t mode;
    float       spin_scale;       /* tumble of the flying piece */
    float       gravity_scale;    /* gravity of the flying piece */
    float       settle_seconds;   /* after this long the piece lies still for good */
    float       settle_damping;   /* damping of the tumble per substep before that */
    float       yaw_scale;        /* the 90-degree yaw kick PER SUBSTEP in the flight arm */
    bool        diagnostics;      /* record the state of every flying piece */
} limb_config_t;

void dismemberment_install(void);

/* Read-only, for limb_flight.c. Valid after dismemberment_install(). */
const limb_config_t *limb_config(void);

/* Walks a node's subtree for the first node that carries a mesh, or -1.
 * Shared because both the mesh-index translation and the blade-node gate need it, and it walks
 * FOREIGN data: depth and child count are capped and every pointer is checked. */
int32_t limb_first_mesh_in_subtree(const uint8_t *node, int depth);

#endif /* DISMEMBERMENT_H */
