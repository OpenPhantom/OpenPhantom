/* halo_copy.c: see halo_copy.h. */
#include "halo_copy.h"

#include "common/memory.h"

#include <stddef.h>
#include <string.h>

const char *halo_copy_result_text(halo_copy_result_t result)
{
    switch (result) {
    case HALO_COPY_OK:              return "the node's vertices were copied";
    case HALO_COPY_NO_MODEL:        return "the object has no readable model";
    case HALO_COPY_NODE_PAST_COUNT: return "the node index is past the model's node count";
    case HALO_COPY_NO_MESH:         return "the node carries no mesh";
    case HALO_COPY_NO_VERTICES:     return "the node's mesh has no vertices";
    case HALO_COPY_UNREADABLE:      return "the node's vertices are not readable";
    default:                        return "?";
    }
}

halo_copy_result_t halo_copy_node_verts(uintptr_t obj, uint32_t node, float *out,
                                        uintptr_t *model_out)
{
    uintptr_t thing = 0, model = 0, nodes = 0, meshes = 0, verts = 0;
    int32_t   mesh_index = -1;
    uint32_t  node_count = 0, vert_count = 0, copied;

    memset(out, 0, HALO_VERTS_FLOATS * sizeof *out);
    if (model_out != NULL) {
        *model_out = 0;
    }
    if (!memory_try_read_u32(obj + HALO_OBJ_THING, (uint32_t *)&thing) || thing == 0 ||
        !memory_try_read_u32(thing + HALO_THING_MODEL, (uint32_t *)&model) || model == 0) {
        return HALO_COPY_NO_MODEL;
    }
    if (model_out != NULL) {
        *model_out = model;
    }
    if (!memory_try_read_u32(model + HALO_MODEL_NODE_COUNT, &node_count) ||
        !memory_try_read_u32(model + HALO_MODEL_NODES, (uint32_t *)&nodes)) {
        return HALO_COPY_NO_MODEL;
    }
    if (node >= node_count) {
        return HALO_COPY_NODE_PAST_COUNT;
    }
    if (!memory_try_read_u32(nodes + node * HALO_NODE_STRIDE + HALO_NODE_MESH_INDEX,
                             (uint32_t *)&mesh_index) || mesh_index < 0) {
        return HALO_COPY_NO_MESH;
    }
    if (!memory_try_read_u32(model + HALO_MODEL_MESHES, (uint32_t *)&meshes) ||
        !memory_try_read_u32(meshes + (uint32_t)mesh_index * HALO_MESH_STRIDE +
                             HALO_MESH_VERT_COUNT, &vert_count) ||
        !memory_try_read_u32(meshes + (uint32_t)mesh_index * HALO_MESH_STRIDE + HALO_MESH_VERTS,
                             (uint32_t *)&verts) || vert_count == 0 || verts == 0) {
        return HALO_COPY_NO_VERTICES;
    }
    copied = vert_count < HALO_VERTS_MAX ? vert_count : HALO_VERTS_MAX;
    if (!memory_try_read(verts, out, copied * HALO_VERT_STRIDE)) {
        memset(out, 0, HALO_VERTS_FLOATS * sizeof *out);
        return HALO_COPY_UNREADABLE;
    }
    return HALO_COPY_OK;
}
