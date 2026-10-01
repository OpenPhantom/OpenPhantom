/* character_prop_engine.c: see character_prop_engine.h. */
#include "character_prop_engine.h"

#include <stddef.h>
#include <string.h>

rig_t        hero;
rig_t        worn;
rig_t        stump;
rig_t        guard;
fake_thing_t body_thing;
fake_thing_t other_thing;
uint8_t      body_obj[0x100];
uint8_t      other_obj[0x100];
uint8_t      orphan_obj[0x100];
uint8_t      player_block[BLOCK_BYTES];
uint8_t      far_block[BLOCK_BYTES];
uint32_t     player_cell;
uint32_t     name_table[NAMES];
uint8_t      weapon_cfg[WEAPON_ROWS * WEAPON_ROW_BYTES];
uint32_t     freed_arrays;
bool         draw_answers;
uint32_t     draws;
fake_thing_t bank_thing[FAKE_BANKS];
uint8_t      bank_obj[FAKE_BANKS][0x100];
uint8_t      bank_block[FAKE_BANKS][BLOCK_BYTES];
uint32_t     drawn_blade_positions;

static char name_text[NAMES][NAME_BYTES];

/* A pose stamp a handle carries after the engine has built one: any value the counter can hold. */
#define POSE_STAMP_BUILT   1000u

void put_u32(void *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

uint32_t get_u32(const void *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

static uint8_t *node_of(rig_t *rig, uint32_t index)
{
    return &rig->node[index * NODE_BYTES];
}

static uint32_t link(rig_t *rig, int32_t index)
{
    return (index < 0) ? 0u : (uint32_t)(uintptr_t)node_of(rig, (uint32_t)index);
}

static void set_node(rig_t *rig, uint32_t index, const char *name, int32_t mesh, int32_t parent,
                     int32_t child, int32_t sibling)
{
    uint8_t *at = node_of(rig, index);
    float    pivot[3];

    memset(at, 0, NODE_BYTES);
    memcpy(at, name, strlen(name));
    put_u32(at + NODE_MATRIX_SLOT, index);
    put_u32(at + NODE_MESH_INDEX, (uint32_t)mesh);
    put_u32(at + NODE_PARENT, link(rig, parent));
    put_u32(at + NODE_FIRST_CHILD, link(rig, child));
    put_u32(at + NODE_NEXT_SIBLING, link(rig, sibling));
    pivot[0] = 0.01f * (float)index;
    pivot[1] = 0.02f * (float)index;
    pivot[2] = 0.03f * (float)index;
    memcpy(at + NODE_PIVOT, pivot, sizeof pivot);
}

static void set_mesh(rig_t *rig, uint32_t index, float radius)
{
    uint8_t *at = &rig->mesh[index * MESH_BYTES];
    float    centre[3];
    uint32_t vertex;

    memset(at, 0, MESH_BYTES);
    centre[0] = 0.1f * (float)(index + 1u);
    centre[1] = 0.2f;
    centre[2] = 0.3f;
    memcpy(at + MESH_CENTRE, centre, sizeof centre);
    memcpy(at + MESH_RADIUS, &radius, sizeof radius);
    /* The vertices the mesh names, and the count the engine's own writer reads as the bound of
     * its loop. The values are the mesh's own so that a pointer that was exchanged and not put
     * back can be told from one that never moved. */
    for (vertex = 0u; vertex < MESH_VERTS; ++vertex) {
        rig->position[index][vertex][0] = 10.0f * (float)index + (float)vertex;
        rig->position[index][vertex][1] = 0.0f;
        rig->position[index][vertex][2] = 0.0f;
    }
    put_u32(at + MESH_NUM_VERTS, MESH_VERTS);
    put_u32(at + MESH_POSITIONS, (uint32_t)(uintptr_t)&rig->position[index][0][0]);
}

static void begin_rig(rig_t *rig, uint32_t count)
{
    uint32_t i;

    memset(rig, 0, sizeof *rig);
    rig->count = count;
    put_u32(rig->model + MODEL_NUM_NODES, count);
    put_u32(rig->model + MODEL_NODES, (uint32_t)(uintptr_t)node_of(rig, 0));
    put_u32(rig->model + MODEL_MESHES, (uint32_t)(uintptr_t)&rig->mesh[0]);
    for (i = 0; i < RIG_MESHES; ++i) {
        set_mesh(rig, i, 0.05f + 0.01f * (float)i);
    }
}

/* The player's own model: `weapon` hangs on `rhand` and carries the blade under it. */
static void build_hero(void)
{
    begin_rig(&hero, 6u);
    set_node(&hero, 0, "dummy01",     -1, -1,  1, -1);
    set_node(&hero, 1, "waist",        0,  0,  2, -1);
    set_node(&hero, 2, "chest",        1,  1,  3, -1);
    set_node(&hero, 3, "rhand",       -1,  2,  4, -1);
    set_node(&hero, 4, "weapon",       2,  3,  5, -1);
    set_node(&hero, 5, "sabreblad01",  3,  4, -1, -1);
}

/* A borrowed rig with a hand and a gun of its own, which has to be hidden. */
static void build_worn(void)
{
    begin_rig(&worn, 6u);
    set_node(&worn, 0, "dummy01", -1, -1,  1, -1);
    set_node(&worn, 1, "waist",    0,  0,  2, -1);
    set_node(&worn, 2, "chest",    1,  1,  3, -1);
    set_node(&worn, 3, "rhand",    2,  2,  4, -1);
    set_node(&worn, 4, "weapon",   3,  3,  5, -1);
    set_node(&worn, 5, "gun01",    4,  4, -1, -1);
}

/* A borrowed rig with no fist, so the weapon has to go on the forearm. */
static void build_stump(void)
{
    begin_rig(&stump, 4u);
    set_node(&stump, 0, "dummy01", -1, -1,  1, -1);
    set_node(&stump, 1, "waist",    0,  0,  2, -1);
    set_node(&stump, 2, "chest",    1,  1,  3, -1);
    set_node(&stump, 3, "rforarm",  2,  2, -1, -1);
}

/* A second borrowed rig with a hand, and a blade of its own hanging under its own weapon: the rig
 * shape the lookup has to answer the drawn weapon's node for rather than the rig's hidden one. */
static void build_guard(void)
{
    begin_rig(&guard, 7u);
    set_node(&guard, 0, "dummy01",     -1, -1,  1, -1);
    set_node(&guard, 1, "waist",        0,  0,  2, -1);
    set_node(&guard, 2, "chest",        1,  1,  3, -1);
    set_node(&guard, 3, "rhand",        2,  2,  4, -1);
    set_node(&guard, 4, "weapon",       3,  3,  5, -1);
    set_node(&guard, 5, "sabre01",      4,  4,  6, -1);
    set_node(&guard, 6, "sabreblad01",  5,  5, -1, -1);
}

static void dress_thing(fake_thing_t *thing, rig_t *rig)
{
    uint32_t i;

    memset(thing, 0, sizeof *thing);
    put_u32(thing->block + THING_MODEL, (uint32_t)(uintptr_t)rig->model);
    put_u32(thing->block + THING_POSE_STAMP, POSE_STAMP_BUILT);
    put_u32(thing->block + THING_MATRICES, (uint32_t)(uintptr_t)&thing->matrix[0][0]);
    put_u32(thing->block + THING_NODE_HIDDEN, (uint32_t)(uintptr_t)&thing->node_hidden[0]);
    put_u32(thing->block + THING_MESH_HIDDEN, (uint32_t)(uintptr_t)&thing->mesh_hidden[0]);
    for (i = 0; i < RIG_NODES; ++i) {
        thing->matrix[i][0] = 1.0f;
        thing->matrix[i][4] = 1.0f;
        thing->matrix[i][8] = 1.0f;
        thing->matrix[i][9] = 0.5f * (float)i;
    }
}

bool pose_was_built(const fake_thing_t *thing)
{
    uint32_t stamp = get_u32(thing->block + THING_POSE_STAMP);

    return stamp != 0u && (stamp & POSE_STAMP_UNBUILT) == 0u;
}

/* ==============================================================================================
 * The sites, handed over instead of resolved: this program has no engine to find them in.
 * ============================================================================================ */

/* One set of per node arrays per render handle, as rdThing_SetModel allocates them. The owner is
 * the handle the set was handed to, and a release gives it back, so a rebind takes a fresh one. */
#define ARRAY_SETS   (FAKE_BANKS + 2u)

static fake_thing_t  array_set[ARRAY_SETS];
static const void   *array_owner[ARRAY_SETS];

static fake_thing_t *arrays_for(void *thing)
{
    uint32_t i;

    for (i = 0; i < ARRAY_SETS; ++i) {
        if (array_owner[i] == thing) {
            return &array_set[i];
        }
    }
    for (i = 0; i < ARRAY_SETS; ++i) {
        if (array_owner[i] == NULL) {
            array_owner[i] = thing;
            memset(&array_set[i], 0, sizeof array_set[i]);
            return &array_set[i];
        }
    }
    return NULL;
}

static int32_t __cdecl fake_thing_init(void *thing, void *owner)
{
    (void)owner;
    memset(thing, 0, RDTHING_BLOCK_BYTES);
    return 1;
}

static int32_t __cdecl fake_set_model(void *thing, void *model3)
{
    uint8_t      *at = (uint8_t *)thing;
    fake_thing_t *arrays = arrays_for(thing);

    if (arrays == NULL) {
        return 0;
    }
    put_u32(at + THING_MODEL, (uint32_t)(uintptr_t)model3);
    put_u32(at + THING_MATRICES, (uint32_t)(uintptr_t)&arrays->matrix[0][0]);
    put_u32(at + THING_NODE_HIDDEN, (uint32_t)(uintptr_t)&arrays->node_hidden[0]);
    put_u32(at + THING_MESH_HIDDEN, (uint32_t)(uintptr_t)&arrays->mesh_hidden[0]);
    return 1;
}

static void __cdecl fake_free_arrays(void *thing)
{
    uint32_t i;

    for (i = 0; i < ARRAY_SETS; ++i) {
        if (array_owner[i] == thing) {
            array_owner[i] = NULL;
        }
    }
    freed_arrays++;
}

static void __cdecl fake_build_joints(void *thing, const float *root)
{
    uint32_t matrices = get_u32((const uint8_t *)thing + THING_MATRICES);
    float   *at = (float *)(uintptr_t)matrices;
    uint32_t i;

    if (at == NULL) {
        return;
    }
    for (i = 0; i < RIG_NODES; ++i) {
        at[i * MATRIX_FLOATS + 0] = 1.0f;
        at[i * MATRIX_FLOATS + 4] = 1.0f;
        at[i * MATRIX_FLOATS + 8] = 1.0f;
        at[i * MATRIX_FLOATS + 9] = ((root != NULL) ? root[9] : 0.0f) + 0.5f * (float)i;
    }
}

static void *__cdecl fake_thing_draw(void *thing, const float *root)
{
    (void)root;
    draws++;
    drawn_blade_positions =
        get_u32(&hero.mesh[HERO_BLADE_MESH * MESH_BYTES] + MESH_POSITIONS);
    return draw_answers ? thing : NULL;
}

bool character_prop_sites_resolve(character_prop_sites_t *out)
{
    memset(out, 0, sizeof *out);
    out->name_table = (uintptr_t)&name_table[0];
    out->weapon_cfg = (uintptr_t)&weapon_cfg[0];
    out->thing_init = &fake_thing_init;
    out->set_model = &fake_set_model;
    out->free_arrays = &fake_free_arrays;
    out->build_joints = &fake_build_joints;
    out->thing_draw = &fake_thing_draw;
    return true;
}

static void build_tables(void)
{
    uint32_t i;

    memset(name_text, 0, sizeof name_text);
    memset(name_table, 0, sizeof name_table);
    for (i = 0; i < NAMES; ++i) {
        name_table[i] = (uint32_t)(uintptr_t)&name_text[i][0];
    }
    memcpy(name_text[NAME_RHAND], "rhand", 6);
    memcpy(name_text[NAME_WEAPON], "weapon", 7);
    memcpy(name_text[NAME_BLADE], "sabreblad01", 12);
    memcpy(name_text[NAME_RFOREARM], "rforarm", 8);

    memset(weapon_cfg, 0, sizeof weapon_cfg);
    put_u32(&weapon_cfg[1u * WEAPON_ROW_BYTES], NAME_WEAPON);
    put_u32(&weapon_cfg[2u * WEAPON_ROW_BYTES], NAME_BLADE);
    /* A third row, so that a weapon change can be made between two names a borrowed rig CARRIES.
     * The node lookup is not hooked in a test process, so a name a rig does not carry resolves to
     * nothing here and the change would be indistinguishable from a rig without a weapon. */
    put_u32(&weapon_cfg[3u * WEAPON_ROW_BYTES], NAME_RHAND);
}

static void build_bank(uint32_t b, rig_t *rig, uint32_t slot)
{
    dress_thing(&bank_thing[b], rig);
    memset(bank_obj[b], 0, sizeof bank_obj[b]);
    put_u32(bank_obj[b] + OBJ_THING, (uint32_t)(uintptr_t)bank_thing[b].block);
    memset(bank_block[b], 0, sizeof bank_block[b]);
    put_u32(bank_block[b] + BLOCK_ACTOR, (uint32_t)(uintptr_t)bank_obj[b]);
    put_u32(bank_block[b] + BLOCK_WEAPON_SLOT, slot);
}

void build_world(void)
{
    build_hero();
    build_worn();
    build_stump();
    build_guard();
    build_tables();
    dress_thing(&body_thing, &worn);
    dress_thing(&other_thing, &hero);

    memset(body_obj, 0, sizeof body_obj);
    memset(other_obj, 0, sizeof other_obj);
    memset(orphan_obj, 0, sizeof orphan_obj);
    put_u32(body_obj + OBJ_THING, (uint32_t)(uintptr_t)body_thing.block);
    put_u32(other_obj + OBJ_THING, (uint32_t)(uintptr_t)other_thing.block);

    memset(player_block, 0, sizeof player_block);
    put_u32(player_block + BLOCK_ACTOR, (uint32_t)(uintptr_t)body_obj);
    put_u32(player_block + BLOCK_WEAPON_SLOT, 1u);

    memset(far_block, 0, sizeof far_block);
    put_u32(far_block + BLOCK_ACTOR, (uint32_t)(uintptr_t)other_obj);
    put_u32(far_block + BLOCK_WEAPON_SLOT, 2u);

    build_bank(0u, &worn, 1u);
    build_bank(1u, &guard, 1u);
    drawn_blade_positions = 0u;

    /* The array sets are NOT given back here. They belong to render handles the module owns, and
     * those outlive a rebuilt world: a set handed back while its handle still names it would be
     * handed to a second handle and two bodies would then be posed into one array. */
    player_cell = (uint32_t)(uintptr_t)player_block;
    freed_arrays = 0;
    draws = 0;
    draw_answers = true;
}
