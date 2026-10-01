/* mp_enemy_nodes.c: the hidden nodes and meshes of an enemy, as the host has them. See the header.
 */
#include "mp_enemy_nodes.h"

#include "mp_enemy_nodes_rule.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"
#include "mp_world_event.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The body, the thing drawn for it, and the model behind the thing. Both arrays hold one i32 per
 * node of the model; a nonzero entry leaves that node, or the mesh of that index, undrawn. */
#define BODY_THING        0x9Cu
#define THING_MODEL       0x04u
#define THING_NODE_HIDDEN 0x28u
#define THING_MESH_HIDDEN 0x2Cu
#define MODEL_NODE_COUNT  0x54u

/* The actor's state flags, and the bit that says it carries a player's body. */
#define ACTOR_STATE_FLAGS  0x14u
#define FLAG_HOSTS_PLAYER  0x2000u

/* A node count past this is not a model this build knows; the shipped rigs carry at most 48. */
#define NODES_SANE 256u

/* How many changes on a client get a line of their own. */
#define NAMED_CHANGES 8u

/* The host's last reading of one key in this level. */
#define SEEN_ANY     0x01u
#define SEEN_HOSTING 0x02u
#define SEEN_PAST    0x04u

typedef struct drawn {
    uint32_t count;
    uint32_t nodes;    /* the node array, 0 when the thing has none */
    uint32_t meshes;   /* the mesh array, 0 when the thing has none */
} drawn_t;

typedef struct key_seen {
    uint8_t               seen;
    mp_enemy_nodes_mask_t nodes;
    mp_enemy_nodes_mask_t meshes;
} key_seen_t;

typedef struct enemy_nodes_state {
    key_seen_t key[MP_WIRE_KEY_COUNT];
    uint32_t   named;
    char       here_text[96];
    char       said_text[96];

    uint32_t read;          /* host: keys read in this level */
    uint32_t changes;       /* host: readings that differed from the one before */
    uint32_t past_64;       /* host: keys whose model has more nodes than the save carries */
    uint32_t hosting;       /* host: keys left out while they carried a player's body */
    uint32_t host_faults;
    uint32_t records;       /* client: records that said something */
    uint32_t silent;        /* client: records with neither presence bit */
    uint32_t nodes_shown;
    uint32_t nodes_hidden;
    uint32_t meshes_shown;
    uint32_t meshes_hidden;
    uint32_t other_model;   /* client: a host bit past this model's node count */
    uint32_t client_faults;
} enemy_nodes_state_t;

static enemy_nodes_state_t nodes_state;

void mp_enemy_nodes_reset(void)
{
    memset(nodes_state.key, 0, sizeof nodes_state.key);
    nodes_state.named = 0u;
}

/* One word, read under the structured handler rather than after a VirtualQuery: this runs for
 * every actor of every record on both sides, and a system call per word would be most of its
 * cost. */
static bool word_at(uintptr_t address, uint32_t *out)
{
    return memory_try_read(address, out, sizeof *out);
}

/* The thing's node count and its two arrays. */
static bool drawn_of(uint32_t body, drawn_t *out)
{
    uint32_t thing = 0;
    uint32_t model = 0;

    memset(out, 0, sizeof *out);
    return body != 0u && word_at((uintptr_t)body + BODY_THING, &thing) && thing != 0u &&
           word_at((uintptr_t)thing + THING_MODEL, &model) && model != 0u &&
           word_at((uintptr_t)model + MODEL_NODE_COUNT, &out->count) && out->count != 0u &&
           out->count <= NODES_SANE && word_at((uintptr_t)thing + THING_NODE_HIDDEN, &out->nodes) &&
           word_at((uintptr_t)thing + THING_MESH_HIDDEN, &out->meshes);
}

static uint32_t saved(uint32_t count)
{
    return count < MP_ENEMY_NODES_MAX ? count : MP_ENEMY_NODES_MAX;
}

static bool read_mask(uint32_t array, uint32_t count, mp_enemy_nodes_mask_t *out)
{
    int32_t entries[MP_ENEMY_NODES_MAX];

    if (array == 0u ||
        !memory_try_read((uintptr_t)array, entries, (size_t)saved(count) * sizeof entries[0])) {
        return false;
    }
    *out = mp_enemy_nodes_rule_fold(entries, saved(count));
    return true;
}

static mp_enemy_nodes_mask_t record_mask(const mp_enemy_record_t *record, mp_enemy_field_t low)
{
    mp_enemy_nodes_mask_t mask;

    mask.word[0] = record->value[low];
    mask.word[1] = record->value[low + 1];
    return mask;
}

/* ==============================================================================================
 * The host's half.
 * ============================================================================================ */

/* Counted once per key and level, and a change only against the key's reading before. */
static void note_reading(uint32_t key, const mp_enemy_nodes_mask_t *nodes,
                         const mp_enemy_nodes_mask_t *meshes, uint32_t count)
{
    key_seen_t *seen;

    if (key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    seen = &nodes_state.key[key];
    if (count > MP_ENEMY_NODES_MAX && (seen->seen & SEEN_PAST) == 0u) {
        seen->seen = (uint8_t)(seen->seen | SEEN_PAST);
        ++nodes_state.past_64;
    }
    if ((seen->seen & SEEN_ANY) == 0u) {
        seen->seen = (uint8_t)(seen->seen | SEEN_ANY);
        ++nodes_state.read;
    } else if (memcmp(&seen->nodes, nodes, sizeof *nodes) != 0 ||
               memcmp(&seen->meshes, meshes, sizeof *meshes) != 0) {
        ++nodes_state.changes;
    }
    seen->nodes  = *nodes;
    seen->meshes = *meshes;
}

void mp_enemy_nodes_read(uintptr_t actor, uint32_t body, uint32_t key, mp_enemy_record_t *record)
{
    mp_enemy_nodes_mask_t nodes;
    mp_enemy_nodes_mask_t meshes;
    drawn_t               drawn;
    uint32_t              flags = 0;

    if (record == NULL) {
        return;
    }
    memset(&nodes, 0, sizeof nodes);
    memset(&meshes, 0, sizeof meshes);
    record->value[MP_ENEMY_F_NODES_LO]  = 0u;
    record->value[MP_ENEMY_F_NODES_HI]  = 0u;
    record->value[MP_ENEMY_F_MESHES_LO] = 0u;
    record->value[MP_ENEMY_F_MESHES_HI] = 0u;
    if (word_at(actor + ACTOR_STATE_FLAGS, &flags) && (flags & FLAG_HOSTS_PLAYER) != 0u) {
        if (key < MP_WIRE_KEY_COUNT && (nodes_state.key[key].seen & SEEN_HOSTING) == 0u) {
            nodes_state.key[key].seen = (uint8_t)(nodes_state.key[key].seen | SEEN_HOSTING);
            ++nodes_state.hosting;
        }
        return;
    }
    if (!drawn_of(body, &drawn)) {
        ++nodes_state.host_faults;
        return;
    }
    /* Each array travels with its own presence bit, as the engine's save flags each: a thing
     * without one, or one that did not read, says nothing about it. */
    if (read_mask(drawn.nodes, drawn.count, &nodes)) {
        record->value[MP_ENEMY_F_NODES_LO] = nodes.word[0];
        record->value[MP_ENEMY_F_NODES_HI] = nodes.word[1];
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_NODES;
    } else if (drawn.nodes != 0u) {
        ++nodes_state.host_faults;
    }
    if (read_mask(drawn.meshes, drawn.count, &meshes)) {
        record->value[MP_ENEMY_F_MESHES_LO] = meshes.word[0];
        record->value[MP_ENEMY_F_MESHES_HI] = meshes.word[1];
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_MESHES;
    } else if (drawn.meshes != 0u) {
        ++nodes_state.host_faults;
    }
    note_reading(key, &nodes, &meshes, drawn.count);
}

bool mp_enemy_nodes_director(uintptr_t actor, bool client_of_a_session)
{
    return mp_world_event_output_is_the_hosts(actor, client_of_a_session);
}

/* ==============================================================================================
 * The client's half.
 * ============================================================================================ */

/* Writes what the step names into one array, 0 for shown and 1 for hidden, the values the engine's
 * own writers store. */
static bool write_step(uint32_t array, const mp_enemy_nodes_step_t *step)
{
    const int32_t shown  = 0;
    const int32_t hidden = 1;
    uint32_t      index;

    for (index = 0; index < MP_ENEMY_NODES_MAX; ++index) {
        const int32_t *value = mp_enemy_nodes_rule_bit(&step->hide, index)   ? &hidden
                               : mp_enemy_nodes_rule_bit(&step->show, index) ? &shown
                                                                             : NULL;

        if (value != NULL &&
            !memory_try_write((uintptr_t)array + index * sizeof(int32_t), value, sizeof *value)) {
            return false;
        }
    }
    return true;
}

/* One array brought to the host's: the step against this side's own entries, then written. */
static bool follow(uint32_t array, uint32_t count, const mp_enemy_nodes_mask_t *want,
                   mp_enemy_nodes_step_t *step)
{
    mp_enemy_nodes_mask_t here;

    memset(step, 0, sizeof *step);
    if (!read_mask(array, count, &here)) {
        return false;
    }
    *step = mp_enemy_nodes_rule_step(want, &here, count);
    return write_step(array, step);
}

static void step_text(const char *what, const mp_enemy_nodes_step_t *step, char *out, size_t size)
{
    uint32_t hidden = mp_enemy_nodes_rule_count(&step->hide);
    uint32_t shown  = mp_enemy_nodes_rule_count(&step->show);
    uint32_t first  = mp_enemy_nodes_rule_first(hidden != 0u ? &step->hide : &step->show);

    if (hidden + shown == 0u) {
        text_format(out, size, "no %s", what);
    } else {
        text_format(out, size, "%s %u %s (%u hidden, %u shown)", what, (unsigned)first,
                    hidden != 0u ? "hidden" : "shown", (unsigned)hidden, (unsigned)shown);
    }
}

static void name_change(uint32_t key, const mp_enemy_nodes_step_t *nodes,
                        const mp_enemy_nodes_step_t *meshes)
{
    char node_text[48];
    char mesh_text[48];

    if (nodes_state.named >= NAMED_CHANGES) {
        return;
    }
    ++nodes_state.named;
    step_text("node", nodes, node_text, sizeof node_text);
    step_text("mesh", meshes, mesh_text, sizeof mesh_text);
    log_info("the hidden nodes changed here: placement %u, %s, %s, as the host has it",
             (unsigned)key, node_text, mesh_text);
}

void mp_enemy_nodes_apply(uintptr_t actor, uint32_t body, uint32_t key,
                          const mp_enemy_record_t *record, const mp_enemy_record_t *previous)
{
    mp_enemy_nodes_mask_t want_nodes;
    mp_enemy_nodes_mask_t want_meshes;
    mp_enemy_nodes_step_t nodes;
    mp_enemy_nodes_step_t meshes;
    drawn_t               drawn;
    bool                  has_nodes;
    bool                  has_meshes;

    (void)actor;
    (void)previous;
    if (record == NULL) {
        return;
    }
    has_nodes  = mp_enemy_wire_has(record, MP_ENEMY_HAS_NODES);
    has_meshes = mp_enemy_wire_has(record, MP_ENEMY_HAS_MESHES);
    if (!has_nodes && !has_meshes) {
        ++nodes_state.silent;
        return;
    }
    ++nodes_state.records;
    want_nodes  = record_mask(record, MP_ENEMY_F_NODES_LO);
    want_meshes = record_mask(record, MP_ENEMY_F_MESHES_LO);
    if (!drawn_of(body, &drawn)) {
        ++nodes_state.client_faults;
        return;
    }
    if ((has_nodes && mp_enemy_nodes_rule_past(&want_nodes, drawn.count)) ||
        (has_meshes && mp_enemy_nodes_rule_past(&want_meshes, drawn.count))) {
        ++nodes_state.other_model;
        return;
    }
    memset(&nodes, 0, sizeof nodes);
    memset(&meshes, 0, sizeof meshes);
    if ((has_nodes && !follow(drawn.nodes, drawn.count, &want_nodes, &nodes)) ||
        (has_meshes && !follow(drawn.meshes, drawn.count, &want_meshes, &meshes))) {
        ++nodes_state.client_faults;
    }
    nodes_state.nodes_shown   += mp_enemy_nodes_rule_count(&nodes.show);
    nodes_state.nodes_hidden  += mp_enemy_nodes_rule_count(&nodes.hide);
    nodes_state.meshes_shown  += mp_enemy_nodes_rule_count(&meshes.show);
    nodes_state.meshes_hidden += mp_enemy_nodes_rule_count(&meshes.hide);
    if (mp_enemy_nodes_rule_count(&nodes.show) + mp_enemy_nodes_rule_count(&nodes.hide) +
            mp_enemy_nodes_rule_count(&meshes.show) + mp_enemy_nodes_rule_count(&meshes.hide) !=
        0u) {
        name_change(key, &nodes, &meshes);
    }
}

/* ==============================================================================================
 * The replica line and the report.
 * ============================================================================================ */

const char *mp_enemy_nodes_describe(uint32_t body)
{
    mp_enemy_nodes_mask_t nodes;
    mp_enemy_nodes_mask_t meshes;
    drawn_t               drawn;

    if (!drawn_of(body, &drawn) || !read_mask(drawn.nodes, drawn.count, &nodes) ||
        !read_mask(drawn.meshes, drawn.count, &meshes)) {
        return "not readable here";
    }
    text_format(nodes_state.here_text, sizeof nodes_state.here_text,
                "nodes %08X%08X and meshes %08X%08X here", (unsigned)nodes.word[1],
                (unsigned)nodes.word[0], (unsigned)meshes.word[1], (unsigned)meshes.word[0]);
    return nodes_state.here_text;
}

const char *mp_enemy_nodes_said(const mp_enemy_record_t *record)
{
    if (record == NULL || (!mp_enemy_wire_has(record, MP_ENEMY_HAS_NODES) &&
                           !mp_enemy_wire_has(record, MP_ENEMY_HAS_MESHES))) {
        return "not said by the host";
    }
    text_format(nodes_state.said_text, sizeof nodes_state.said_text,
                "nodes %08X%08X and meshes %08X%08X on the host",
                (unsigned)record->value[MP_ENEMY_F_NODES_HI],
                (unsigned)record->value[MP_ENEMY_F_NODES_LO],
                (unsigned)record->value[MP_ENEMY_F_MESHES_HI],
                (unsigned)record->value[MP_ENEMY_F_MESHES_LO]);
    return nodes_state.said_text;
}

void mp_enemy_nodes_report(void)
{
    log_info("the hidden nodes (host): %u actor(s) read, %u change(s) described, %u model(s) past "
             "64 nodes, %u actor(s) left out while they carried a player's body, %u unreadable",
             (unsigned)nodes_state.read, (unsigned)nodes_state.changes,
             (unsigned)nodes_state.past_64, (unsigned)nodes_state.hosting,
             (unsigned)nodes_state.host_faults);
    log_info("the hidden nodes (client): %u record(s), nodes %u shown and %u hidden, meshes %u "
             "shown and %u hidden to match, %u refused for another model (a mask past this "
             "model's node count), %u unreadable; %u record(s) that said nothing",
             (unsigned)nodes_state.records, (unsigned)nodes_state.nodes_shown,
             (unsigned)nodes_state.nodes_hidden, (unsigned)nodes_state.meshes_shown,
             (unsigned)nodes_state.meshes_hidden, (unsigned)nodes_state.other_model,
             (unsigned)nodes_state.client_faults, (unsigned)nodes_state.silent);
}
