/* character_nodemap.c: the name layer the pose path does not have.
 *
 * SIZE NOTE: the file is over the six hundred line review limit. Two seams have been
 * taken. The clip copy went first: `clip_copy_of`, the arenas and the caches are
 * character_clipcopy.c, and the map crosses that boundary by value, so neither side can hold a
 * second opinion about which ordinal space it is in. The pure half went second, to
 * character_restmap.c: the name map, the floor and the rest pose shift read no engine memory and
 * are the part the unit test drives. What is left is what reads the engine: the two model walks,
 * the table of bodies with the one read every hook asks it, and the hook itself, which is nine
 * lines of substitution around one call and not a seam.
 *
 * The table of bodies lives here because the pose hook asks it more often than anything else, and
 * the draw hook and the far bodies' path ask it through the calls below. What a row may hold and
 * when it holds is decided in character_bodies.c, which reads nothing; this file does the reading.
 */
#include "character_nodemap.h"

#include "character_bodies.h"
#include "character_buoyancy.h"
#include "character_ground.h"
#include "character_cards.h"
#include "character_clipcopy.h"
#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* rdPuppet_buildJointMatrices. The function is compiled without a frame pointer, so the pattern
 * opens on the stack reservation and the load of the thing rather than on a push of ebp. What
 * makes it recognisable is the four instruction run that follows: the puppet is taken out of the
 * handle at +0x18, the model out of +0x04, the puppet is tested against a register that was just
 * zeroed, and the track count 4 is written to the stack. The two branch displacements are the only
 * wildcards. */
static const uint8_t SIG_BUILD_POSE[] = {
    0x83, 0xEC, 0x70,                                /* sub esp,0x70                            */
    0x8B, 0x4C, 0x24, 0x74,                          /* mov ecx,[esp+0x74]   the thing          */
    0x53, 0x55,
    0x33, 0xD2,                                      /* xor edx,edx                             */
    0x8B, 0x41, 0x18,                                /* mov eax,[ecx+0x18]   the puppet         */
    0x8B, 0x69, 0x04,                                /* mov ebp,[ecx+4]      the model          */
    0x56,
    0x3B, 0xC2,                                      /* cmp eax,edx          no puppet?         */
    0x57,
    0x89, 0x6C, 0x24, 0x20,
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,              /* je the bind pose fast path              */
    0x39, 0x10,                                      /* cmp [eax],edx        suspended?         */
    0x0F, 0x85, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x08,                                /* add eax,8            the first track    */
    0xC7, 0x44, 0x24, 0x14, 0x04, 0x00, 0x00, 0x00   /* mov [esp+0x14],4     the four tracks    */
};
static const uint8_t MSK_BUILD_POSE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_BUILD_POSE) == sizeof(MSK_BUILD_POSE),
               "the pose build pattern and its mask are different lengths");

/* The stack reservation and the load of the argument, seven bytes, one instruction boundary and
 * no relative operand. */
#define BUILD_POSE_PROLOGUE   7u

#define RDTHING_MODEL3        0x04u
#define RDTHING_PUPPET        0x18u
#define PUPPET_TRACKS         0x08u
#define TRACK_BYTES           0x14Cu
#define TRACK_FLAGS           0x00u
#define TRACK_CURSORS         0x20u   /* 64 signed words, indexed in the clip's ordinal space */
#define TRACK_CLIP            0x128u
#define TRACK_COUNT           4u

#define MODEL_NUM_NODES       0x54u
#define MODEL_NODES           0x58u
#define NODE_BYTES            0xB4u
#define NODE_NAME             0x00u
#define NODE_TYPE             0x48u   /* the body part bit the clip type is ANDed with */
#define NODE_REST             0x6Cu   /* three offset floats, then three of orientation at 0x78 */

#define BLOCK_BODY            0x0Cu   /* a player block's bapObj                     */
#define BAPOBJ_ACTOR          0x14u
#define BAPOBJ_THING          0x9Cu
#define BAPACTOR_MODEL        0xE0u

typedef void (__cdecl *build_pose_fn_t)(void *thing, const float *world);

typedef struct nodemap_state {
    bool               tried;
    bool               installed;
    detour_t           detour;
    build_pose_fn_t    original;

    character_bodies_t bodies;
    uint32_t           said[1u + MODEL_WEAR_BANKS];   /* the far body each bank last warned for */
    bool               frozen_said;
} nodemap_state_t;

static nodemap_state_t nodemap;

/* ============================================================================================ */

uint32_t character_nodemap_node_count(uintptr_t model)
{
    uint32_t count = 0;

    if (model == 0u || !memory_try_read(model + MODEL_NUM_NODES, &count, sizeof count)) {
        return 0u;
    }
    return count;
}

/* The node array, checked over its whole length rather than at its first byte, because everything
 * below walks it to the end. */
static bool model_nodes(uintptr_t model, uintptr_t *out_base, uint32_t *out_count)
{
    uint32_t count = character_nodemap_node_count(model);
    uint32_t base = 0;

    if (count == 0u || count > NODEMAP_MAX_NODES) {
        return false;
    }
    if (!memory_try_read(model + MODEL_NODES, &base, sizeof base) || base == 0u) {
        return false;
    }
    if (!memory_try_readable((uintptr_t)base, (size_t)count * NODE_BYTES)) {
        return false;
    }
    *out_base = (uintptr_t)base;
    *out_count = count;
    return true;
}

static void collect_names(uintptr_t base, uint32_t count, const char **names)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        names[i] = (const char *)(base + (uintptr_t)i * NODE_BYTES + NODE_NAME);
    }
}

int32_t character_nodemap_find(uintptr_t model, const char *name)
{
    uintptr_t base = 0;
    uint32_t  count = 0;
    uint32_t  i;

    if (name == NULL || !model_nodes(model, &base, &count)) {
        return NODEMAP_NO_TRACK;
    }
    for (i = 0; i < count; ++i) {
        const char *node = (const char *)(base + (uintptr_t)i * NODE_BYTES + NODE_NAME);

        if (character_nodemap_same_name(node, name)) {
            return (int32_t)i;
        }
    }
    return NODEMAP_NO_TRACK;
}

/* The six floats at node+0x6c, read straight out of the live node array. They are contiguous and in
 * the order the compositor reads them, so one copy per node is the whole job. */
static void collect_rest(uintptr_t base, uint32_t count, nodemap_rest_t *out)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        memcpy(&out[i], (const void *)(base + (uintptr_t)i * NODE_BYTES + NODE_REST),
               sizeof out[i]);
    }
}

/* Fills the map and, when asked, the reference part words and the per node shift beside it. They
 * are read in one pass because they come out of the same two arrays and are used by the same copy.
 */
static bool measure_pair(uintptr_t reference_model, uintptr_t target_model,
                         int32_t *map, uint32_t *reference_type, nodemap_rest_t *rebase,
                         nodemap_fit_t *fit)
{
    const char *reference[NODEMAP_MAX_NODES];
    const char *target[NODEMAP_MAX_NODES];
    uintptr_t   reference_base = 0;
    uintptr_t   target_base = 0;
    uint32_t    reference_count = 0;
    uint32_t    target_count = 0;
    uint32_t    i;

    if (!model_nodes(reference_model, &reference_base, &reference_count) ||
        !model_nodes(target_model, &target_base, &target_count)) {
        return false;
    }
    collect_names(reference_base, reference_count, reference);
    collect_names(target_base, target_count, target);

    if (!character_nodemap_build(reference, reference_count, target, target_count, map, fit)) {
        return false;
    }
    if (reference_type != NULL) {
        for (i = 0; i < reference_count; ++i) {
            reference_type[i] = *(const uint32_t *)(reference_base + (uintptr_t)i * NODE_BYTES +
                                                    NODE_TYPE);
        }
        for (; i < NODEMAP_MAX_NODES; ++i) {
            reference_type[i] = 0u;
        }
    }
    if (rebase != NULL) {
        nodemap_rest_t reference_rest[NODEMAP_MAX_NODES];
        nodemap_rest_t target_rest[NODEMAP_MAX_NODES];

        collect_rest(reference_base, reference_count, reference_rest);
        collect_rest(target_base, target_count, target_rest);
        if (!character_nodemap_rebase(reference_rest, reference_count, target_rest, target_count,
                                      map, rebase)) {
            return false;
        }
    }
    return true;
}

bool character_nodemap_measure(uintptr_t reference_model, uintptr_t target_model,
                               nodemap_fit_t *fit)
{
    int32_t map[NODEMAP_MAX_NODES];

    if (fit == NULL) {
        return false;
    }
    return measure_pair(reference_model, target_model, map, NULL, NULL, fit);
}

/* ============================================================================================ */

/* The cursor array ends where the clip pointer begins, and the clear must stay inside it. Checked
 * by the compiler rather than by a comment, because the three numbers involved are read out of the
 * engine and a wrong one here writes over the clip a track is playing. */
_Static_assert(TRACK_CURSORS + NODEMAP_MAX_NODES * sizeof(int32_t) <= TRACK_CLIP,
               "the cursor clear would reach the clip pointer of the same track");
_Static_assert(TRACK_CLIP < TRACK_BYTES, "the clip pointer lies outside the track record");

static void clear_cursors(uintptr_t puppet)
{
    uint32_t slot;

    if (!memory_try_readable(puppet, PUPPET_TRACKS + TRACK_COUNT * TRACK_BYTES)) {
        return;
    }
    for (slot = 0; slot < TRACK_COUNT; ++slot) {
        uintptr_t track = puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_BYTES;

        memset((void *)(track + TRACK_CURSORS), 0, NODEMAP_MAX_NODES * sizeof(int32_t));
    }
}

/* Whether the handle is still wearing what the map was built for, asked of the engine on every
 * call rather than remembered.
 *
 * Nothing tells this module that a level ended, and a level change frees the render handle and the
 * asset behind every clip copy in the cache. The allocator is free to hand the same address back,
 * and it hands back agreement with it: a pointer comparison alone would let a stale map and
 * pointers into a released keyframe pool reach a body that has nothing to do with either. So the
 * row's handle is read out of its own object, the model on it out of the handle, the node count out
 * of that model and the actor's model out of the object, and character_bodies_holds decides. The
 * node count is the number the copy's table length was computed from; the actor's model is what a
 * stranger built into a recycled handle from the target's own asset gets wrong, because a body
 * built from an asset wears that asset's model as its actor's.
 *
 * A far body's block is read first and alone: it is the multiplayer's memory and always valid, and
 * a far body taken down since the row was filed stops the reading there, before anything of a freed
 * body is looked at. */
static void observe(const body_entry_t *body, body_reads_t *reads)
{
    uint32_t word = 0;
    uint32_t actor = 0;

    memset(reads, 0, sizeof *reads);
    if (!body->local) {
        if (!memory_try_read(body->block + BLOCK_BODY, &word, sizeof word)) {
            return;
        }
        reads->block_body = (uintptr_t)word;
        if (reads->block_body != body->obj) {
            return;
        }
    }
    if (!memory_try_read(body->obj + BAPOBJ_THING, &word, sizeof word)) {
        return;
    }
    reads->obj_thing = (uintptr_t)word;
    if (reads->obj_thing != body->thing ||
        !memory_try_read(body->thing + RDTHING_MODEL3, &word, sizeof word)) {
        return;
    }
    reads->worn = (uintptr_t)word;
    if (reads->worn != body->target) {
        return;
    }
    reads->worn_nodes = character_nodemap_node_count(reads->worn);
    if (!memory_try_read(body->obj + BAPOBJ_ACTOR, &actor, sizeof actor) || actor == 0u ||
        !memory_try_read((uintptr_t)actor + BAPACTOR_MODEL, &word, sizeof word)) {
        return;
    }
    reads->actor_model = (uintptr_t)word;
}

static bool row_holds(uint32_t index)
{
    body_reads_t reads;

    if (index >= BODY_MAX || !nodemap.bodies.body[index].used) {
        return false;
    }
    observe(&nodemap.bodies.body[index], &reads);
    return character_bodies_holds(&nodemap.bodies.body[index], &reads);
}

static uintptr_t live_puppet(uintptr_t thing)
{
    uint32_t puppet = 0;

    if (!memory_try_read(thing + RDTHING_PUPPET, &puppet, sizeof puppet) || puppet == 0u) {
        return 0u;
    }
    if (!memory_try_readable((uintptr_t)puppet, PUPPET_TRACKS + TRACK_COUNT * TRACK_BYTES)) {
        return 0u;
    }
    return (uintptr_t)puppet;
}

/* The bind pose for one call: every track's flags word cleared and put back afterwards. Said once.
 * Pass one of the compositor tests the flags before it reads anything of a track. Pass two reads
 * the track's clip (+0x128) and that clip's part mask (+0x2C) first and tests bit 2 of the flags
 * only then (0x00483FF3, 0x00484001, 0x00484013), before any keyframe; those two reads are the
 * ones the engine makes for every track, on the clip this call leaves in place. */
static void pose_frozen(void *thing, const float *world, uintptr_t puppet)
{
    uint32_t saved_flags[TRACK_COUNT];
    uint32_t slot;

    if (!nodemap.frozen_said) {
        nodemap.frozen_said = true;
        log_warning("the body at %08X still wears the model its translation was filed for, and "
                    "the translation does not recognise it; it is posed in its bind pose rather "
                    "than from clips of another rig", (unsigned)(uintptr_t)thing);
    }
    for (slot = 0; slot < TRACK_COUNT; ++slot) {
        uintptr_t track = puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_BYTES;

        saved_flags[slot] = *(volatile uint32_t *)(track + TRACK_FLAGS);
        *(volatile uint32_t *)(track + TRACK_FLAGS) = 0u;
    }
    nodemap.original(thing, world);
    for (slot = 0; slot < TRACK_COUNT; ++slot) {
        uintptr_t track = puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_BYTES;

        *(volatile uint32_t *)(track + TRACK_FLAGS) = saved_flags[slot];
    }
}

/* THE SUBSTITUTION, and it lasts exactly one call. Every other reader of a track still sees the
 * clip the engine put there, which is what keeps this module out of the way of anything that asks
 * what is playing.
 *
 * A track whose clip could not be copied is not left alone: its clip is authored in the reference
 * rig's ordinal space and the model under it is not, so posing from it would drive the wrong
 * joints. The flags word is cleared instead, which both passes of the compositor test before they
 * read a keyframe of the track (pass two reads the clip and its part mask first, pose_frozen),
 * and it is put back afterwards. A row that does not hold on a handle that still
 * wears its target has every track cleared that way (character_bodies_pose). */
static void __cdecl hook_build_pose(void *thing, const float *world)
{
    uintptr_t   puppet;
    uint32_t    saved_clip[TRACK_COUNT];
    uint32_t    saved_flags[TRACK_COUNT];
    uint32_t    index = character_bodies_find(&nodemap.bodies, (uintptr_t)thing);
    uint32_t    worn = 0;
    body_pose_t way = BODY_POSE_ENGINE;
    uint32_t    pair;
    uint32_t    slot;

    if (index != BODY_NONE) {
        bool holds = row_holds(index);

        if (!holds) {
            (void)memory_try_read((uintptr_t)thing + RDTHING_MODEL3, &worn, sizeof worn);
        }
        way = character_bodies_pose(&nodemap.bodies.body[index], holds, (uintptr_t)worn);
    }
    puppet = (way == BODY_POSE_ENGINE) ? 0u : live_puppet((uintptr_t)thing);
    if (puppet == 0u) {
        nodemap.original(thing, world);
        return;
    }
    if (way == BODY_POSE_FROZEN) {
        pose_frozen(thing, world, puppet);
        return;
    }
    pair = nodemap.bodies.body[index].pair;

    for (slot = 0; slot < TRACK_COUNT; ++slot) {
        uintptr_t track = puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_BYTES;
        uintptr_t copy;

        saved_clip[slot] = *(volatile uint32_t *)(track + TRACK_CLIP);
        saved_flags[slot] = *(volatile uint32_t *)(track + TRACK_FLAGS);
        if (saved_clip[slot] == 0u) {
            continue;
        }
        copy = character_clipcopy_for(pair, (uintptr_t)saved_clip[slot]);
        if (copy != 0u) {
            *(volatile uint32_t *)(track + TRACK_CLIP) = (uint32_t)copy;
        } else {
            *(volatile uint32_t *)(track + TRACK_FLAGS) = 0u;
        }
    }

    nodemap.original(thing, world);

    for (slot = 0; slot < TRACK_COUNT; ++slot) {
        uintptr_t track = puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_BYTES;

        *(volatile uint32_t *)(track + TRACK_CLIP) = saved_clip[slot];
        *(volatile uint32_t *)(track + TRACK_FLAGS) = saved_flags[slot];
    }
}

/* ============================================================================================ */

bool character_nodemap_install(void)
{
    uintptr_t site;

    if (nodemap.tried) {
        return nodemap.installed;
    }
    nodemap.tried = true;

    site = signature_find_detour_target(SIG_BUILD_POSE, MSK_BUILD_POSE, sizeof SIG_BUILD_POSE,
                                        BUILD_POSE_PROLOGUE);
    if (site == 0u) {
        log_warning("the pose build did not resolve, so no model swap is offered: without it a "
                    "borrowed skeleton is driven by tracks meant for other joints");
        return false;
    }

    if (!character_clipcopy_reserve()) {
        return false;
    }

    if (!detour_install(&nodemap.detour, site, (const void *)&hook_build_pose,
                        BUILD_POSE_PROLOGUE)) {
        log_warning("the pose build at %08X could not be hooked, so no model swap is offered",
                    (unsigned)site);
        return false;
    }
    nodemap.original = (build_pose_fn_t)nodemap.detour.original;
    nodemap.installed = true;
    log_info("the pose build at %08X is hooked, so a borrowed skeleton can be driven by name",
             (unsigned)site);

    /* The waterline correction rides along with the swap and is NOT a condition of it. A body of
     * another height that floats at the player's own depth is what this feature did before that
     * module existed, and it is still worth wearing. */
    (void)character_buoyancy_install();

    /* So does the bound on the sabre glow cards, and it belongs HERE rather than with the borrowed
     * weapon: the hazard is the rebind itself. A sabre effect record holds a node INDEX, the model
     * under that index is replaced and the pass that draws the record copies the new node's whole
     * mesh into a four vertex buffer. That is true whether or not a weapon is being carried, so the
     * bound is placed wherever a model can be swapped, and a build where it cannot be placed still
     * gets the swap: the warning says what is then unbounded. */
    /* And so does the point every mark under the player is stamped at. The name is resolved against
     * the WORN model and the point it names is then looked up in the model the body was BUILT from,
     * which the swap never rewrites, so the index is the borrowed rig's and the mesh it lands on is
     * the hero's. A footprint was being stamped at the bounding box centre of whatever sits at that
     * array position on Obi-Wan, which for most rigs is a holstered blaster. */
    (void)character_ground_install();
    (void)character_cards_install();

    return true;
}

/* Any body at all, because the footprint correction under it is measured on whatever rig the body
 * it is asked about wears, and is right for every one of them. */
bool character_nodemap_is_armed(void)
{
    return character_bodies_any(&nodemap.bodies);
}

/* The row goes, and with it what was built for it when it was the pair's last wearer.
 *
 * The puppet is only reached through a handle that still wears what the row was filed for. The
 * most common way to arrive here is a level change or a far body taken down, which freed both, and
 * reading a pointer out of a released block and then clearing a kilobyte through it is a write
 * into whatever took its place. The cursors of a handle that does still wear the target were left
 * in the target's ordinal space and the hero's own rig reads the same array in its own; they are
 * cleared rather than translated, because cursor zero is a valid position on every keyframe
 * record and the walk moves it forward again on the same frame. */
static void forget(uint32_t index)
{
    body_entry_t body = nodemap.bodies.body[index];
    body_pair_t  pair;
    body_reads_t reads;
    body_plan_t  plan;
    uintptr_t    puppet = 0u;
    uint32_t     released;

    memset(&pair, 0, sizeof pair);
    if (body.pair < PAIR_MAX) {
        pair = nodemap.bodies.pair[body.pair];
    }
    observe(&body, &reads);
    plan = character_bodies_on_disarm(&body, &reads);
    if (plan.clear_cursors) {
        puppet = live_puppet(body.thing);
    }
    released = character_bodies_remove(&nodemap.bodies, index);
    if (plan.waterline) {
        character_buoyancy_disarm();
    }
    if (puppet != 0u) {
        clear_cursors(puppet);
    }
    if (released != BODY_NONE) {
        size_t bytes = character_clipcopy_release(released);

        log_info("the pair %08X onto %08X is released: no body wears it any more (arena %u KB "
                 "decommitted)", (unsigned)pair.reference, (unsigned)pair.target,
                 (unsigned)(bytes / 1024u));
    }
}

void character_nodemap_disarm(uintptr_t thing)
{
    uint32_t index = character_bodies_find(&nodemap.bodies, thing);

    if (index != BODY_NONE) {
        forget(index);
    }
}

void character_nodemap_disarm_local(void)
{
    uint32_t index = character_bodies_find_bank(&nodemap.bodies, 0u);

    if (index != BODY_NONE) {
        forget(index);
    }
}

/* The widest shift the translation had to apply, in thousandths of a model unit and in degrees.
 * It is what a reader of the log can hold against the two rigs: zero says the target's build is
 * the hero's build, and anything else is the amount of the target's own build that used to be
 * overwritten. Said once per pair, when it is measured. */
static void report_rebase(const nodemap_fit_t *fit, const nodemap_rest_t *rebase, bool local)
{
    float    widest_pos = 0.0f;
    float    widest_euler = 0.0f;
    uint32_t j;
    uint32_t k;

    for (j = 0; j < fit->target_nodes; ++j) {
        for (k = 0; k < 3u; ++k) {
            float p = rebase[j].pos[k];
            float e = rebase[j].euler[k];

            p = (p < 0.0f) ? -p : p;
            e = (e < 0.0f) ? -e : e;
            widest_pos = (p > widest_pos) ? p : widest_pos;
            widest_euler = (e > widest_euler) ? e : widest_euler;
        }
    }
    log_info("the skeleton is translated by name: %u of the target's %u nodes are driven, %u hold "
             "their rest pose, %u of %s %u tracks are dropped, and the pose is re-seated "
             "on the target's own build by up to %d/1000 of a unit and %d degrees",
             fit->matched, fit->target_nodes, fit->held, fit->dropped,
             local ? "the player's" : "the far hero's", fit->reference_nodes,
             (int)(widest_pos * 1000.0f), (int)widest_euler);
}

/* A far body that finds no room is asked again at every scene end for as long as its bank's body
 * stands, so what it could not get is said once per bank and body serial. The player's own swap is
 * asked by a click and says it every time. */
static bool may_say(const nodemap_body_t *spec)
{
    return spec->local || spec->bank > MODEL_WEAR_BANKS ||
           nodemap.said[spec->bank] != spec->serial;
}

static void note_said(const nodemap_body_t *spec)
{
    if (!spec->local && spec->bank <= MODEL_WEAR_BANKS) {
        nodemap.said[spec->bank] = spec->serial;
    }
}

/* Files a body and gives it a pair: a pair another body plays already, when one of its wearers was
 * found alive in the pass the caller names, or a fresh one measured here. The fresh pair's copies
 * start empty; a shared pair's copies are never thrown away for a second wearer, because the first
 * one is posing out of them. */
bool character_nodemap_arm(const nodemap_body_t *spec)
{
    int32_t        map[NODEMAP_MAX_NODES];
    uint32_t       reference_type[NODEMAP_MAX_NODES];
    nodemap_rest_t rebase[NODEMAP_MAX_NODES];
    nodemap_fit_t  fit;
    body_entry_t   body;
    body_plan_t    plan;
    uint32_t       existing;
    uint32_t       pair;
    uint32_t       index;
    uintptr_t      puppet;
    bool           fresh = false;

    if (!nodemap.installed || spec == NULL || spec->thing == 0u || spec->obj == 0u) {
        return false;
    }
    /* A handle filed by somebody else is not taken from them; the player's own re-arm and a far
     * bank's re-arm each replace only their own row. */
    existing = character_bodies_find(&nodemap.bodies, spec->thing);
    if (existing != BODY_NONE && (nodemap.bodies.body[existing].local != spec->local ||
                                  nodemap.bodies.body[existing].bank != spec->bank)) {
        return false;
    }
    if (existing != BODY_NONE) {
        forget(existing);
    }
    if (!measure_pair(spec->reference, spec->target, map, reference_type, rebase, &fit) ||
        !character_nodemap_fit_is_offered(&fit)) {
        return false;
    }
    pair = character_bodies_pair_take(&nodemap.bodies, spec->reference, spec->target,
                                      fit.target_nodes, spec->share_pass, &fresh);
    if (pair == BODY_NONE) {
        if (may_say(spec)) {
            log_warning("every pair of the translation is taken, so no further body is "
                        "translated");
        }
        note_said(spec);
        return false;
    }
    /* The clip half takes the map, the part words and the shift by value. Nothing below this point
     * can leave the two halves holding different answers about which ordinal space a record is in,
     * and a refusal here leaves nothing armed on either side. */
    if (fresh && !character_clipcopy_arm(pair, map, reference_type, rebase, fit.target_nodes,
                                         may_say(spec))) {
        note_said(spec);
        character_bodies_pair_drop(&nodemap.bodies, pair);
        return false;
    }

    memset(&body, 0, sizeof body);
    body.local = spec->local;
    body.bank = spec->bank;
    body.serial = spec->serial;
    body.thing = spec->thing;
    body.obj = spec->obj;
    body.block = spec->block;
    body.reference = spec->reference;
    body.target = spec->target;
    body.target_nodes = fit.target_nodes;
    body.pair = pair;
    body.checked = spec->share_pass;
    index = character_bodies_add(&nodemap.bodies, &body);
    if (index == BODY_NONE) {
        if (fresh) {
            character_bodies_pair_drop(&nodemap.bodies, pair);
            (void)character_clipcopy_release(pair);
        }
        if (may_say(spec)) {
            log_warning("the body at %08X could not be filed, so it is not translated",
                        (unsigned)spec->thing);
        }
        note_said(spec);
        return false;
    }

    plan = character_bodies_on_arm(&nodemap.bodies.body[index]);
    puppet = plan.clear_cursors ? live_puppet(spec->thing) : 0u;
    if (puppet != 0u) {
        clear_cursors(puppet);
    }
    if (fresh) {
        report_rebase(&fit, rebase, spec->local);
    } else {
        log_info("the body at %08X plays the translation already built for its pair",
                 (unsigned)spec->thing);
    }

    /* Armed here and nowhere else, and only for the player's own body, because this is the last
     * moment his handle still wears his own body, and the scale that body is drawn at can be read
     * off the object only while it is. A far body swims as its owner says it does. A refusal
     * there leaves the swimmer at the engine's own depth and nothing else. */
    if (plan.waterline) {
        (void)character_buoyancy_arm(spec->thing, spec->reference, spec->target);
    }
    return true;
}

bool character_nodemap_holds(uintptr_t thing, body_entry_t *out)
{
    uint32_t index = character_bodies_find(&nodemap.bodies, thing);

    if (index == BODY_NONE || !row_holds(index)) {
        return false;
    }
    if (out != NULL) {
        *out = nodemap.bodies.body[index];
    }
    return true;
}

bool character_nodemap_row_holds(uint32_t index)
{
    return row_holds(index);
}

const character_bodies_t *character_nodemap_bodies(void)
{
    return &nodemap.bodies;
}

uint32_t character_nodemap_begin_pass(void)
{
    return character_bodies_begin_pass(&nodemap.bodies);
}

void character_nodemap_mark(uint32_t index, uint32_t pass)
{
    character_bodies_mark(&nodemap.bodies, index, pass);
}

bool character_nodemap_set_two_sided(uintptr_t thing, bool on)
{
    uint32_t index = character_bodies_find(&nodemap.bodies, thing);

    if (index == BODY_NONE) {
        return false;
    }
    nodemap.bodies.body[index].two_sided = on;
    return true;
}

bool character_nodemap_any_two_sided(void)
{
    uint32_t i;

    for (i = 0; i < BODY_MAX; ++i) {
        if (nodemap.bodies.body[i].used && nodemap.bodies.body[i].two_sided) {
            return true;
        }
    }
    return false;
}

bool character_nodemap_set_hidden(uintptr_t thing, const uint32_t *slots, uint32_t count)
{
    uint32_t index = character_bodies_find(&nodemap.bodies, thing);
    uint32_t i;

    if (index == BODY_NONE || (count != 0u && slots == NULL)) {
        return false;
    }
    if (count > BODY_HIDE_MAX) {
        count = BODY_HIDE_MAX;
    }
    for (i = 0; i < count; ++i) {
        nodemap.bodies.body[index].hidden[i] = slots[i];
    }
    nodemap.bodies.body[index].hidden_count = count;
    return true;
}
