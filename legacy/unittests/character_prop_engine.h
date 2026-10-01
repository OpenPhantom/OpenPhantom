/* character_prop_engine.h: the pretend engine the borrowed weapon's tests are driven over.
 *
 * A test process has no engine. What the module reads instead is this: four rigs in the test's own
 * memory, render handles, objects, player blocks and the cell the engine keeps the live one in,
 * plus the name table and the weapon configuration the module indexes. The entry points
 * character_prop_sites.c would resolve are handed over rather than found, which is why the
 * programs that use this file link the swap family WITHOUT that source.
 *
 * What it is not. It does not draw and it does not concatenate a hierarchy. The joint matrices it
 * hands back are made up, and what is proven with them is which matrix was asked for and what was
 * done with the answer, never the arithmetic: that is the other program next door, against numbers
 * read out of the shipped assets.
 *
 * Every handle gets arrays of its own, which is the one place this file had to grow a rule of the
 * engine's own. rdThing_SetModel allocates four arrays per node for the handle it binds, and the
 * module now holds up to four handles at once; arrays shared between two of them would have one
 * body's weapon measured out of another body's pose and every answer would still look plausible.
 */
#ifndef CHARACTER_PROP_ENGINE_H
#define CHARACTER_PROP_ENGINE_H

#include "character_prop_sites.h"

#include <stdbool.h>
#include <stdint.h>

/* The engine fields the module reads, at the offsets it reads them at. */
#define NODE_BYTES         0xB4u
#define NODE_MATRIX_SLOT   0x44u
#define NODE_MESH_INDEX    0x4Cu
#define NODE_PARENT        0x50u
#define NODE_FIRST_CHILD   0x58u
#define NODE_NEXT_SIBLING  0x5Cu
#define NODE_PIVOT         0x60u
#define NAME_BYTES         0x40u
#define MODEL_MESHES       0x28u
#define MODEL_NUM_NODES    0x54u
#define MODEL_NODES        0x58u
#define MESH_BYTES         0x70u
#define MESH_POSITIONS     0x30u
#define MESH_NUM_VERTS     0x48u
#define MESH_RADIUS        0x54u
#define MESH_CENTRE        0x58u
#define THING_MODEL        0x04u
#define THING_POSE_STAMP   0x1Cu
#define THING_MATRICES     0x20u
#define THING_NODE_HIDDEN  0x28u
#define THING_MESH_HIDDEN  0x2Cu
#define OBJ_THING          0x9Cu
#define BLOCK_ACTOR        0x0Cu
#define BLOCK_WEAPON_SLOT  0x84u
#define BLOCK_BLADE_VECTORS 0x1C8u
#define BLOCK_BLADE_SIZE    0x210u

/* A player block reaches past the blade cells, which are the last of it this feature reads. */
#define BLOCK_BYTES        0x240u
#define MATRIX_FLOATS      12u
#define NAMES              33u
#define NAME_RHAND          4u
#define NAME_WEAPON         7u
#define NAME_BLADE          9u
#define NAME_RFOREARM      27u
#define WEAPON_ROWS        12u
#define WEAPON_ROW_BYTES   0x18u
#define RIG_NODES          16u
#define RIG_MESHES          8u

/* Every mesh here carries four vertices, which is what a blade mesh has and what the engine's own
 * length setter writes. `sabreblad01` of the hero rig is node 5 and mesh 3. */
#define MESH_VERTS          4u
#define HERO_BLADE_MESH     3u

/* A stamp the engine's own frame counter cannot hold. The counter is seeded to 1000 and only
 * counts up, so the top bit set says no pose was ever built on this handle. */
#define POSE_STAMP_UNBUILT 0x80000000u

/* The far banks this file wires up: two of the MODEL_WEAR_BANKS the note can describe. */
#define FAKE_BANKS          2u

typedef struct rig {
    uint8_t  model[0x90];
    uint8_t  node[RIG_NODES * NODE_BYTES];
    uint8_t  mesh[RIG_MESHES * MESH_BYTES];
    float    position[RIG_MESHES][MESH_VERTS][3];
    uint32_t count;
} rig_t;

typedef struct fake_thing {
    uint8_t  block[RDTHING_BLOCK_BYTES];
    float    matrix[RIG_NODES][MATRIX_FLOATS];
    uint32_t node_hidden[RIG_NODES];
    uint32_t mesh_hidden[RIG_NODES];
} fake_thing_t;

extern rig_t        hero;         /* the model the weapon meshes live in     */
extern rig_t        worn;         /* a borrowed rig with a right hand        */
extern rig_t        stump;        /* a borrowed rig with only a forearm      */
extern rig_t        guard;        /* a second borrowed rig, of its own size  */
extern fake_thing_t body_thing;   /* the handle the player's borrowed body wears */
extern fake_thing_t other_thing;
extern uint8_t      body_obj[0x100];
extern uint8_t      other_obj[0x100];
extern uint8_t      orphan_obj[0x100];   /* a body no block here names */
extern uint8_t      player_block[BLOCK_BYTES];
extern uint8_t      far_block[BLOCK_BYTES];   /* a block naming other_obj */
extern uint32_t     player_cell;
extern uint32_t     name_table[NAMES];
extern uint8_t      weapon_cfg[WEAPON_ROWS * WEAPON_ROW_BYTES];
extern uint32_t     freed_arrays;
extern bool         draw_answers;   /* whether the pretend draw says it drew */
extern uint32_t     draws;

/* A far player's body: a handle wearing a borrowed rig, the object that owns it and the block the
 * multiplayer keeps for that bank. Bank 0 wears `worn`, bank 1 wears `guard`, which carries a
 * blade of its own under its own weapon mount, and both start out holding weapon row 1. */
extern fake_thing_t bank_thing[FAKE_BANKS];
extern uint8_t      bank_obj[FAKE_BANKS][0x100];
extern uint8_t      bank_block[FAKE_BANKS][BLOCK_BYTES];

/* What the hero rig's blade mesh named as its vertices at the moment of the last pretend draw.
 * The exchange this feature makes is undone inside the call, so it can be seen from nowhere
 * else. */
extern uint32_t     drawn_blade_positions;

void     put_u32(void *at, uint32_t value);
uint32_t get_u32(const void *at);

/* Every rig, handle, object, block and table back in its starting state, and the counters at
 * zero. Each check calls this first, so no check can be read as depending on the one before it. */
void build_world(void);

/* Whether a pose was built on this handle since it was last bound, as the engine's own stamp says
 * it. The module asks the same question before it measures a body it did not draw. */
bool pose_was_built(const fake_thing_t *thing);

#endif /* CHARACTER_PROP_ENGINE_H */
