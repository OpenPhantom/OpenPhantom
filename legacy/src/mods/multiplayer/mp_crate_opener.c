/* mp_crate_opener.c: whether a call of the engine's opener is a push block sinking. See the
 * header. */
#include "mp_crate_opener.h"

#include "mp_crate_rule.h"
#include "mp_world.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

/* The mover record's authored rig, which the rule reads beside the kind. */
#define MOVER_RIG_FLAGS_AT 0x10u

typedef struct crate_opener {
    bool     armed;
    uint32_t sinks;
} crate_opener_t;

static crate_opener_t opener;

void mp_crate_opener_arm(void)
{
    opener.armed = true;
}

bool mp_crate_opener_is_a_sink(uint32_t world, int32_t index)
{
    uint32_t mover = 0u;
    int32_t  rig   = 0;
    int32_t  kind  = 0;

    if (!opener.armed || index < 0 || !mp_world_mover_at(world, (uint32_t)index, &mover) ||
        !memory_try_read(mover + MOVER_RIG_FLAGS_AT, &rig, sizeof rig) ||
        !memory_try_read(mover + MOVER_TYPE, &kind, sizeof kind) ||
        !mp_crate_rule_opener_is_sink(rig, kind)) {
        return false;
    }
    ++opener.sinks;
    return true;
}

uint32_t mp_crate_opener_sinks(void)
{
    return opener.sinks;
}
