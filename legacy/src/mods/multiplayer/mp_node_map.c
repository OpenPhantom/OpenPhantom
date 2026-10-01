/* mp_node_map.c: see mp_node_map.h. */
#include "mp_node_map.h"

#include "common/logging.h"
#include "common/memory.h"

#include <string.h>

/* The body, and the two steps from it to the node array. Every offset is the reconstruction's own
 * layout: bapObj.pThing at +0x9c, rdThing.pModel3 at +0x04, rdModel3.numNodes at +0x54 and
 * pNodes at +0x58. A node is 0xb4 bytes, its name starts at +0x00 and its matrix slot is at
 * +0x44, which is where the name field ends. */
#define OBJECT_THING    0x9Cu
#define THING_MODEL3    0x04u
#define MODEL_NUM_NODES 0x54u
#define MODEL_NODES     0x58u
#define NODE_STRIDE     0xB4u
#define NODE_SLOT       0x44u

/* A model with more nodes than this is not a model. The engine's own rigs carry between 24 and 48;
 * the bound is here so a garbage pointer cannot walk the address space. */
#define NODES_SANE_MAX 256u

/* THE NAME TABLE, and why it is not the engine's own.
 *
 * The engine keeps one: `g_actorNodeName`, 33 entries, read by `bapobj_findNodeByNameId`. It was
 * the obvious answer and it is NOT ENOUGH, which a census of the shipped models settled rather
 * than an opinion. That table holds the nodes the engine's own CODE looks a body up by: the
 * weapon mounts, the muzzles, and the nine joints it needs for aiming. It does not hold the
 * joints a blade actually takes off. `ruparm` and `luparm` stand on 2143 and 2142 models in
 * big.lab, `rthigh` and `lthigh` on 1965 each, `rcalf` and `lcalf` on 1963 and 1946, `neck` on
 * 1631. That is the same order as the nine that are in the engine's table, so they sit on
 * essentially every humanoid rig, and not one of them can be named by an engine id.
 *
 * So the table below is ours. It is generated from the shipped archive: every NUL terminated
 * lowercase token that occurs at least eight times, minus the file extensions and the two strings
 * that are plainly data, with the engine's 33 kept FIRST and in their own order. That last part is
 * deliberate and worth keeping: an id below 33 is also a valid engine name id, so the two tables
 * agree wherever they overlap.
 *
 * 254 entries, and the byte's last two values are kept back on purpose: 0xFF is the word for none
 * and 0xFE is no name at all, so a torn or invented id can be REFUSED rather than resolved against
 * whatever it happens to point at. The census offered one name more than that and the least common
 * of them was dropped to buy the check. A model that names a joint outside this list travels as
 * none, which is what every note carried before this existed; the counters say how often.
 *
 * Generated on 2026-09-22 against big.lab of the retail installation. Re-running the census is the
 * way to extend it, not adding a line by hand.
 */
static const char *const NODE_NAME[MP_NODE_NAME_COUNT] = {
    "waist", "head", "chest", "lhand", "rhand", "lfoot",
    "rfoot", "weapon", "sabre", "sabreblad01", "sabreblad02", "sabreblade1",
    "sabreblade2", "shotflar", "shotflrb", "gun01", "gun02", "gun03",
    "gun04", "gun05", "gun06", "gun07", "gun08", "gun09",
    "turret", "target", "lforarm", "rforarm", "barrelend", "blade",
    "gun", "gunbarrel", "fire", "dummy01", "ruparm", "luparm",
    "lthigh", "rthigh", "rcalf", "lcalf", "neck", "gun10",
    "gun11", "gun12", "gun13", "gun14", "gun15", "gun16",
    "gun17", "gun18", "gun19", "gun20", "gun21", "gun22",
    "gun23", "gun24", "ponytail", "sabrewaist", "sabreglow1", "sabreglow2",
    "body", "lleg", "rleg", "pistol", "lear", "rear",
    "cfoot", "cleg", "waistdum", "arm", "robe", "backpack",
    "hips", "skirt", "backskirt", "lskirt", "rskirt", "skrtb",
    "skrtf", "jaw", "midsect", "lshould", "rshould", "larm",
    "rarm", "box01", "glass", "sabre1glow1", "sabre1glow2", "sabre2glow1",
    "sabre2glow2", "lfoot01", "lshoulder", "rshoulder", "tail01", "feathers",
    "jabnode01", "lwing", "rwing", "lengine", "lforarm01", "lhand01",
    "luparm01", "particle", "rengine", "ruparm01", "ears", "electropole",
    "hood", "faces", "base", "body02", "cortaxi", "glasses",
    "vlec_null", "body01", "lcable", "lcalf01", "rcable", "skirtback01",
    "skirtfront01", "skrtbhi", "skrtblow", "skrtblowlpan", "skrtblowrpan", "skrtfhi",
    "skrtflow", "skrtflowlpan", "skrtflowrpan", "train", "braidl", "braidr",
    "lbfoot", "lffoot", "rbfoot", "rffoot", "tail", "gunpoint",
    "bbdeghjkmm", "pelt1", "pelt2", "skull", "skull2", "skull3",
    "sss", "stake", "axe", "bottle", "lbgun", "lbsupport",
    "lbwing", "lfsupport", "lpanel", "ltgun", "ltwing", "pipe01",
    "pipe02", "pipe03", "rbgun", "rbsupport", "rbwing", "rfsupport",
    "rpanel", "rtgun", "rtwing", "arm01", "arm02", "arm03",
    "arm04", "cape", "face01", "glass01", "hand01", "hand02",
    "hat", "hood01", "lcalf1", "lcalf2", "leg01", "leg02",
    "panel", "rcalf1", "rcalf2", "skirt01", "skirt02", "chestplate",
    "head01", "kkh", "lbcalf", "lbleg", "lbthigh", "lfcalf",
    "lfthigh", "llegdum", "ltail", "lwing1", "lwing2", "plry",
    "rbcalf", "rbleg", "rbthigh", "rfcalf", "rfleg", "rfthigh",
    "rlegdum", "rtail", "rwing2", "sabre01", "tail1", "tail2",
    "vkkh", "ylacc", "yrp", "cloak", "cloakback", "cloakfront",
    "gunarms", "gunlodr", "lhatch", "lhipplate", "lplate", "lshouldplate",
    "ltent", "lwattle", "pelvis", "queenship", "rhatch", "rhipplate",
    "rotationdum", "rplate", "rshouldplate", "rtent", "rwing1", "shell",
    "shoulders", "tankbody", "windshield", "body03", "body04", "body05",
    "body06", "body07", "body08", "body09", "body10", "body11",
    "body12", "body13", "body14", "cane", "iii", "leg",
    "lfin", "lforleg"
};

typedef struct node_map_state {
    uintptr_t cell;      /* the seventh contact cell, or zero before it resolved */
    uint32_t cleared;
    uint32_t discarded;
    uint32_t written;
    uint32_t faults;
    uint32_t named;
    uint32_t no_contact_node;
    uint32_t unnamed;
    uint32_t resolved;
    uint32_t dropped;
} node_map_state_t;

static node_map_state_t map;

bool mp_node_map_install(void)
{
    /* Nothing to resolve: the table is part of this build. The line is here because a run report
     * that cannot say whether a feature is on says nothing at all. */
    log_info("a struck node travels as one of %u known node names, by its id "
             "(%s, %s, %s, ...)", (unsigned)MP_NODE_NAME_COUNT, NODE_NAME[0],
             NODE_NAME[1], NODE_NAME[2]);
    return true;
}

uint8_t mp_node_map_match(const char *name, const char *const *names, uint32_t count)
{
    uint32_t i;

    if (name == NULL || name[0] == '\0' || names == NULL) {
        return MP_NODE_ID_NONE;
    }
    for (i = 0; i < count && i < MP_NODE_ID_NONE; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return (uint8_t)i;
        }
    }
    return MP_NODE_ID_NONE;
}

/* The node array of a body's model, and how many nodes it has. False for anything that does not
 * read, which is every case where this feature must simply say "no node". */
static bool nodes_of(uint32_t body, uintptr_t *nodes, uint32_t *count)
{
    uint32_t thing = 0;
    uint32_t model = 0;

    if (body == 0u ||
        !memory_try_read_u32((uintptr_t)body + OBJECT_THING, &thing) || thing == 0u ||
        !memory_try_read_u32((uintptr_t)thing + THING_MODEL3, &model) || model == 0u ||
        !memory_try_read_u32((uintptr_t)model + MODEL_NUM_NODES, count) ||
        *count == 0u || *count > NODES_SANE_MAX) {
        return false;
    }
    if (!memory_try_read_u32((uintptr_t)model + MODEL_NODES, (uint32_t *)nodes) || *nodes == 0) {
        return false;
    }
    return true;
}

/* One node's name, terminated. */
static bool name_of_node(uintptr_t node, char out[MP_NODE_NAME_MAX])
{
    if (!memory_try_read(node, out, MP_NODE_NAME_MAX)) {
        return false;
    }
    out[MP_NODE_NAME_MAX - 1u] = '\0';
    return true;
}

uint8_t mp_node_map_id_of_slot(uint32_t body, uint32_t slot)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  i;

    if (slot == 0u) {
        ++map.no_contact_node;   /* the root, which is what the engine writes for "nothing" */
        return MP_NODE_ID_NONE;
    }
    if (!nodes_of(body, &nodes, &count)) {
        ++map.unnamed;
        return MP_NODE_ID_NONE;
    }
    for (i = 0; i < count; ++i) {
        uintptr_t node = nodes + (uintptr_t)i * NODE_STRIDE;
        uint32_t  at   = 0;
        char      name[MP_NODE_NAME_MAX];

        if (!memory_try_read_u32(node + NODE_SLOT, &at) || at != slot) {
            continue;
        }
        if (name_of_node(node, name)) {
            uint8_t id = mp_node_map_match(name, NODE_NAME, MP_NODE_NAME_COUNT);

            if (id != MP_NODE_ID_NONE) {
                ++map.named;
                return id;
            }
        }
        break;   /* the slot was found and it has no name the table knows */
    }
    ++map.unnamed;
    return MP_NODE_ID_NONE;
}

bool mp_node_map_body_has(uint32_t body, uint8_t id)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  i;

    if (id == MP_NODE_ID_NONE || id >= MP_NODE_NAME_COUNT ||
        !nodes_of(body, &nodes, &count)) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        char name[MP_NODE_NAME_MAX];

        if (name_of_node(nodes + (uintptr_t)i * NODE_STRIDE, name) &&
            strcmp(name, NODE_NAME[id]) == 0) {
            return true;
        }
    }
    return false;
}

uint32_t mp_node_map_slot_of_id(uint32_t body, uint8_t id)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  i;

    if (id == MP_NODE_ID_NONE || id >= MP_NODE_NAME_COUNT ||
        !nodes_of(body, &nodes, &count)) {
        return 0u;
    }
    for (i = 0; i < count; ++i) {
        uintptr_t node = nodes + (uintptr_t)i * NODE_STRIDE;
        char      name[MP_NODE_NAME_MAX];
        uint32_t  slot = 0;

        if (!name_of_node(node, name) || strcmp(name, NODE_NAME[id]) != 0) {
            continue;
        }
        if (memory_read_u32(node + NODE_SLOT, &slot) && slot != 0u) {
            ++map.resolved;
            return slot;
        }
        break;
    }
    ++map.dropped;
    return 0u;
}

void mp_node_map_counters(uint32_t *named, uint32_t *no_contact_node, uint32_t *unnamed,
                          uint32_t *resolved, uint32_t *dropped)
{
    if (named != NULL) {
        *named = map.named;
    }
    if (no_contact_node != NULL) {
        *no_contact_node = map.no_contact_node;
    }
    if (unnamed != NULL) {
        *unnamed = map.unnamed;
    }
    if (resolved != NULL) {
        *resolved = map.resolved;
    }
    if (dropped != NULL) {
        *dropped = map.dropped;
    }
}

/* ==============================================================================================
 * The seventh contact cell.
 *
 * The pair pass publishes six globals through post_contact and writes the node a blade struck into
 * a seventh one just before it. A replay that sets six of them would run the engine's receiver on
 * whatever the last real contact on this machine struck, so every replay goes through `put` and
 * the cell is either the node the sender named or nothing at all.
 * ============================================================================================ */

void mp_node_map_bind(uintptr_t cell)
{
    map.cell = cell;
}

uint8_t mp_node_map_take(uint32_t victim_body, bool carries_a_node)
{
    uint32_t slot = 0;

    if (!carries_a_node || map.cell == 0 || !memory_try_read_u32(map.cell, &slot) || slot == 0u) {
        return (uint8_t)MP_NODE_ID_NONE;
    }
    return mp_node_map_id_of_slot(victim_body, slot);
}

void mp_node_map_put(uint32_t victim_body, uint8_t id)
{
    uint32_t slot = (id == (uint8_t)MP_NODE_ID_NONE) ? 0u
                                                     : mp_node_map_slot_of_id(victim_body, id);
    uint32_t held = 0;

    if (map.cell == 0) {
        return;
    }
    if (slot != 0u) {
        if (memory_try_write(map.cell, &slot, sizeof slot)) {
            ++map.written;
        } else {
            ++map.faults;
        }
        return;
    }
    /* Read before the write: `cleared` rises on every replay and is therefore a second name for
     * how many ran, while `discarded` counts the ones that threw a real node away, which is the
     * set that carrying the node on the wire changes. */
    if (memory_try_read(map.cell, &held, sizeof held) && held != 0u) {
        ++map.discarded;
    }
    if (memory_try_write(map.cell, &slot, sizeof slot)) {
        ++map.cleared;
    } else {
        ++map.faults;
    }
}

/* The raw slot standing in the cell, for a caller on the machine the victim's body lives on.
 *
 * The name detour above is for the WIRE. On one machine it would be a round trip that loses
 * something: the slot is already exact here, and translating it to a name and back drops every
 * joint whose name the table has no word for. */
uint32_t mp_node_map_cell_slot(void)
{
    uint32_t slot = 0;

    if (map.cell == 0 || !memory_try_read_u32(map.cell, &slot)) {
        return 0u;
    }
    return slot;
}

void mp_node_map_cell_counters(uint32_t *cleared, uint32_t *discarded, uint32_t *written,
                               uint32_t *faults)
{
    if (cleared != NULL) {
        *cleared = map.cleared;
    }
    if (discarded != NULL) {
        *discarded = map.discarded;
    }
    if (written != NULL) {
        *written = map.written;
    }
    if (faults != NULL) {
        *faults = map.faults;
    }
}
