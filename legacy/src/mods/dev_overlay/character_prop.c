/* character_prop.c: a second render handle on the player's own model, drawn at the borrowed hand.
 *
 * Nothing here writes into a model. The weapon is the player's own mesh, in the player's own
 * asset, on a render handle this module owns; the borrowed rig contributes one matrix. A node hung
 * into the target model would hang on every actor in the level built from that asset, and turning
 * the feature off would not take it back.
 *
 * The placement is the engine's own composition and nothing else. After
 * rdPuppet_buildJointMatrices returns, thing+0x20 holds one world matrix per joint, and a handle
 * with no puppet seeds that array from each node's own rest matrix, so a build against an identity
 * root leaves the reference rig's REST chain there. rdThing_buildWorldMatrices makes a node's local
 * transform `translate(pivot) * rest * translate(-parent->pivot)`, and the last of those three
 * belongs to the PARENT. Exchange it, and the weapon subtree is re-parented:
 *
 *     C    = the reference rest chain, its root to its right hand
 *     H    = the borrowed rig's live right hand, world
 *     root = inverse(C) * translate(reference pivot - target pivot)   then composed with H
 *
 * Every node drawn under the reference hand then arrives at exactly the matrix the borrowed rig
 * would have built for it, and the weapon's offset, its angle and the scale the borrowed body is
 * drawn at all ride inside those two matrices. There is no table per asset, no anchor and no size
 * rule: a hand mesh anchor and a hand radius scale were both carried here for a while and both are
 * gone, because the engine applies neither and the weapon on a hero rig is placed by the engine.
 *
 * SIZE NOTE: over the 600 line review limit after four cuts. First the patterns, now
 * character_prop_sites.c, then the arithmetic, now character_prop_math.c, then the state,
 * character_prop_body.c, and now what a RIG SAYS, character_prop_rig.c. What is left here is what
 * is DRAWN and what the engine is answered with, and every routine of it takes the body as an
 * argument, so the boundary crosses no field at all.
 */
#include "character_prop.h"

#include "character_prop_blade.h"
#include "character_prop_body.h"
#include "character_prop_rig.h"
#include "character_prop_sites.h"

#include "character_mount.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stddef.h>
#include <string.h>

/* The render handle, the model, the node and the mesh, as the draw reads them. */
#define RDTHING_MODEL3         0x04u
#define RDTHING_POSE_STAMP     0x1Cu
#define RDTHING_NODE_HIDDEN    0x28u
#define RDTHING_MESH_HIDDEN    0x2Cu
#define MODEL_GEOSET0_MESHES   0x28u
#define NODE_MATRIX_SLOT       0x44u
#define NODE_MESH_INDEX        0x4Cu
#define NODE_FIRST_CHILD       0x58u
#define NODE_NEXT_SIBLING      0x5Cu
#define MESH_BYTES             0x70u
#define MESH_RADIUS            0x54u
#define MESH_CENTRE            0x58u
#define PLAYER_WEAPON_SLOT     0x84u
#define WEAPON_SLOTS           12u
#define WEAPON_ROW_BYTES       0x18u
#define NAME_ID_RHAND           4u
/* The forearm, for a rig that ends at it. Four of the 98 rows the panel offers carry no `rhand`:
 * `destroyr` and `tatcrit` stop at the forearm, and `jawa` and `jawagun` stop at a node named
 * `rarm`, which the engine's own 33 name table does not contain at all. On those four NEITHER the
 * hilt NOR the blade was drawn and nothing said so.
 *
 * IT IS 27 AND NOT 23. The table at 0x004AA448 was read out one entry at a time: 23 is `gun09`,
 * which is also weapon row ten, so the old constant asked a rig for a gun the placement then hid
 * on it, and no forearm was ever found by this file. A forearm is not a fist, so the weapon sits
 * further up the arm than it should; that is visibly a compromise and better than nothing at
 * all. */
#define NAME_ID_RFOREARM       27u
#define NAME_ID_WEAPON          7u
/* The blade, which is the name the engine's own spawn resolves the blade node with: the push of 9
 * at 0x00448174 in front of the lookup at 0x00448180, whose answer it stores at pr+0x4C. */
#define NAME_ID_BLADE           9u

/* A stamp the frame counter cannot hold: it is seeded to 1000 and only counts up. */
#define POSE_STAMP_UNBUILT     0x80000000u

/* What this module holds that is not a body: the resolution, which happens once per process. */
static struct {
    bool                   tried;
    bool                   resolved;
    character_prop_sites_t sites;
} prop;

static const float IDENTITY_MATRIX[PROP_MATRIX_FLOATS] = {
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
};

/* ============================================================================================ */

static void *handle_of(prop_body_t *body)
{
    return (void *)&body->handle[0];
}

bool character_prop_sites_ready(void)
{
    if (!prop.tried) {
        prop.tried = true;
        prop.resolved = character_prop_sites_resolve(&prop.sites);
    }
    return prop.resolved;
}

const character_prop_sites_t *character_prop_sites(void)
{
    return character_prop_sites_ready() ? &prop.sites : NULL;
}

/* The engine's own string for a name id, and the node of a model that carries it. Both are
 * character_prop_rig's; the table they are read out of is this file's, because this file is what
 * resolved it. */
static const char *name_of_id(uint32_t name_id, char *buffer)
{
    return character_prop_rig_name(prop.sites.name_table, name_id, buffer);
}

static bool node_by_name_id(uintptr_t model, uint32_t name_id, uint32_t *out_slot,
                            uintptr_t *out_node)
{
    return character_prop_rig_node(model, prop.sites.name_table, name_id, out_slot, out_node);
}

/* ============================================================================================ */

/* The rest chain of the reference rig, taken from the engine rather than rebuilt here.
 *
 * The reference rig is the player's own model for the row of bank 0 and a far player's hero model
 * for every other row, so the lines below name the model the weapon meshes live in rather than the
 * player: for three of the four rows in the table it is somebody else's asset. */
static bool measure_reference(prop_body_t *body)
{
    uintptr_t nodes = 0;
    uintptr_t node = 0;
    float     chain[PROP_MATRIX_FLOATS];

    if (!character_prop_rig_nodes(body->reference_model, &nodes,
                                  &body->reference_nodes)) {
        return false;
    }
    if (!node_by_name_id(body->reference_model, NAME_ID_RHAND, &body->hand_slot, &node)) {
        log_warning("the model the weapon meshes live in carries no right hand, so there is no "
                    "frame to hold a weapon in and none is drawn");
        return false;
    }
    if (!character_prop_rig_pivot(node, body->hand_pivot)) {
        log_warning("the right hand of the model the weapon meshes live in states no pivot, so "
                    "the joint the weapon hangs off cannot be named and none is drawn");
        return false;
    }
    *(volatile uint32_t *)((uintptr_t)handle_of(body) + RDTHING_POSE_STAMP) = POSE_STAMP_UNBUILT;
    prop.sites.build_joints(handle_of(body), IDENTITY_MATRIX);

    if (!character_prop_rig_joint((uintptr_t)handle_of(body), body->hand_slot, chain)) {
        return false;
    }
    if (!character_prop_mat_invert(body->hand_inverse, chain)) {
        log_warning("the rest chain to that model's right hand departs from a rotation by %d "
                    "thousandths, so its transpose is not its inverse and no weapon is placed",
                    (int)(character_prop_mat_skew(chain) * 1000.0f));
        return false;
    }
    return true;
}

bool character_prop_bind_reference(prop_body_t *body, uintptr_t reference_model)
{
    character_prop_blade_forget(body);   /* the mesh named below is the OUTGOING model's */
    if (body->reference_model != 0) {
        prop.sites.free_arrays(handle_of(body));
    }
    body->reference_model = 0;
    memset(body->handle, 0, sizeof body->handle);
    if (prop.sites.thing_init(handle_of(body), NULL) == 0 ||
        prop.sites.set_model(handle_of(body), (void *)reference_model) == 0) {
        log_warning("a second render handle on %08X could not be built, so nothing is carried on "
                    "the borrowed body it would be drawn at", (unsigned)reference_model);
        return false;
    }
    body->reference_model = reference_model;
    if (!measure_reference(body)) {
        prop.sites.free_arrays(handle_of(body));
        body->reference_model = 0;
        return false;
    }
    (void)character_prop_blade_bind(body, reference_model, prop.sites.name_table, NAME_ID_BLADE);
    return true;
}

/* ============================================================================================ */

/* One node of the drawn weapon, kept so that the sphere the engine asks for can be built from every
 * mesh that is visible rather than from the one the weapon table names. */
static void record_part(prop_body_t *body, uintptr_t meshes, uintptr_t node, uint32_t mesh_index)
{
    const uintptr_t record = meshes + (uintptr_t)mesh_index * MESH_BYTES;
    prop_part_t     part;

    if (meshes == 0u || body->shown_part_count >= PROP_PARTS_MAX ||
        !memory_try_read(node + NODE_MATRIX_SLOT, &part.slot, sizeof part.slot) ||
        part.slot >= body->reference_nodes ||
        !memory_try_read(record + MESH_CENTRE, part.centre, sizeof part.centre) ||
        !memory_try_read(record + MESH_RADIUS, &part.radius, sizeof part.radius)) {
        return;
    }
    body->shown_part[body->shown_part_count++] = part;
}

/* The mesh visibility table is one word per NODE, which is how the engine sizes it. `words` is that
 * count, read from the model the handle is wearing at the time rather than from a remembered
 * number, so the index and the length of the table it addresses come from one model. */
static void show_subtree(prop_body_t *body, uintptr_t table, uint32_t words, uintptr_t meshes,
                         uintptr_t node, uint32_t budget)
{
    uint32_t child = 0;
    int32_t  mesh = 0;

    if (budget == 0u) {
        return;                        /* links that do not form a tree must still terminate */
    }
    if (memory_try_read(node + NODE_MESH_INDEX, &mesh, sizeof mesh) && mesh >= 0 &&
        (uint32_t)mesh < words) {
        *(volatile uint32_t *)(table + 4u * (uintptr_t)mesh) = 0u;
        record_part(body, meshes, node, (uint32_t)mesh);
    }
    if (!memory_try_read(node + NODE_FIRST_CHILD, &child, sizeof child)) {
        return;
    }
    while (child != 0u && budget != 0u) {
        uint32_t next = 0;

        show_subtree(body, table, words, meshes, (uintptr_t)child, budget - 1u);
        if (!memory_try_read((uintptr_t)child + NODE_NEXT_SIBLING, &next, sizeof next)) {
            return;
        }
        child = next;
        --budget;
    }
}

/* What the equipped slot names, made visible and measured. Everything is hidden first and the
 * equipped node's whole subtree shown again, so a lightsaber brings its blade and its two glow
 * cards. The same walk collects every mesh it shows, because those meshes together are the weapon,
 * and the sphere the engine will be answered with has to describe all of them. */
static bool show_only(prop_body_t *body, uint32_t name_id)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  worn = 0;
    uint32_t  slot = 0;
    uintptr_t top = 0;
    uint32_t  meshes = 0;
    uint32_t  table = 0;
    uint32_t  i;

    body->shown_part_count = 0;
    /* The table is read out of the handle and its length out of the model, so the handle has to be
     * wearing that model for the two to belong together. It always is, because nothing but this
     * module binds this handle; asking costs one read and makes the loop below bounded by
     * something that was measured rather than by something that was remembered. */
    if (!memory_try_read((uintptr_t)handle_of(body) + RDTHING_MODEL3, &worn, sizeof worn) ||
        (uintptr_t)worn != body->reference_model ||
        !character_prop_rig_nodes(body->reference_model, &nodes, &count) ||
        !memory_try_read((uintptr_t)handle_of(body) + RDTHING_MESH_HIDDEN, &table, sizeof table) ||
        table == 0u || !node_by_name_id(body->reference_model, name_id, &slot, &top)) {
        return false;
    }
    (void)memory_try_read(body->reference_model + MODEL_GEOSET0_MESHES, &meshes, sizeof meshes);
    for (i = 0; i < count; ++i) {
        *(volatile uint32_t *)((uintptr_t)table + 4u * (uintptr_t)i) = 1u;
    }
    /* The record the lookup landed on, not `nodes + slot * NODE_BYTES`. `node+0x44` is the MATRIX
     * slot and it is the array position only as long as the two agree, which is a property of the
     * assets and not of the format: measured, `num == index` on all 266 shipped ones. The walk
     * needs the record, the visibility table needs the slot, and the two are read from the field
     * that means each. */
    show_subtree(body, (uintptr_t)table, count, (uintptr_t)meshes, top, count);
    return true;
}

/* ============================================================================================ */

/* The weapon geometry the borrowed rig carries of its own, so the player does not wear two.
 *
 * This used to ask the rig for the eleven names the weapon configuration happens to use, plus the
 * mount, and that is the wrong question: it names the nodes the ENGINE'S OWN REMOUNT can show on a
 * hero, not the nodes a rig can have. Measured: of the 132 shipped rigs that carry a hand at all,
 * 28 were left wearing a second weapon, 26 of them rows the panel offers. Twenty carry a node
 * called `gun` on a hand, and six, `obi`, `obiwan`, `quigon`, `quigung`, `quiweap` and `mace`,
 * carry `sabrewaist`, a lightsaber hilt hanging on the WAIST.
 *
 * The walk is character_mount's, because that module already owns what weapon geometry IS and the
 * two halves of this feature then cannot disagree about it. The mount is named out of the engine's
 * own table here and handed over rather than spelled there. */
static void collect_target_weapon_nodes(prop_body_t *body, uintptr_t model)
{
    char mount[PROP_NAME_BYTES];

    body->hidden_from = PROP_HIDDEN_COLLECTED;
    body->hidden_count =
        character_mount_own_weapon_nodes(model, body->target_hand_node,
                                         name_of_id(NAME_ID_WEAPON, mount), body->hidden,
                                         PROP_HIDE_MAX);
}

uint32_t character_prop_slots_outside(const uint32_t *slots, uint32_t count, uint32_t words)
{
    uint32_t outside = 0;
    uint32_t i;

    if (slots == NULL) {
        return 0u;
    }
    for (i = 0; i < count; ++i) {
        if (slots[i] >= words) {
            outside++;
        }
    }
    return outside;
}

/* The slots and the table must come out of one model. When they did not, a hero's slots written
 * into a smaller rig's table ran off the end of an engine allocation into the pool header behind
 * it, and the process died in an unrelated function a swap later. Both halves are checked, and
 * either alone would have been enough: the handle must still wear the model the slots were resolved
 * against, and every slot is measured against that model's own node count.
 *
 * A row that was LENT its words writes none of them: the module that collected them writes them,
 * and one visibility word with two writers is how a rig ends up with half of itself invisible. */
void character_prop_write_own_weapons(prop_body_t *body, uint32_t value)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  worn = 0;
    uint32_t  hidden = 0;
    uint32_t  i;

    if (body->hidden_from != PROP_HIDDEN_COLLECTED || body->hidden_count == 0u ||
        body->thing == 0 || body->target_model == 0) {
        return;
    }
    if (!memory_try_read(body->thing + RDTHING_MODEL3, &worn, sizeof worn) ||
        (uintptr_t)worn != body->target_model ||
        !character_prop_rig_nodes(body->target_model, &nodes, &count) ||
        !memory_try_read(body->thing + RDTHING_NODE_HIDDEN, &hidden, sizeof hidden) ||
        hidden == 0u) {
        return;
    }
    for (i = 0; i < body->hidden_count; ++i) {
        if (body->hidden[i] < count) {
            *(volatile uint32_t *)((uintptr_t)hidden + 4u * (uintptr_t)body->hidden[i]) = value;
        }
    }
}

/* ============================================================================================ */

/* Which slot `model`'s right hand sits in, and the matrix that re-parents the weapon onto it. */
static bool resolve_hand_on(prop_body_t *body, uintptr_t model)
{
    uintptr_t node = 0;
    float     target_pivot[3];

    if (!node_by_name_id(model, NAME_ID_RHAND, &body->target_hand_slot, &node) ||
        !character_prop_rig_pivot(node, target_pivot)) {
        /* The forearm, and it is named rather than assumed: a rig that has no fist still has an
         * arm, and hanging the weapon there is the difference between a compromise and an empty
         * hand. Its pivot is its joint exactly as a hand's is, so nothing downstream has to know
         * which of the two answered. */
        if (!node_by_name_id(model, NAME_ID_RFOREARM, &body->target_hand_slot, &node) ||
            !character_prop_rig_pivot(node, target_pivot)) {
            log_warning("this rig has neither a right hand nor a right forearm the weapon could "
                        "hang on, so the body is worn and carries nothing");
            return false;
        }
        log_info("this rig has no right hand, so the weapon hangs on its right forearm and sits "
                 "further up the arm than it would on a fist");
    }
    character_prop_mat_reparent(body->hand_to_hand, body->hand_inverse, body->hand_pivot,
                                target_pivot);
    body->target_model = model;
    body->target_hand_node = node;   /* kept so the chain the weapon hangs off is never hidden */
    return true;
}

/* Which slot the borrowed rig's right hand sits in, where in that hand a weapon is held and how
 * big the hand is. Re-resolved whenever the body changes model, which is also when the hand to
 * hand matrix is rebuilt: another model can be put on the same body, and every model states its
 * own hand.
 *
 * A far row is not re-resolved here. Its rig was settled when the row was armed and the words that
 * hide that rig's own weapon were lent to it by the module that collected them. A handle wearing
 * something else is a body the far path has to let go of, and collecting a second set of those
 * words for it would give one visibility word two writers. */
static bool target_hand(prop_body_t *body)
{
    uint32_t model = 0;

    if (!memory_try_read(body->thing + RDTHING_MODEL3, &model, sizeof model) || model == 0u) {
        return false;
    }
    if ((uintptr_t)model == body->target_model) {
        return true;
    }
    if (body->block != 0) {
        return false;
    }
    character_prop_body_forget(body);   /* the outgoing model's words, and its slots forgotten */
    if (!resolve_hand_on(body, (uintptr_t)model)) {
        return false;
    }
    collect_target_weapon_nodes(body, body->target_model);
    if (body->hidden_count != 0u) {
        log_info("the borrowed rig carries %u weapon nodes of its own, and they are hidden on its "
                 "render handle so only the player's weapon is drawn",
                 (unsigned)body->hidden_count);
    }
    return true;
}

/* The node the equipped slot shows, or false for slot zero, the empty hand the remount skips. */
static bool equipped_name_id(uintptr_t block, uint32_t *out_name_id)
{
    uint32_t slot = 0;
    uint32_t name_id = 0;

    if (!memory_try_read(block + PLAYER_WEAPON_SLOT, &slot, sizeof slot) ||
        slot == 0u || slot >= WEAPON_SLOTS ||
        !memory_try_read(prop.sites.weapon_cfg + (uintptr_t)slot * WEAPON_ROW_BYTES, &name_id,
                         sizeof name_id) || name_id >= PROP_NAME_COUNT) {
        return false;
    }
    *out_name_id = name_id;
    return true;
}

bool character_prop_resolve_hand(prop_body_t *body, char weapon[PROP_NAME_BYTES])
{
    uintptr_t block = 0;
    uint32_t  model = 0;
    uint32_t  name_id = 0;

    if (body == NULL ||
        !memory_try_read(body->thing + RDTHING_MODEL3, &model, sizeof model) || model == 0u ||
        !resolve_hand_on(body, (uintptr_t)model)) {
        return false;
    }
    if (!character_prop_block_of(body, &block) || !equipped_name_id(block, &name_id) ||
        name_of_id(name_id, weapon) == NULL) {
        memcpy(weapon, "empty hand", 11u);
    }
    return true;
}

void character_prop_release_reference(prop_body_t *body)
{
    if (body == NULL || body->reference_model == 0) {
        return;
    }
    character_prop_blade_forget(body);   /* the asset that mesh is in is about to be given up */
    prop.sites.free_arrays(handle_of(body));
    memset(body->handle, 0, sizeof body->handle);
    body->reference_model = 0;
}

/* The world sphere of everything the handle has just drawn, merged from the parts. It is built here
 * and not when the engine asks, because the parts ride different joints and can only be combined
 * once each one is in world space, and the matrices they need are the ones the draw above left
 * behind. False means nothing measurable was drawn and the engine's own answer must stand. */
static bool place_weapon_sphere(prop_body_t *body)
{
    float    matrix[PROP_MATRIX_FLOATS];
    float    point[3];
    bool     first = true;
    uint32_t i;

    for (i = 0; i < body->shown_part_count; ++i) {
        float radius;

        if (!character_prop_rig_joint((uintptr_t)handle_of(body),
                                      body->shown_part[i].slot, matrix)) {
            continue;
        }
        character_prop_mat_point(point, body->shown_part[i].centre, matrix);
        /* The centre comes through the matrix and the radius does not, so the radius has to be
         * taken through the same scale by hand. The matrix carries two: the borrowed actor's own,
         * which the engine put there, and this module's own hand ratio. A merge that mixed a
         * scaled centre with an unscaled radius would answer the wrong CENTRE, and the centre is
         * the half the fire path reads. */
        radius = body->shown_part[i].radius * character_prop_mat_scale(matrix);
        if (first) {
            memcpy(body->shown_centre, point, sizeof body->shown_centre);
            body->shown_radius = radius;
            first = false;
        } else {
            character_prop_sphere_merge(body->shown_centre, &body->shown_radius, point, radius);
        }
    }
    return !first;
}

void character_prop_before_thing_draw(const void *thing, const float *root)
{
    prop_body_t *body;
    uintptr_t    block = 0;
    uint32_t     name_id = 0;
    void        *drawn = NULL;
    float        hand[PROP_MATRIX_FLOATS];
    float        place[PROP_MATRIX_FLOATS];

    if (character_prop_body_is_drawing()) {
        return;                        /* the prop's own draw comes back through this hook */
    }
    if (root == NULL) {
        return;
    }
    /* The first object dispatched after a scene end opens the pass every sphere is stamped with,
     * and it is asked for here, before the row is looked up, because it has to happen once per
     * frame rather than once per body carrying a weapon. */
    character_prop_body_begin_pass();
    body = character_prop_body_for_thing(thing);
    if (body == NULL) {
        return;                        /* every other drawn object, and this runs for all of them */
    }
    /* The sphere is cleared here rather than above, because clearing it for somebody else's body
     * would throw away the weapon matrix the muzzle answer needs. */
    character_prop_body_place(body, false);
    if (!character_prop_body_still_ours(body, &block)) {
        character_prop_body_disarm(body);
        return;
    }
    if (!target_hand(body)) {
        return;
    }
    character_prop_write_own_weapons(body, 1u);
    if (!equipped_name_id(block, &name_id)) {
        return;                        /* the empty hand */
    }
    if (name_id != body->shown_name_id) {
        if (!show_only(body, name_id)) {
            body->shown_name_id = PROP_NAME_ID_NONE;
            return;
        }
        body->shown_name_id = name_id;
        /* On a weapon change, which is a setting rather than a frame. It is the one place a reader
         * can tell whether the blow is measured from the whole weapon or from its grip alone. */
        log_info("the drawn weapon is measured from %u of its own meshes, and that sphere is what "
                 "answers the shot and the blow", (unsigned)body->shown_part_count);
    }

    /* The borrowed rig's joint matrices, rebuilt with the matrix this hook was handed, every frame
     * and without a condition. This used to ask whether `thing+0x1C` differed from the counter at
     * 0x004B8860 first, which is the question rdThing_Draw asks at 0x00410029 before it rebuilds.
     * That question was the wrong one HERE:
     *
     *   * the counter is a SUBSTEP counter, so it stands still across most rendered frames;
     *   * rdPuppet_buildJointMatrices stamps it at 0x004846A6 at the end of every build;
     *   * framerate_fix NOPs the `je` at 0x0041002E, so the body is rebuilt every frame anyway.
     *
     * So on every frame that did not begin a new substep, the stamp already equalled the counter,
     * this hook skipped, and the hand matrix read below was the one the PREVIOUS frame's draw left
     * behind, built against the previous frame's interpolated root. The body then went on to be
     * rebuilt for this frame and drawn correctly. At 60 fps that is a weapon one frame behind its
     * own hand on roughly half the frames and current on the others, which is a beat against the
     * substep clock rather than a lag: the weapon stutters while the body runs smoothly.
     *
     * Rebuilding unconditionally costs one skeleton concatenation for one body per frame. The
     * argument matrix is the same one the original is about to use, so the two builds agree, and
     * when the throttle is NOT removed this build is the only one: it stamps the counter itself
     * and rdThing_Draw then skips, which leaves the body drawn from these matrices. */
    prop.sites.build_joints((void *)body->thing, root);
    if (!character_prop_rig_joint(body->thing, body->target_hand_slot, hand)) {
        return;
    }

    character_prop_mat_compose(place, hand, body->hand_to_hand);

    *(volatile uint32_t *)((uintptr_t)handle_of(body) + RDTHING_POSE_STAMP) = POSE_STAMP_UNBUILT;
    character_prop_body_drawing(true);
    character_prop_blade_open(body, block);
    drawn = prop.sites.thing_draw(handle_of(body), place);
    character_prop_blade_close(body);
    character_prop_body_drawing(false);

    /* The draw answers whether it drew, and the sphere may not be measured without it. The
     * dispatcher answers 0 while the render module is down and rdThing_Draw answers 0 at
     * 0x0040FFD0 when the object failed its own visibility test, and on both of those it returns
     * BEFORE it builds the pose. The matrices place_weapon_sphere reads are then last frame's, or,
     * on the first frame after a bind, an allocation nothing has written: rdThing_SetModel zeroes
     * thing+0x24, +0x28 and +0x2c and NOT thing+0x20. The muzzle and the blow would be answered
     * from uninitialised heap, which is the shot from thin air this module exists to end. */
    character_prop_body_place(body, (drawn != NULL) && place_weapon_sphere(body));
    if (body->block != 0 && body->placed) {
        character_prop_body_tally(PROP_TALLY_FROM_DRAW);
    }
}

/* Whether a pose was ever built on this render handle. rdThing_SetModel leaves the joint matrix
 * array unwritten, and the rebind stamps thing+0x1C with a value the frame counter cannot hold;
 * the counter is seeded to 1000 and only counts up, so the top bit set says no build has
 * happened. Reading matrices that were never built is the heap this module exists to keep out of
 * a contact point. */
static bool body_pose_is_built(uintptr_t thing)
{
    uint32_t stamp = 0;

    return memory_try_read(thing + RDTHING_POSE_STAMP, &stamp, sizeof stamp) && stamp != 0u &&
           (stamp & POSE_STAMP_UNBUILT) == 0u;
}

/* The weapon sphere of a body this pass did not draw, which for a far player is the ordinary case:
 * behind the camera, outside the frustum, or simply asked about before the frame it appears in.
 *
 * Everything the draw does is done here except the draw. The hand is asked for again, what the
 * block says is equipped is made visible on the prop's own handle and its meshes collected, the
 * prop's joints are built against the body's live hand, and the parts are merged into one world
 * sphere. What is NOT done here is building the body's own pose: that is the engine's, it is read
 * as it stands, and it is only read at all when the handle's stamp says a pose was built on it. */
static bool measure_without_a_draw(prop_body_t *body, uint32_t name_id)
{
    float hand[PROP_MATRIX_FLOATS];
    float place[PROP_MATRIX_FLOATS];

    if (!body_pose_is_built(body->thing) || !target_hand(body)) {
        return false;
    }
    if (name_id != body->shown_name_id) {
        if (!show_only(body, name_id)) {
            body->shown_name_id = PROP_NAME_ID_NONE;
            return false;
        }
        body->shown_name_id = name_id;
    }
    if (!character_prop_rig_joint(body->thing, body->target_hand_slot, hand)) {
        return false;
    }
    character_prop_mat_compose(place, hand, body->hand_to_hand);
    *(volatile uint32_t *)((uintptr_t)handle_of(body) + RDTHING_POSE_STAMP) = POSE_STAMP_UNBUILT;
    prop.sites.build_joints(handle_of(body), place);
    /* Only a measurement that came to something is stamped, and none of the ways out above writes
     * the row either. A stamp written for a failure clears `placed`, and with it the last sphere
     * the row did measure, so a row that had one would fall through to the engine instead of
     * answering with it: a failed attempt would be worse than never having tried. */
    if (!place_weapon_sphere(body)) {
        return false;
    }
    character_prop_body_place(body, true);
    return true;
}

/* ============================================================================================ */

/* The hand of one row, with the handle and the model on its body read RIGHT NOW rather than the
 * ones this module last saw. The player spawn builds a fresh body and resolves six node names on
 * it, one of them the blade cell the shared mesh resize hangs off. A pooled body can come back at
 * the same address with a new handle, and a reloaded model at the old model's address, so the
 * handle is compared first. */
static bool weapon_node_of(prop_body_t *body, int32_t *out_node)
{
    uint32_t thing = 0;
    uint32_t model = 0;

    if (body == NULL || body->target_model == 0) {
        return false;
    }
    if (!memory_try_read(body->obj + PROP_OBJ_THING, &thing, sizeof thing) ||
        (uintptr_t)thing != body->thing ||
        !memory_try_read(body->thing + RDTHING_MODEL3, &model, sizeof model) ||
        (uintptr_t)model != body->target_model) {
        return false;
    }
    *out_node = (int32_t)body->target_hand_slot;
    return true;
}

bool character_prop_weapon_node(const void *obj, int32_t *out_node)
{
    if (out_node == NULL) {
        return false;
    }
    return weapon_node_of(character_prop_body_for_obj(obj), out_node);
}

/* What the lookup answers for `name_id` on the borrowed model: the model's own node when it
 * carries the name, and otherwise whatever the substitution puts there. Both sides of that come
 * from one rule, so the node a caller resolved one step earlier and the node answered here cannot
 * be different numbers. */
static uint32_t resolved_node_of(prop_body_t *body, uint32_t name_id)
{
    char     wanted[PROP_NAME_BYTES];
    uint32_t answered = 0;
    int32_t  substitute;

    if (node_by_name_id(body->target_model, name_id, &answered, NULL)) {
        return answered;
    }
    if (!character_mount_is_armed() || name_of_id(name_id, wanted) == NULL) {
        return 0u;
    }
    substitute = character_mount_answer(wanted, 0, (int32_t)body->target_hand_slot);
    return (substitute == MOUNT_NO_NODE) ? 0u : (uint32_t)substitute;
}

/* A weapon name the borrowed rig does carry, found on a node that holds no sphere where the weapon
 * is drawn. The rule is character_mount's; the hidden words and the node answered instead are this
 * module's, the second out of the same resolved_node_of the sphere below compares against, so the
 * contact node the starter stores is one this module answers for. */
int32_t character_prop_found_node(const void *obj, const char *wanted, int32_t found)
{
    prop_body_t *body = character_prop_body_for_obj(obj);
    uintptr_t    block = 0;
    uint32_t     name_id = 0;
    int32_t      hand = 0;
    int32_t      node;

    if (!weapon_node_of(body, &hand) || !character_prop_body_still_ours(body, &block) ||
        !equipped_name_id(block, &name_id)) {
        return MOUNT_NO_NODE;
    }
    node = character_mount_answer_found(body->target_model, wanted, found, body->hidden,
                                        body->hidden_count,
                                        (int32_t)resolved_node_of(body, name_id));
    if (node != MOUNT_NO_NODE && !body->found_said) {
        body->found_said = true;
        log_info("a sabre name on this rig answers a node with no mesh or a hidden one; the node "
                 "the drawn weapon's sphere answers for is given instead");
    }
    return node;
}

/* The sphere of the weapon a body is carrying, and on a borrowed rig it may not be the engine's.
 *
 * The engine answers the sphere of the NODE it was asked about, and the pair pass behind it
 * replaces only the RADIUS of what comes back, never the centre. On a borrowed rig that node is
 * the fist the weapon was hung on, so the shot would leave the hand and the blow would land at the
 * hand whatever is in it and however long the blade is. Counted over the shipped assets, every rig
 * that carries a right hand carries a mesh on it; the node WITHOUT one, which bapobj_nodeSphere
 * turns back on without writing a centre at all, is the blade node, and a fall through there is a
 * contact point taken off somebody's stack.
 *
 * So a row answers four ways, in this order: out of the draw of this pass; out of a measurement
 * taken here for a body this pass did not draw; out of the last sphere it did manage to measure;
 * and, only when it has never measured one at all, not at all. */
bool character_prop_node_sphere(const void *obj, int32_t node_index, float out_centre[3],
                                float *out_radius)
{
    prop_body_t *body;
    uintptr_t    block = 0;
    uint32_t     name_id = 0;

    if (out_centre == NULL || out_radius == NULL) {
        return false;
    }
    body = character_prop_body_for_obj(obj);
    /* The row is asked whether it still holds this body, and not merely which block names it: a
     * body that died between one draw and the next is answered for by nobody here. */
    if (body == NULL || !character_prop_body_still_ours(body, &block) ||
        !equipped_name_id(block, &name_id)) {
        return false;
    }
    /* The index the caller resolved one step earlier. Anything else means the caller wanted a
     * different node and the original must run. */
    if (node_index < 0 || (uint32_t)node_index != resolved_node_of(body, name_id)) {
        return false;
    }
    /* Measuring is the far row's alone, and holding is every row's. The player's own body is
     * drawn in every frame it is dispatched in, so a measurement taken for it here would be a
     * second way to one answer; the sphere its last draw measured is not a second way, it is the
     * same sphere one frame older, and it is what answered before the pass stamp existed. */
    if (!character_prop_body_is_placed(body) || name_id != body->shown_name_id) {
        if (body->block == 0) {
            if (!body->placed || name_id != body->shown_name_id) {
                return false;
            }
            if (!body->older_said) {
                body->older_said = true;
                log_info("a frame went by without the player's own body being dispatched, so his "
                         "weapon answers out of the sphere the last frame that drew it measured");
            }
        } else if (measure_without_a_draw(body, name_id)) {
            character_prop_body_tally(PROP_TALLY_MEASURED);
        } else if (body->placed) {
            character_prop_body_tally(PROP_TALLY_OLDER);
        } else {
            character_prop_body_tally(PROP_TALLY_UNANSWERED);
            return false;
        }
    }
    memcpy(out_centre, body->shown_centre, 3u * sizeof out_centre[0]);
    *out_radius = body->shown_radius;
    return true;
}
