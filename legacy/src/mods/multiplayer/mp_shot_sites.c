/* mp_shot_sites.c: the nine calls of the AI into shot_spawn. See the header. */
#include "mp_shot_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Each pattern ends on `call shot_spawn; add esp,0x14`, the call's displacement masked. RET is
 * where the call returns to, counted from the first byte. The arguments pushed before the call
 * are what makes each one this call and no other; the class they push is the actor's own. */

/* op_shoot: the twin muzzles' first bolt, kind 2 from the second hardpoint. */
static const uint8_t SIG_OP_SHOOT_A[] = {
    0x52, 0x6A, 0x02, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_OP_SHOOT_A[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
/* op_shoot: the twin muzzles' second bolt. */
static const uint8_t SIG_OP_SHOOT_B[] = {
    0x51, 0x6A, 0x02, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_OP_SHOOT_B[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
/* op_shoot: the bolt of the kind the op names, from the muzzle on the stack. */
static const uint8_t SIG_OP_SHOOT_C[] = {
    0x8D, 0x4D, 0xB8, 0x51, 0x8B, 0x55, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_OP_SHOOT_C[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
/* op_shoot: the same kind from the actor's own muzzle. */
static const uint8_t SIG_OP_SHOOT_D[] = {
    0x8B, 0x4D, 0x0C, 0x51, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_OP_SHOOT_D[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
/* fire_emplacement: its three barrels. */
static const uint8_t SIG_EMPLACEMENT_A[] = {
    0x8B, 0x4D, 0xD8, 0x51, 0x8D, 0x55, 0xB0, 0x52, 0x8B, 0x45, 0x0C, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_EMPLACEMENT_A[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
static const uint8_t SIG_EMPLACEMENT_B[] = {
    0x8D, 0x4D, 0xB0, 0x51, 0x8B, 0x55, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_EMPLACEMENT_B[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
static const uint8_t SIG_EMPLACEMENT_C[] = {
    0x8B, 0x4D, 0xC0, 0x51, 0x8D, 0x55, 0xB0, 0x52, 0x8B, 0x45, 0x0C, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_EMPLACEMENT_C[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
/* enemy_scriptedDeath: the two blasts a dying actor leaves, kinds 0x23 and 0x24. */
static const uint8_t SIG_SCRIPTED_DEATH_A[] = {
    0x6A, 0x23, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_SCRIPTED_DEATH_A[] = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
static const uint8_t SIG_SCRIPTED_DEATH_B[] = {
    0x6A, 0x24, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x14
};
static const uint8_t MSK_SCRIPTED_DEATH_B[] = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};

#define CALL_AND_CLEANUP 8u   /* the call, then `add esp,0x14`, at the end of every pattern */

typedef struct shot_site {
    const char    *name;
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t         size;
} shot_site_t;

#define SITE(name_, sig_, msk_) { name_, sig_, msk_, sizeof sig_ }

static const shot_site_t SITES[MP_SHOT_SITES_AI] = {
    SITE("op_shoot, the first twin muzzle", SIG_OP_SHOOT_A, MSK_OP_SHOOT_A),
    SITE("op_shoot, the second twin muzzle", SIG_OP_SHOOT_B, MSK_OP_SHOOT_B),
    SITE("op_shoot, the named kind", SIG_OP_SHOOT_C, MSK_OP_SHOOT_C),
    SITE("op_shoot, the actor's muzzle", SIG_OP_SHOOT_D, MSK_OP_SHOOT_D),
    SITE("fire_emplacement, the first barrel", SIG_EMPLACEMENT_A, MSK_EMPLACEMENT_A),
    SITE("fire_emplacement, the second barrel", SIG_EMPLACEMENT_B, MSK_EMPLACEMENT_B),
    SITE("fire_emplacement, the third barrel", SIG_EMPLACEMENT_C, MSK_EMPLACEMENT_C),
    SITE("enemy_scriptedDeath, kind 0x23", SIG_SCRIPTED_DEATH_A, MSK_SCRIPTED_DEATH_A),
    SITE("enemy_scriptedDeath, kind 0x24", SIG_SCRIPTED_DEATH_B, MSK_SCRIPTED_DEATH_B),
};

static struct {
    bool      tried;
    bool      resolved;
    uintptr_t returns[MP_SHOT_SITES_AI];
} sites;

/* The one match of `site` whose call reaches `shot_spawn`, as the address the call returns to;
 * 0 when there is none or more than one. */
static uintptr_t return_of(const shot_site_t *site, uintptr_t shot_spawn)
{
    uintptr_t hits[SIGNATURE_MAX_REPORTED];
    uintptr_t found = 0u;
    size_t    count = signature_count_matches(site->bytes, site->mask, site->size, hits,
                                              SIGNATURE_MAX_REPORTED);
    size_t    i;

    if (count > SIGNATURE_MAX_REPORTED) {
        return 0u;   /* more than can be judged: not a place */
    }
    for (i = 0; i < count; ++i) {
        uintptr_t call = hits[i] + site->size - CALL_AND_CLEANUP;
        uintptr_t back = call + 5u;
        int32_t   displacement = 0;

        if (!memory_try_read(call + 1u, &displacement, sizeof displacement) ||
            back + (uintptr_t)(intptr_t)displacement != shot_spawn) {
            continue;
        }
        if (found != 0u) {
            return 0u;   /* two calls that fit: neither is known to be this one */
        }
        found = back;
    }
    return found;
}

bool mp_shot_sites_resolve(uintptr_t shot_spawn)
{
    uintptr_t found[MP_SHOT_SITES_AI];
    size_t    i;

    if (sites.tried) {
        return sites.resolved;
    }
    sites.tried = true;
    if (shot_spawn == 0u) {
        return false;
    }
    for (i = 0; i < MP_SHOT_SITES_AI; ++i) {
        found[i] = return_of(&SITES[i], shot_spawn);
        if (found[i] == 0u) {
            log_warning("the AI's shot call \"%s\" did not resolve, so no call of the AI is known "
                        "and an ally's class 1 bolt is still taken for the player's own",
                        SITES[i].name);
            return false;
        }
    }
    for (i = 0; i < MP_SHOT_SITES_AI; ++i) {
        sites.returns[i] = found[i];
    }
    sites.resolved = true;
    log_info("the AI's nine shot calls resolved, returning to %08X %08X %08X %08X (op_shoot), "
             "%08X %08X %08X (fire_emplacement), %08X %08X (enemy_scriptedDeath)",
             (unsigned)found[0], (unsigned)found[1], (unsigned)found[2], (unsigned)found[3],
             (unsigned)found[4], (unsigned)found[5], (unsigned)found[6], (unsigned)found[7],
             (unsigned)found[8]);
    return true;
}

mp_shot_whose_t mp_shot_whose(int32_t shooter_class, bool from_the_ai, bool a_copy)
{
    if (shooter_class > 1) {
        return MP_SHOT_NPCS;
    }
    if (shooter_class != 1 || a_copy) {
        return MP_SHOT_NOBODYS;
    }
    return from_the_ai ? MP_SHOT_NPCS : MP_SHOT_PLAYERS;
}

bool mp_shot_sites_resolved(void)
{
    return sites.resolved;
}

bool mp_shot_sites_is_ai(uintptr_t return_address)
{
    size_t i;

    if (!sites.resolved) {
        return false;
    }
    for (i = 0; i < MP_SHOT_SITES_AI; ++i) {
        if (sites.returns[i] == return_address) {
            return true;
        }
    }
    return false;
}
