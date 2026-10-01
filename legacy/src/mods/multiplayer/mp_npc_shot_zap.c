/* mp_npc_shot_zap.c: the arcs of an NPC's player zap, from the host's handler to a client's
 * engine. See the header.
 */
#include "mp_npc_shot_zap.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_npc_shot.h"
#include "mp_signatures_effects.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The actor's body and the bolt it tracks, which the AI stores right after the spawn and the bolt
 * clears when it ends; a bolt node's own body; a body's class; the player record's mode, which is
 * the death descriptor exactly when the player is dead. */
#define ACTOR_BODY         0x34u
#define ACTOR_TRACKED_SHOT 0x1ECu
#define SHOT_NODE_OBJECT   0xA0u
#define OBJECT_CLASS       0x04u
#define RECORD_MODE        0x60u

/* How many zaps one substep can note before the next resolves them. A zap is one per shooter per
 * fire interval; more than this in a substep is counted and not sent. */
#define PENDING 8u

/* The zap's two arcs, as the handler's own pushes prove them: a second, then a second and a half,
 * both 16 wide, in two reds, and bHurts 0. */
#define ARC_WIDTH        16.0f
#define ARC_SHORT_LIFE   1.0f
#define ARC_LONG_LIFE    1.5f
#define ARC_SHORT_COLOUR 0xF0FF0000u
#define ARC_LONG_COLOUR  0xC8FF0000u

/* `int fxzappo_create(bapObj *a, bapObj *b, f32 lifeSec, f32 width, u32 colourRGB, i32 bHurts)`,
 * cdecl; b may be 0. */
typedef int32_t(__cdecl *zappo_create_fn_t)(uint32_t a, uint32_t b, float life, float width,
                                            uint32_t colour, int32_t hurts);

typedef struct zap_state {
    bool              installed;
    zappo_create_fn_t create;
    uint32_t          pending[PENDING];
    uint32_t          pending_count;
    uint8_t           victim_slot;

    uint32_t noted;          /* host: zaps an NPC fired while this side describes */
    uint32_t posted;
    uint32_t host_dead;      /* host: the host's player was dead, so the handler drew nothing */
    uint32_t no_shooter;     /* host: no actor tracked the zap any more */
    uint32_t full;           /* host: past the pending list */
    uint32_t arcs;           /* client: arcs drawn */
    uint32_t kept_off;       /* client: an end left out because its body was of class 1 */
    uint32_t shooter_alone;  /* client: arcs on the shooter only */
    uint32_t no_body;        /* client: the shooter's replica had no body to hang an arc on */
} zap_state_t;

static zap_state_t zap;

/* ==============================================================================================
 * The host's half.
 * ============================================================================================ */

/* The handler draws its arcs for the player alive on this machine, and for none when that player
 * is dead, which is when the record's mode is the death descriptor. */
static bool host_player_alive(void)
{
    uintptr_t cell   = mp_cells_address(MP_CELL_PR);
    uintptr_t death  = mp_cells_address(MP_CELL_MODE_DEATH_DESC);
    uint32_t  record = 0;
    uint32_t  mode   = 0;

    return cell != 0u && death != 0u && memory_try_read_u32(cell, &record) && record != 0u &&
           memory_try_read_u32((uintptr_t)record + RECORD_MODE, &mode) && (uintptr_t)mode != death;
}

void mp_npc_shot_zap_note(uint32_t object)
{
    if (!zap.installed || object == 0u || !mp_enemy_sync_describing()) {
        return;
    }
    ++zap.noted;
    if (!host_player_alive()) {
        ++zap.host_dead;
        return;
    }
    if (zap.pending_count >= PENDING) {
        ++zap.full;
        return;
    }
    zap.pending[zap.pending_count++] = object;
}

/* One actor of the walk: a zap whose body is the one its tracked shot flies as is its. */
static void find_shooter(uintptr_t actor, void *user)
{
    uint32_t node   = 0;
    uint32_t object = 0;
    uint32_t i;

    (void)user;
    if (!memory_try_read_u32(actor + ACTOR_TRACKED_SHOT, &node) || node == 0u ||
        !memory_try_read_u32((uintptr_t)node + SHOT_NODE_OBJECT, &object) || object == 0u) {
        return;
    }
    for (i = 0; i < zap.pending_count; ++i) {
        if (zap.pending[i] == object) {
            zap.pending[i] = 0u;
            if (mp_world_event_post_at_actor(MP_WORLD_EVENT_ZAP_ARCS, actor,
                                             mp_npc_shot_zap_pack(zap.victim_slot), NULL, 0u)) {
                ++zap.posted;
            }
        }
    }
}

void mp_npc_shot_zap_resolve(void)
{
    uint32_t i;

    if (zap.pending_count == 0u) {
        return;
    }
    zap.victim_slot = mp_bridge_drain_my_slot();
    (void)mp_enemy_bind_walk(&find_shooter, NULL);
    for (i = 0; i < zap.pending_count; ++i) {
        zap.no_shooter += zap.pending[i] != 0u ? 1u : 0u;
    }
    zap.pending_count = 0u;
}

/* ==============================================================================================
 * The client's half.
 * ============================================================================================ */

static bool class_of(uint32_t body, int32_t *out)
{
    return body != 0u && memory_try_read((uintptr_t)body + OBJECT_CLASS, out, sizeof *out);
}

/* The far body of the player in world slot `slot`, and its class. False for this machine's own
 * player and for a slot no far body shows here. */
static bool victim_body(uint8_t slot, uint32_t *body, int32_t *cls)
{
    size_t bank;

    *body = 0u;
    if (slot == mp_bridge_drain_my_slot()) {
        return false;
    }
    bank = mp_bridge_far_bank_of_slot(slot);
    return bank != 0u && mp_body_exists_at(bank) &&
           mp_bank_read_at(bank, MP_HERO_BLOCK_HACTOR, body, sizeof *body) && *body != 0u &&
           class_of(*body, cls);
}

static bool play_zap(const mp_world_event_t *event, uintptr_t replica)
{
    uint32_t shooter       = 0;
    uint32_t victim        = 0;
    int32_t  shooter_class = 0;
    int32_t  victim_class  = 0;
    uint8_t  slot          = 0;
    bool     here          = false;

    if (replica == 0u || !memory_read_u32(replica + ACTOR_BODY, &shooter) ||
        !class_of(shooter, &shooter_class)) {
        ++zap.no_body;
        return false;
    }
    if (mp_npc_shot_zap_unpack(event->a, &slot)) {
        here = victim_body(slot, &victim, &victim_class);
    }
    switch (mp_npc_shot_zap_ends(shooter_class, here, victim_class)) {
    case MP_NPC_SHOT_ZAP_NONE:
        ++zap.kept_off;
        return false;
    case MP_NPC_SHOT_ZAP_SHOOTER_ALONE:
        zap.kept_off += (here && victim_class == MP_BODY_CLASS_PLAYER0) ? 1u : 0u;
        ++zap.shooter_alone;
        (void)zap.create(shooter, 0u, ARC_SHORT_LIFE, ARC_WIDTH, ARC_SHORT_COLOUR, 0);
        (void)zap.create(shooter, 0u, ARC_LONG_LIFE, ARC_WIDTH, ARC_LONG_COLOUR, 0);
        break;
    case MP_NPC_SHOT_ZAP_BOTH:
    default:
        /* The handler's own order, the player first and the zap second, which puts three of the
         * four points on the second and the last on the first. */
        (void)zap.create(victim, shooter, ARC_SHORT_LIFE, ARC_WIDTH, ARC_SHORT_COLOUR, 0);
        (void)zap.create(victim, shooter, ARC_LONG_LIFE, ARC_WIDTH, ARC_LONG_COLOUR, 0);
        break;
    }
    zap.arcs += 2u;
    return true;
}

bool mp_npc_shot_zap_install(void)
{
    uintptr_t create;

    if (zap.installed) {
        return true;
    }
    create = mp_signatures_effects_callee(MP_EFFECTS_SITE_PLAYER_ZAP_ARCS,
                                          MP_EFFECTS_ZAP_FIRST_CALL,
                                          MP_EFFECTS_SITE_FXZAPPO_CREATE);
    if (create == 0u ||
        mp_signatures_effects_callee(MP_EFFECTS_SITE_PLAYER_ZAP_ARCS, MP_EFFECTS_ZAP_SECOND_CALL,
                                     MP_EFFECTS_SITE_FXZAPPO_CREATE) != create) {
        log_warning("the arcs of an NPC's player zap stay on the host: fxzappo_create and the two "
                    "calls the zap makes of it do not agree");
        return false;
    }
    zap.create    = (zappo_create_fn_t)create;
    zap.installed = true;
    (void)mp_world_event_set_player(MP_WORLD_EVENT_ZAP_ARCS, &play_zap);
    log_info("the arcs of an NPC's player zap travel: fxzappo_create at %08X, named by both calls "
             "of the zap", (unsigned)create);
    return true;
}

void mp_npc_shot_zap_report(void)
{
    if (!zap.installed) {
        log_info("  the arcs of a player zap: fxzappo_create is not bound on this side");
        return;
    }
    log_info("  the arcs of a player zap (host): %u fired by an NPC, %u posted to the world "
             "events, %u while this side's player was dead, %u whose shooter no longer tracked it, "
             "%u past the list | (client): %u arc(s), %u end(s) kept off a body of class 1, %u on "
             "the shooter alone, %u with no shooter body",
             (unsigned)zap.noted, (unsigned)zap.posted, (unsigned)zap.host_dead,
             (unsigned)zap.no_shooter, (unsigned)zap.full, (unsigned)zap.arcs,
             (unsigned)zap.kept_off, (unsigned)zap.shooter_alone, (unsigned)zap.no_body);
}
