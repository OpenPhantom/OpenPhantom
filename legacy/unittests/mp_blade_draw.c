/* A far Jedi's blade drawn from vertices of its own, over an engine made up here, with no game.
 *
 * The model, its node array, its meshes, the body, its render handle and the far bank's block are
 * all laid out at the offsets the engine uses, with a decoy wherever a wrong offset or one read too
 * few would land: a second node named sabreblad01 behind the first, a model word in front of the
 * handle's model, a hero and a node beside the block's own. The engine calls are stood in for by
 * functions that record what the mesh named while they ran, which is the only place the pointer
 * exchange can be seen from outside. The mesh array lives on a page of its own, so a test can make
 * it refuse the write that puts a pointer back.
 *
 * The far bank's block is the bank module's own, which exists without an installed bank. Nothing
 * resolves here, so the two hulls are never placed and the frame witness is called by hand.
 *
 * SIZE NOTE: over 600 lines, and over a third of them build the made up engine: a model with its
 * node array and meshes is the least the one way to the mesh can be asked about. The seam, if it
 * grows, is the guard and the window watch, which share the made up engine and nothing else.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_blade_draw.h"
#include "mp_blade_rule.h"

#include <windows.h>

#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BLOCK_ACTOR      0x00u
#define BLOCK_OBJECT     0x0Cu
#define BLOCK_NODE       0x4Cu
#define BLOCK_HERO       0x6Cu
#define BLOCK_BLADE      0x1C8u
#define BLOCK_SIZE       0x210u
#define OBJECT_ACTOR     0x14u
#define OBJECT_THING     0x9Cu
#define THING_MODEL      0x04u
#define ACTOR_MODEL      0xE0u
#define MODEL_MESHES     0x28u
#define MODEL_NODE_COUNT 0x54u
#define MODEL_NODES      0x58u
#define NODE_BYTES       0xB4u
#define NODE_SLOT        0x44u
#define NODE_MESH        0x4Cu
#define MESH_BYTES       0x70u
#define MESH_VERTS       0x30u
#define MESH_COUNT       0x48u

#define FAR_BANK   1u
#define BLADE_SLOT 2u   /* the matrix slot of the blade node in the hero's rig */

typedef struct made_thing {
    uint8_t bytes[0x100];
} made_thing_t;

static uint8_t      s_hero_nodes[4 * NODE_BYTES];   /* dummy01, sabre, sabreblad01 twice */
static uint8_t      s_bare_nodes[1 * NODE_BYTES];   /* dummy01 alone */
static uint8_t      s_hop_nodes[2 * NODE_BYTES];    /* sabreblad01 whose slot names the other */
static uint8_t      s_three_nodes[1 * NODE_BYTES];  /* sabreblad01 on a mesh of three */
static made_thing_t s_hero_model;
static made_thing_t s_bare_model;
static made_thing_t s_hop_model;
static made_thing_t s_three_model;
static made_thing_t s_hero_actor;
static made_thing_t s_bare_actor;
static made_thing_t s_far_object;
static made_thing_t s_far_thing;
static made_thing_t s_other_object;
static made_thing_t s_other_thing;
static made_thing_t s_clone_object;   /* another object whose render handle is the far body's */
static uint8_t      s_local_block[0x100];
static uint8_t     *s_meshes;          /* four meshes on a page of their own */
static float        s_shared[12];      /* the asset's blade, mesh 1 */
static float        s_decoy[12];       /* the second sabreblad01's, mesh 2 */
static float        s_hilt_mesh[24];   /* the hilt, mesh 0 */

static const float HILT[2][3] = { { -0.0654f, 0.0100f, 0.0546f }, { -0.0412f, 0.0103f, 0.0536f } };
static const float LENGTH[2]  = { 0.4269f, 0.4274f };

static void put_word(void *base, uint32_t offset, uint32_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof value);
}

static uint32_t word_at(const void *base, uint32_t offset)
{
    uint32_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof value);
    return value;
}

static uint32_t address_of(const void *thing)
{
    return (uint32_t)(uintptr_t)thing;
}

static uint8_t *mesh(uint32_t index)
{
    return s_meshes + index * MESH_BYTES;
}

static uint32_t mesh_pointer(uint32_t index)
{
    return word_at(mesh(index), MESH_VERTS);
}

/* The blade at `size` of its length, the long way round. */
static void blade_at(float size, float out[12])
{
    int edge;
    int axis;

    for (edge = 0; edge < 2; ++edge) {
        for (axis = 0; axis < 3; ++axis) {
            out[3 * edge + axis]     = HILT[edge][axis];
            out[6 + 3 * edge + axis] = HILT[edge][axis];
        }
        out[6 + 3 * edge + 1] += LENGTH[edge] * size;
    }
}

static bool near_all(const float *a, const float *b)
{
    int i;

    for (i = 0; i < 12; ++i) {
        if (fabs((double)a[i] - (double)b[i]) > 1e-6) {
            return false;
        }
    }
    return true;
}

static void make_node(uint8_t *nodes, uint32_t index, const char *name, int32_t slot,
                      int32_t mesh_index)
{
    uint8_t *node = nodes + index * NODE_BYTES;

    memset(node, 0, NODE_BYTES);
    memcpy(node, name, strlen(name));
    memcpy(node + NODE_SLOT, &slot, sizeof slot);
    memcpy(node + NODE_MESH, &mesh_index, sizeof mesh_index);
}

static void make_model(made_thing_t *model, const uint8_t *nodes, uint32_t count)
{
    memset(model, 0, sizeof *model);
    put_word(model->bytes, MODEL_MESHES, address_of(s_meshes));
    put_word(model->bytes, MODEL_NODE_COUNT, count);
    put_word(model->bytes, MODEL_NODES, address_of(nodes));
}

static void make_mesh(uint32_t index, const float *verts, uint32_t count)
{
    memset(mesh(index), 0, MESH_BYTES);
    put_word(mesh(index), MESH_VERTS, address_of(verts));
    put_word(mesh(index), MESH_COUNT, count);
}

/* The far bank's block: hero `hero`, the blade node `node`, standing with the far body, its
 * vectors the hero's and its length `size`. */
static void make_far_block(uint32_t hero, uint32_t node, float size)
{
    uint8_t *block = (uint8_t *)mp_bank_block_at(FAR_BANK);
    float    vectors[18];
    int      edge;
    int      axis;

    memset(block, 0, MP_BANK_HERO_BLOCK_BYTES);
    put_word(block, BLOCK_OBJECT, address_of(&s_far_object));
    put_word(block, BLOCK_HERO, hero);
    put_word(block, BLOCK_HERO - 4u, 1u);
    put_word(block, BLOCK_NODE, node);
    put_word(block, BLOCK_NODE + 4u, BLADE_SLOT);
    memset(vectors, 0, sizeof vectors);
    for (edge = 0; edge < 2; ++edge) {
        for (axis = 0; axis < 3; ++axis) {
            vectors[3 * edge + axis]     = HILT[edge][axis];
            vectors[6 + 3 * edge + axis] = HILT[edge][axis];
        }
        vectors[6 + 3 * edge + 1] += LENGTH[edge];
        vectors[12 + 3 * edge + 1] = LENGTH[edge];
    }
    memcpy(block + BLOCK_BLADE, vectors, sizeof vectors);
    memcpy(block + BLOCK_SIZE, &size, sizeof size);
}

static void set_size(float size)
{
    memcpy((uint8_t *)mp_bank_block_at(FAR_BANK) + BLOCK_SIZE, &size, sizeof size);
}

static bool set_up(void)
{
    s_meshes = (uint8_t *)VirtualAlloc(NULL, 4096u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (s_meshes == NULL) {
        return false;
    }
    blade_at(1.0f, s_shared);
    blade_at(0.25f, s_decoy);
    make_mesh(0u, s_hilt_mesh, 8u);
    make_mesh(1u, s_shared, 4u);
    make_mesh(2u, s_decoy, 4u);
    make_mesh(3u, s_decoy, 3u);

    make_node(s_hero_nodes, 0u, "dummy01", 0, -1);
    make_node(s_hero_nodes, 1u, "sabre", 1, 0);
    make_node(s_hero_nodes, 2u, "sabreblad01", (int32_t)BLADE_SLOT, 1);
    make_node(s_hero_nodes, 3u, "sabreblad01", 3, 2);
    make_model(&s_hero_model, s_hero_nodes, 4u);
    make_node(s_bare_nodes, 0u, "dummy01", 0, -1);
    make_model(&s_bare_model, s_bare_nodes, 1u);
    make_node(s_hop_nodes, 0u, "sabreblad01", 1, 2);
    make_node(s_hop_nodes, 1u, "sabreblad", 1, 1);
    make_model(&s_hop_model, s_hop_nodes, 2u);
    make_node(s_three_nodes, 0u, "sabreblad01", 0, 3);
    make_model(&s_three_model, s_three_nodes, 1u);

    put_word(s_hero_actor.bytes, ACTOR_MODEL, address_of(&s_hero_model));
    put_word(s_hero_actor.bytes, ACTOR_MODEL + 4u, address_of(&s_bare_model));
    put_word(s_bare_actor.bytes, ACTOR_MODEL, address_of(&s_bare_model));
    put_word(s_far_object.bytes, OBJECT_ACTOR, address_of(&s_hero_actor));
    put_word(s_far_object.bytes, OBJECT_THING, address_of(&s_far_thing));
    put_word(s_far_thing.bytes, 0u, address_of(&s_bare_model));
    put_word(s_far_thing.bytes, THING_MODEL, address_of(&s_hero_model));
    put_word(s_other_object.bytes, OBJECT_ACTOR, address_of(&s_hero_actor));
    put_word(s_other_object.bytes, OBJECT_THING, address_of(&s_other_thing));
    put_word(s_other_thing.bytes, THING_MODEL, address_of(&s_hero_model));
    put_word(s_local_block, BLOCK_ACTOR, address_of(&s_hero_actor));
    make_far_block(1u, BLADE_SLOT, 1.0f);
    return true;
}

static mp_blade_draw_counters_t counted(void)
{
    mp_blade_draw_counters_t out;

    mp_blade_draw_get_counters(&out);
    return out;
}

static uint32_t watched(mp_blade_window_t kind)
{
    mp_blade_draw_counters_t now = counted();

    return now.watched[kind];
}

static uint32_t changed(mp_blade_window_t kind)
{
    mp_blade_draw_counters_t now = counted();

    return now.changed[kind];
}

/* ==============================================================================================
 * The engine calls, stood in for.
 * ============================================================================================ */

static uint32_t s_seen_pointer;
static float    s_seen_verts[12];
static uint32_t s_inner_pointer;
static bool     s_reenter;
static bool     s_escape;
static bool     s_protect;
static jmp_buf  s_escape_to;

static void see(uint32_t *pointer, float *verts)
{
    *pointer = mesh_pointer(1u);
    memcpy(verts, (const void *)(uintptr_t)*pointer, 12u * sizeof(float));
}

static int32_t __cdecl inner_dispatch(uint32_t thing, uint32_t pose)
{
    float ignored[12];

    (void)thing;
    see(&s_inner_pointer, ignored);
    return (int32_t)pose;
}

static int32_t __cdecl pretend_dispatch(uint32_t thing, uint32_t pose)
{
    DWORD old = 0;

    see(&s_seen_pointer, s_seen_verts);
    if (s_reenter) {
        s_reenter = false;
        (void)mp_blade_draw_dispatch(thing, pose, &inner_dispatch);
    }
    if (s_escape) {
        s_escape = false;
        longjmp(s_escape_to, 1);
    }
    if (s_protect) {
        s_protect = false;
        (void)VirtualProtect(s_meshes, 4096u, PAGE_READONLY, &old);
    }
    return 7 + (int32_t)pose;
}

static void __cdecl pretend_halo(uint32_t object)
{
    (void)object;
    see(&s_seen_pointer, s_seen_verts);
}

static void writable(void)
{
    DWORD old = 0;

    (void)VirtualProtect(s_meshes, 4096u, PAGE_READWRITE, &old);
}

/* ==============================================================================================
 * The checks.
 * ============================================================================================ */

static void check_the_mesh(void)
{
    uint32_t verts = 99u;

    ut_section("the one way to the blade mesh");
    ut_check(mp_blade_draw_mesh_of(address_of(&s_hero_model), &verts) ==
                 (uintptr_t)mesh(1u) && verts == 4u,
             "the first node named sabreblad01 and its mesh of four, not the second of that name");
    ut_check(mp_blade_draw_mesh_of(address_of(&s_hop_model), &verts) == (uintptr_t)mesh(1u),
             "the node the named one's matrix slot names, as the engine's setter indexes it");
    ut_check(mp_blade_draw_mesh_of(address_of(&s_three_model), &verts) == (uintptr_t)mesh(3u) &&
                 verts == 3u,
             "a mesh of another count is found and its count answered, for the caller to refuse");
    ut_check(mp_blade_draw_mesh_of(address_of(&s_bare_model), &verts) == 0u && verts == 0u,
             "a rig without the name has no blade mesh");
    ut_check(mp_blade_draw_mesh_of(0u, &verts) == 0u && mp_blade_draw_mesh_of(16u, NULL) == 0u,
             "no model, or one that does not read, has none either");
}

static void check_the_rows(void)
{
    mp_blade_draw_counters_t before;
    float                    want[12];
    float                    shared[12];
    int32_t                  answer;

    ut_section("a far Jedi's blade is drawn from vertices of its own");
    memcpy(shared, s_shared, sizeof shared);
    make_far_block(1u, BLADE_SLOT, 0.5f);
    mp_blade_draw_fill(FAR_BANK);
    before = counted();
    answer = mp_blade_draw_dispatch(address_of(&s_far_thing), 3u, &pretend_dispatch);
    blade_at(0.5f, want);
    ut_check(answer == 10, "the handle draw answers what the engine answered");
    ut_check(s_seen_pointer != address_of(s_shared) && near_all(s_seen_verts, want),
             "while the engine draws, the mesh names the row's own vertices at half length");
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 memcmp(s_shared, shared, sizeof shared) == 0,
             "and afterwards its own again, the asset's vertices untouched");
    ut_check(counted().draws == before.draws + 1u && counted().refused == before.refused,
             "counted as a draw at a length of its own");

    mp_blade_draw_halo(address_of(&s_far_object), &pretend_halo);
    ut_check(s_seen_pointer != address_of(s_shared) && near_all(s_seen_verts, want) &&
                 mesh_pointer(1u) == address_of(s_shared) &&
                 counted().halos == before.halos + 1u,
             "the halo pass the same, keyed by the object");

    set_size(0.0f);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    blade_at(0.0f, want);
    ut_check(near_all(s_seen_verts, want), "a length of 0 draws the tips on the hilt");
    set_size(1.0f);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(counted().shortest == 0.0f && counted().longest == 1.0f,
             "the shortest and longest length drawn are kept");

    ut_section("everything else goes straight through");
    before = counted();
    (void)mp_blade_draw_dispatch(address_of(&s_other_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().refused == before.refused,
             "another render handle, the local player's for one, draws the asset's vertices and "
             "is not counted");
    mp_blade_draw_halo(address_of(&s_other_object), &pretend_halo);
    ut_check(s_seen_pointer == address_of(s_shared), "and another object's halo pass too");

    put_word(s_far_thing.bytes, THING_MODEL, address_of(&s_bare_model));
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().refused == before.refused + 1u,
             "a body dressed in another model since its spawn is refused by the live check");
    put_word(s_far_thing.bytes, THING_MODEL, address_of(&s_hero_model));

    s_clone_object = s_far_object;
    put_word((void *)mp_bank_block_at(FAR_BANK), BLOCK_OBJECT, address_of(&s_clone_object));
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().refused == before.refused + 2u,
             "and so is a row whose bank names another object since the row was filled, even one "
             "whose render handle is the row's");
    put_word((void *)mp_bank_block_at(FAR_BANK), BLOCK_OBJECT, address_of(&s_far_object));

    put_word((void *)mp_bank_block_at(FAR_BANK), BLOCK_HERO, 2u);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().refused == before.refused + 3u,
             "and a bank whose hero is no Jedi any more, read at +0x6C and not the word before");
    put_word((void *)mp_bank_block_at(FAR_BANK), BLOCK_HERO, 1u);

    ut_section("a call that comes back in opens nothing twice");
    before = counted();
    s_reenter = true;
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_inner_pointer == s_seen_pointer && s_inner_pointer != address_of(s_shared),
             "the inner call draws from the outer call's field");
    ut_check(counted().draws == before.draws + 1u && counted().refused == before.refused,
             "and does not try to open again: one draw, nothing refused");
    ut_check(mesh_pointer(1u) == address_of(s_shared), "and the mesh is its own once both are out");
}

static void check_the_witness(void)
{
    mp_blade_draw_counters_t before = counted();
    uint32_t                 field;

    ut_section("the frame witness puts back what a draw left behind");
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    field = s_seen_pointer;
    put_word(mesh(1u), MESH_VERTS, field);
    mp_blade_draw_witness();
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().put_back == before.put_back + 1u &&
                 counted().stray_frames == before.stray_frames + 1u,
             "a mesh found naming a row's field outside a draw gets its own vertices back");
    mp_blade_draw_witness();
    ut_check(counted().stray_frames == before.stray_frames + 1u,
             "and a frame with nothing to put back counts nothing");

    if (setjmp(s_escape_to) == 0) {
        s_escape = true;
        (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    }
    ut_check(mesh_pointer(1u) != address_of(s_shared),
             "a draw that never came back leaves the mesh naming the row's field");
    mp_blade_draw_witness();
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().put_back == before.put_back + 2u,
             "and the witness closes the row it left open");
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer != address_of(s_shared) && mesh_pointer(1u) == address_of(s_shared),
             "after which the row draws as before");

    ut_section("a pointer that would not go back");
    s_protect = true;
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    writable();
    ut_check(counted().stuck == before.stuck + 1u && mesh_pointer(1u) != address_of(s_shared),
             "is counted when the mesh refuses it");
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(counted().refused == before.refused + 1u,
             "and the row opens no more: its next draw is refused");
    mp_blade_draw_witness();
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().put_back == before.put_back + 3u,
             "the witness puts it back once the mesh takes a write again");
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared),
             "and the row stays off until it is filled again");
    mp_blade_draw_fill(FAR_BANK);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer != address_of(s_shared), "which a new spawn does");
}

static void check_no_rows(void)
{
    mp_blade_draw_counters_t before;
    uint32_t                 stray = address_of(s_decoy);

    ut_section("with no far Jedi nothing runs");
    mp_blade_draw_forget(FAR_BANK);
    before = counted();
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    mp_blade_draw_halo(address_of(&s_far_object), &pretend_halo);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().draws == before.draws &&
                 counted().halos == before.halos && counted().refused == before.refused,
             "a taken down body's handle and halo pass go straight through");
    put_word(mesh(1u), MESH_VERTS, stray);
    mp_blade_draw_witness();
    ut_check(mesh_pointer(1u) == stray && counted().stray_frames == before.stray_frames,
             "and the witness looks at nothing, not even a mesh that looks wrong");
    put_word(mesh(1u), MESH_VERTS, address_of(s_shared));

    make_far_block(2u, 0u, 1.0f);
    mp_blade_draw_fill(FAR_BANK);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().refused == before.refused,
             "a far Panaka gets no row at all");
    make_far_block(1u, BLADE_SLOT, 1.0f);
    put_word(s_hero_actor.bytes, ACTOR_MODEL, address_of(&s_three_model));
    mp_blade_draw_fill(FAR_BANK);
    (void)mp_blade_draw_dispatch(address_of(&s_far_thing), 0u, &pretend_dispatch);
    ut_check(s_seen_pointer == address_of(s_shared) && counted().draws == before.draws,
             "and neither does a Jedi whose blade mesh is not the four vertices the setter writes");
    put_word(s_hero_actor.bytes, ACTOR_MODEL, address_of(&s_hero_model));
    mp_blade_draw_report(0u, 0u);
}

/* A spawn and a take down as the engine runs them: the capture reads the mesh through its
 * pointer, then folds it; the take down parks it at full length. */
static float s_captured[12];

static void pretend_capture_and_fold(void)
{
    float *verts = (float *)(uintptr_t)mesh_pointer(1u);

    memcpy(s_captured, verts, sizeof s_captured);
    memcpy(verts + 6, verts, 6u * sizeof(float));
}

static void pretend_park(void)
{
    blade_at(1.0f, (float *)(uintptr_t)mesh_pointer(1u));
}

static void check_the_guards(void)
{
    mp_blade_draw_counters_t before = counted();
    float                    shared[12];

    ut_section("a far spawn folds a copy of the mesh, not the asset's");
    blade_at(0.75f, s_shared);
    memcpy(shared, s_shared, sizeof shared);
    mp_blade_draw_guard_spawn_over(FAR_BANK, 1, 1, NULL, (uintptr_t)s_local_block);
    pretend_capture_and_fold();
    mp_blade_draw_guard_close();
    ut_check(memcmp(s_captured, shared, sizeof shared) == 0,
             "the capture reads the mesh as it stands, the local player's length");
    ut_check(memcmp(s_shared, shared, sizeof shared) == 0 &&
                 mesh_pointer(1u) == address_of(s_shared),
             "and its fold lands in the copy: the asset's vertices are bit for bit what they were");
    ut_check(counted().guarded == before.guarded + 1u &&
                 watched(MP_BLADE_WINDOW_SPAWN) ==
                     before.watched[MP_BLADE_WINDOW_SPAWN] + 1u &&
                 changed(MP_BLADE_WINDOW_SPAWN) == before.changed[MP_BLADE_WINDOW_SPAWN],
             "counted as a write kept off, in a watched spawn that changed nothing");

    mp_blade_draw_guard_spawn_over(FAR_BANK, 0, 1, &s_hero_actor, (uintptr_t)s_local_block);
    pretend_capture_and_fold();
    mp_blade_draw_guard_close();
    ut_check(memcmp(s_shared, shared, sizeof shared) == 0 &&
                 counted().guarded == before.guarded + 2u,
             "the same for another hero's slot, whose model is the probed actor's");

    mp_blade_draw_guard_spawn_over(FAR_BANK, 2, 1, &s_hero_actor, (uintptr_t)s_local_block);
    ut_check(mesh_pointer(1u) == address_of(s_shared) && counted().guarded == before.guarded + 2u &&
                 counted().unguarded == before.unguarded,
             "a spawn on a slot that is no Jedi's reads no blade and is neither guarded nor "
             "counted");
    mp_blade_draw_guard_close();

    mp_blade_draw_guard_spawn_over(FAR_BANK, 0, 1, &s_bare_actor, (uintptr_t)s_local_block);
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().unguarded == before.unguarded + 1u,
             "a Jedi slot whose model has no blade mesh is counted as unguarded");
    mp_blade_draw_guard_close();
    mp_blade_draw_guard_spawn_over(FAR_BANK, 1, 1, NULL, 0u);
    ut_check(counted().unguarded == before.unguarded + 2u,
             "and so is the local player's slot with no hero block to read his actor from");
    mp_blade_draw_guard_close();

    ut_section("a far take down parks a copy");
    make_far_block(1u, BLADE_SLOT, 0.3f);
    mp_blade_draw_guard_despawn(FAR_BANK);
    pretend_park();
    mp_blade_draw_guard_close();
    ut_check(memcmp(s_shared, shared, sizeof shared) == 0 &&
                 watched(MP_BLADE_WINDOW_DESPAWN) ==
                     before.watched[MP_BLADE_WINDOW_DESPAWN] + 1u &&
                 changed(MP_BLADE_WINDOW_DESPAWN) ==
                     before.changed[MP_BLADE_WINDOW_DESPAWN],
             "the asset's vertices stay where the local player left them");
    make_far_block(1u, 0u, 0.3f);
    mp_blade_draw_guard_despawn(FAR_BANK);
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().unguarded == before.unguarded + 3u,
             "a record with no blade node at +0x4C is left alone and counted, the word after it "
             "a decoy");
    mp_blade_draw_guard_close();
    make_far_block(2u, BLADE_SLOT, 0.3f);
    mp_blade_draw_guard_despawn(FAR_BANK);
    ut_check(mesh_pointer(1u) == address_of(s_shared) &&
                 counted().unguarded == before.unguarded + 3u,
             "a hero that is no Jedi is not parked, and not counted");
    mp_blade_draw_guard_close();
    make_far_block(1u, BLADE_SLOT, 1.0f);
}

static void check_the_window_watch(void)
{
    mp_blade_draw_counters_t before = counted();

    ut_section("a puppet's window is held against the mesh its body draws");
    mp_blade_draw_window_open(FAR_BANK, MP_BLADE_WINDOW_PUPPET);
    mp_blade_draw_window_close();
    ut_check(watched(MP_BLADE_WINDOW_PUPPET) ==
                     before.watched[MP_BLADE_WINDOW_PUPPET] + 1u &&
                 changed(MP_BLADE_WINDOW_PUPPET) ==
                     before.changed[MP_BLADE_WINDOW_PUPPET],
             "a window that writes nothing is watched and changes nothing");
    mp_blade_draw_window_open(FAR_BANK, MP_BLADE_WINDOW_PUPPET);
    s_shared[10] += 0.01f;
    mp_blade_draw_window_close();
    ut_check(changed(MP_BLADE_WINDOW_PUPPET) ==
                 before.changed[MP_BLADE_WINDOW_PUPPET] + 1u,
             "one that writes the mesh is counted as having changed it");
    s_shared[10] -= 0.01f;
    make_far_block(2u, 0u, 1.0f);
    put_word(s_far_thing.bytes, THING_MODEL, address_of(&s_bare_model));
    mp_blade_draw_window_open(FAR_BANK, MP_BLADE_WINDOW_PUPPET);
    mp_blade_draw_window_close();
    ut_check(watched(MP_BLADE_WINDOW_PUPPET) ==
                 before.watched[MP_BLADE_WINDOW_PUPPET] + 2u,
             "a body whose model carries no blade mesh has nothing to watch");
    put_word(s_far_thing.bytes, THING_MODEL, address_of(&s_hero_model));
    make_far_block(1u, BLADE_SLOT, 1.0f);
}

int main(void)
{
    ut_check(set_up(), "a page for the meshes");
    if (ut_failures() != 0u) {
        return ut_summary("mp_blade_draw");
    }
    check_the_mesh();
    check_the_rows();
    check_the_witness();
    check_no_rows();
    check_the_guards();
    check_the_window_watch();

    return ut_summary("mp_blade_draw");
}
