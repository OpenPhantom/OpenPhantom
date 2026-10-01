/* character_prop_blade.c: a far player's blade at his own length. See the header. */
#include "character_prop_blade.h"

#include "character_prop_rig.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4,
               "the position pointer exchanged here is a 32-bit field of an engine mesh set");

/* The model, the node and the mesh set, as the draw reads them. */
#define MODEL_GEOSET0_MESHES 0x28u
#define NODE_MESH_INDEX      0x4Cu
#define MESH_BYTES           0x70u
#define MESH_POSITIONS       0x30u
#define MESH_NUM_VERTS       0x48u

/* The player block's own blade cells: four vertices and then the two tip deltas at +0x1C8, and
 * the length the engine's tick grows and shrinks at +0x210. */
#define BLOCK_BLADE_VECTORS  0x1C8u
#define BLOCK_BLADE_SIZE     0x210u
#define BLADE_VECTOR_FLOATS  18u

/* A blade mesh is four vertices. bapobj_setNodeMeshVerts reads the count at mesh+0x48 RAW as the
 * bound of its own loop and the buffer the engine pushes is 0x30 bytes, so a blade mesh with any
 * other count would already be a mesh that setter writes past the end of its source. */
#define BLADE_VERTICES        4u

static struct {
    uint32_t drawn;       /* draws that showed a length of the row's own                    */
    uint32_t no_blade;    /* binds whose model carries no blade mesh of four; the player's   */
                          /* own row is bound here too, and every hero without a sabre has   */
                          /* none, so this is a count and not a fault                        */
    uint32_t unread;      /* opens whose block or whose mesh would not read or not write    */
    uint32_t stuck;       /* pointers that would not go back, which is the loud one         */
    bool     stuck_said;
    float    shortest;
    float    longest;
    bool     any_length;
} blade;

/* Finite by the bit pattern: an exponent of all ones is an infinity or a NaN and nothing else. */
static bool finite_one(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

static bool finite_run(const float *values, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        if (!finite_one(values[i])) {
            return false;
        }
    }
    return true;
}

static void note_length(float size)
{
    if (!blade.any_length) {
        blade.any_length = true;
        blade.shortest = size;
        blade.longest = size;
        return;
    }
    if (size < blade.shortest) {
        blade.shortest = size;
    }
    if (size > blade.longest) {
        blade.longest = size;
    }
}

bool character_prop_blade_bind(prop_body_t *body, uintptr_t reference_model, uintptr_t name_table,
                               uint32_t name_id)
{
    uintptr_t node = 0;
    uintptr_t mesh;
    uint32_t  slot = 0;
    uint32_t  meshes = 0;
    uint32_t  verts = 0;
    int32_t   index = -1;

    if (body == NULL) {
        return false;
    }
    character_prop_blade_forget(body);
    if (!character_prop_rig_node(reference_model, name_table, name_id, &slot, &node) ||
        !memory_try_read(node + NODE_MESH_INDEX, &index, sizeof index) || index < 0 ||
        !memory_try_read(reference_model + MODEL_GEOSET0_MESHES, &meshes, sizeof meshes) ||
        meshes == 0u) {
        ++blade.no_blade;
        return false;   /* no blade in that asset, and a gun has no length to draw */
    }
    mesh = (uintptr_t)meshes + (uintptr_t)index * MESH_BYTES;
    if (!memory_try_read(mesh + MESH_NUM_VERTS, &verts, sizeof verts) ||
        verts != BLADE_VERTICES) {
        ++blade.no_blade;
        return false;
    }
    body->blade_mesh = mesh;
    return true;
}

void character_prop_blade_open(prop_body_t *body, uintptr_t block)
{
    float    vectors[BLADE_VECTOR_FLOATS];
    float    size = 0.0f;
    uint32_t field;
    uint32_t axis;

    if (body == NULL || body->blade_mesh == 0u || body->blade_open || body->block == 0u) {
        return;   /* no blade in the asset, a draw already open, or the player's own row */
    }
    if (!memory_try_read(block + BLOCK_BLADE_VECTORS, vectors, sizeof vectors) ||
        !memory_try_read(block + BLOCK_BLADE_SIZE, &size, sizeof size) ||
        !finite_run(vectors, BLADE_VECTOR_FLOATS) || !finite_one(size)) {
        ++blade.unread;
        return;
    }
    /* Plr_SetBladeSize 0x00449C6E, at the stores 0x00449E00 and behind: the two hilt vertices
     * stand, and each tip vertex is its hilt vertex plus the delta behind it times the length. */
    for (axis = 0u; axis < 3u; ++axis) {
        body->blade_vert[axis] = vectors[axis];
        body->blade_vert[3u + axis] = vectors[3u + axis];
        body->blade_vert[6u + axis] = vectors[12u + axis] * size + vectors[axis];
        body->blade_vert[9u + axis] = vectors[15u + axis] * size + vectors[3u + axis];
    }
    if (!memory_try_read(body->blade_mesh + MESH_POSITIONS, &body->blade_saved,
                         sizeof body->blade_saved) || body->blade_saved == 0u) {
        ++blade.unread;
        return;
    }
    field = (uint32_t)(uintptr_t)&body->blade_vert[0];
    if (!memory_try_write(body->blade_mesh + MESH_POSITIONS, &field, sizeof field)) {
        ++blade.unread;
        return;
    }
    body->blade_open = true;
    ++blade.drawn;
    note_length(size);
}

void character_prop_blade_close(prop_body_t *body)
{
    if (body == NULL || !body->blade_open) {
        return;
    }
    body->blade_open = false;
    if (memory_try_write(body->blade_mesh + MESH_POSITIONS, &body->blade_saved,
                         sizeof body->blade_saved)) {
        return;
    }
    /* The one failure that matters, because the field it left named belongs to this row and the
     * mesh belongs to the asset: every body wearing that asset would draw this row's blade from
     * here on. The row stops exchanging anything, which cannot undo it and can stop it growing. */
    ++blade.stuck;
    body->blade_mesh = 0u;
    if (!blade.stuck_said) {
        blade.stuck_said = true;
        log_warning("a far blade's vertex pointer could not be put back into the mesh it was "
                    "borrowed from; that row draws no length of its own any more");
    }
}

void character_prop_blade_forget(prop_body_t *body)
{
    if (body == NULL) {
        return;
    }
    character_prop_blade_close(body);
    body->blade_mesh = 0u;
    body->blade_saved = 0u;
}

void character_prop_blade_report(void)
{
    if (blade.drawn == 0u && blade.no_blade == 0u && blade.unread == 0u && blade.stuck == 0u) {
        return;
    }
    log_info("far blades: %u draw(s) at a length of their own, %d to %d thousandths; %u row(s) "
             "bound to a model with no blade mesh of four vertices, which every hero without a "
             "sabre is; %u block(s) or mesh(es) that would not read, %u pointer(s) that would "
             "not go back",
             (unsigned)blade.drawn, (int)(blade.shortest * 1000.0f),
             (int)(blade.longest * 1000.0f), (unsigned)blade.no_blade, (unsigned)blade.unread,
             (unsigned)blade.stuck);
    blade.drawn = 0u;
    blade.no_blade = 0u;
    blade.unread = 0u;
    blade.any_length = false;
}
