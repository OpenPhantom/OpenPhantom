/* character_restmap.c: the pure half of the name translation, the part a test drives.
 *
 * It reads no engine memory at all: the node names and the rest poses arrive as arrays, and what
 * leaves is a map, a fit and a shift per target node. character_nodemap.c collects those arrays
 * out of two live models and hands them in here, so the decision is the same whether the models
 * are the engine's or a test's. The declarations stay in character_nodemap.h, where the header
 * explains what the two halves are for together.
 */
#include "character_nodemap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The node name field, and only the part up to the terminator is a name. */
#define NODE_NAME_MAX         0x40u

#define KEYENTRY_POS          0x08u   /* absolute local offset to the parent joint */
#define KEYENTRY_EULER        0x14u   /* absolute local orientation, degrees */

/* The three the floor is expressed in. They are ids 0, 2 and 1 of the engine's own actor node name
 * table, which is the enumeration the whole game addresses skeleton nodes by. */
static const char *const SPINE_NAMES[] = { "waist", "chest", "head" };
#define SPINE_COUNT ((uint32_t)(sizeof SPINE_NAMES / sizeof SPINE_NAMES[0]))

/* ============================================================================================ */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* Bounded at the node name field width, because the tail of that field is uninitialised fill and
 * only the part up to the terminator is a name. */
bool character_nodemap_same_name(const char *left, const char *right)
{
    size_t i;

    if (left == NULL || right == NULL) {
        return false;
    }
    for (i = 0; i < NODE_NAME_MAX; ++i) {
        char a = lower(left[i]);
        char b = lower(right[i]);

        if (a != b) {
            return false;
        }
        if (a == '\0') {
            return i != 0u;          /* two empty names are not a match */
        }
    }
    return false;                    /* neither ended inside the field, so neither is a name */
}

static uint32_t index_of_name(const char *const *names, uint32_t count, const char *wanted)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        if (character_nodemap_same_name(names[i], wanted)) {
            return i;
        }
    }
    return count;
}

/* ============================================================================================ */

bool character_nodemap_build(const char *const *reference, uint32_t reference_count,
                             const char *const *target, uint32_t target_count,
                             int32_t *map, nodemap_fit_t *fit)
{
    bool     claimed[NODEMAP_MAX_NODES];
    uint32_t i;
    uint32_t j;
    uint32_t spine = 0;

    if (reference == NULL || target == NULL || map == NULL || fit == NULL) {
        return false;
    }
    if (reference_count == 0u || target_count == 0u ||
        reference_count > NODEMAP_MAX_NODES || target_count > NODEMAP_MAX_NODES) {
        return false;
    }

    memset(&claimed[0], 0, sizeof claimed);
    memset(fit, 0, sizeof *fit);
    fit->reference_nodes = reference_count;
    fit->target_nodes = target_count;

    for (j = 0; j < target_count; ++j) {
        map[j] = NODEMAP_NO_TRACK;
        for (i = 0; i < reference_count; ++i) {
            if (!claimed[i] && character_nodemap_same_name(reference[i], target[j])) {
                claimed[i] = true;
                map[j] = (int32_t)i;
                fit->matched++;
                break;
            }
        }
        if (map[j] == NODEMAP_NO_TRACK) {
            fit->held++;
        }
    }
    fit->dropped = reference_count - fit->matched;

    for (i = 0; i < SPINE_COUNT; ++i) {
        uint32_t at = index_of_name(reference, reference_count, SPINE_NAMES[i]);

        if (at < reference_count && claimed[at]) {
            spine++;
        }
    }
    fit->spine = (spine == SPINE_COUNT);
    return true;
}

bool character_nodemap_fit_is_offered(const nodemap_fit_t *fit)
{
    if (fit == NULL) {
        return false;
    }
    if (fit->target_nodes == 0u || fit->target_nodes > NODEMAP_MAX_NODES) {
        return false;
    }
    return fit->spine;
}

bool character_nodemap_rebase(const nodemap_rest_t *reference, uint32_t reference_count,
                              const nodemap_rest_t *target, uint32_t target_count,
                              const int32_t *map, nodemap_rest_t *rebase)
{
    uint32_t j;
    uint32_t k;

    if (reference == NULL || target == NULL || map == NULL || rebase == NULL) {
        return false;
    }
    if (reference_count == 0u || target_count == 0u ||
        reference_count > NODEMAP_MAX_NODES || target_count > NODEMAP_MAX_NODES) {
        return false;
    }
    for (j = 0; j < target_count; ++j) {
        int32_t from = map[j];

        memset(&rebase[j], 0, sizeof rebase[j]);
        if (from < 0 || (uint32_t)from >= reference_count) {
            continue;
        }
        for (k = 0; k < 3u; ++k) {
            rebase[j].pos[k]   = target[j].pos[k]   - reference[from].pos[k];
            rebase[j].euler[k] = target[j].euler[k] - reference[from].euler[k];
        }
    }
    return true;
}

bool character_nodemap_shift_is_zero(const nodemap_rest_t *shift)
{
    uint32_t k;

    if (shift == NULL) {
        return true;
    }
    for (k = 0; k < 3u; ++k) {
        if (shift->pos[k] != 0.0f || shift->euler[k] != 0.0f) {
            return false;
        }
    }
    return true;
}

void character_nodemap_shift_entry(void *entry, const nodemap_rest_t *shift)
{
    float   *pos;
    float   *euler;
    uint32_t k;

    if (entry == NULL || shift == NULL) {
        return;
    }
    pos   = (float *)(void *)((uint8_t *)entry + KEYENTRY_POS);
    euler = (float *)(void *)((uint8_t *)entry + KEYENTRY_EULER);
    for (k = 0; k < 3u; ++k) {
        pos[k]   += shift->pos[k];
        euler[k] += shift->euler[k];
    }
}
