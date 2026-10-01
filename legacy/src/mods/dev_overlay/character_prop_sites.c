/* character_prop_sites.c: the seven patterns the borrowed weapon stands on.
 *
 * Not one address is written into a pattern. Every address this module hands back is READ out of
 * an operand at a site that proves what the operand is for, because three builds of this engine
 * ship in one installation and five of these seven sit at different addresses in the recompile.
 *
 * Two of the seven are DELIBERATELY not unique and are checked by AGREEMENT instead: the pose gate
 * must have both of its sites naming one frame counter and one pose builder, and the weapon
 * configuration both of its sites naming one table. One site could be a coincidence; two agreeing
 * on both operands cannot be.
 */
#include "character_prop_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>

/* The pose gate, at the two sites that rebuild a pose before querying a node. Neither the frame
 * counter nor the pose builder is named where a shorter pattern could reach, and both sites have
 * to agree on BOTH operands: one site could be a coincidence, two agreeing cannot. */
static const uint8_t SIG_PROP_POSE_GATE[] = {
    0x8B, 0x4D, 0xF8,                          /* mov ecx,[ebp-8]      the object          */
    0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00,        /* mov edx,[ecx+0x9C]   its render handle   */
    0x8B, 0x42, 0x1C,                          /* mov eax,[edx+0x1C]   the pose stamp      */
    0x3B, 0x05, 0x00, 0x00, 0x00, 0x00,        /* cmp eax,[the frame counter]              */
    0x74, 0x00,                                /* je  past the rebuild                     */
    0x8D, 0x4D, 0xC8, 0x51, 0x8B, 0x55, 0xF8, 0x8B, 0x82, 0x9C, 0x00, 0x00, 0x00, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call the pose builder                    */
    0x83, 0xC4, 0x08
};
static const uint8_t MSK_PROP_POSE_GATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_PROP_POSE_GATE) == sizeof(MSK_PROP_POSE_GATE),
               "the pose gate pattern and its mask are different lengths");

/* The node name lookup, entered for its TABLE rather than as a function: a weapon row names its
 * node by id, and an id means nothing without the strings the engine compares against. */
static const uint8_t SIG_PROP_NAME_TABLE[] = {
    0x8B, 0x45, 0x0C,                          /* mov eax,[ebp+0x0C]   the name id         */
    0x8B, 0x0C, 0x85, 0x00, 0x00, 0x00, 0x00,  /* mov ecx,[eax*4+the name table]           */
    0x89, 0x4D, 0xE4, 0x8B, 0x55, 0xF0, 0x89, 0x55, 0xE0, 0x8B, 0x45, 0xE0,
    0x8A, 0x08,                                /* mov cl,[eax]         the node's own name */
    0x88, 0x4D, 0xDF, 0x8B, 0x55, 0xE4, 0x3A, 0x0A, 0x75, 0x00
};
static const uint8_t MSK_PROP_NAME_TABLE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
_Static_assert(sizeof(SIG_PROP_NAME_TABLE) == sizeof(MSK_PROP_NAME_TABLE),
               "the name table pattern and its mask are different lengths");

/* rdThing_New, entered for the SIZE of a render handle and for the initialiser it calls. What may
 * not be guessed is how many bytes the engine treats a handle as, and that is the immediate. */
static const uint8_t SIG_PROP_THING_NEW[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x68, 0x00, 0x00, 0x00, 0x00,              /* push the handle size                     */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call the allocator                       */
    0x83, 0xC4, 0x04, 0x89, 0x45, 0xFC, 0x83, 0x7D, 0xFC, 0x00,
    0x75, 0x04, 0x33, 0xC0, 0xEB, 0x00,
    0x8B, 0x45, 0x08, 0x50, 0x8B, 0x4D, 0xFC, 0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00               /* call the initialiser                     */
};
static const uint8_t MSK_PROP_THING_NEW[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_PROP_THING_NEW) == sizeof(MSK_PROP_THING_NEW),
               "the render handle allocation pattern and its mask are different lengths");

/* rdThing_SetModel: the type, the model and the geoset override. No wildcard, so no mask. */
static const uint8_t SIG_PROP_SET_MODEL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x57, 0x8B, 0x45, 0x08,
    0xC7, 0x00, 0x01, 0x00, 0x00, 0x00,        /* mov [eax],1          a model handle      */
    0x8B, 0x4D, 0x08, 0x8B, 0x55, 0x0C, 0x89, 0x51, 0x04, 0x8B, 0x45, 0x08,
    0xC7, 0x80, 0x34, 0x01, 0x00, 0x00,        /* mov [eax+0x134],-1   no geoset override  */
    0xFF, 0xFF, 0xFF, 0xFF
};

/* rdThing_freeArrays, so a second bind cannot leak the first bind's arrays. Its own null test on
 * the puppet makes it safe on a handle that never had one. */
static const uint8_t SIG_PROP_FREE_ARRAYS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x45, 0x08, 0x8B, 0x08,
    0x89, 0x4D, 0xF8, 0x83, 0x7D, 0xF8, 0x01, 0x74, 0x05,
    0xE9, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x55, 0x08,
    0x83, 0x7A, 0x20, 0x00,                    /* cmp [edx+0x20],0     the joint matrices  */
    0x74, 0x19
};
static const uint8_t MSK_PROP_FREE_ARRAYS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_PROP_FREE_ARRAYS) == sizeof(MSK_PROP_FREE_ARRAYS),
               "the array release pattern and its mask are different lengths");

/* The render handle dispatcher, the entry that takes a handle and a matrix. Used as a function,
 * never detoured; going through it also inherits its test that the render module is up.
 *
 * It is called, not hulled, but its head is hulled elsewhere: diagnostics puts its x87 tracer
 * over the first eleven bytes. A pattern matched from the head finds nothing once that DLL has
 * loaded first, so the head is declared and the search anchors on the tail; calling a head
 * another module has detoured goes through that hook and on, like any other call. */
static const uint8_t SIG_PROP_THING_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,  /* cmp [the render module gate],0           */
    0x75, 0x04, 0x33, 0xC0, 0xEB, 0x00, 0x8B, 0x45, 0x08, 0x8B, 0x08, 0x89, 0x4D, 0xFC,
    0x83, 0x7D, 0xFC, 0x06,                    /* cmp [ebp-4],6        the handle type     */
    0x77, 0x00, 0x8B, 0x55, 0xFC,
    0xFF, 0x24, 0x95, 0x00, 0x00, 0x00, 0x00   /* jmp [edx*4+the jump table]               */
};
static const uint8_t MSK_PROP_THING_DRAW[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_PROP_THING_DRAW) == sizeof(MSK_PROP_THING_DRAW),
               "the render handle dispatch pattern and its mask are different lengths");
#define PROP_THING_DRAW_PROLOGUE 11u   /* push ebp; mov ebp,esp; push ecx; cmp [gate],0 */

/* The weapon configuration, read the way the equip commit and the fire path both read it. Its
 * first word is the name id of the node that IS the weapon, which is what this module shows and
 * what the muzzle query asks for. Both sites must name one table. */
static const uint8_t SIG_PROP_WEAPON_CFG[] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x82, 0x84, 0x00, 0x00, 0x00,        /* mov eax,[edx+0x84]   the weapon slot     */
    0x6B, 0xC0, 0x18,                          /* imul eax,0x18        the row size        */
    0x8B, 0x88, 0x00, 0x00, 0x00, 0x00,        /* mov ecx,[eax+the table]                  */
    0x51, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x42, 0x0C, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_PROP_WEAPON_CFG[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_PROP_WEAPON_CFG) == sizeof(MSK_PROP_WEAPON_CFG),
               "the weapon configuration pattern and its mask are different lengths");

#define POSE_GATE_SITES        2u
#define OFFSET_FRAME_COUNTER   14u
#define OFFSET_BUILD_JOINTS    34u   /* the call opcode, not its displacement */
#define OFFSET_NAME_TABLE       6u
#define OFFSET_HANDLE_SIZE      5u
#define OFFSET_HANDLE_INIT     38u
#define WEAPON_CFG_SITES        2u
#define OFFSET_WEAPON_CFG      17u

/* ============================================================================================ */

/* Both sites of the pose gate, agreeing on the counter and on the builder. */
static bool resolve_pose_gate(character_prop_sites_t *out)
{
    uintptr_t site[POSE_GATE_SITES];
    size_t    hits;
    size_t    i;

    hits = signature_count_matches(SIG_PROP_POSE_GATE, MSK_PROP_POSE_GATE,
                                   sizeof SIG_PROP_POSE_GATE, site, POSE_GATE_SITES);
    if (hits != POSE_GATE_SITES) {
        log_warning("the pose gate matched %u times and not %u, so the player cannot be given his "
                    "own weapon on a borrowed body", (unsigned)hits, POSE_GATE_SITES);
        return false;
    }
    for (i = 0; i < POSE_GATE_SITES; ++i) {
        uint32_t  counter = 0;
        uintptr_t builder = 0;

        if (!memory_read_u32(site[i] + OFFSET_FRAME_COUNTER, &counter) || counter == 0u ||
            !patch_read_call_target(site[i] + OFFSET_BUILD_JOINTS, &builder) || builder == 0) {
            log_warning("the pose gate operands at %08X could not be read", (unsigned)site[i]);
            return false;
        }
        if (i == 0) {
            out->frame_counter = (uintptr_t)counter;
            out->build_joints = (rd_build_joints_fn_t)builder;
        } else if (out->frame_counter != (uintptr_t)counter ||
                   (uintptr_t)out->build_joints != builder) {
            log_warning("the two pose gates disagree, %08X against %08X, so the pattern found "
                        "something else", (unsigned)out->frame_counter, (unsigned)counter);
            return false;
        }
    }
    return true;
}

/* Both sites of the weapon configuration, agreeing on the table. */
static bool resolve_weapon_cfg(character_prop_sites_t *out)
{
    uintptr_t site[WEAPON_CFG_SITES];
    uint32_t  first = 0;
    uint32_t  second = 0;
    size_t    hits;

    hits = signature_count_matches(SIG_PROP_WEAPON_CFG, MSK_PROP_WEAPON_CFG,
                                   sizeof SIG_PROP_WEAPON_CFG, site, WEAPON_CFG_SITES);
    if (hits != WEAPON_CFG_SITES ||
        !memory_read_u32(site[0] + OFFSET_WEAPON_CFG, &first) ||
        !memory_read_u32(site[1] + OFFSET_WEAPON_CFG, &second) || first != second || first == 0u) {
        log_warning("the weapon configuration did not resolve on %u agreeing sites, so which node "
                    "is the weapon cannot be read", WEAPON_CFG_SITES);
        return false;
    }
    out->weapon_cfg = (uintptr_t)first;
    return true;
}

bool character_prop_sites_resolve(character_prop_sites_t *out)
{
    uintptr_t found;
    uint32_t  table = 0;
    uint32_t  size = 0;

    out->frame_counter = 0;
    out->name_table = 0;
    out->weapon_cfg = 0;
    out->thing_init = NULL;
    out->set_model = NULL;
    out->free_arrays = NULL;
    out->build_joints = NULL;
    out->thing_draw = NULL;

    if (!resolve_pose_gate(out)) {
        return false;
    }

    found = signature_find_unique(SIG_PROP_NAME_TABLE, MSK_PROP_NAME_TABLE,
                                  sizeof SIG_PROP_NAME_TABLE);
    if (found == 0 || !memory_read_u32(found + OFFSET_NAME_TABLE, &table) || table == 0u) {
        log_warning("the node name table did not resolve, so a weapon row's node id means nothing");
        return false;
    }
    out->name_table = (uintptr_t)table;

    found = signature_find_unique(SIG_PROP_THING_NEW, MSK_PROP_THING_NEW,
                                  sizeof SIG_PROP_THING_NEW);
    if (found == 0 || !memory_read_u32(found + OFFSET_HANDLE_SIZE, &size) ||
        !patch_read_call_target(found + OFFSET_HANDLE_INIT, (uintptr_t *)&out->thing_init)) {
        log_warning("the render handle allocation did not resolve, so no second handle is made");
        return false;
    }
    if (size == 0u || size > RDTHING_BLOCK_BYTES) {
        log_warning("this build's render handle is %u bytes and the block here holds %u, so a "
                    "second handle would be written past its end", (unsigned)size,
                    (unsigned)RDTHING_BLOCK_BYTES);
        return false;
    }

    out->set_model = (rd_set_model_fn_t)signature_find_unique(SIG_PROP_SET_MODEL, NULL,
                                                              sizeof SIG_PROP_SET_MODEL);
    out->free_arrays = (rd_free_arrays_fn_t)signature_find_unique(
        SIG_PROP_FREE_ARRAYS, MSK_PROP_FREE_ARRAYS, sizeof SIG_PROP_FREE_ARRAYS);
    out->thing_draw = (rd_thing_draw_fn_t)signature_find_detour_target(
        SIG_PROP_THING_DRAW, MSK_PROP_THING_DRAW, sizeof SIG_PROP_THING_DRAW,
        PROP_THING_DRAW_PROLOGUE);
    if (out->set_model == NULL || out->free_arrays == NULL || out->thing_draw == NULL) {
        log_warning("the model bind, the array release and the handle dispatch did not all "
                    "resolve, so no weapon is drawn");
        return false;
    }

    if (!resolve_weapon_cfg(out)) {
        return false;
    }

    log_info("the borrowed weapon has its entry points: counter %08X, pose %08X, names %08X, "
             "bind %08X, draw %08X, weapons %08X, and a %u byte handle",
             (unsigned)out->frame_counter, (unsigned)(uintptr_t)out->build_joints,
             (unsigned)out->name_table, (unsigned)(uintptr_t)out->set_model,
             (unsigned)(uintptr_t)out->thing_draw, (unsigned)out->weapon_cfg, (unsigned)size);
    return true;
}
