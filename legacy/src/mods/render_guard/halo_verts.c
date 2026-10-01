/* halo_verts.c: see halo_verts.h. */
#include "halo_verts.h"

#include "halo_copy.h"

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

/* One line per model and node, the first time it fails; a small table is enough for a level. */
#define FAILURES_NAMED 16u
static struct {
    char     model[HALO_MODEL_NAME_SIZE + 1];
    uint32_t node;
} named[FAILURES_NAMED];
static unsigned named_count;

static void name_failure(uintptr_t model, uint32_t node, halo_copy_result_t why)
{
    char     name[HALO_MODEL_NAME_SIZE + 1] = { 0 };
    unsigned i;

    if (model != 0) {
        (void)memory_try_read(model, name, HALO_MODEL_NAME_SIZE);
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
             "from the stack", name[0] ? name : "?", (unsigned)node, halo_copy_result_text(why));
}

static void copy_node_verts(uintptr_t obj, uint32_t node, float *out)
{
    uintptr_t          model = 0;
    halo_copy_result_t result = halo_copy_node_verts(obj, node, out, &model);

    if (result != HALO_COPY_OK) {
        name_failure(model, node, result);
    }
}

/* The two shapes the call site can want, chosen at install from the bytes after the call: the
 * routine's own float return for a caller that still pops one, nothing for a caller whose pop
 * node_verts.c has already taken out. */
static void __cdecl halo_node_verts(uintptr_t obj, uint32_t node, float *out)
{
    copy_node_verts(obj, node, out);
}

static float __cdecl halo_node_verts_float(uintptr_t obj, uint32_t node, float *out)
{
    copy_node_verts(obj, node, out);
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
