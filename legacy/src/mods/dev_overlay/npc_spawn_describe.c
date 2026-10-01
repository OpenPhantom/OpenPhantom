/* npc_spawn_describe.c: see npc_spawn_describe.h. */
#include "npc_spawn_describe.h"

#include "npc_census.h"
#include "npc_spawner.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <math.h>
#include <string.h>

#define ACTOR_POSITION 0xD0u   /* the live actor's position, vec3 */

static float wrap_degrees(float degrees)
{
    degrees = fmodf(degrees, 360.0f);
    return (degrees < 0.0f) ? degrees + 360.0f : degrees;
}

/* Everything a description holds but where the copy stands and looks: the kind, where its record
 * comes from and the behaviour in force. False, with a line where it is not obvious, when nothing
 * is chosen or the kind cannot be carried. */
static bool describe_kind(npc_spawn_desc_t *out, npc_spawner_kind_t *kind)
{
    int32_t chosen = npc_spawner_chosen();

    memset(out, 0, sizeof *out);
    if (chosen < 0 || !npc_spawner_kind((uint32_t)chosen, kind)) {
        return false;
    }
    if (strlen(kind->file) > NPC_SPAWN_FILE_MAX) {
        log_warning("npc spawner: %s is a longer name than a copy can carry, nothing spawned",
                    kind->file);
        return false;
    }
    if (kind->foreign) {
        /* An archive kind borrows the level's first offered placement's everyday numbers, and
         * with none offered the retail defaults (npc_foreign.c). The first in the level's own
         * order, which the census keeps apart from the list it sorts by name. */
        out->archive = true;
        out->source  = (npc_census()->donor < NPC_SPAWN_NO_SOURCE)
                           ? (uint8_t)npc_census()->donor
                           : (uint8_t)NPC_SPAWN_NO_SOURCE;
    } else if (npc_census()->source[(uint32_t)chosen] < NPC_SPAWN_NO_SOURCE) {
        out->source = (uint8_t)npc_census()->source[(uint32_t)chosen];
    } else {
        log_warning("npc spawner: %s is copied from placement %u, past what a copy can name, "
                    "nothing spawned", kind->name, npc_census()->source[(uint32_t)chosen]);
        return false;
    }
    out->behaviour = (uint8_t)npc_spawner_behaviour();
    text_format(out->file, sizeof out->file, "%s", kind->file);
    return true;
}

bool npc_spawn_describe_at(const float *position, float facing, npc_spawn_desc_t *out)
{
    npc_spawner_kind_t kind;

    if (position == NULL || out == NULL || !describe_kind(out, &kind)) {
        return false;
    }
    memcpy(out->position, position, sizeof out->position);
    out->facing = wrap_degrees(facing);
    return true;
}

#define ACTOR_STATE_FLAGS 0x14u   /* the live actor's stateFlags, from the record's flags */
#define ACTOR_STATE       0x20u   /* the live actor's state: 1 active, 3 parked */
#define ACTOR_BODY        0x34u   /* the live actor's object, pBody */
#define BODY_POSITION     0x18u   /* the object's position, vec3 */

void npc_spawn_describe_where(const uint8_t *actor, char *out, uint32_t out_size)
{
    float    pos[3] = { 0.0f, 0.0f, 0.0f };
    float    body_pos[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t body = 0;
    int32_t  state = -1;
    uint32_t flags = 0;
    bool     body_read;

    (void)memory_try_read((uintptr_t)actor + ACTOR_POSITION, pos, sizeof pos);
    (void)memory_try_read((uintptr_t)actor + ACTOR_STATE, &state, sizeof state);
    (void)memory_try_read((uintptr_t)actor + ACTOR_STATE_FLAGS, &flags, sizeof flags);
    body_read = memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) && body != 0 &&
                memory_try_read((uintptr_t)body + BODY_POSITION, body_pos, sizeof body_pos);
    if (body_read) {
        text_format(out, out_size, "at %.1f %.1f %.1f, body %08X at %.1f %.1f %.1f, state %d, "
                    "flags %08X", (double)pos[0], (double)pos[1], (double)pos[2], body,
                    (double)body_pos[0], (double)body_pos[1], (double)body_pos[2], state, flags);
    } else {
        text_format(out, out_size, "at %.1f %.1f %.1f, no body, state %d, flags %08X",
                    (double)pos[0], (double)pos[1], (double)pos[2], state, flags);
    }
}
