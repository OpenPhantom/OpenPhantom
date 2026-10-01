/* character_cards.c: the bound on the sabre card fetch, and the bracket on the registration.
 *
 * The reasoning is in character_cards.h. The byte level evidence, retail WMAIN.EXE:
 *
 *   0041378A  the fetch, bapobj_getNodeMeshVerts(obj, node, out). It tests the node against the
 *             model's node count (004137CB) and then copies while i is below the MESH's vertex
 *             count (00413832, cmp edx,[ecx+0x48]); nothing bounds the destination.
 *   00439B41  halo_draw calls it with a buffer at ebp-0x70, and the next live local, the joint
 *             matrix, starts at ebp-0x40: 48 bytes, and vertex 9 reaches the saved ebp and the
 *             return address.
 *   0044987F  Plr_CaptureBladeMesh calls it with playerRecord+0x1C8, reads back to +0x1F4 and
 *             writes its first result at +0x1F8, where a fifth vertex would land.
 *
 * What is here is the two patterns, the two hooks and the reads each one has to make before it is
 * allowed to answer.
 */
#include "character_cards.h"

#include "character_mount.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The object, the render handle, the model, the node and the mesh, as the fetch routine reads
 * them. Every one of these is the same number character_prop.c reads the same field at, and the
 * two files were written from the same disassembly rather than from each other. */
#define BAPOBJ_THING         0x9Cu
#define RDTHING_MODEL3       0x04u
#define MODEL_GEOSET0_MESHES 0x28u
#define MODEL_NUM_NODES      0x54u
#define MODEL_NODES          0x58u
#define NODE_BYTES           0xB4u
#define NODE_MESH_INDEX      0x4Cu
#define MESH_BYTES           0x70u
#define MESH_VERTICES        0x30u
#define MESH_NUM_VERTICES    0x48u
#define VERTEX_BYTES         0x0Cu

/* There is deliberately no plausibility ceiling on either count.
 *
 * The obvious pair, a node count that is not a skeleton and a vertex count no allocation could
 * hold, was written and then taken out again. Refusing on them means handing the call back to the
 * engine, and the engine's version of this call is the unbounded copy: a header wild enough to
 * claim four hundred nodes is the last one that should be given to a loop with no bound. The node
 * index is still measured against the count that model states, which is the same test the engine
 * makes, and the vertex count is only ever used through a cap of four. */

/* The fetch routine: the object, its render handle, the assert on a handle that is not there, and
 * the source line the assert pushes. Prologue boundaries are at 0, 1, 3 and 6 by decoding:
 *
 *   55              push ebp                    boundary at 0
 *   8B EC           mov ebp,esp                 boundary at 1
 *   83 EC 14        sub esp,0x14                boundary at 3
 *   8B 45 08        mov eax,[ebp+8]             boundary at 6
 *
 * Six is the first boundary at or past the five bytes a jump needs, and all six bytes are required
 * rather than wildcards, so the two stage rule keeps its honesty test. The pushed source line is
 * what makes the match a proof rather than a prologue that fits everywhere. */
static const uint8_t SIG_CARD_POINTS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14,
    0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC,
    0x8B, 0x4D, 0xFC,
    0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00,        /* mov edx,[ecx+0x9C]   the render handle    */
    0x89, 0x55, 0xEC,
    0x83, 0x7D, 0xEC, 0x00,
    0x75, 0x00,
    0x68, 0x97, 0x08, 0x00, 0x00               /* push 2199, the asserted source line       */
};
static const uint8_t MSK_CARD_POINTS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CARD_POINTS) == sizeof(MSK_CARD_POINTS),
               "the card fetch pattern and its mask are different lengths");

#define CARD_POINTS_PROLOGUE 6u

/* A node index the routine's own test refuses. The test is `cmp ecx,[eax+0x54]` then `jb`, which
 * is unsigned, so no model's node count reaches it; the arm it takes writes nothing into the
 * buffer, loads the float it returns (or nothing, where node_verts has taken that load out) and
 * leaves. */
#define CARD_NODE_REFUSED 0xFFFFFFFFu

/* The registration: the default colour, the asset name it compares against a table of weapon
 * assets, and the walk over that table. The table address is masked out and never read, because
 * this module only needs to know when the six offers are being made, not which colour they get.
 * Prologue boundaries are at 0, 1, 3 and 6 by the same decoding as above. */
static const uint8_t SIG_CARD_REGISTER[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0xC7, 0x45, 0xF8, 0xFF, 0xFF, 0xFF, 0x00,  /* mov [ebp-8],0xFFFFFF   the default colour */
    0x8B, 0x45, 0x08,
    0x8B, 0x48, 0x14,                          /* mov ecx,[eax+0x14]     the object's type  */
    0x83, 0xC1, 0x08,
    0x89, 0x4D, 0xFC,
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x09,
    0x8B, 0x55, 0xF4, 0x83, 0xC2, 0x01, 0x89, 0x55, 0xF4,
    0x8B, 0x45, 0xF4,
    0x83, 0x3C, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x00,  /* cmp [eax*8+the asset table],0       */
    0x74, 0x2C
};
static const uint8_t MSK_CARD_REGISTER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00
};
_Static_assert(sizeof(SIG_CARD_REGISTER) == sizeof(MSK_CARD_REGISTER),
               "the card registration pattern and its mask are different lengths");

#define CARD_REGISTER_PROLOGUE 6u

/* Both prototypes carry their return value, and neither hook makes one of its own. The fetch
 * routine pushes a float on its two refusal arms and nothing on the copy, and whether a caller
 * pops one afterwards depends on whether render_guard's node_verts is in the image. So every path
 * out of the fetch hook ends in a call of the routine itself, and whatever that call leaves in
 * ST(0) is handed back untouched. */
typedef float (__cdecl *card_points_fn_t)(void *obj, uint32_t node, float *out);
typedef void  (__cdecl *card_register_fn_t)(void *obj);

static struct {
    bool               armed;
    detour_t           points;
    detour_t           registration;
    card_points_fn_t   points_original;
    card_register_fn_t register_original;
    uint32_t           bound_count;
    bool               bound_reported;
} cards;

/* ============================================================================================ */

uint32_t character_cards_copy_count(uint32_t mesh_vertices)
{
    return (mesh_vertices < CHARACTER_CARD_VERTICES) ? mesh_vertices : CHARACTER_CARD_VERTICES;
}

bool character_cards_mesh_fits(uint32_t mesh_vertices)
{
    return mesh_vertices <= CHARACTER_CARD_VERTICES;
}

/* ============================================================================================ */

/* The mesh the fetch routine would read, resolved exactly the way it resolves it and refused
 * wherever it refuses. Every number comes off the live heap: the node count out of the model the
 * handle is wearing RIGHT NOW, the mesh index out of that model's own node record and the vertex
 * count out of the mesh. Nothing here is remembered between calls, because the whole failure this
 * bounds is a number that was true when it was written down and is not true now. */
static bool live_node_mesh(const void *obj, uint32_t node, uintptr_t *out_vertices,
                           uint32_t *out_count)
{
    uint32_t  thing = 0;
    uint32_t  model = 0;
    uint32_t  node_count = 0;
    uint32_t  nodes = 0;
    uint32_t  meshes = 0;
    int32_t   mesh_index = 0;
    uint32_t  vertices = 0;
    uint32_t  count = 0;
    uintptr_t record;
    uintptr_t mesh;

    if (obj == NULL ||
        !memory_try_read((uintptr_t)obj + BAPOBJ_THING, &thing, sizeof thing) || thing == 0u ||
        !memory_try_read((uintptr_t)thing + RDTHING_MODEL3, &model, sizeof model) || model == 0u ||
        !memory_try_read((uintptr_t)model + MODEL_NUM_NODES, &node_count, sizeof node_count) ||
        node_count == 0u || node >= node_count ||
        !memory_try_read((uintptr_t)model + MODEL_NODES, &nodes, sizeof nodes) || nodes == 0u) {
        return false;
    }
    record = (uintptr_t)nodes + (uintptr_t)node * NODE_BYTES;
    if (!memory_try_read(record + NODE_MESH_INDEX, &mesh_index, sizeof mesh_index) ||
        mesh_index < 0 ||
        !memory_try_read((uintptr_t)model + MODEL_GEOSET0_MESHES, &meshes, sizeof meshes) ||
        meshes == 0u) {
        return false;
    }
    mesh = (uintptr_t)meshes + (uintptr_t)(uint32_t)mesh_index * MESH_BYTES;
    if (!memory_try_read(mesh + MESH_NUM_VERTICES, &count, sizeof count) || count == 0u ||
        !memory_try_read(mesh + MESH_VERTICES, &vertices, sizeof vertices) || vertices == 0u) {
        return false;
    }
    *out_vertices = (uintptr_t)vertices;
    *out_count = count;
    return true;
}

/* Reported the first time it happens and counted after that. The three numbers are what tells a
 * reader WHICH node the record ended up naming and how far past the buffer the engine was about to
 * write, which is the one measurement the crash it replaces could not leave behind. */
static void report_bound(uint32_t node, uint32_t count)
{
    cards.bound_count++;
    if (cards.bound_reported) {
        return;
    }
    cards.bound_reported = true;
    log_warning("a sabre glow record names node %u, whose mesh has %u vertices where a blade card "
                "has %u. The engine copies the whole mesh into a four vertex buffer, so the tenth "
                "vertex lands on the caller's return address. The copy is bounded here and the "
                "record describes nothing, which is what a rig without blade cards has always "
                "given the engine", (unsigned)node, (unsigned)count,
                (unsigned)CHARACTER_CARD_VERTICES);
}

/* THE BOUND. It answers only for a node whose mesh cannot fit, and everything else, including
 * every read that failed, goes to the engine unchanged: a refusal this module cannot justify is
 * one the engine is better at making than a guess here would be. The vertices are copied as bytes
 * rather than as floats, so nothing here touches the x87 stack the last call hands back. */
static float __cdecl hook_card_points(void *obj, uint32_t node, float *out)
{
    uintptr_t vertices = 0;
    uint32_t  count = 0;
    uint32_t  wanted;
    uint32_t  i;

    if (out == NULL || !live_node_mesh(obj, node, &vertices, &count) ||
        character_cards_mesh_fits(count)) {
        return cards.points_original(obj, node, out);
    }

    report_bound(node, count);
    wanted = character_cards_copy_count(count);
    for (i = 0; i < wanted; ++i) {
        if (!memory_try_read(vertices + (uintptr_t)i * VERTEX_BYTES, out + i * 3u,
                             VERTEX_BYTES)) {
            break;
        }
    }
    return cards.points_original(obj, CARD_NODE_REFUSED, out);
}

/* THE BRACKET. The six offers are the one place in the image where a blade name is asked for as
 * GEOMETRY rather than as a place on the body, and it is the only asker whose answer has to carry
 * a four vertex mesh. Holding character_mount across them is the whole of the discrimination: the
 * swing rows and the muzzle keep the substituted hand, and this asker gets the engine's own answer,
 * which for a rig without blade cards is no record at all. */
static void __cdecl hook_card_register(void *obj)
{
    const bool held = character_mount_hold(true);

    cards.register_original(obj);
    (void)character_mount_hold(held);
}

/* ============================================================================================ */

bool character_cards_install(void)
{
    uintptr_t points_site;
    uintptr_t register_site;

    if (cards.armed) {
        return true;
    }
    points_site = signature_find_detour_target(SIG_CARD_POINTS, MSK_CARD_POINTS,
                                               sizeof SIG_CARD_POINTS, CARD_POINTS_PROLOGUE);
    if (points_site == 0u) {
        log_warning("the card fetch did not resolve, so a sabre glow record left naming a node of "
                    "a model the body no longer wears keeps its unbounded copy");
        return false;
    }
    if (!detour_install(&cards.points, points_site, (const void *)&hook_card_points,
                        CARD_POINTS_PROLOGUE)) {
        log_warning("the card fetch at %08X could not be detoured", (unsigned)points_site);
        return false;
    }
    cards.points_original = (card_points_fn_t)cards.points.original;

    register_site = signature_find_detour_target(SIG_CARD_REGISTER, MSK_CARD_REGISTER,
                                                 sizeof SIG_CARD_REGISTER,
                                                 CARD_REGISTER_PROLOGUE);
    if (register_site == 0u ||
        !detour_install(&cards.registration, register_site, (const void *)&hook_card_register,
                        CARD_REGISTER_PROLOGUE)) {
        /* The bound above stays where it is. There is no removal in this project's chained
         * detours, and it is the half that matters: without the bracket a borrowed rig collects
         * four records that describe nothing, and with the bound they describe nothing HARMLESSLY
         * rather than fatally. */
        log_warning("the sabre card registration did not resolve, so a borrowed rig still collects "
                    "records for blade cards it does not carry; they are bounded but they are "
                    "drawn");
        cards.armed = true;
        log_info("the card fetch at %08X is bounded to %u vertices", (unsigned)points_site,
                 (unsigned)CHARACTER_CARD_VERTICES);
        return true;
    }
    cards.register_original = (card_register_fn_t)cards.registration.original;

    cards.armed = true;
    log_info("the sabre glow cards are held to their four vertex contract: the fetch at %08X is "
             "bounded and the registration at %08X asks the engine's own node names, so a borrowed "
             "rig collects no record for a card it does not carry",
             (unsigned)points_site, (unsigned)register_site);
    return true;
}

bool character_cards_is_armed(void)
{
    return cards.armed;
}

uint32_t character_cards_bound_count(void)
{
    return cards.bound_count;
}
