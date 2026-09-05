/* two_sided_faces.c: the software backface cull word, and the one detour that clears it for a
 * body with a hole in it.
 *
 * THE SEAM. Lifted out of view_distance_fix.c, which was past the hard limit. This is one whole
 * responsibility and it has nothing to do with how far the world is drawn: one engine byte, one
 * detour on rdThing_Draw, a per frame budget, and the predicate that decides what counts as
 * dismembered. All of the evidence that explains those came across with them.
 */
#include "two_sided_faces.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

/* The cull word's address is read out of the `mov bl,[imm32]` four bytes in front of the resolved
 * anchor rather than written down. See that site's own pattern comment. */
#define OFFSET_CULL_WORD_ADDRESS (-4)

/* Engine field offsets. */
#define THING_MODEL3        0x004
#define THING_NODE_HIDDEN   0x028   /* int32 per node */
#define THING_MESH_HIDDEN   0x02C   /* int32 per MESH (0x414C1C) */
#define MODEL3_NODE_COUNT   0x054
#define MAX_PLAUSIBLE_NODES 1024u

/* rdThing_Draw RETURNS A VALUE and this typedef said `void` until 2026-08-07. Three return paths
 * in the retail image: `xor eax,eax` at 0x0040FFD0, `mov eax,1` at 0x00410094 and
 * `mov eax,[ebx+0x54]` at 0x00410126. bapthing_dispatch 0x00417930 is a seven-way jump table whose
 * case 3 is `call 0x40fe70` followed straight by the epilogue, no `mov eax` in between, while
 * every other case does an explicit `xor eax,eax`. So EAX is the contract, and it reaches FOUR
 * callers: 0x004116F1 (bapobj_drawAll, where it becomes `visCode`), 0x00414A2A, 0x00458904 (shot.c)
 * and 0x0045C3B9.
 *
 * With the void typedef the compiler emitted `mov eax,[cull_word]` immediately after the call, so
 * every caller read a POINTER as the visibility code. MEASURED consequence: at the shadow gate that
 * is merely always-non-zero and harmless, but through shot.c the blaster scorch decal was never
 * stamped at all, 0 of them with the hook active, 5 in the same test without it. The direction of
 * the damage at one call site says nothing about the others. */
typedef int32_t (__cdecl *thing_draw_fn_t)(void *thing, void *matrix);

typedef struct two_sided_state {
    detour_t thing_draw_detour;
    uint8_t *cull_word;
    int      count_this_frame;
    bool     enabled;
    int      max_per_frame;
} two_sided_state_t;

static two_sided_state_t two_sided_state;

/* ============================================================================================
 * Two-sided faces, but ONLY on dismembered bodies
 *
 * Drawing two-sided globally would be the simpler patch (two bytes) but it is the wrong default:
 * the frame pools g_queuePoly (4096 records) and g_queueVert (8192 vertices) are GLOBAL, not per
 * asset. The backface pass throws away roughly half of everything today, which is exactly what
 * keeps the shipped game with its ~36 simultaneous actors of ~165 faces below the limit. If the
 * vertex buffer overflows, bapdraw_reserveVerts returns NULL and rdMesh_draw aborts silently: a
 * WHOLE MODEL disappears.
 *
 * So per object, and SHIPPED OFF. The marking needs no bookkeeping of its own, a thing with a set
 * entry in pMeshHidden has a hole, and that is the severed piece. It is off by default because
 * this feature never once ran in a released build: dev_overlay hooks rdThing_Draw for giant and
 * tiny player and loads first, so the plain signature form the site was first declared in found
 * nothing and the warning that said so went unread for months. The first session in which it
 * did run drew a beam across the level, and one evening of testing is not enough to put it back
 * on by default.
 *
 * WARNING: the word has to be RESET at the end. rdMesh_draw has a second caller (0x456E17 in
 * shot_drawAll) which would otherwise see the value of the last drawn thing.
 * HONEST: this does not close the hole, it softens it. A severed limb is not a cut mesh,
 * bapobj_detachNode only hides a node, there is no cap and no cut mesh. Two-sided means you see
 * the inside of the far side, lit with the front normal, i.e. flat.
 * ============================================================================================ */

/* A thing has a hole when an entry in pMeshHidden is set, and NOT when one in pNodeHidden is.
 *
 * It tested both until the beam. pNodeHidden is ordinary engine bookkeeping with nothing to do
 * with dismemberment: Plr_RebindWeaponModel calls bapobj_hideNodeChildren on the weapon mount,
 * node name id 7, which is the HAND, once on spawn and again on every weapon change, and menu.c
 * does the same to the inventory preview. Every armed actor in the game therefore carries a set
 * entry in +0x28 from the moment it spawns, this predicate called all of them severed, and the
 * player was drawn with backface culling off for the whole session. On screen that is a long
 * bright sliver out of Obi-Wan's hand that follows him when he walks.
 *
 * pMeshHidden is reached only through bapobj_hideMeshesBelow, which only bapobj_detachNode and
 * the reattach beside it call, so a set entry there does mean a cut. The narrowing costs the
 * corpse: detachNode marks the PIECE through +0x2C and the body it came off through +0x28, so
 * the piece keeps its two sides and the body loses them. That is the half worth having, and the
 * half that cannot be mistaken for a holstered blaster.
 * Both fields are allocated by rdThing_SetModel with numNodes*4 and zeroed; maxMeshIdx < numNodes
 * holds in 265/265 measured models, so numNodes bounds the mesh array too. */
static bool thing_has_hole(const void *thing)
{
    const char *record = (const char *)thing;
    const char *model;
    const char *hidden;
    uint32_t    node_count;
    uint32_t    index;

    /* Every read below is the faulting form rather than the asking one, and that is a performance
     * decision with a measurable size. This function runs for every thing the engine draws, and it
     * makes four of these reads each time; at three dozen actors that is a couple of hundred system
     * calls per frame for nothing but permission to look. The guarantee is unchanged: a bad pointer
     * still refuses rather than killing the process. */
    if (record == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)(record + THING_MODEL3), &model, sizeof(model)) ||
        model == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)(model + MODEL3_NODE_COUNT), &node_count, sizeof(node_count)) ||
        node_count > MAX_PLAUSIBLE_NODES) {
        return false;                              /* plausibility, never read blind */
    }

    if (memory_try_read((uintptr_t)(record + THING_MESH_HIDDEN), &hidden, sizeof(hidden)) &&
        hidden != NULL &&
        memory_try_readable((uintptr_t)hidden, node_count * sizeof(uint32_t))) {
        for (index = 0; index < node_count; ++index) {
            if (((const uint32_t *)hidden)[index] != 0) {
                return true;
            }
        }
    }

    return false;
}

static int32_t __cdecl hook_thing_draw(void *thing, void *matrix)
{
    thing_draw_fn_t original = (thing_draw_fn_t)two_sided_state.thing_draw_detour.original;
    uint8_t         saved;
    int32_t         result;

    if (!two_sided_state.enabled || two_sided_state.cull_word == NULL) {
        return original(thing, matrix);
    }

    saved = *two_sided_state.cull_word;
    if (two_sided_state.count_this_frame < two_sided_state.max_per_frame &&
        thing_has_hole(thing)) {
        ++two_sided_state.count_this_frame;
        *two_sided_state.cull_word = (uint8_t)(saved & ~1u);   /* clear bit 0 = draw backfaces */
    }

    result = original(thing, matrix);              /* KEEP IT: it is the caller's visibility code */

    *two_sided_state.cull_word = saved;            /* ALWAYS back, see shot_drawAll */
    return result;
}

void two_sided_faces_begin_frame(void)
{
    two_sided_state.count_this_frame = 0;
}

void two_sided_faces_install(uintptr_t cull_site, uintptr_t draw_site, bool enabled,
                             int max_per_frame)
{
    uint32_t cull_address;

    two_sided_state.enabled       = enabled;
    two_sided_state.max_per_frame = max_per_frame;

    if (!two_sided_state.enabled) {
        log_info("TwoSidedSevered=0");
        return;
    }
    if (cull_site == 0) {
        log_warning("mesh_cull_word did not resolve, dismembered bodies stay see-through");
        return;
    }
    if (!memory_read_u32((uintptr_t)((intptr_t)cull_site + OFFSET_CULL_WORD_ADDRESS),
                         &cull_address) ||
        !memory_is_inside_image(cull_address, sizeof(uint8_t))) {
        log_warning("the cull word %08X is outside the image, refused", (unsigned)cull_address);
        return;
    }
    if (draw_site == 0) {
        log_warning("thing_draw did not resolve, two-sided is OFF (a cull word with no writer "
                    "would be worse than none)");
        return;
    }

    two_sided_state.cull_word = (uint8_t *)(uintptr_t)cull_address;
    if (detour_install(&two_sided_state.thing_draw_detour, draw_site,
                       (const void *)hook_thing_draw, THING_DRAW_PROLOGUE_SIZE)) {
        log_info("dismembered bodies are drawn two-sided (cull word %08X, hook %08X), at most %d "
                 "per frame. The marking uses pMeshHidden alone, no extra state.",
                 (unsigned)cull_address, (unsigned)draw_site, two_sided_state.max_per_frame);
    } else {
        two_sided_state.cull_word = NULL;
        log_error("the rdThing_Draw detour at %08X failed, two-sided is OFF", (unsigned)draw_site);
    }
}
