/* character_prop_rig.c: what a rig says. See the header.
 *
 * Cut out of character_prop.c at the seam that file's size note names: what a RIG SAYS against
 * what is drawn and what the engine is answered with. Nothing here was changed in the move.
 */
#include "character_prop_rig.h"

#include "common/memory.h"

#include <stddef.h>
#include <string.h>

/* The render handle, the model and the node, as the draw reads them. */
#define RDTHING_MODEL3      0x04u
#define RDTHING_NODE_MATRIX 0x20u
#define MODEL_NUM_NODES     0x54u
#define MODEL_NODES         0x58u
#define NODE_BYTES          0xB4u
#define NODE_MATRIX_SLOT    0x44u
#define NODE_PIVOT          0x60u
#define MATRIX_BYTES        0x30u

/* The widest shipped skeleton is 48 nodes. Anything claiming more is not a skeleton. */
#define NODES_MAX           256u

bool character_prop_rig_nodes(uintptr_t model, uintptr_t *out_nodes, uint32_t *out_count)
{
    uint32_t count = 0;
    uint32_t nodes = 0;

    if (model == 0 || !memory_try_read(model + MODEL_NUM_NODES, &count, sizeof count) ||
        !memory_try_read(model + MODEL_NODES, &nodes, sizeof nodes) ||
        count == 0u || count > NODES_MAX || nodes == 0u) {
        return false;
    }
    *out_nodes = (uintptr_t)nodes;
    *out_count = count;
    return true;
}

const char *character_prop_rig_name(uintptr_t name_table, uint32_t name_id, char *buffer)
{
    uint32_t wanted = 0;

    if (name_table == 0 || name_id >= PROP_NAME_COUNT ||
        !memory_try_read(name_table + 4u * name_id, &wanted, sizeof wanted) ||
        wanted == 0u || !memory_try_read((uintptr_t)wanted, buffer, PROP_NAME_BYTES)) {
        return NULL;
    }
    buffer[PROP_NAME_BYTES - 1u] = '\0';
    return buffer;
}

bool character_prop_rig_node(uintptr_t model, uintptr_t name_table, uint32_t name_id,
                             uint32_t *out_slot, uintptr_t *out_node)
{
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    char      want[PROP_NAME_BYTES];
    char      have[PROP_NAME_BYTES];
    uint32_t  i;

    if (!character_prop_rig_nodes(model, &nodes, &count) ||
        character_prop_rig_name(name_table, name_id, want) == NULL) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        uintptr_t node = nodes + (uintptr_t)i * NODE_BYTES;
        uint32_t  slot = 0;

        if (!memory_try_read(node, have, sizeof have)) {
            return false;
        }
        have[sizeof have - 1u] = '\0';
        if (strcmp(have, want) != 0) {
            continue;
        }
        if (!memory_try_read(node + NODE_MATRIX_SLOT, &slot, sizeof slot) || slot >= count) {
            return false;
        }
        *out_slot = slot;
        if (out_node != NULL) {
            *out_node = node;
        }
        return true;
    }
    return false;
}

bool character_prop_rig_pivot(uintptr_t node, float out_pivot[3])
{
    return memory_try_read(node + NODE_PIVOT, out_pivot, 3u * sizeof out_pivot[0]);
}

bool character_prop_rig_joint(uintptr_t thing, uint32_t slot, float out[PROP_MATRIX_FLOATS])
{
    uint32_t  model = 0;
    uintptr_t nodes = 0;
    uint32_t  count = 0;
    uint32_t  matrices = 0;

    return memory_try_read(thing + RDTHING_MODEL3, &model, sizeof model) &&
           character_prop_rig_nodes((uintptr_t)model, &nodes, &count) && slot < count &&
           memory_try_read(thing + RDTHING_NODE_MATRIX, &matrices, sizeof matrices) &&
           matrices != 0u &&
           memory_try_read((uintptr_t)matrices + (uintptr_t)slot * MATRIX_BYTES, out,
                           MATRIX_BYTES);
}
