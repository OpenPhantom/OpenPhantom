/* mp_enemy_fx.c: the emitter a script hangs on an NPC, on the machine that only watches. */
#include "mp_enemy_fx.h"

#include "mp_enemy_pack.h"
#include "mp_signatures.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stdint.h>

/* The handle the engine keeps on the actor: -1 is none (0x00437626), and the actor's deletion is
 * what releases it, so this feature never has to. Read for the report only. */
#define A_FX_HANDLE 0x1E0u

typedef void(__cdecl *start_emitter_fn_t)(uint32_t actor, int32_t fx, int32_t unused, int32_t node);

typedef struct fx_state {
    detour_t              detour;
    start_emitter_fn_t    original;
    bool                  installed;
    mp_enemy_fx_role_fn_t role;

    uint32_t seen_starts;  /* the hull ran */
    uint32_t posted;       /* of them, taken by the world events on a host that describes */
    uint32_t too_large;    /* a template or node the packing cannot carry */
    uint32_t withheld;     /* a client's own start for an actor whose life the host describes */
    uint32_t started;      /* emitters hung on a replica here */
    uint32_t stacked;      /* of those, over a handle the replica still held, as the host does */
    uint32_t unresolved;   /* an emitter the host sent to a side whose hull never stood */
    uint32_t unreadable;   /* the replica did not read as far as its handle, or the event is torn */
} fx_state_t;

static fx_state_t fx;

uint32_t mp_enemy_fx_pack(uint32_t template_index, uint32_t node)
{
    return mp_enemy_pack_pair(template_index, node, MP_ENEMY_FX_MAX_TEMPLATE,
                              MP_ENEMY_FX_MAX_NODE);
}

bool mp_enemy_fx_unpack(uint32_t packed, uint32_t *template_index, uint32_t *node)
{
    return mp_enemy_unpack_pair(packed & 0xFFFFu, template_index, node);
}

/* The hull, on the machine the scripts run on. Our own call below goes through the trampoline and
 * never reaches this, so no guard against ourselves is needed.
 *
 * It asks the bank nothing, and that is deliberate rather than an omission. The footstep hull has
 * to, because the thing it reads belongs to whichever player the window holds. This one is handed
 * its actor as an argument, and an actor is nobody's bank.
 *
 * On a client the start of an actor whose life the host describes is withheld: the host's own
 * start of it reaches the replica as an event, and a second one here would hang the emitter twice.
 *
 * engine: void ai_startEmitter(character *a, int emitterIndex, u32 unusedA1, u32 nodeId) */
static void __cdecl hook_start_emitter(uint32_t actor, int32_t fx_index, int32_t unused,
                                       int32_t node)
{
    bool client = fx.role != NULL && fx.role(NULL, NULL);

    if (actor != 0u && fx_index >= 0 && node >= 0) {
        uint32_t packed = mp_enemy_fx_pack((uint32_t)fx_index, (uint32_t)node);

        ++fx.seen_starts;
        if (packed == 0u) {
            ++fx.too_large;
        } else if (mp_world_event_post_at_actor(MP_WORLD_EVENT_SCRIPT_EMITTER, (uintptr_t)actor,
                                                (uint16_t)packed, NULL, 0u)) {
            ++fx.posted;
        }
    }
    if (mp_world_event_output_is_the_hosts((uintptr_t)actor, client)) {
        ++fx.withheld;
        return;
    }
    if (fx.original != NULL) {
        fx.original(actor, fx_index, unused, node);
    }
}

/* Whether a handle the engine left at +0x1E0 names an emitter: -1 is none, and 0 is a slot of the
 * pool like any other. For the report only; it decides nothing. */
static bool had_one(uint32_t handle)
{
    return (int32_t)handle >= 0;
}

/* On a client, one start the host made, on the replica. The engine overwrites the handle without
 * ending what it held, so the replica does what the host did: one emitter per start. */
static bool play_emitter(const mp_world_event_t *event, uintptr_t replica)
{
    uint32_t template_index = 0;
    uint32_t node           = 0;
    uint32_t held           = 0;

    if (!fx.installed || fx.original == NULL) {
        ++fx.unresolved;
        return false;
    }
    if (replica == 0u || !memory_try_readable(replica, A_FX_HANDLE + sizeof held) ||
        !mp_enemy_fx_unpack(event->a, &template_index, &node)) {
        ++fx.unreadable;
        return false;
    }
    if (memory_read_u32(replica + A_FX_HANDLE, &held) && had_one(held)) {
        ++fx.stacked;
    }
    fx.original((uint32_t)replica, (int32_t)template_index, 0, (int32_t)node);
    ++fx.started;
    return true;
}

bool mp_enemy_fx_install(mp_enemy_fx_role_fn_t role)
{
    uintptr_t site = mp_signatures_address(MP_SITE_AI_START_EMITTER);

    fx.role = role;
    if (fx.installed) {
        return true;
    }
    /* Before the site is asked: a side that cannot hang an emitter still counts every one the host
     * sent it, under its own line, rather than as a kind nobody plays. */
    (void)mp_world_event_set_player(MP_WORLD_EVENT_SCRIPT_EMITTER, &play_emitter);
    if (site == 0) {
        log_warning("a script's emitter on an NPC stays on the machine that ran the script: the "
                    "site did not resolve");
        return false;
    }
    if (!detour_install(&fx.detour, site, (const void *)&hook_start_emitter,
                        mp_signatures_prologue(MP_SITE_AI_START_EMITTER))) {
        log_error("the script emitter at %08X refused the detour", (unsigned)site);
        return false;
    }
    fx.original  = (start_emitter_fn_t)fx.detour.original;
    fx.installed = true;
    return true;
}

/* Printed whenever the hull stands, on both sides: a client whose line was missing could not be
 * told from one where every start was swallowed. A side whose hull never stood but was sent
 * emitters says so with the same line, under "with no site". What arrived, what was seen again and
 * what found no replica is the world events' line now. */
void mp_enemy_fx_report(void)
{
    if (!fx.installed && fx.unresolved == 0u) {
        log_info("  a script's emitter on an NPC: the hull never stood on this side");
        return;
    }
    log_info("  a script's emitter on an NPC: %u started here by a script, %u posted to the world "
             "events, %u refused for a template or node the event cannot carry, %u withheld for "
             "an actor whose life the host describes | on a replica: %u fired (%u over a handle it "
             "still held, as the host does), %u with no site, %u unreadable",
             (unsigned)fx.seen_starts, (unsigned)fx.posted, (unsigned)fx.too_large,
             (unsigned)fx.withheld, (unsigned)fx.started, (unsigned)fx.stacked,
             (unsigned)fx.unresolved, (unsigned)fx.unreadable);
}
