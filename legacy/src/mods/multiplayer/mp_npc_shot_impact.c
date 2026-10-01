/* mp_npc_shot_impact.c: which bolt's impact is running. See the header. */
#include "mp_npc_shot_impact.h"

#include "mp_armed.h"
#include "mp_signatures_shot_impact.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A shot node's body object, the sender every contact of the shot names. */
#define SHOT_NODE_OBJECT 0xA0u

typedef int32_t(__cdecl *shot_impact_fn_t)(void *shot, void *victim);

typedef struct npc_shot_impact {
    detour_t         detour;
    shot_impact_fn_t original;
    bool             installed;
    uintptr_t        site;
    uint32_t         depth;    /* impacts running now; the engine never nests one, this would */
    uint32_t         parent;   /* the object of the innermost one */
    uint32_t         seen;
} npc_shot_impact_t;

static npc_shot_impact_t impact;

/* engine: void shot_impact(shot *pShot, void *pVictim) */
static int32_t __cdecl hook_shot_impact(void *shot, void *victim)
{
    uint32_t outer  = impact.parent;
    uint32_t object = 0u;
    int32_t  result;

    if (!mp_armed_transport()) {
        return impact.original(shot, victim);
    }
    if (shot != NULL) {
        (void)memory_try_read((uintptr_t)shot + SHOT_NODE_OBJECT, &object, sizeof object);
    }
    ++impact.seen;
    ++impact.depth;
    impact.parent = object;
    result = impact.original(shot, victim);
    impact.parent = outer;
    --impact.depth;
    return result;
}

bool mp_npc_shot_impact_parent(uint32_t *parent)
{
    if (impact.depth == 0u) {
        return false;
    }
    if (parent != NULL) {
        *parent = impact.parent;
    }
    return true;
}

bool mp_npc_shot_impact_install(uintptr_t shot_spawn)
{
    uintptr_t site;

    if (impact.installed) {
        return true;
    }
    site = mp_signatures_shot_impact_address(MP_SHOT_IMPACT_SITE_HEAD);
    if (site == 0u || !mp_signatures_shot_impact_proves_sub_shots(shot_spawn)) {
        log_warning("a bolt's sub-shots cannot be tied to it: shot_impact %s, so the thermal "
                    "detonator stays on the host, where its fireball hurts once",
                    site == 0u ? "did not resolve"
                               : "resolved but its two sub-shot spawns do not both call "
                                 "shot_spawn from inside it");
        return false;
    }
    if (!detour_install(&impact.detour, site, (const void *)&hook_shot_impact,
                        mp_signatures_shot_impact_prologue(MP_SHOT_IMPACT_SITE_HEAD))) {
        log_error("shot_impact at %08X refused the detour, so the thermal detonator stays on the "
                  "host", (unsigned)site);
        return false;
    }
    impact.original  = (shot_impact_fn_t)impact.detour.original;
    impact.site      = site;
    impact.installed = true;
    log_info("a bolt's sub-shots are tied to it: shot_impact at %08X is hulled and both of its "
             "sub-shot spawns call shot_spawn, so the fireball of a thermal detonator is the "
             "detonator's and the detonator can travel", (unsigned)site);
    return true;
}

bool mp_npc_shot_impact_installed(void)
{
    return impact.installed;
}

uint32_t mp_npc_shot_impact_seen(void)
{
    return impact.seen;
}

uintptr_t mp_npc_shot_impact_site(void)
{
    return impact.site;
}
