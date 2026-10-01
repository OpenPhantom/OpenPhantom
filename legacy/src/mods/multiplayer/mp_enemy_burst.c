/* mp_enemy_burst.c: an enemy that bursts into its pieces on the host bursts on a client too. */
#include "mp_enemy_burst.h"

#include "mp_armed.h"
#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_signatures.h"
#include "mp_wire.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <intrin.h>
#include <string.h>

/* The body's words the burst reads first: its flags at +0x00, bit 0 drawn; the asset at +0x14,
 * whose model at +0xE0 counts the nodes at +0x54; the thing at +0x9C, which the burst poses; and
 * the cylinder radius at +0xB8, the engine's second gate. */
#define BODY_FLAGS   0x00u
#define BODY_ASSET   0x14u
#define BODY_THING   0x9Cu
#define BODY_RADIUS  0xB8u
#define ASSET_MODEL  0xE0u
#define MODEL_NODES  0x54u

/* The actor's state flags and its body. */
#define ACTOR_STATE_FLAGS 0x14u
#define ACTOR_BODY        0x34u

/* The player record's object: the body the engine's first gate compares against. */
#define RECORD_HACTOR 0x0Cu

/* The object pool's capacity as the engine's own free count takes it: 255 less what is live. */
#define OBJECT_POOL 0xFFu

/* How many bursts a run names one by one before it goes back to counting. */
#define NAMED_MAX 8u

typedef void(__cdecl *shatter_fn_t)(uintptr_t body, float speed, int32_t unused);

typedef struct burst_state {
    detour_t     detour;
    shatter_fn_t original;
    bool         installed;
    bool         host;
    uint32_t     tick;

    mp_enemy_burst_ring_t    ring;
    mp_enemy_burst_callers_t callers;
    uint32_t                 named;

    /* The host's side: every call of the engine's burst, and what became of each that burst. */
    uint32_t calls;
    uint32_t burst_seen;
    uint32_t drawn;
    uint32_t hidden;
    uint32_t carried;
    uint32_t unclaimed;
    uint32_t claimed_untravelled;
    uint32_t sent_without;
    uint32_t without_speed;
    uint32_t without_player;
    uint32_t ring_full;
    uint32_t not_host;

    /* A client's side: every removal with a burst, and what became of it. */
    uint32_t carried_in;
    uint32_t burst_here;
    uint32_t burst_copies;
    uint32_t gated;
    uint32_t gate_hidden;
    uint32_t gate_wide;
    uint32_t gate_crowded;
    uint32_t gate_player;
    uint32_t stopped;
    uint32_t no_site;
    uint32_t only_removed;
    uint32_t no_replica;
    uint32_t moved_on;
} burst_state_t;

static burst_state_t burst;

static const char *key_noun(uint32_t key)
{
    return mp_wire_key_is_copy(key) ? "copy" : "placement";
}

static uint32_t key_number(uint32_t key)
{
    return mp_wire_key_is_copy(key) ? key - MP_WIRE_KEY_COPY_BASE : key;
}

/* ==============================================================================================
 * The host's half.
 * ============================================================================================ */

/* The hull. It changes nothing and always calls the engine: the question is only what the call
 * did, which bit 0 of the body's flags answers, and a burst this side describes waits in the ring
 * for its removal. The return address names the caller for the line and decides nothing.
 *
 * engine: void shot_shatterThing(struct bapObj *obj, f32 chunkSpeed, int unused) */
static void __cdecl hook_shatter_thing(uintptr_t body, float speed, int32_t unused)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();
    uint32_t  before = 0;
    uint32_t  after  = 0;
    bool      read_before;
    bool      read_after;

    if (!mp_armed_transport()) {
        burst.original(body, speed, unused);   /* single player bursts and nothing watches */
        return;
    }
    read_before = body != 0u && memory_try_read(body + BODY_FLAGS, &before, sizeof before);
    burst.original(body, speed, unused);
    read_after = body != 0u && memory_try_read(body + BODY_FLAGS, &after, sizeof after);
    ++burst.calls;
    switch (mp_enemy_burst_observe(read_before, before, read_after, after)) {
    case MP_ENEMY_BURST_SEEN_HIDDEN:
        ++burst.hidden;
        return;
    case MP_ENEMY_BURST_SEEN_DRAWN:
        ++burst.drawn;
        return;
    case MP_ENEMY_BURST_SEEN_BURST:
    default:
        break;
    }
    ++burst.burst_seen;
    if (!burst.host) {
        ++burst.not_host;
        return;
    }
    if (!mp_enemy_burst_ring_put(&burst.ring, (uint32_t)body, speed, burst.tick,
                                 (uint32_t)caller)) {
        ++burst.ring_full;
    }
}

bool mp_enemy_burst_install(void)
{
    uintptr_t site = mp_signatures_address(MP_SITE_SHOT_SHATTER_THING);

    if (burst.installed) {
        return true;
    }
    if (site == 0) {
        log_warning("an enemy that bursts into its pieces on the host only vanishes on a client: "
                    "the burst site did not resolve");
        return false;
    }
    if (!detour_install(&burst.detour, site, (const void *)&hook_shatter_thing,
                        mp_signatures_prologue(MP_SITE_SHOT_SHATTER_THING))) {
        log_error("the burst site at %08X refused the detour", (unsigned)site);
        return false;
    }
    burst.original  = (shatter_fn_t)burst.detour.original;
    burst.installed = true;
    log_info("the bursts are bound at %08X: an enemy the host bursts into its pieces bursts on a "
             "client before its removal", (unsigned)site);
    return true;
}

void mp_enemy_burst_set_host(bool host)
{
    burst.host = host;
}

void mp_enemy_burst_set_tick(uint32_t substep)
{
    burst.tick = substep;
}

uint8_t mp_enemy_burst_speed_for(uint32_t body, uint32_t actor_flags)
{
    float   speed = 0.0f;
    uint8_t quarters = 0u;
    bool    found = body != 0u && mp_enemy_burst_ring_find(&burst.ring, body, burst.tick, &speed);

    (void)mp_enemy_burst_attach(found, speed, actor_flags, &quarters);
    return quarters;
}

void mp_enemy_burst_claim(uint32_t body, uint32_t actor_flags, uint32_t key, uint8_t generation,
                          bool travelled)
{
    float   speed = 0.0f;
    uint8_t quarters = 0u;
    bool    found = body != 0u && mp_enemy_burst_ring_find(&burst.ring, body, burst.tick, &speed);

    if (!found) {
        return;
    }
    (void)mp_enemy_burst_ring_take(&burst.ring, body, burst.tick);
    if (!travelled) {
        ++burst.claimed_untravelled;
        return;
    }
    switch (mp_enemy_burst_attach(found, speed, actor_flags, &quarters)) {
    case MP_ENEMY_BURST_ATTACH_PLAYER:
        ++burst.sent_without;
        ++burst.without_player;
        return;
    case MP_ENEMY_BURST_ATTACH_NO_SPEED:
        ++burst.sent_without;
        ++burst.without_speed;
        return;
    case MP_ENEMY_BURST_ATTACH_NONE:
    case MP_ENEMY_BURST_ATTACH_CARRIED:
    default:
        break;
    }
    ++burst.carried;
    if (burst.named < NAMED_MAX) {
        ++burst.named;
        log_info("%s %u (life %u) burst into its pieces here at speed %.2f, and its removal "
                 "carries it", key_noun(key), (unsigned)key_number(key), (unsigned)generation,
                 (double)mp_enemy_burst_speed(quarters));
    }
}

void mp_enemy_burst_census_done(void)
{
    burst.unclaimed += mp_enemy_burst_ring_drop(&burst.ring, &burst.callers);
}

/* ==============================================================================================
 * A client's half.
 * ============================================================================================ */

mp_enemy_burst_verdict_t mp_enemy_burst_decide_for(mp_enemy_slot_t slot, bool newer,
                                                   uint8_t quarters)
{
    return mp_enemy_burst_decide(mp_enemy_slot_is_actor(slot), slot == MP_ENEMY_SLOT_KEPT,
                                 slot == MP_ENEMY_SLOT_LIVE, newer, quarters);
}

static void name_one(const mp_event_t *event, const char *outcome)
{
    if (burst.named >= NAMED_MAX) {
        return;
    }
    ++burst.named;
    if (outcome == NULL) {
        log_info("%s %u (life %u) burst into its pieces here before its removal, as on the host",
                 key_noun(event->actor_index), (unsigned)key_number(event->actor_index),
                 (unsigned)event->actor_generation);
        return;
    }
    log_info("%s %u (life %u) was to burst and did not: %s", key_noun(event->actor_index),
             (unsigned)key_number(event->actor_index), (unsigned)event->actor_generation, outcome);
}

void mp_enemy_burst_unmet(const mp_event_t *event, mp_enemy_burst_unmet_t why)
{
    if (event == NULL || event->actor_burst == 0u) {
        return;
    }
    ++burst.carried_in;
    switch (why) {
    case MP_ENEMY_BURST_UNMET_MOVED_ON:
        ++burst.moved_on;
        name_one(event, "a life this side had moved on from");
        return;
    case MP_ENEMY_BURST_UNMET_KEPT:
        ++burst.only_removed;
        name_one(event, "a kept corpse");
        return;
    case MP_ENEMY_BURST_UNMET_NO_REPLICA:
    default:
        ++burst.no_replica;
        name_one(event, "no replica left");
        return;
    }
}

/* What the engine's gates will read, read first, so a burst that does not happen can be named.
 * The naming decides nothing: bit 0 decided. */
static void read_gates(uint32_t body, uint32_t asset, mp_enemy_burst_reading_t *r)
{
    const mp_capacity_reading_t *pool;
    uintptr_t                    pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t                     record = 0;
    uint32_t                     player = 0;
    uint32_t                     model = 0;

    memset(r, 0, sizeof *r);
    r->flags_read  = memory_try_read((uintptr_t)body + BODY_FLAGS, &r->flags, sizeof r->flags);
    r->radius_read = memory_try_read((uintptr_t)body + BODY_RADIUS, &r->radius, sizeof r->radius);
    r->nodes_read  = memory_try_read((uintptr_t)asset + ASSET_MODEL, &model, sizeof model) &&
                     model != 0u &&
                     memory_try_read((uintptr_t)model + MODEL_NODES, &r->nodes, sizeof r->nodes);
    r->player_body = pr_cell != 0u && memory_try_read(pr_cell, &record, sizeof record) &&
                     record != 0u &&
                     memory_try_read((uintptr_t)record + RECORD_HACTOR, &player, sizeof player) &&
                     player == body;
    mp_capacity_sample();
    pool         = mp_capacity_last();
    r->free_read = pool->objects_readable;
    /* The engine's own count, 255 less the live objects, taken as it takes it: signed, then
     * doubled and compared unsigned by the rule. */
    r->free_things = (uint32_t)((int32_t)OBJECT_POOL - (int32_t)pool->objects_live);
}

static void count_refusal(const mp_event_t *event, const mp_enemy_burst_reading_t *reading)
{
    switch (mp_enemy_burst_gate_name(reading)) {
    case MP_ENEMY_BURST_GATE_PLAYER:
        ++burst.gated;
        ++burst.gate_player;
        name_one(event, "the player's body");
        return;
    case MP_ENEMY_BURST_GATE_WIDE:
        ++burst.gated;
        ++burst.gate_wide;
        name_one(event, "too wide");
        return;
    case MP_ENEMY_BURST_GATE_HIDDEN:
        ++burst.gated;
        ++burst.gate_hidden;
        name_one(event, "already hidden");
        return;
    case MP_ENEMY_BURST_GATE_CROWDED:
        ++burst.gated;
        ++burst.gate_crowded;
        name_one(event, "too few free objects");
        return;
    case MP_ENEMY_BURST_GATE_NONE:
    default:
        ++burst.stopped;
        name_one(event, "stopped inside the engine");
        return;
    }
}

void mp_enemy_burst_replica(uintptr_t actor, const mp_event_t *event)
{
    mp_enemy_burst_reading_t reading;
    mp_enemy_marks_t         marks;
    uint32_t                 flags = 0;
    uint32_t                 body = 0;
    uint32_t                 thing = 0;
    uint32_t                 asset = 0;
    uint32_t                 model = 0;
    uint32_t                 after = 0;
    bool                     read_after;

    if (event == NULL || event->actor_burst == 0u) {
        return;
    }
    ++burst.carried_in;
    if (memory_try_read(actor + ACTOR_STATE_FLAGS, &flags, sizeof flags) &&
        mp_enemy_burst_carries_player(flags)) {
        ++burst.only_removed;
        name_one(event, "an actor that carries the player");
        return;
    }
    if (!burst.installed || burst.original == NULL) {
        ++burst.no_site;
        name_one(event, "no site to call");
        return;
    }
    /* What the engine dereferences without asking: the body, its thing, its asset and the
     * asset's model, whose node count the burst reads first. */
    if (!memory_try_read(actor + ACTOR_BODY, &body, sizeof body) || body == 0u ||
        !memory_try_read((uintptr_t)body + BODY_THING, &thing, sizeof thing) || thing == 0u ||
        !memory_try_read((uintptr_t)body + BODY_ASSET, &asset, sizeof asset) || asset == 0u ||
        !memory_try_read((uintptr_t)asset + ASSET_MODEL, &model, sizeof model) || model == 0u) {
        ++burst.no_replica;
        name_one(event, "no replica left");
        return;
    }
    /* The host's death arm takes the class and the shadow before it bursts; so does this, so an
     * invisible body with a class never stands here waiting for its removal. */
    if (mp_enemy_bind_marks(actor, &marks)) {
        marks.objclass = 0;
        marks.flags &= ~MP_ENEMY_BODY_SHADOW;
        (void)mp_enemy_bind_set_marks(actor, &marks);
    }
    read_gates(body, asset, &reading);
    burst.original((uintptr_t)body, mp_enemy_burst_speed(event->actor_burst), 0);
    read_after = memory_try_read((uintptr_t)body + BODY_FLAGS, &after, sizeof after);
    if (mp_enemy_burst_observe(reading.flags_read, reading.flags, read_after, after) !=
        MP_ENEMY_BURST_SEEN_BURST) {
        count_refusal(event, &reading);
        return;
    }
    ++burst.burst_here;
    burst.burst_copies += mp_wire_key_is_copy(event->actor_index) ? 1u : 0u;
    name_one(event, NULL);
}

/* ==============================================================================================
 * Reset and report.
 * ============================================================================================ */

void mp_enemy_burst_reset(void)
{
    burst.unclaimed += mp_enemy_burst_ring_drop(&burst.ring, &burst.callers);
}

/* Both lines on both sides and whether the hull stands or not: a line that went missing would
 * say nothing about why. A host line on a client counts the client's own engine, a client line on
 * a host counts nothing. */
void mp_enemy_burst_report(void)
{
    const mp_enemy_burst_callers_t *c = &burst.callers;

    if (!burst.installed) {
        log_info("  the enemies that burst: the hull on shot_shatterThing is not bound on this "
                 "side, so the numbers below cannot move");
    }
    log_info("  the enemies that burst (host): %u call(s) of the engine's burst, %u burst, %u left "
             "drawn (a gate, or the engine gave up inside), %u whose body was already hidden or "
             "did not read | of the burst: %u carried on their actor's removal, %u that no "
             "removal claimed in the same substep (callers: %08X x%u, %08X x%u, %08X x%u, %08X "
             "x%u, %u other), %u claimed by a removal that did not travel, %u removal(s) sent "
             "without it (%u a speed the note cannot carry, %u an actor that carries the "
             "player), %u with the ring full, %u on a side that sends no removal",
             (unsigned)burst.calls, (unsigned)burst.burst_seen, (unsigned)burst.drawn,
             (unsigned)burst.hidden, (unsigned)burst.carried, (unsigned)burst.unclaimed,
             (unsigned)c->address[0], (unsigned)c->count[0], (unsigned)c->address[1],
             (unsigned)c->count[1], (unsigned)c->address[2], (unsigned)c->count[2],
             (unsigned)c->address[3], (unsigned)c->count[3], (unsigned)c->others,
             (unsigned)burst.claimed_untravelled, (unsigned)burst.sent_without,
             (unsigned)burst.without_speed, (unsigned)burst.without_player,
             (unsigned)burst.ring_full, (unsigned)burst.not_host);
    log_info("  the enemies that burst (client): %u removal(s) from the host carried a burst, %u "
             "burst here on the replica before it was removed (%u of them a copy), %u refused by "
             "the engine's gates (%u already hidden, %u too wide, %u too few free objects, %u the "
             "player's body), %u stopped inside the engine, %u with no site to call, %u only "
             "removed (a kept corpse or an actor that carries the player), %u with no replica "
             "left to burst, %u for a life this side had moved on from",
             (unsigned)burst.carried_in, (unsigned)burst.burst_here, (unsigned)burst.burst_copies,
             (unsigned)burst.gated, (unsigned)burst.gate_hidden, (unsigned)burst.gate_wide,
             (unsigned)burst.gate_crowded, (unsigned)burst.gate_player, (unsigned)burst.stopped,
             (unsigned)burst.no_site, (unsigned)burst.only_removed, (unsigned)burst.no_replica,
             (unsigned)burst.moved_on);
}
