/* character_buoyancy.c: the swim tick's pin, corrected for the height of the body being worn. */
#include "character_buoyancy.h"

#include "character_nodemap.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The swim tick. The pattern reaches from the prologue past the surface subtraction, because the
 * float the engine pins with is the operand of that subtraction and reading it out of the match is
 * what keeps this module from carrying a second copy of the number. The player record is named four
 * times inside the match and every one of them is a wildcard; the first is read back out and agreed
 * with the other sites that name it. */
static const uint8_t SIG_UPDATE_SWIM[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,              /* push ebp; mov ebp,esp; sub esp,0x10     */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the player record]             */
    0x83, 0xB8, 0xE0, 0x02, 0x00, 0x00, 0x00,        /* cmp dword [eax+0x2E0],0  the probe poly */
    0x75, 0x00,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x91, 0xE0, 0x02, 0x00, 0x00,
    0x33, 0xC0,
    0x66, 0x8B, 0x42, 0x3C,                          /* mov ax,[edx+0x3C]   the surface flags   */
    0x89, 0x45, 0xF8,
    0x8B, 0x4D, 0xF8,
    0x81, 0xE1, 0x00, 0x20, 0x00, 0x00,              /* and ecx,0x2000      the throw off bit   */
    0x85, 0xC9,
    0x74, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the fall entry                     */
    0xE9, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x81, 0xC2, 0x18, 0x01, 0x00, 0x00,              /* add edx,0x118       the position        */
    0x52,
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the water surface delta            */
    0x83, 0xC4, 0x04,
    0xD9, 0x55, 0xFC,
    0xD8, 0x25, 0x00, 0x00, 0x00, 0x00               /* fsub [the float depth]                  */
};
static const uint8_t MSK_UPDATE_SWIM[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_UPDATE_SWIM) == sizeof(MSK_UPDATE_SWIM),
               "the swim tick pattern and its mask are different lengths");

#define OFFSET_SWIM_PLAYER_RECORD  0x07u  /* the operand of the first load, inside the pattern    */
#define OFFSET_SWIM_FLOAT_DEPTH    0x63u  /* the operand of the subtraction, inside the pattern   */

/* The prologue is the same six bytes the other player module site opens with, and five would be
 * inside the `sub esp`. */
#define SWIM_PROLOGUE_SIZE   6u

#define PLAYER_ACTOR         0x0Cu   /* the player's own bapObj                                  */
#define PLAYER_CUR_MODE      0x60u   /* the descriptor the mode runs out of; the state variable   */
#define PLAYER_POS_Z         0x120u  /* the swimmer's height, and what the tick pins              */

#define BAPOBJ_SCALE_Z       0x38u   /* the third of the three scale floats                       */
#define BAPOBJ_THING         0x9Cu

#define RDTHING_MODEL3       0x04u

#define MODEL_GEOSET_MESHES  0x24u   /* the first geoset: how many meshes, then where they are    */
#define MODEL_GEOSET_BASE    0x28u
#define MODEL_NODES          0x58u   /* the skeleton, the same field the name lookup walks        */
#define MESH_BYTES           0x70u
#define MESH_NUM_VERTS       0x48u
#define MESH_CENTRE_Z        0x60u   /* the loader's bounding box, midpoint then half the size    */
#define MESH_HALF_Z          0x6Cu
#define NODE_BYTES           0xB4u
#define NODE_MESH_INDEX      0x4Cu   /* negative when the node carries no geometry                */

/* A mesh the loader left empty keeps its inverted seed, and its vertex count is what says so. The
 * upper bound is the widest mesh the shipped assets carry, an order above it. */
#define MESH_VERTS_MAX       0x8000u

/* The narrowest and widest a worn body may be against the player's own before the measurement is
 * treated as a misread rather than as a body. The shipped roster spans 0.48 to 1.44. */
#define RATIO_MIN            0.1f
#define RATIO_MAX            10.0f

typedef void (__cdecl *update_swim_fn_t)(void);

typedef struct buoyancy_state {
    bool             tried;
    bool             installed;
    bool             armed;
    bool             refused_reported;
    bool             lift_reported;

    detour_t         detour;
    update_swim_fn_t original;

    uintptr_t        player_record;   /* the cell, not the record */
    float            float_depth;     /* read out of the matched subtraction */

    uintptr_t        thing;
    uintptr_t        worn_model;
    float            worn_crown;      /* model units */
    uintptr_t        own_model;
    float            own_crown;       /* model units */
    float            own_scale;       /* what the player's own body is drawn at */
} buoyancy_state_t;

static buoyancy_state_t buoy;

/* ============================================================================================ */

bool character_buoyancy_ratio_is_plausible(float own_height, float worn_height)
{
    float ratio;

    if (!isfinite(own_height) || !isfinite(worn_height) ||
        own_height <= 0.0f || worn_height <= 0.0f) {
        return false;
    }
    ratio = worn_height / own_height;
    return isfinite(ratio) && ratio >= RATIO_MIN && ratio <= RATIO_MAX;
}

float character_buoyancy_lift_for(float depth, float own_height, float worn_height)
{
    float lift;

    if (!isfinite(depth) || depth <= 0.0f) {
        return 0.0f;
    }
    if (!character_buoyancy_ratio_is_plausible(own_height, worn_height)) {
        return 0.0f;
    }
    lift = depth * (1.0f - worn_height / own_height);
    return isfinite(lift) ? lift : 0.0f;
}

/* ============================================================================================ */

/* The bounding box the loader computed for one mesh, as the height of its top over the model's own
 * origin. The two floats are the box midpoint and half its size, both written by the same pass that
 * placed the vertex array, so a mesh with vertices has a box and a mesh without keeps an inverted
 * seed. The vertex count is what tells them apart. */
static bool mesh_top(uintptr_t mesh, float *out)
{
    uint32_t verts = 0;
    float    centre = 0.0f;
    float    half = 0.0f;

    if (!memory_try_read(mesh + MESH_NUM_VERTS, &verts, sizeof verts) ||
        verts == 0u || verts > MESH_VERTS_MAX) {
        return false;
    }
    if (!memory_try_read(mesh + MESH_CENTRE_Z, &centre, sizeof centre) ||
        !memory_try_read(mesh + MESH_HALF_Z, &half, sizeof half)) {
        return false;
    }
    if (!isfinite(centre) || !isfinite(half) || half < 0.0f) {
        return false;
    }
    *out = centre + half;
    return true;
}

float character_buoyancy_crown(uintptr_t model)
{
    uintptr_t nodes = 0;
    uint32_t  node_base = 0;
    uint32_t  mesh_base = 0;
    uint32_t  mesh_count = 0;
    int32_t   head;
    int32_t   mesh_index = 0;
    float     top = 0.0f;

    head = character_nodemap_find(model, "head");
    if (head < 0) {
        return 0.0f;
    }
    /* The node array and the mesh array are both reached through the model's own header, and both
     * are read over their whole length before anything indexes into them. */
    if (!memory_try_read(model + MODEL_NODES, &node_base, sizeof node_base) || node_base == 0u) {
        return 0.0f;
    }
    nodes = (uintptr_t)node_base;
    if (!memory_try_readable(nodes + (uintptr_t)head * NODE_BYTES, NODE_BYTES) ||
        !memory_try_read(nodes + (uintptr_t)head * NODE_BYTES + NODE_MESH_INDEX,
                         &mesh_index, sizeof mesh_index) ||
        mesh_index < 0) {
        return 0.0f;
    }
    if (!memory_try_read(model + MODEL_GEOSET_MESHES, &mesh_count, sizeof mesh_count) ||
        !memory_try_read(model + MODEL_GEOSET_BASE, &mesh_base, sizeof mesh_base) ||
        mesh_base == 0u || mesh_count == 0u || (uint32_t)mesh_index >= mesh_count) {
        return 0.0f;
    }
    if (!memory_try_readable((uintptr_t)mesh_base, (size_t)mesh_count * MESH_BYTES)) {
        return 0.0f;
    }
    if (!mesh_top((uintptr_t)mesh_base + (uintptr_t)mesh_index * MESH_BYTES, &top) || top <= 0.0f) {
        return 0.0f;
    }
    return top;
}

/* ============================================================================================ */

static bool player_block(uintptr_t *out)
{
    uint32_t block = 0;

    if (buoy.player_record == 0u ||
        !memory_try_read(buoy.player_record, &block, sizeof block) || block == 0u) {
        return false;
    }
    *out = (uintptr_t)block;
    return true;
}

static bool player_object(uintptr_t block, uintptr_t *out)
{
    uint32_t obj = 0;

    if (!memory_try_read(block + PLAYER_ACTOR, &obj, sizeof obj) || obj == 0u) {
        return false;
    }
    *out = (uintptr_t)obj;
    return true;
}

/* The model the render handle is wearing right now, asked of the engine rather than remembered. A
 * level change frees the handle and the allocator is free to hand the same address back, so the
 * model is the one thing that cannot come to agree by accident. */
static uintptr_t worn_model_of(uintptr_t thing)
{
    uint32_t model = 0;

    if (thing == 0u || !memory_try_read(thing + RDTHING_MODEL3, &model, sizeof model)) {
        return 0u;
    }
    return (uintptr_t)model;
}

static bool live_scale(uintptr_t block, float *out)
{
    uintptr_t obj = 0;

    if (!player_object(block, &obj) ||
        !memory_try_read(obj + BAPOBJ_SCALE_Z, out, sizeof *out) ||
        !isfinite(*out) || *out <= 0.0f) {
        return false;
    }
    return true;
}

/* What the swimmer has to be lifted by after the engine has pinned him, computed from live state
 * every substep rather than cached: the scale is written by the swap a few calls after this module
 * is armed, and a remembered copy of it could only be right by timing. */
static float live_lift(uintptr_t block)
{
    float scale = 0.0f;
    float own;
    float worn;
    float lift;

    if (!buoy.armed || buoy.own_scale <= 0.0f) {
        return 0.0f;
    }
    if (worn_model_of(buoy.thing) != buoy.worn_model) {
        return 0.0f;                       /* the handle no longer wears what was measured */
    }
    if (!live_scale(block, &scale)) {
        return 0.0f;
    }
    own = buoy.own_crown * buoy.own_scale;
    worn = buoy.worn_crown * scale;
    if (!character_buoyancy_ratio_is_plausible(own, worn)) {
        if (!buoy.refused_reported) {
            buoy.refused_reported = true;
            log_warning("the worn body measures %d/1000 of a unit against the player's own %d/1000,"
                        " which is not a body, so the waterline is left where the engine puts it",
                        (int)(worn * 1000.0f), (int)(own * 1000.0f));
        }
        return 0.0f;
    }
    lift = character_buoyancy_lift_for(buoy.float_depth, own, worn);

    /* Reported here rather than at the arm, because the scale the worn body is drawn at is written
     * a few calls AFTER this module is armed and the number is only true once it has been. */
    if (!buoy.lift_reported && lift != 0.0f) {
        buoy.lift_reported = true;
        log_info("swimming in a borrowed body: it stands %d/1000 of a unit to the crown against the"
                 " player's own %d/1000, so the swimmer is floated %d/1000 above the engine's pin "
                 "and shows the same share of himself over the water",
                 (int)(worn * 1000.0f), (int)(own * 1000.0f), (int)(lift * 1000.0f));
    }
    return lift;
}

/* The correction, and why it is added after the tick rather than built into the pin.
 *
 * The tick pins the swimmer to the surface and then asks, with that position, whether there is a
 * bank in front of him low enough to climb out on. Moving the pin would move the answer and could
 * take the only exit off a body of water. Adding the lift afterwards leaves every test inside the
 * tick standing on the engine's own geometry, because the pin is absolute: the next substep writes
 * the position again from the water surface, whatever this one left behind.
 *
 * The mode descriptor is read on both sides of the call and the lift is applied only when it is
 * unchanged. Every way the tick can leave the water replaces that pointer, and a mode that is no
 * longer swimming is one whose height is not ours to move: being thrown off, losing the water and
 * climbing out all end in another mode, and the last of them has already launched an arc whose
 * height is its own. */
static void __cdecl hook_update_swim(void)
{
    uintptr_t block = 0;
    uint32_t  mode_before = 0;
    uint32_t  mode_after = 0;
    float     lift;
    float     z = 0.0f;

    if (!buoy.armed || !player_block(&block)) {
        buoy.original();
        return;
    }
    lift = live_lift(block);
    if (lift == 0.0f ||
        !memory_try_read(block + PLAYER_CUR_MODE, &mode_before, sizeof mode_before) ||
        mode_before == 0u) {
        buoy.original();
        return;
    }

    buoy.original();

    if (!memory_try_read(block + PLAYER_CUR_MODE, &mode_after, sizeof mode_after) ||
        mode_after != mode_before) {
        return;
    }
    if (!memory_try_read(block + PLAYER_POS_Z, &z, sizeof z) || !isfinite(z)) {
        return;
    }
    *(volatile float *)(block + PLAYER_POS_Z) = z + lift;
}

/* ============================================================================================ */

bool character_buoyancy_install(void)
{
    uintptr_t site;
    uint32_t  record = 0;
    uint32_t  depth_cell = 0;
    float     depth = 0.0f;

    if (buoy.tried) {
        return buoy.installed;
    }
    buoy.tried = true;

    site = signature_find_detour_target(SIG_UPDATE_SWIM, MSK_UPDATE_SWIM, sizeof SIG_UPDATE_SWIM,
                                        SWIM_PROLOGUE_SIZE);
    if (site == 0u) {
        log_warning("the swim tick did not resolve, so a borrowed body floats at the depth the "
                    "player's own body was measured for");
        return false;
    }
    /* The record comes out of THIS site's own operand and is then held against the cell the guard
     * funnel handed over. A disagreement means one of the two patterns matched a function it was
     * not cut from, and taking the address from a single site would have hidden exactly that. */
    if (!memory_read_u32(site + OFFSET_SWIM_PLAYER_RECORD, &record) || record == 0u) {
        log_warning("the swim tick at %08X does not name the player record", (unsigned)site);
        return false;
    }
    if (buoy.player_record != 0u && buoy.player_record != (uintptr_t)record) {
        log_warning("the swim tick names the player record at %08X while another site named %08X",
                    (unsigned)record, (unsigned)buoy.player_record);
        return false;
    }
    buoy.player_record = (uintptr_t)record;

    /* The depth comes out of the matched subtraction rather than out of a constant here, so the
     * correction is expressed against the number the engine actually pins with. It also survives
     * the recompile, where the site moves and the cell is a different address. */
    if (!memory_read_u32(site + OFFSET_SWIM_FLOAT_DEPTH, &depth_cell) || depth_cell == 0u ||
        !memory_try_read((uintptr_t)depth_cell, &depth, sizeof depth) ||
        !isfinite(depth) || depth <= 0.0f) {
        log_warning("the swim tick at %08X does not name a float depth", (unsigned)site);
        return false;
    }
    if (!detour_install(&buoy.detour, site, (const void *)&hook_update_swim, SWIM_PROLOGUE_SIZE)) {
        log_warning("the swim tick at %08X could not be detoured", (unsigned)site);
        return false;
    }
    buoy.original = (update_swim_fn_t)buoy.detour.original;
    buoy.float_depth = depth;
    buoy.installed = true;
    log_info("the swim tick at %08X pins the body %d/1000 of a unit under the surface, and a "
             "borrowed body of another height is now floated by its own share of that",
             (unsigned)site, (int)(depth * 1000.0f));
    return true;
}

void character_buoyancy_bind_player_record(uintptr_t player_record_cell)
{
    if (buoy.player_record != 0u && player_record_cell != 0u &&
        buoy.player_record != player_record_cell) {
        log_warning("the waterline was given the player record at %08X while the swim tick named "
                    "%08X, so it is left where the engine puts it",
                    (unsigned)player_record_cell, (unsigned)buoy.player_record);
        buoy.armed = false;
        return;
    }
    buoy.player_record = player_record_cell;
}

bool character_buoyancy_is_armed(void)
{
    return buoy.armed;
}

void character_buoyancy_disarm(void)
{
    /* The player's own measurement is kept. A second borrow made without going home first arrives
     * at the arm below with somebody else's body already on the handle, and then this pair is the
     * only record of what the player's own body measures. It is re-taken the moment the handle
     * carries that body again. */
    buoy.armed = false;
    buoy.refused_reported = false;
    buoy.lift_reported = false;
    buoy.thing = 0u;
    buoy.worn_model = 0u;
    buoy.worn_crown = 0.0f;
}

bool character_buoyancy_arm(uintptr_t thing, uintptr_t own_model, uintptr_t worn_model)
{
    float own_crown;
    float worn_crown;
    float scale = 0.0f;
    uintptr_t block = 0;

    character_buoyancy_disarm();
    if (!buoy.installed || thing == 0u || own_model == 0u || worn_model == 0u) {
        return false;
    }
    own_crown = character_buoyancy_crown(own_model);
    worn_crown = character_buoyancy_crown(worn_model);
    if (own_crown <= 0.0f || worn_crown <= 0.0f) {
        log_warning("one of the two bodies carries no head mesh to measure, so the waterline is "
                    "left where the engine puts it");
        return false;
    }

    /* The scale is re-measured whenever the handle still wears the player's own body, which is the
     * only moment it can be read, and the reading is against the LIVE model rather than against a
     * remembered pointer. A carried reading belongs to the body it was taken from, so naming a
     * different body throws it away rather than lending it to the new one. */
    if (own_model != buoy.own_model) {
        buoy.own_scale = 0.0f;
    }
    buoy.own_model = own_model;
    buoy.own_crown = own_crown;
    if (worn_model_of(thing) == own_model && player_block(&block) && live_scale(block, &scale)) {
        buoy.own_scale = scale;
    }
    if (buoy.own_scale <= 0.0f) {
        log_warning("the scale the player's own body is drawn at could not be read, so the "
                    "waterline is left where the engine puts it");
        return false;
    }

    buoy.thing = thing;
    buoy.worn_model = worn_model;
    buoy.worn_crown = worn_crown;
    buoy.armed = true;
    log_info("the two bodies measure %d/1000 and %d/1000 of a unit to the crown, drawn at the "
             "player's own scale of %d/1000; what the waterline becomes is reported the first time "
             "he swims, because the scale the borrowed body is drawn at is written after this",
             (int)(own_crown * 1000.0f), (int)(worn_crown * 1000.0f),
             (int)(buoy.own_scale * 1000.0f));
    return true;
}
