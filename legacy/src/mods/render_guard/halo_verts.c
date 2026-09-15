/* halo_verts.c: see halo_verts.h. */
#include "halo_verts.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RENDER_GUARD_SECTION "render_guard"

/* --- halo_draw's call of bapobj_getNodeMeshVerts, 0x00439B2F ---------------------------------- *
 *   8D 45 90                 lea eax,[ebp-0x70]         the 12 float buffer, 48 bytes
 *   50                       push eax
 *   8B 8D EC FE FF FF        mov ecx,[ebp-0x114]        the halo record
 *   8B 51 04                 mov edx,[ecx+4]            its node index
 *   52                       push edx
 *   8B 45 08                 mov eax,[ebp+8]            the object
 *   50                       push eax
 *   E8 <rel32>               call bapobj_getNodeMeshVerts, replaced
 *   DD D8 / 90 90            fstp st(0), or the no-ops node_verts.c leaves there
 *   83 C4 0C                 add esp,0xC                cdecl, three arguments
 * The buffer is 48 bytes and the node pose copy sits right above it at [ebp-0x40], so four
 * vertices is the most the routine can write without reaching the pose. */
static const uint8_t SIG_HALO_COPY[] = {
    0x8D, 0x45, 0x90, 0x50, 0x8B, 0x8D, 0xEC, 0xFE, 0xFF, 0xFF, 0x8B, 0x51, 0x04, 0x52,
    0x8B, 0x45, 0x08, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C
};
static const uint8_t MSK_HALO_COPY[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_HALO_COPY == sizeof MSK_HALO_COPY, "mask length");
#define HALO_COPY_CALL_OFFSET 18u
#define HALO_COPY_POP_OFFSET  23u
#define HALO_VERTS_MAX        4u
#define HALO_VERTS_FLOATS     (HALO_VERTS_MAX * 3u)

/* --- the walk bapobj_getNodeMeshVerts makes, from its disassembly at 0x0041378A ---------------- *
 *   obj+0x9C       the rdThing                mov edx,[ecx+0x9C]
 *   thing+0x04     the model                  mov eax,[edx+4]
 *   model+0x54     the node count             cmp ecx,[eax+0x54]
 *   model+0x58     the node array, 0xB4 each  mov edx,[eax+0x58]; imul ecx,ecx,0xB4
 *   node+0x4C      the mesh index, <0 = none  cmp dword [eax+0x4C],0
 *   model+0x28     the mesh array, 0x70 each  mov edx,[edx+0x28]; imul ecx,ecx,0x70
 *   mesh+0x48      the vertex count           cmp edx,[ecx+0x48]
 *   mesh+0x30      the vertices, 12 bytes each mov edx,[ecx+0x30]; imul eax,eax,0xC
 *   model+0x00     the model's name, 0x24 bytes (view_distance_fix reads it the same way) */
#define OBJ_THING        0x9Cu
#define THING_MODEL      0x04u
#define MODEL_NAME_SIZE  0x24u
#define MODEL_MESHES     0x28u
#define MODEL_NODE_COUNT 0x54u
#define MODEL_NODES      0x58u
#define NODE_STRIDE      0xB4u
#define NODE_MESH_INDEX  0x4Cu
#define MESH_STRIDE      0x70u
#define MESH_VERTS       0x30u
#define MESH_VERT_COUNT  0x48u
#define VERT_STRIDE      0x0Cu

/* One line per model and node, the first time it fails; a small table is enough for a level. */
#define FAILURES_NAMED 16u
static struct {
    char     model[MODEL_NAME_SIZE + 1];
    uint32_t node;
} named[FAILURES_NAMED];
static unsigned named_count;

static void name_failure(uintptr_t model, uint32_t node, const char *why)
{
    char     name[MODEL_NAME_SIZE + 1] = { 0 };
    unsigned i;

    if (model != 0) {
        (void)memory_try_read(model, name, MODEL_NAME_SIZE);
    }
    for (i = 0; i < named_count; ++i) {
        if (named[i].node == node && strcmp(named[i].model, name) == 0) {
            return;
        }
    }
    if (named_count < FAILURES_NAMED) {
        memcpy(named[named_count].model, name, sizeof named[named_count].model);
        named[named_count].node = node;
        ++named_count;
    }
    log_info("halo on %s node %u: %s, so the halo is drawn from nothing and culled instead of "
             "from the stack", name[0] ? name : "?", (unsigned)node, why);
}

/* The two tests the routine makes and the copy it does, with the buffer's size honoured. Returns
 * false with `out` zeroed when the node has no vertices to read. */
static bool copy_node_verts(uintptr_t obj, uint32_t node, float *out)
{
    uintptr_t thing = 0, model = 0, nodes = 0, meshes = 0, verts = 0;
    int32_t   mesh_index = -1;
    uint32_t  node_count = 0, vert_count = 0, copied;

    memset(out, 0, HALO_VERTS_FLOATS * sizeof *out);
    if (!memory_try_read_u32(obj + OBJ_THING, (uint32_t *)&thing) || thing == 0 ||
        !memory_try_read_u32(thing + THING_MODEL, (uint32_t *)&model) || model == 0 ||
        !memory_try_read_u32(model + MODEL_NODE_COUNT, &node_count) ||
        !memory_try_read_u32(model + MODEL_NODES, (uint32_t *)&nodes)) {
        name_failure(model, node, "the object has no readable model");
        return false;
    }
    if (node >= node_count) {
        name_failure(model, node, "the node index is past the model's node count");
        return false;
    }
    if (!memory_try_read_u32(nodes + node * NODE_STRIDE + NODE_MESH_INDEX,
                             (uint32_t *)&mesh_index) || mesh_index < 0) {
        name_failure(model, node, "the node carries no mesh");
        return false;
    }
    if (!memory_try_read_u32(model + MODEL_MESHES, (uint32_t *)&meshes) ||
        !memory_try_read_u32(meshes + (uint32_t)mesh_index * MESH_STRIDE + MESH_VERT_COUNT,
                             &vert_count) ||
        !memory_try_read_u32(meshes + (uint32_t)mesh_index * MESH_STRIDE + MESH_VERTS,
                             (uint32_t *)&verts) || vert_count == 0 || verts == 0) {
        name_failure(model, node, "the node's mesh has no vertices");
        return false;
    }
    copied = vert_count < HALO_VERTS_MAX ? vert_count : HALO_VERTS_MAX;
    if (!memory_try_read(verts, out, copied * VERT_STRIDE)) {
        memset(out, 0, HALO_VERTS_FLOATS * sizeof *out);
        name_failure(model, node, "the node's vertices are not readable");
        return false;
    }
    return true;
}

/* The two shapes the call site can want, chosen at install from the bytes after the call: the
 * routine's own float return for a caller that still pops one, nothing for a caller whose pop
 * node_verts.c has already taken out. */
static void __cdecl halo_node_verts(uintptr_t obj, uint32_t node, float *out)
{
    (void)copy_node_verts(obj, node, out);
}

static float __cdecl halo_node_verts_float(uintptr_t obj, uint32_t node, float *out)
{
    (void)copy_node_verts(obj, node, out);
    return 0.0f;
}

void halo_verts_install(void)
{
    uintptr_t site;
    uint8_t   pop[2] = { 0, 0 };
    bool      caller_pops;

    if (!ini_read_bool(RENDER_GUARD_SECTION, "GuardHaloVerts", true)) {
        log_info("GuardHaloVerts=0, so a halo on a node with no mesh is drawn from whatever the "
                 "stack held, as the game shipped");
        return;
    }
    site = signature_find_unique(SIG_HALO_COPY, MSK_HALO_COPY, sizeof SIG_HALO_COPY);
    if (site == 0) {
        log_warning("halo_draw's node vertex copy did not resolve, so a halo on a node with no "
                    "mesh is left drawn from the stack");
        return;
    }
    if (!memory_try_read(site + HALO_COPY_POP_OFFSET, pop, sizeof pop)) {
        log_warning("the bytes after halo_draw's copy could not be read, refused");
        return;
    }
    caller_pops = pop[0] == 0xDD && pop[1] == 0xD8;
    if (!caller_pops && (pop[0] != 0x90 || pop[1] != 0x90)) {
        log_warning("the bytes after halo_draw's copy read %02X %02X, neither the pop nor the "
                    "no-ops expected, refused", (unsigned)pop[0], (unsigned)pop[1]);
        return;
    }
    if (patch_redirect_call(site + HALO_COPY_CALL_OFFSET,
                            caller_pops ? (const void *)&halo_node_verts_float
                                        : (const void *)&halo_node_verts) != PATCH_RESULT_OK) {
        log_warning("halo_draw's call at %08X could not be redirected, refused",
                    (unsigned)(site + HALO_COPY_CALL_OFFSET));
        return;
    }
    log_info("halo_draw's node vertex copy at %08X goes through a copy that zeroes its twelve "
             "floats when the node has no mesh, so such a halo projects to a point and is culled "
             "instead of drawn from the stack%s", (unsigned)(site + HALO_COPY_CALL_OFFSET),
             caller_pops ? "; the caller still pops a float and is handed one" : "");
}
