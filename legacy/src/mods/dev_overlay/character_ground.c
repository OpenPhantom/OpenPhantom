/* character_ground.c: the footstep module's node position, taken out of the model being worn. */
#include "character_ground.h"

#include "character_nodemap.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The footstep module's node position entry. The pattern reaches from the prologue to the first
 * call because the walker it works on is a file static of that module, and the cell is named twice
 * inside the match; the first naming is read back out and is the only address this module needs.
 *
 * Both namings are wildcards, and so is the stamp the pose rebuild is gated on, so the pattern
 * carries no absolute address of its own. What is left is the shape: a handle taken out of a
 * global, its render handle, the stamp compare, and the euler build the rebuild opens with. */
static const uint8_t SIG_NODE_TO_WORLD[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44,              /* push ebp; mov ebp,esp; sub esp,0x44      */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the walker]                     */
    0x8B, 0x88, 0x9C, 0x00, 0x00, 0x00,              /* mov ecx,[eax+0x9C]  the render handle    */
    0x89, 0x4D, 0xF8,
    0x8B, 0x55, 0xF8,
    0x8B, 0x42, 0x1C,                                /* mov eax,[edx+0x1C]  the pose stamp       */
    0x3B, 0x05, 0x00, 0x00, 0x00, 0x00,              /* cmp eax,[the render stamp]               */
    0x74, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,              /* mov ecx,[the walker]                     */
    0x83, 0xC1, 0x3C,                                /* add ecx,0x3C        the rotation         */
    0x51,
    0x8D, 0x55, 0xBC,
    0x52,
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the euler to matrix build           */
    0x83, 0xC4, 0x08
};
static const uint8_t MSK_NODE_TO_WORLD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_NODE_TO_WORLD) == sizeof(MSK_NODE_TO_WORLD),
               "the node position pattern and its mask are different lengths");

/* The operand of the first load, inside the pattern: the cell that holds the body the footstep
 * tick is running on. It is set on entry to the tick and cleared on the way out, and the one other
 * entry point into this function sets it too, so it is never stale while the function runs. */
#define OFFSET_WALKER          0x07u

/* push ebp; mov ebp,esp; sub esp,0x44. Five would land inside the frame size. */
#define NODE_TO_WORLD_PROLOGUE 6u

#define BAPOBJ_ACTOR          0x14u   /* the authored asset the body was built from             */
#define BAPOBJ_THING          0x9Cu   /* the render handle                                      */
#define BAPACTOR_MODEL        0xE0u

#define RDTHING_MODEL3        0x04u   /* the model the handle is WEARING                        */
#define RDTHING_NODE_MATRIX   0x20u   /* one mat34 per node, in the worn model's ordinal space   */

#define MODEL_NUM_MESHES      0x24u
#define MODEL_MESHES          0x28u
#define MODEL_NUM_NODES       0x54u
#define MODEL_NODES           0x58u

#define NODE_BYTES            0xB4u
#define NODE_MESH_INDEX       0x4Cu   /* -1 when the node carries no geometry                   */

#define MESH_BYTES            0x70u
#define MESH_CENTRE           0x58u   /* the loader's bounding box midpoint, three floats        */

#define MAT34_FLOATS          12u
#define MAT34_BYTES           0x30u

/* The widest mesh array the shipped assets carry is two orders below this. It bounds the
 * multiplication before the index reaches it, which is the only thing the bound is for. */
#define GROUND_MAX_MESHES     4096u

/* Worth a line in the log, in thousandths of a world unit. Below this the two models agree about
 * where the node is and there is nothing to report. */
#define GROUND_REPORT_MIN     0.001f

typedef void (__cdecl *node_to_world_fn_t)(int32_t node_index, float *out);

typedef struct ground_state {
    bool               tried;
    bool               installed;

    detour_t           detour;
    node_to_world_fn_t original;

    uintptr_t          walker_cell;    /* the cell, not the body */

    uintptr_t          reported_model; /* the worn model the line below was written for */
    bool               reported;
} ground_state_t;

static ground_state_t ground;

/* ============================================================================================ */

bool character_ground_transform(const float matrix[12], const float point[3], float out[3])
{
    float result[3];
    uint32_t i;

    if (matrix == NULL || point == NULL || out == NULL) {
        return false;
    }
    for (i = 0; i < MAT34_FLOATS; ++i) {
        if (!isfinite(matrix[i])) {
            return false;
        }
    }
    for (i = 0; i < 3u; ++i) {
        if (!isfinite(point[i])) {
            return false;
        }
    }

    /* Three columns of three and a translation, which is what the engine's own multiplication
     * reads: the first output word takes the matrix at 0x00, 0x0c and 0x18 and adds 0x24. */
    result[0] = matrix[0] * point[0] + matrix[3] * point[1] + matrix[6] * point[2] + matrix[9];
    result[1] = matrix[1] * point[0] + matrix[4] * point[1] + matrix[7] * point[2] + matrix[10];
    result[2] = matrix[2] * point[0] + matrix[5] * point[1] + matrix[8] * point[2] + matrix[11];

    for (i = 0; i < 3u; ++i) {
        if (!isfinite(result[i])) {
            return false;
        }
    }
    out[0] = result[0];
    out[1] = result[1];
    out[2] = result[2];
    return true;
}

uint32_t character_ground_node_of(int32_t node_index, uint32_t node_count)
{
    if (node_index < 0 || node_count == 0u || (uint32_t)node_index >= node_count) {
        return 0u;
    }
    return (uint32_t)node_index;
}

/* ============================================================================================ */

static bool read_u32(uintptr_t address, uint32_t *out)
{
    return memory_try_read(address, out, sizeof *out) && *out != 0u;
}

/* The mesh bounding box centre the engine transforms, taken out of `model` for `node`. A node
 * without geometry answers the origin rather than the three floats in front of the mesh array,
 * which is what the engine reads for a mesh index of -1. Under the swap that case is reachable and
 * on every shipped rig it is not: all 85 rigs that carry `lfoot`, all 88 that carry `rfoot` and
 * all 94 that carry `chest` put a mesh on it. The origin is the node's own world position under
 * the same matrix, so a rig that ever did arrive here would print from the joint. */
static bool node_mesh_centre(uintptr_t model, uint32_t node, float centre[3])
{
    uint32_t nodes = 0;
    uint32_t meshes = 0;
    uint32_t mesh_count = 0;
    int32_t  mesh_index = 0;

    centre[0] = 0.0f;
    centre[1] = 0.0f;
    centre[2] = 0.0f;

    if (!read_u32(model + MODEL_NODES, &nodes)) {
        return false;
    }
    if (!memory_try_readable((uintptr_t)nodes + (uintptr_t)node * NODE_BYTES, NODE_BYTES) ||
        !memory_try_read((uintptr_t)nodes + (uintptr_t)node * NODE_BYTES + NODE_MESH_INDEX,
                         &mesh_index, sizeof mesh_index)) {
        return false;
    }
    if (mesh_index < 0) {
        return true;                       /* no geometry: the joint itself */
    }
    if (!memory_try_read(model + MODEL_NUM_MESHES, &mesh_count, sizeof mesh_count) ||
        mesh_count == 0u || mesh_count > GROUND_MAX_MESHES ||
        (uint32_t)mesh_index >= mesh_count) {
        return false;
    }
    if (!read_u32(model + MODEL_MESHES, &meshes)) {
        return false;
    }
    return memory_try_read((uintptr_t)meshes + (uintptr_t)mesh_index * MESH_BYTES + MESH_CENTRE,
                           centre, sizeof(float) * 3u);
}

/* THE CORRECTION. Everything it reads comes off the render handle the engine has just posed, so
 * the joint matrix is already built and the stamp is already current: this recomputes the point
 * that matrix is applied to and nothing else.
 *
 * The node count it clamps against is the WORN model's, not the actor's. The engine clamps against
 * the actor's, which is how a rig whose foot sits past the player's own node count, as two of the
 * offered rows do for a player who is Panaka or the Queen, ends up printing from node 0. */
static void correct_position(int32_t node_index, float *out)
{
    uintptr_t body;
    uint32_t  thing = 0;
    uint32_t  handle = 0;
    uint32_t  worn = 0;
    uint32_t  actor = 0;
    uint32_t  home = 0;
    uint32_t  matrices = 0;
    uint32_t  node_count = 0;
    uint32_t  node;
    float     matrix[MAT34_FLOATS];
    float     centre[3];
    float     world[3];

    if (!read_u32(ground.walker_cell, &thing)) {
        return;
    }
    body = (uintptr_t)thing;
    if (!read_u32(body + BAPOBJ_THING, &handle) ||
        !read_u32((uintptr_t)handle + RDTHING_MODEL3, &worn) ||
        !read_u32(body + BAPOBJ_ACTOR, &actor) ||
        !read_u32((uintptr_t)actor + BAPACTOR_MODEL, &home)) {
        return;
    }
    if (worn == home) {
        return;                            /* the body wears what its actor names */
    }
    if (!memory_try_read((uintptr_t)worn + MODEL_NUM_NODES, &node_count, sizeof node_count) ||
        node_count == 0u || node_count > NODEMAP_MAX_NODES) {
        return;
    }
    node = character_ground_node_of(node_index, node_count);

    if (!read_u32((uintptr_t)handle + RDTHING_NODE_MATRIX, &matrices) ||
        !memory_try_read((uintptr_t)matrices + (uintptr_t)node * MAT34_BYTES,
                         &matrix[0], sizeof matrix)) {
        return;
    }
    if (!node_mesh_centre((uintptr_t)worn, node, centre)) {
        return;
    }
    if (!character_ground_transform(matrix, centre, world)) {
        return;
    }

    if (!ground.reported || ground.reported_model != (uintptr_t)worn) {
        float dx = world[0] - out[0];
        float dy = world[1] - out[1];
        float dz = world[2] - out[2];
        float shift = sqrtf(dx * dx + dy * dy + dz * dz);

        if (shift >= GROUND_REPORT_MIN) {
            ground.reported = true;
            ground.reported_model = (uintptr_t)worn;
            log_info("the borrowed body's ground marks were landing %d/1000 of a unit away from "
                     "the node they belong to, because the node was looked up in the worn rig and "
                     "measured in the player's own; they are now taken from the worn rig",
                     (int)(shift * 1000.0f));
        }
    }

    out[0] = world[0];
    out[1] = world[1];
    out[2] = world[2];
}

/* The original runs FIRST and unconditionally, and that is not an ordering preference.
 *
 * The function rebuilds the body's pose when the stamp says it is stale, and the joint matrix this
 * correction reads is what that rebuild writes. Recomputing before it would read the previous
 * frame's pose; refusing to call it at all would leave the pose unbuilt for every later caller in
 * the tick. So the engine does its whole job, and the last three floats it wrote are replaced.
 *
 * The `original` pointer is written one statement AFTER the branch that makes this reachable, and
 * it is not guarded here on purpose: the install runs once from the overlay's own startup path,
 * before the game has a frame loop, and this engine is single threaded. A guard would trade a call
 * through a null pointer for a footstep position that was never written, which is the worse of the
 * two. If this module ever comes to be installed from a running frame, that changes. */
static void __cdecl hook_node_to_world(int32_t node_index, float *out)
{
    ground.original(node_index, out);

    if (!ground.installed || out == NULL || ground.walker_cell == 0u) {
        return;
    }
    if (!character_nodemap_is_armed()) {
        return;                            /* nothing is borrowed: the engine was right */
    }
    correct_position(node_index, out);
}

/* ============================================================================================ */

bool character_ground_install(void)
{
    static signature_t site = SIGNATURE_ENTRY_DETOUR_MASKED("footstep node position",
                                                            SIG_NODE_TO_WORLD, MSK_NODE_TO_WORLD,
                                                            NODE_TO_WORLD_PROLOGUE);
    uintptr_t address;
    uintptr_t cell = 0;

    if (ground.tried) {
        return ground.installed;
    }
    ground.tried = true;

    address = signature_find_detour_target(SIG_NODE_TO_WORLD, MSK_NODE_TO_WORLD,
                                           sizeof SIG_NODE_TO_WORLD, NODE_TO_WORLD_PROLOGUE);
    if (address == 0u) {
        log_warning("the footstep module's node position did not resolve, so a borrowed body "
                    "leaves its footprints, its dust and its splashes where the player's own "
                    "skeleton would have put them");
        return false;
    }

    /* Read before the detour, and through the operand reader rather than by hand: the cell is
     * named a byte past the prologue this install is about to write over, and the reader is what
     * decides whether to believe memory or the file. */
    site.address = address;
    if (!signature_read_address_operand(&site, OFFSET_WALKER, &cell) || cell == 0u) {
        log_warning("the footstep node position at %08X does not name the body it works on",
                    (unsigned)address);
        return false;
    }

    if (!detour_install(&ground.detour, address, (const void *)&hook_node_to_world,
                        NODE_TO_WORLD_PROLOGUE)) {
        log_warning("the footstep node position at %08X could not be detoured", (unsigned)address);
        return false;
    }
    ground.original = (node_to_world_fn_t)ground.detour.original;
    ground.walker_cell = cell;
    ground.installed = true;
    log_info("the footstep node position at %08X is hooked, so a borrowed body's footprints, dust "
             "and splashes are measured on the rig it is wearing", (unsigned)address);
    return true;
}

bool character_ground_is_installed(void)
{
    return ground.installed;
}
