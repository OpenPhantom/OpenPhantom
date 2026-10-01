/* halo_copy.h: the node vertex copy halo_draw needs, with the buffer's size honoured.
 *
 * The walk bapobj_getNodeMeshVerts makes, from its disassembly at 0x0041378A, and the two tests
 * it makes on the way. Kept apart from the install so the unit test can hand it structures of its
 * own and check every arm: the routine writes nothing on either failure and this must write zeros.
 *
 *   obj+0x9C       the rdThing                 mov edx,[ecx+0x9C]
 *   thing+0x04     the model                   mov eax,[edx+4]
 *   model+0x54     the node count              cmp ecx,[eax+0x54]
 *   model+0x58     the node array, 0xB4 each   mov edx,[eax+0x58]; imul ecx,ecx,0xB4
 *   node+0x4C      the mesh index, <0 = none   cmp dword [eax+0x4C],0
 *   model+0x28     the mesh array, 0x70 each   mov edx,[edx+0x28]; imul ecx,ecx,0x70
 *   mesh+0x48      the vertex count            cmp edx,[ecx+0x48]
 *   mesh+0x30      the vertices, 12 bytes each mov edx,[ecx+0x30]; imul eax,eax,0xC
 *   model+0x00     the model's name, 0x24 bytes (view_distance_fix reads it the same way)
 */
#ifndef HALO_COPY_H
#define HALO_COPY_H

#include <stdbool.h>
#include <stdint.h>

#define HALO_OBJ_THING        0x9Cu
#define HALO_THING_MODEL      0x04u
#define HALO_MODEL_NAME_SIZE  0x24u
#define HALO_MODEL_MESHES     0x28u
#define HALO_MODEL_NODE_COUNT 0x54u
#define HALO_MODEL_NODES      0x58u
#define HALO_NODE_STRIDE      0xB4u
#define HALO_NODE_MESH_INDEX  0x4Cu
#define HALO_MESH_STRIDE      0x70u
#define HALO_MESH_VERTS       0x30u
#define HALO_MESH_VERT_COUNT  0x48u
#define HALO_VERT_STRIDE      0x0Cu

/* halo_draw's buffer is 48 bytes, four vertices, with the node pose copy right above it. */
#define HALO_VERTS_MAX        4u
#define HALO_VERTS_FLOATS     (HALO_VERTS_MAX * 3u)

typedef enum halo_copy_result {
    HALO_COPY_OK,
    HALO_COPY_NO_MODEL,        /* the object, its thing or its model is not readable */
    HALO_COPY_NODE_PAST_COUNT, /* the routine's first failure arm */
    HALO_COPY_NO_MESH,         /* its second: the node's mesh index is negative */
    HALO_COPY_NO_VERTICES,     /* a mesh with no vertices, or a table nothing can read */
    HALO_COPY_UNREADABLE       /* the vertices themselves could not be read */
} halo_copy_result_t;

/* Fills `out` with the node's first four vertices at most and returns HALO_COPY_OK, or zeroes all
 * twelve floats and says why. `model_out`, when given, receives the model's address on any result
 * that reached it, for the log line that names it. */
halo_copy_result_t halo_copy_node_verts(uintptr_t obj, uint32_t node, float *out,
                                        uintptr_t *model_out);

/* The reason in words, for the log. */
const char *halo_copy_result_text(halo_copy_result_t result);

#endif /* HALO_COPY_H */
