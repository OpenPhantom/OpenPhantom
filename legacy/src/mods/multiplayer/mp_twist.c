/* mp_twist.c: read and write the per-node rotation array of a body's render thing, and draw it
 * between two substeps instead of stepping it once per substep.
 *
 * SIZE NOTE: a little over 600 lines, one subject in two halves, the substep write and the frame
 * draw, which share the pair they hand each other; most of the length is the reasoning above each
 * half.
 *
 * Nothing in the simulation half calls the engine. The node setters are three loads and a store
 * each, and the array they store into is the same one the pose build reads, so the module reads
 * and writes the array directly and checks the same bound the setters check: the node index
 * against the model's node count, whose top two bits carry something the animation path never
 * looks at.
 *
 * ==============================================================================================
 * The drawn half
 *
 * The rotations are a simulation quantity, one value per substep, and until this file had a
 * second half they were also the DRAWN value. The pose build multiplies each node's euler in
 * after the animation blend, so whatever stands in the array is exactly what appears; and the
 * root node turns the whole body. At a hundred frames a second the far player therefore held one
 * orientation for three frames and then jumped, thirty two times a second, while everything else
 * about him moved at the frame rate: his position and heading are interpolated by the engine's
 * own draw, and his clip is rebuilt every frame. Only the rotations stepped, and the whole body
 * turning in steps reads as the animation stuttering.
 *
 * So the pair of the previous substep's value and this one's is kept, and the mixture is written
 * once per rendered frame on the engine's own weight. The weight is not invented here: the world
 * draw reads it out of a cell at the top of its own frame set-up, together with the two cells
 * that choose between its live and its frozen form, and those three are what the cell table hands
 * over. Writing at the top of that function is what makes the value land in the SAME frame; at
 * the end of a frame it would land one frame late.
 *
 * The collapse, which is not optional. When a substep produces no new value the pair must be
 * brought to one value before any frame is drawn from it, or the body sweeps the same few degrees
 * from end to end thirty two times a second while the weight runs from zero to one, which is
 * worse than the staircase it replaces. It is arranged so that it cannot be forgotten: opening a
 * substep carries the current values into the previous ones, and only a value actually produced
 * in that substep parts them again. The open is the first statement of the puppet's window, ahead
 * of every one of its own early returns, so a substep with no peer, a halted timeline, no resolved
 * sample or an unplaced puppet all leave the pair holding one value.
 *
 * A window that does not run at all cannot be caught that way, and that is what the frame hook's
 * own guard is for: on a frame with no substep behind it, a weight lower than the one last drawn
 * says the weight has wrapped without a new value, and the pair is collapsed there. After a run of
 * such frames the claim on the nodes is given up altogether, because a puppet whose window has
 * stopped is a body this module should no longer be writing into.
 *
 * What it costs when the pose is still throttled. The engine rebuilds a body's joint matrices only
 * when the substep counter has moved, unless another fix in this tree has removed that test. With
 * the test in place the pose is built once per substep and reads whatever stands in the array at
 * that moment, so an interpolated value neither helps nor hurts: the far body steps once per
 * substep either way, along with its position and its clip, which are throttled by the same test.
 * With the test removed, which is the configuration the far body's animation is smooth in at all,
 * every frame builds the pose and every frame reads what this hook has just written. Nothing here
 * asks which of the two it is, because the answer changes nothing it would do.
 *
 * WHICH OBJECT. The hook runs outside the bank window and once for the whole draw list, so the
 * one thing it must never do is turn somebody else's body. The object it writes is required to be
 * named by two independent sources: the handle the puppet's window captured, and the second
 * body's own banked block. And it is required NOT to be the handle the live hero block names,
 * which outside a window is the local player's own body.
 */
#include "mp_twist.h"

#include "mp_bank.h"
#include "mp_blade_draw.h"
#include "mp_cells.h"
#include "mp_interp.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The object's render thing, the thing's model and node rotation array, the model's node count
 * word, and the twelve byte stride of one node's pitch, yaw and roll. Every one of them is read
 * out of the engine's own node yaw setter at 0x0041481B:
 *
 *     8B 91 9C 00 00 00     edx = obj->thing              (+0x9C)
 *     8B 48 04              ecx = thing->model            (+0x04)
 *     3B 42 54              cmp nodeIdx, model->nodes     (+0x54)
 *     73 1A                 jae, answer 0
 *     6B C9 0C              idx * 12, one vec3 per node
 *     8B 42 24              eax = thing->nodeRot          (+0x24)
 *     89 54 08 04           nodeRot[idx].y = degrees      (+4 is the yaw)
 *
 * The pitch setter at 0x00414789 is the same function byte for byte with its store at +0, and the
 * two getters at 0x00414867 and 0x004147D4 read the same two words back. The animation path masks
 * the node count word with 0x3FFFFFFF before it uses it, so the same mask is applied here. */
#define OBJECT_THING       0x9Cu
#define THING_MODEL        0x04u
#define THING_NODE_ROT     0x24u
#define MODEL_NUM_NODES    0x54u
#define NODE_ROT_STRIDE    12u
#define NODE_COUNT_MASK    0x3FFFFFFFu

/* The body handle of whichever player block is being read: the hero block's for the local player,
 * the bank's own copy for the second body. */
#define BLOCK_HACTOR       0x0Cu

/* Sixty-four nodes cover every hero rig with room; a model claiming more is not one this module
 * knows and is left alone. */
#define MAX_NODES 64u

/* One argument, cdecl, and a result in EAX, read out of the world draw's only call site: the
 * caller pushes one dword, does `add esp, 4` afterwards, and stores what came back.
 *
 *     00410908  mov  eax, [ebp+0x10]
 *     0041090B  push eax
 *     0041090C  call 0x411028
 *     00410911  add  esp, 4
 *     00410914  mov  [ebp-4], eax
 *
 * The draw itself is entered once per rendered frame and before any pose is composed, which is
 * what makes a write at its top land in the same frame. The dword is a float, carried as its
 * bits so that nothing on the way loads it as a number. */
typedef int32_t(__cdecl *draw_all_fn_t)(uint32_t dt);

/* How many frames the hook keeps writing without a substep behind it before it lets the nodes go.
 * Frames rather than substeps because there is no substep to count on: the case this bounds is
 * exactly the one where the puppet's window has stopped running. Generous on purpose, since the
 * pair is already collapsed by then and the value being written is constant; what the limit buys
 * is that the module stops writing into a body it can no longer see being placed. */
#define IDLE_FRAME_LIMIT 256u

typedef struct twist_angles {
    float pitch;
    float yaw;
} twist_angles_t;

/* One far body's rotations. Each far bank shows its own player and turns its own nodes; one
 * record for all of them would draw one player's turn onto another's body. */
typedef struct twist_peer {
    /* The simulation half. `current` is the value of the substep now running and `previous` the
     * value the substep started from; a node absent from a mask stands at zero in both, which is
     * how a rotation that ended on the far side runs out rather than snapping. */
    twist_angles_t previous[MAX_NODES];
    twist_angles_t current[MAX_NODES];
    uint64_t       previous_mask;
    uint64_t       current_mask;
    uint64_t       written_before;   /* nodes the substep half has turned */
    uint64_t       drawn_before;     /* nodes the frame half has turned */
    uint32_t       object;           /* the puppet's body, captured inside the window */
    bool           owns_nodes;       /* there is something of ours in the array */
    bool           produced;         /* a value was applied in the substep now open */
    bool           pair_open;        /* the two ends may differ, so a collapse would do something */
    bool           opened;           /* a substep was opened since the last drawn frame */
    bool           array_writable;   /* the substep half's last writes into the array were taken */
    uint32_t       idle_frames;      /* frames drawn with no substep behind them */
    float          last_weight;      /* the weight this body was last drawn at */
} twist_peer_t;

typedef struct mp_twist_state {
    twist_peer_t peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */

    /* The drawn half. Plain pointers rather than reads through the memory helper: this runs once
     * per rendered frame and the cells are inside the host image, which is never unmapped, so the
     * one check at resolve time is the whole check needed. */
    bool                     installed;
    detour_t                 draw;
    draw_all_fn_t            draw_original;
    const volatile float    *weight;
    const volatile uint32_t *weight_gate;
    const volatile float    *weight_frozen;

    mp_twist_counters_t counters;
} mp_twist_state_t;

static mp_twist_state_t twist;

/* One far body's rotations by the bank that shows it. An index that is no far bank lands on the
 * spare at 0, which is what a caller outside every window, a unit test, gets. */
static twist_peer_t *peer_of(size_t bank)
{
    return &twist.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

static uint64_t node_bit(uint32_t node)
{
    return (uint64_t)1u << node;
}

/* The chain from the object to its rotation array, through the structured-exception read rather
 * than the asking one: this is on the render path now, and the asking form is a system call per
 * field. Both refuse a fault; only one of them costs four VirtualQuery calls per frame. */
static bool node_array(uint32_t object, uint32_t *rot, uint32_t *nodes)
{
    uint32_t thing = 0;
    uint32_t model = 0;
    uint32_t count = 0;

    if (object == 0 ||
        !memory_try_read(object + OBJECT_THING, &thing, sizeof thing) || thing == 0 ||
        !memory_try_read(thing + THING_MODEL, &model, sizeof model) || model == 0 ||
        !memory_try_read(thing + THING_NODE_ROT, rot, sizeof *rot) || *rot == 0 ||
        !memory_try_read(model + MODEL_NUM_NODES, &count, sizeof count)) {
        return false;
    }
    count &= NODE_COUNT_MASK;
    if (count == 0 || count > MAX_NODES) {
        return false;
    }
    *nodes = count;
    return true;
}

size_t mp_twist_read(uint32_t object, mp_wire_twist_t out[MP_WIRE_MAX_TWISTS])
{
    uint32_t rot = 0;
    uint32_t nodes = 0;
    uint32_t node;
    size_t   found = 0;

    if (out == NULL || !node_array(object, &rot, &nodes)) {
        return 0;
    }
    for (node = 0; node < nodes && found < MP_WIRE_MAX_TWISTS; ++node) {
        float angles[2];

        if (!memory_try_read(rot + node * NODE_ROT_STRIDE, angles, sizeof angles)) {
            return found;
        }
        if (angles[0] == 0.0f && angles[1] == 0.0f) {
            continue;
        }
        out[found].node  = (uint8_t)node;
        out[found].pitch = angles[0];
        out[found].yaw   = angles[1];
        ++found;
    }
    return found;
}

/* The substep half's write, which is also what proves the array can be written at all: the frame
 * half writes the same two words and stands down when this has not succeeded.
 *
 * It used to go through the patch layer, and that was the wrong tool for it. The node rotation
 * array is a RUN TIME allocation, never code, and the patch layer lifts the page protection and
 * flushes the instruction cache on every call. For a body carrying six turned nodes that was
 * twenty four VirtualProtect calls and twelve cache flushes a second, and for the duration of each
 * one a data page stood executable. What the proof actually needs is that the write lands and that
 * a bad pointer does not take the process down, and that is what the fault handler gives. */
static void write_node(twist_peer_t *p, uint32_t rot, uint32_t node, float pitch, float yaw)
{
    uintptr_t at    = (uintptr_t)(rot + node * NODE_ROT_STRIDE);
    bool      taken = memory_try_write(at, &pitch, sizeof pitch);

    taken = memory_try_write(at + 4u, &yaw, sizeof yaw) && taken;
    if (!taken) {
        p->array_writable = false;
    }
}

/* The frame half's write. Direct, because it happens for every turned node of every rendered
 * frame and the range it writes is the one the substep half has just written and proved
 * writable; a page protection call per node per frame, twelve hundred system calls a second at a
 * hundred frames and six nodes, would be the entire cost of this feature for nothing, since the
 * array is ordinary read-write heap and the protection change is a no-op on it. */
static void draw_node(uint32_t rot, uint32_t node, float pitch, float yaw)
{
    float *angles = (float *)(uintptr_t)(rot + node * NODE_ROT_STRIDE);

    angles[0] = pitch;
    angles[1] = yaw;
}

/* The frame half closing the pair itself, which it only ever has to do when no substep was opened
 * at all. Counted once per stretch of that rather than once per frame of it, which is what the
 * standing state is for; a report where this number grows while a session runs is saying that the
 * puppet's window has stopped being entered. */
static void collapse_pair(twist_peer_t *p)
{
    if (!p->pair_open) {
        return;
    }
    memcpy(p->previous, p->current, sizeof p->previous);
    p->previous_mask = p->current_mask;
    p->pair_open     = false;
    ++twist.counters.stalls;
}

void mp_twist_open_substep(size_t bank)
{
    twist_peer_t *p = peer_of(bank);

    /* The substep that ends here produced nothing, so the pair it leaves behind holds one value at
     * both ends. That is not a fault and it is the ordinary case whenever the far side sent
     * nothing this substep; it is counted because a report where it is most of the substeps is
     * saying the far body is being drawn from held values rather than from new ones. */
    if (p->owns_nodes && !p->produced) {
        ++twist.counters.collapses;
    }
    p->produced      = false;
    memcpy(p->previous, p->current, sizeof p->previous);
    p->previous_mask = p->current_mask;
    p->pair_open     = false;
    p->opened        = true;
    p->idle_frames   = 0u;
}

void mp_twist_apply(size_t bank, uint32_t object, const mp_wire_twist_t *twists, size_t count)
{
    twist_peer_t *p = peer_of(bank);
    uint32_t      rot = 0;
    uint32_t      nodes = 0;
    uint64_t      now = 0;
    size_t        index;
    uint32_t      node;

    if (!node_array(object, &rot, &nodes)) {
        return;
    }
    /* A caller that reached here without opening the substep would leave the pair standing open
     * across every substep it ever produces, so the open is done here as well rather than
     * trusted. The window does open it, ahead of its own early returns; this is the belt. */
    if (!p->opened) {
        mp_twist_open_substep(bank);
    }

    memset(p->current, 0, sizeof p->current);
    p->array_writable = true;
    for (index = 0; index < count && twists != NULL; ++index) {
        node = twists[index].node;
        if (node >= nodes) {
            continue;   /* a node the far model has and this one does not */
        }
        p->current[node].pitch = twists[index].pitch;
        p->current[node].yaw   = twists[index].yaw;
        now |= node_bit(node);
        write_node(p, rot, node, twists[index].pitch, twists[index].yaw);
    }
    for (node = 0; node < nodes; ++node) {
        uint64_t bit = node_bit(node);

        if ((p->written_before & bit) != 0 && (now & bit) == 0) {
            write_node(p, rot, node, 0.0f, 0.0f);
        }
    }
    p->written_before = now;
    p->current_mask   = now;
    p->produced       = true;
    p->pair_open      = true;
    p->object         = object;
    p->owns_nodes     = true;
}

void mp_twist_withhold(size_t bank, uint32_t object)
{
    ++twist.counters.withheld;
    mp_twist_apply(bank, object, NULL, 0u);
}

void mp_twist_reset(size_t bank)
{
    twist_peer_t *p = peer_of(bank);

    memset(p->previous, 0, sizeof p->previous);
    memset(p->current, 0, sizeof p->current);
    p->previous_mask  = 0u;
    p->current_mask   = 0u;
    p->written_before = 0u;
    p->drawn_before   = 0u;
    p->object         = 0u;
    p->owns_nodes     = false;
    p->produced       = false;
    p->pair_open      = false;
    p->opened         = false;
    p->array_writable = false;
    p->idle_frames    = 0u;
    p->last_weight    = 0.0f;
}

/* The node array holds these in -180..180 and the wire carries them the same way, so a mixture
 * that left the band by the width of one step is folded back into it. One conditional step is
 * enough for any mixture of two values already inside the band. */
static float fold180(float degrees)
{
    if (degrees > 180.0f) {
        return degrees - 360.0f;
    }
    if (degrees < -180.0f) {
        return degrees + 360.0f;
    }
    return degrees;
}

float mp_twist_draw_angle(float previous, float current, float weight)
{
    if (!(weight >= 0.0f)) {          /* also catches NaN */
        weight = 0.0f;
    } else if (weight > 1.0f) {
        weight = 1.0f;
    }
    return fold180(mp_interp_blend_angle(previous, current, weight));
}

/* The engine's own choice, read exactly as it is about to make it a few instructions after we
 * return: the frozen weight while the gate is set, the live one otherwise. Reading only the live
 * one would be wrong on every frame the simulation was held while the draw kept running. */
static float current_weight(void)
{
    float weight = (*twist.weight_gate != 0u) ? *twist.weight_frozen : *twist.weight;

    /* The engine's own arithmetic keeps this inside (0, 1], so this is a bolt rather than a
     * correction. It stays because the product goes straight into the drawn pose, and a NaN there
     * is a body that silently disappears. The !(>) form catches NaN as well. */
    if (!(weight > 0.0f)) {
        return 0.0f;
    }
    return (weight > 1.0f) ? 1.0f : weight;
}

/* The one body this hook may write into. Two independent sources have to name it and a third has
 * to disagree: the handle the puppet's window captured and the second body's own banked block
 * must be the same object, and the live hero block, which outside a window holds the LOCAL
 * player, must name a different one. A hook that turned the local player's root node would turn
 * the player nobody asked to turn, so every doubt is answered by declining. */
static bool puppet_object(size_t bank, const twist_peer_t *p, uint32_t *object)
{
    uintptr_t hero_block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  banked = 0;
    uint32_t  local = 0;

    if (p->object == 0u || hero_block == 0u) {
        return false;
    }
    if (!mp_bank_read_at(bank, BLOCK_HACTOR, &banked, sizeof banked) || banked != p->object) {
        return false;
    }
    if (!memory_try_read(hero_block + BLOCK_HACTOR, &local, sizeof local) ||
        local == 0u || local == p->object) {
        return false;
    }
    *object = p->object;
    return true;
}

static void draw_bank(size_t bank, float weight)
{
    twist_peer_t *p = peer_of(bank);
    uint32_t      object = 0;
    uint32_t      rot = 0;
    uint32_t      nodes = 0;
    uint64_t      live;
    uint32_t      node;

    /* FIRST, ahead of every return below. A frame that declines to write still has to leave the
     * pair in a state the next frame can draw from. */
    if (p->opened) {
        p->opened      = false;
        p->idle_frames = 0u;
    } else {
        /* The wrap test has a known weakness, a frame rate at or below the substep rate makes the
         * weight creep upward rather than wrap, and it costs nothing here: at those rates the
         * window runs at least once per frame, so the open above always arrives first. */
        if (weight < p->last_weight) {
            collapse_pair(p);   /* the weight wrapped with no new value behind it */
        }
        if (p->idle_frames < IDLE_FRAME_LIMIT) {
            ++p->idle_frames;
        } else if (p->owns_nodes) {
            p->owns_nodes = false;
            ++twist.counters.given_up;
        }
    }
    p->last_weight = weight;

    if (!p->owns_nodes || !p->array_writable) {
        return;
    }
    if (!puppet_object(bank, p, &object) || !node_array(object, &rot, &nodes)) {
        ++twist.counters.refusals;
        return;
    }

    live = p->previous_mask | p->current_mask;
    for (node = 0; node < nodes; ++node) {
        uint64_t bit = node_bit(node);
        float    pitch;
        float    yaw;

        if ((live & bit) != 0) {
            pitch = mp_twist_draw_angle(p->previous[node].pitch, p->current[node].pitch,
                                        weight);
            yaw   = mp_twist_draw_angle(p->previous[node].yaw, p->current[node].yaw, weight);
            if (!isfinite(pitch) || !isfinite(yaw)) {
                continue;
            }
        } else if ((p->drawn_before & bit) != 0) {
            pitch = 0.0f;   /* it ran out in the substep before this one: the exact zero, once */
            yaw   = 0.0f;
        } else {
            continue;
        }
        draw_node(rot, node, pitch, yaw);
    }
    p->drawn_before = live;
    ++twist.counters.frames;

    /* The claim is given up only after the exact zero has been WRITTEN, which is one substep
     * later than the last rotation reaching zero. Giving it up as soon as the pair is empty would
     * stop the writing while the last frames were still running out toward zero. */
    if (live == 0u) {
        p->owns_nodes = false;
    }
}

/* Every far body, on the one weight the engine is about to draw this frame with. */
static void draw_interpolated_twists(void)
{
    float  weight = current_weight();
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        draw_bank(bank, weight);
    }
}

/* The head of the world draw, where no object is being drawn: the far blades' witness first,
 * then the rotations. The argument is the frame's time step, handed on untouched.
 * engine: u32 bapobj_drawAll(f32 dt) */
static int32_t __cdecl hook_draw_all(uint32_t dt)
{
    mp_blade_draw_witness();
    draw_interpolated_twists();
    return twist.draw_original(dt);
}

/* The three cells are operands inside the world draw's own frame set-up, which is why that site's
 * pattern is a hundred and three bytes long rather than twenty:
 *
 *     00411063  A1 1C 87 86 00           mov eax, [0086871C]       the live weight    (+0x3C)
 *     00411068  89 85 E0 FB FF FF        mov [ebp-0x420], eax
 *     0041106E  83 3D E8 AC 5B 00 00     cmp dword [005BACE8], 0   the hold gate      (+0x48)
 *     00411075  74 0C                    je  0x411083
 *     00411077  8B 0D E4 AC 5B 00        mov ecx, [005BACE4]       the frozen weight  (+0x51)
 *     0041107D  89 8D E0 FB FF FF        mov [ebp-0x420], ecx
 *
 * The five retail builds agree on those three addresses and the recompile answers 008686BC,
 * 005BAC98 and 005BAC94, which is the whole reason none of them is written down as a number. The
 * live weight is read a second time out of the store in the substep runner, `D9 1D 1C 87 86 00`
 * at 0x00475817, and the cell table requires the two readings to agree; they do in all six. */
static bool resolve_weight_cells(void)
{
    uintptr_t live   = mp_cells_address(MP_CELL_SUBSTEP_ALPHA);
    uintptr_t gate   = mp_cells_address(MP_CELL_DRAW_WEIGHT_GATE);
    uintptr_t frozen = mp_cells_address(MP_CELL_DRAW_WEIGHT_FROZEN);

    if (live == 0u || gate == 0u || frozen == 0u ||
        !memory_is_inside_image(live, sizeof(float)) ||
        !memory_is_inside_image(gate, sizeof(uint32_t)) ||
        !memory_is_inside_image(frozen, sizeof(float))) {
        return false;
    }
    twist.weight        = (const volatile float *)live;
    twist.weight_gate   = (const volatile uint32_t *)gate;
    twist.weight_frozen = (const volatile float *)frozen;
    return true;
}

void mp_twist_resolve(void)
{
    uintptr_t site;

    if (twist.installed) {
        return;
    }
    site = mp_signatures_address(MP_SITE_BAPOBJ_DRAW_ALL);
    if (site == 0u) {
        log_warning("the world draw did not resolve, so the puppet's node rotations keep being "
                    "written once per substep and step at the simulation rate. Everything else "
                    "about the puppet is unaffected.");
        return;
    }
    if (!resolve_weight_cells()) {
        log_warning("the interpolation weight behind the world draw did not resolve, so the "
                    "puppet's node rotations are left on the simulation clock. Interpolating "
                    "without the engine's own weight would mean inventing a clock, and a wrong "
                    "one would sweep the body rather than smooth it.");
        return;
    }
    if (!detour_install(&twist.draw, site, (const void *)hook_draw_all,
                        mp_signatures_prologue(MP_SITE_BAPOBJ_DRAW_ALL))) {
        log_warning("the detour on the world draw at %08X failed, so the puppet's node rotations "
                    "are left on the simulation clock", (unsigned)site);
        return;
    }
    twist.draw_original      = (draw_all_fn_t)twist.draw.original;
    twist.installed          = true;
    twist.counters.installed = true;

    log_info("the puppet's node rotations are now DRAWN interpolated, at %08X, once per rendered "
             "frame on the engine's own weight. They are still produced once per substep, which "
             "is where a replicated value belongs; what changed is that the frames between two "
             "substeps no longer all show the same orientation. Above about sixty frames a second "
             "that was a staircase of thirty two steps a second on the whole body, because the "
             "root node turns the body and the pose build multiplies it in after the animation "
             "blend. The pair is collapsed on any substep that produced no new value, which is "
             "what keeps a halted timeline or an unplaced puppet from sweeping the body instead.",
             (unsigned)site);
}

void mp_twist_get_counters(mp_twist_counters_t *out)
{
    if (out != NULL) {
        *out = twist.counters;
    }
}
