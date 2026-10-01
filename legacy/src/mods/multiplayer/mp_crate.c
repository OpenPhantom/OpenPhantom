/* mp_crate.c: push blocks as the host's state, bound to the engine. See the header.
 *
 * SIZE NOTE: over 600 lines. The engine half is five calls written out with every field they read,
 * the four hooks and an installation that is all or none, and each says why it has its shape; the
 * decisions are in the machines already. The seam is the report: it reads the two machines'
 * counters and nothing of the engine, and leaves for a file of its own with one function when this
 * one grows.
 */
#include "mp_crate.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_crate_client.h"
#include "mp_crate_host.h"
#include "mp_crate_opener.h"
#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"
#include "mp_signatures.h"
#include "mp_signatures_crate.h"
#include "mp_signatures_world.h"
#include "mp_world.h"
#include "mp_world_apply.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The push block's part of the mover record. The kind and the index share their offsets with
 * every mover; the rest is the block's own bookkeeping. The home is the first part's authored
 * pivot, and the radius is that part's, which the engine's push and sink read at +0xE4. */
#define MOVER_KIND          0x04u
#define MOVER_INDEX         0x08u
#define MOVER_RIG_FLAGS     0x10u
#define MOVER_WORLD_POS     0x44u   /* three floats */
#define MOVER_FLAGS         0x50u   /* a byte */
#define MOVER_CARRIER       0x51u   /* a byte each: the carrier, its part, the block carried */
#define MOVER_CARRIER_PART  0x52u
#define MOVER_CARRYING      0x53u
#define MOVER_CARRY_OFFSET  0x54u   /* three floats */
#define MOVER_REVEALS       0x60u   /* the placement index the sinking reveals, below 0 for none */
#define MOVER_HOME          0x8Cu   /* three floats */
#define MOVER_RADIUS        0xE4u

/* The ground contact block the floor probe fills: 0x88 bytes, its polygon at +0x14, and the two
 * fields the probe reads before it clears the block, whether the point rides a mover and which. */
#define CONTACT_BYTES     0x88u
#define CONTACT_POLYGON   0x14u
#define CONTACT_ON_MOVER  0x18u
#define CONTACT_MOVER     0x20u

/* What a record that is not in the level's table answers for its index: past every index a
 * message can carry, so neither machine finds a block under it. */
#define CRATE_NO_INDEX    0xFFFFFFFFu

/* The push lowers the floor probe this far below the block's radius. */
#define FLOOR_PROBE_SLACK 0.01f

/* The body object's position and its crush cylinder, which the engine's crush test reads. */
#define BODY_POSITION     0x18u
#define BODY_RADIUS       0xB8u
#define BODY_HEIGHT       0xBCu

/* engine: i32 bapmap_pushBlock(void *hActor, i32 moverIndex, const vec3 *pusherPos, vec3 *delta) */
typedef int32_t(__cdecl *push_fn_t)(uint32_t actor, int32_t index, const float *pusher,
                                    float *delta);
/* engine: i32 bapmap_dropBlock(bapMover *block, const vec3 *pos, const vec3 *delta) */
typedef int32_t(__cdecl *drop_fn_t)(uint32_t block, const float *position, const float *delta);
/* engine: void bapmap_hidePiece(bapMover *pMov) */
typedef void(__cdecl *sink_fn_t)(uint32_t block);
/* engine: i32 bapobj_crushActorsInCylinder(f32 height, f32 radius, const vec3 *c, bapObj *self) */
typedef int32_t(__cdecl *crush_fn_t)(float height, float radius, const float *centre,
                                     uint32_t self);
/* engine: void bapmap_probeFloor(const vec3 *pPt, bapGroundContact *pOut) */
typedef void(__cdecl *probe_floor_fn_t)(const float *point, uint8_t *contact);
/* engine: void bapmap_attachRider(bapMover *pMov, bapPoly *pFloor, const vec3 *pPos) */
typedef void(__cdecl *attach_fn_t)(uint32_t block, uint32_t floor, const float *position);
/* engine: void bapmap_applyNodePos(i32 id, f32 radius, const vec3 *pLeave, const vec3 *pEnter) */
typedef void(__cdecl *node_pos_fn_t)(int32_t index, float radius, const float *leave,
                                     const float *enter);

typedef enum crate_role {
    CRATE_ROLE_NONE = 0,
    CRATE_ROLE_HOST,
    CRATE_ROLE_CLIENT
} crate_role_t;

typedef enum crate_redirect {
    CRATE_REDIRECT_PUSH = 0,
    CRATE_REDIRECT_LAND_SINK,
    CRATE_REDIRECT_DROP,
    CRATE_REDIRECT_CRUSH,
    CRATE_REDIRECT_COUNT
} crate_redirect_t;

typedef struct crate_state {
    bool              installed;
    bool              bound;
    uintptr_t         call[CRATE_REDIRECT_COUNT];
    uintptr_t         original[CRATE_REDIRECT_COUNT];
    push_fn_t         push;
    drop_fn_t         drop;
    sink_fn_t         sink;
    crush_fn_t        crush;
    probe_floor_fn_t  probe_floor;
    attach_fn_t       attach;
    node_pos_fn_t     node_pos;

    bool              replaying;   /* this side sinks a block the host sank: no crush here */
    uint32_t          now;
    bool              was_host;
    bool              was_client;

    mp_crate_engine_t engine;
    mp_crate_host_t   host;
    mp_crate_client_t client;

    uint32_t          crushes_left_out;
    uint32_t          crushes_on_player;
} crate_state_t;

static crate_state_t crate;

static crate_role_t role(void)
{
    if (!crate.bound || !mp_armed_transport() || !mp_bridge_drain_is_udp()) {
        return CRATE_ROLE_NONE;
    }
    return mp_bridge_drain_is_client() ? CRATE_ROLE_CLIENT : CRATE_ROLE_HOST;
}

/* ==============================================================================================
 * The engine, as the machines reach it.
 * ============================================================================================ */

static bool mover_of(uint32_t id, uint32_t *mover)
{
    return mp_world_mover_at(mp_world_pointer(), id, mover);
}

static bool engine_read(void *context, uint32_t id, mp_crate_body_t *out)
{
    uint32_t mover = 0u;

    (void)context;
    memset(out, 0, sizeof *out);
    return mover_of(id, &mover) &&
           memory_try_read(mover + MOVER_KIND, &out->kind, sizeof out->kind) &&
           memory_try_read(mover + MOVER_RIG_FLAGS, &out->rig_flags, sizeof out->rig_flags) &&
           memory_try_read(mover + MOVER_FLAGS, &out->flags, sizeof out->flags) &&
           memory_try_read(mover + MOVER_CARRIER, &out->carrier, sizeof out->carrier) &&
           memory_try_read(mover + MOVER_CARRIER_PART, &out->carrier_part,
                           sizeof out->carrier_part) &&
           memory_try_read(mover + MOVER_CARRYING, &out->carrying, sizeof out->carrying) &&
           memory_try_read(mover + MOVER_WORLD_POS, out->position, sizeof out->position) &&
           memory_try_read(mover + MOVER_CARRY_OFFSET, out->carry_offset,
                           sizeof out->carry_offset) &&
           memory_try_read(mover + MOVER_HOME, out->home, sizeof out->home) &&
           memory_try_read(mover + MOVER_RADIUS, &out->radius, sizeof out->radius) &&
           memory_try_read(mover + MOVER_REVEALS, &out->reveals, sizeof out->reveals);
}

/* A far player's push, through the engine's own, with that player's body as the one pushing. The
 * body test in the push skips exactly the body it is handed, so the host's player and every other
 * puppet stand in the way as bodies do in single player. */
static int32_t engine_push(void *context, uint8_t slot, uint32_t id, float step[3])
{
    size_t   bank = mp_bridge_far_bank_of_slot(slot);
    uint32_t actor = 0u;
    float    where[3];

    (void)context;
    if (bank == 0u || mp_bank_active() != 0u || !mp_body_exists_at(bank) ||
        !mp_bank_read_at(bank, MP_HERO_BLOCK_HACTOR, &actor, sizeof actor) || actor == 0u ||
        !mp_bank_read_at(bank, MP_HERO_BLOCK_POS, where, sizeof where)) {
        return -1;
    }
    return crate.push(actor, (int32_t)id, where, step);
}

/* The tail of the engine's push: the floor under the point with the carrier the block rides, the
 * attach to whatever it now stands on, the cell occupancy moved, the position written. The push
 * checked everything before its tail; the host checked it here. */
static bool engine_place(void *context, uint32_t id, const float position[3], uint8_t host_flags)
{
    uint8_t  contact[CONTACT_BYTES];
    uint32_t mover = 0u;
    uint32_t carrier = 0u;
    uint32_t polygon = 0u;
    uint8_t  flags = 0u;
    uint8_t  follow = 0u;
    float    radius = 0.0f;
    float    old[3];
    float    probe[3];
    int32_t  on_mover;

    (void)context;
    if (!mover_of(id, &mover) || !memory_try_read(mover + MOVER_FLAGS, &flags, sizeof flags) ||
        !memory_try_read(mover + MOVER_CARRIER, &follow, sizeof follow) ||
        !memory_try_read(mover + MOVER_RADIUS, &radius, sizeof radius) ||
        !memory_try_read(mover + MOVER_WORLD_POS, old, sizeof old)) {
        return false;
    }
    memset(contact, 0, sizeof contact);
    on_mover = (int32_t)(flags & MP_CRATE_FLAG_CARRIED);
    memcpy(contact + CONTACT_ON_MOVER, &on_mover, sizeof on_mover);
    if (on_mover != 0 && mover_of(follow, &carrier)) {
        memcpy(contact + CONTACT_MOVER, &carrier, sizeof carrier);
    }
    probe[0] = position[0];
    probe[1] = position[1];
    probe[2] = position[2] - (radius + FLOOR_PROBE_SLACK);
    crate.probe_floor(probe, contact);
    memcpy(&polygon, contact + CONTACT_POLYGON, sizeof polygon);
    if ((flags & MP_CRATE_FLAG_FALLING) == 0u) {
        crate.attach(mover, polygon, position);
    }
    crate.node_pos((int32_t)id, radius, old, position);
    if (!memory_try_write(mover + MOVER_WORLD_POS, position, 3u * sizeof(float)) ||
        !memory_try_read(mover + MOVER_FLAGS, &flags, sizeof flags)) {
        return false;
    }
    flags = mp_crate_rule_merge_flags(flags, host_flags);
    return memory_try_write(mover + MOVER_FLAGS, &flags, sizeof flags);
}

/* The engine's drop with the host's point and direction, then the tail the push ends with after a
 * drop: the cells moved and the position written. Only an answer of 1 is a fall; 2 is a step too
 * small to fall, which the push would have gone on with, and 0 a refusal. */
static int32_t engine_fall(void *context, uint32_t id, const float position[3],
                           const float direction[2])
{
    uint32_t mover = 0u;
    float    radius = 0.0f;
    float    old[3];
    float    delta[3];
    int32_t  answer;

    (void)context;
    if (!mover_of(id, &mover) || !memory_try_read(mover + MOVER_RADIUS, &radius, sizeof radius) ||
        !memory_try_read(mover + MOVER_WORLD_POS, old, sizeof old)) {
        return 0;
    }
    delta[0] = direction[0];
    delta[1] = direction[1];
    delta[2] = 0.0f;
    answer   = crate.drop(mover, position, delta);
    if (answer == 1) {
        crate.node_pos((int32_t)id, radius, old, position);
        (void)memory_try_write(mover + MOVER_WORLD_POS, position, 3u * sizeof(float));
    }
    return answer;
}

/* The engine's sink as a savegame runs it on a block it restores, with the crush left out. */
static bool engine_sink(void *context, uint32_t id)
{
    uint32_t mover = 0u;

    (void)context;
    if (!mover_of(id, &mover)) {
        return false;
    }
    crate.replaying = true;
    crate.sink(mover);
    crate.replaying = false;
    return true;
}

/* ==============================================================================================
 * The level, the role and the four hooks.
 * ============================================================================================ */

static crate_role_t current_role(void)
{
    crate_role_t now = role();
    uint32_t     count = 0u;

    if (now == CRATE_ROLE_NONE || !mp_world_mover_count(mp_world_pointer(), &count) ||
        count == 0u) {
        return CRATE_ROLE_NONE;
    }
    if (now == CRATE_ROLE_HOST) {
        crate.was_host = true;
        mp_crate_host_level(&crate.host, &crate.engine, (uint16_t)count,
                            mp_world_apply_generation(), count);
    } else {
        crate.was_client = true;
        mp_crate_client_level(&crate.client, (uint16_t)count);
    }
    return now;
}

static uint32_t index_of(uint32_t block)
{
    uint32_t index = 0u;
    uint32_t mover = 0u;

    if (memory_try_read(block + MOVER_INDEX, &index, sizeof index) && mover_of(index, &mover) &&
        mover == block) {
        return index;
    }
    return CRATE_NO_INDEX;   /* no index of a level: the machines find no block under it */
}

/* engine: i32 bapmap_pushBlock(void *hActor, i32 moverIndex, const vec3 *pusherPos, vec3 *delta) */
static int32_t __cdecl hook_push(uint32_t actor, int32_t index, const float *pusher, float *delta)
{
    crate_role_t    now = mp_bank_active() == 0u && index >= 0 ? current_role() : CRATE_ROLE_NONE;
    mp_crate_body_t before;
    mp_crate_body_t after;
    int32_t         answer;

    if (now == CRATE_ROLE_HOST &&
        !mp_crate_host_own_push(&crate.host, (uint32_t)index, crate.now)) {
        return 0;
    }
    if (now != CRATE_ROLE_CLIENT) {
        return crate.push(actor, index, pusher, delta);
    }
    if (mp_crate_client_gate(&crate.client, &crate.engine, (uint32_t)index,
                             mp_bridge_drain_my_slot(), crate.now) != MP_CRATE_GATE_LET) {
        return 0;
    }
    (void)engine_read(NULL, (uint32_t)index, &before);
    answer = crate.push(actor, index, pusher, delta);
    if (engine_read(NULL, (uint32_t)index, &after)) {
        mp_crate_client_pushed(&crate.client, (uint32_t)index, answer, &after,
                               mp_crate_rule_is_pull(before.position, pusher, delta), crate.now);
    }
    return answer;
}

/* engine: i32 bapmap_dropBlock(bapMover *block, const vec3 *pos, const vec3 *delta) */
static int32_t __cdecl hook_drop(uint32_t block, const float *position, const float *delta)
{
    int32_t answer = crate.drop(block, position, delta);

    if (answer == 1 && role() == CRATE_ROLE_HOST) {
        mp_crate_host_fell(&crate.host, crate.now, index_of(block), position, delta);
    }
    return answer;
}

/* engine: void bapmap_hidePiece(bapMover *pMov), from a landing */
static void __cdecl hook_land_sink(uint32_t block)
{
    crate_role_t now = role();
    int32_t      rig = 0;
    int32_t      reveals = -1;
    int32_t      kind = 0;

    if (now == CRATE_ROLE_CLIENT) {
        mp_crate_client_hold_sink(&crate.client, index_of(block));
        return;
    }
    crate.sink(block);
    if (now != CRATE_ROLE_HOST) {
        return;
    }
    mp_crate_host_sank(&crate.host);
    (void)memory_try_read(block + MOVER_RIG_FLAGS, &rig, sizeof rig);
    (void)memory_try_read(block + MOVER_REVEALS, &reveals, sizeof reveals);
    (void)memory_try_read(block + MOVER_KIND, &kind, sizeof kind);
    if (reveals >= 0) {
        log_info("a push block sank on the host: block %u, its kind now %d, rigFlags 0x%X; it "
                 "reveals placement %d", (unsigned)index_of(block), (int)kind, (unsigned)rig,
                 (int)reveals);
    } else {
        log_info("a push block sank on the host: block %u, its kind now %d, rigFlags 0x%X; it "
                 "reveals nothing", (unsigned)index_of(block), (int)kind, (unsigned)rig);
    }
}

/* Whether this machine's own player stands in the crush cylinder, by the engine's own test. */
static bool player_inside(const float *centre, float radius, float height)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  body = 0u;
    float     where[3];
    float     body_radius = 0.0f;
    float     body_height = 0.0f;

    return block != 0u &&
           memory_try_read(block + MP_HERO_BLOCK_HACTOR, &body, sizeof body) && body != 0u &&
           memory_try_read(body + BODY_POSITION, where, sizeof where) &&
           memory_try_read(body + BODY_RADIUS, &body_radius, sizeof body_radius) &&
           memory_try_read(body + BODY_HEIGHT, &body_height, sizeof body_height) &&
           mp_crate_rule_in_cylinder(centre, radius, height, where, body_radius, body_height);
}

/* engine: i32 bapobj_crushActorsInCylinder(f32 height, f32 radius, const vec3 *c, bapObj *self) */
static int32_t __cdecl hook_crush(float height, float radius, const float *centre, uint32_t self)
{
    if (!crate.replaying) {
        return crate.crush(height, radius, centre, self);
    }
    ++crate.crushes_left_out;
    if (player_inside(centre, radius, height)) {
        ++crate.crushes_on_player;
    }
    return 0;
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

static void say_held(uint8_t id, uint8_t slot)
{
    log_info("a push block is held: block %u by slot %u", (unsigned)id, (unsigned)slot);
}

/* Which site stands in the way, by name, so the line points at the pattern to look at. */
static void say_what_did_not_resolve(void)
{
    size_t             count = 0;
    const signature_t *sites = mp_signatures_crate_sites(&count);
    size_t             i;

    for (i = 0; i < count; ++i) {
        if (sites[i].address == 0u) {
            log_warning("push blocks do not travel: %s did not resolve, so each machine pushes "
                        "and sinks its own blocks as before", sites[i].name);
            return;
        }
    }
    log_warning("push blocks do not travel: the world cell did not resolve, so each machine "
                "pushes and sinks its own blocks as before");
}

/* Every function the machines call, read out of the calls that reach it. The sink is named by two
 * calls, the landing's and the savegame's, and they have to agree; the floor probe is named by
 * the push's call and has to be the one the world probes resolved. */
static bool read_the_callees(uintptr_t read[MP_CRATE_CALL_COUNT])
{
    size_t    call;
    uintptr_t probe_floor = mp_signatures_world_address(MP_WORLD_SITE_PROBE_FLOOR);

    for (call = 0; call < MP_CRATE_CALL_COUNT; ++call) {
        uintptr_t at = mp_signatures_crate_call((mp_crate_call_t)call);

        if (at == 0u || !patch_read_call_target(at, &read[call])) {
            log_warning("push blocks do not travel: the call of site %u did not read",
                        (unsigned)call);
            return false;
        }
    }
    if (probe_floor == 0u && mp_signatures_world_resolve() != 0u) {
        probe_floor = mp_signatures_world_address(MP_WORLD_SITE_PROBE_FLOOR);
    }
    if (read[MP_CRATE_CALL_LAND_SINK] != read[MP_CRATE_CALL_RESTORE_SINK] ||
        read[MP_CRATE_CALL_PROBE_FLOOR] != probe_floor) {
        log_warning("push blocks do not travel: the two calls of the sink name %08X and %08X, "
                    "and the push's floor probe %08X against %08X resolved",
                    (unsigned)read[MP_CRATE_CALL_LAND_SINK],
                    (unsigned)read[MP_CRATE_CALL_RESTORE_SINK],
                    (unsigned)read[MP_CRATE_CALL_PROBE_FLOOR], (unsigned)probe_floor);
        return false;
    }
    return true;
}

/* The four calls, all or none: a push that is gated while its sink is not would let a client sink
 * a block by itself again, which is the defect this exists for. */
static bool repoint_the_calls(void)
{
    static const mp_crate_call_t CALLS[CRATE_REDIRECT_COUNT] = {
        MP_CRATE_CALL_PUSH, MP_CRATE_CALL_LAND_SINK, MP_CRATE_CALL_DROP, MP_CRATE_CALL_CRUSH
    };
    const void *hooks[CRATE_REDIRECT_COUNT] = {
        (const void *)&hook_push, (const void *)&hook_land_sink, (const void *)&hook_drop,
        (const void *)&hook_crush
    };
    size_t done;

    for (done = 0; done < CRATE_REDIRECT_COUNT; ++done) {
        crate.call[done] = mp_signatures_crate_call(CALLS[done]);
        if (patch_redirect_call(crate.call[done], hooks[done]) != PATCH_RESULT_OK) {
            while (done-- > 0u) {
                (void)patch_redirect_call(crate.call[done], (const void *)crate.original[done]);
            }
            log_warning("push blocks do not travel: a call did not move, and the ones that had "
                        "were put back");
            return false;
        }
    }
    return true;
}

bool mp_crate_install(void)
{
    uintptr_t read[MP_CRATE_CALL_COUNT];

    if (crate.installed) {
        return crate.bound;
    }
    crate.installed = true;
    mp_crate_host_init(&crate.host);
    mp_crate_client_init(&crate.client);
    crate.host.say_held = &say_held;
    crate.engine.context = NULL;
    crate.engine.read    = &engine_read;
    crate.engine.push    = &engine_push;
    crate.engine.place   = &engine_place;
    crate.engine.fall    = &engine_fall;
    crate.engine.sink    = &engine_sink;

    if (mp_signatures_crate_resolve() != (size_t)MP_CRATE_CALL_COUNT ||
        mp_cells_address(MP_CELL_LEVEL) == 0u) {
        say_what_did_not_resolve();
        return false;
    }
    memset(read, 0, sizeof read);
    if (!read_the_callees(read)) {
        return false;
    }
    /* Stored before the first call moves: from then on the hooks can be entered. */
    crate.push        = (push_fn_t)read[MP_CRATE_CALL_PUSH];
    crate.sink        = (sink_fn_t)read[MP_CRATE_CALL_LAND_SINK];
    crate.drop        = (drop_fn_t)read[MP_CRATE_CALL_DROP];
    crate.crush       = (crush_fn_t)read[MP_CRATE_CALL_CRUSH];
    crate.probe_floor = (probe_floor_fn_t)read[MP_CRATE_CALL_PROBE_FLOOR];
    crate.attach      = (attach_fn_t)read[MP_CRATE_CALL_ATTACH];
    crate.node_pos    = (node_pos_fn_t)read[MP_CRATE_CALL_NODE_POS];
    crate.original[CRATE_REDIRECT_PUSH]      = read[MP_CRATE_CALL_PUSH];
    crate.original[CRATE_REDIRECT_LAND_SINK] = read[MP_CRATE_CALL_LAND_SINK];
    crate.original[CRATE_REDIRECT_DROP]      = read[MP_CRATE_CALL_DROP];
    crate.original[CRATE_REDIRECT_CRUSH]     = read[MP_CRATE_CALL_CRUSH];
    crate.bound = true;
    if (!repoint_the_calls()) {
        crate.bound = false;
        return false;
    }
    mp_crate_opener_arm();
    log_info("push blocks are the host's: the push at %08X, the landing's sink at %08X, the "
             "drop at %08X and the sink's crush at %08X ask this module, and hand the engine "
             "its own call whenever no session runs over a socket",
             (unsigned)crate.call[CRATE_REDIRECT_PUSH],
             (unsigned)crate.call[CRATE_REDIRECT_LAND_SINK],
             (unsigned)crate.call[CRATE_REDIRECT_DROP], (unsigned)crate.call[CRATE_REDIRECT_CRUSH]);
    return true;
}

/* ==============================================================================================
 * The bridge's calls.
 * ============================================================================================ */

void mp_crate_apply_pending(uint32_t tick)
{
    crate.now = tick;
    if (current_role() == CRATE_ROLE_CLIENT) {
        mp_crate_client_apply(&crate.client, &crate.engine, mp_bridge_drain_my_slot(), tick);
    }
}

void mp_crate_run_due(uint32_t tick)
{
    crate.now = tick;
    if (mp_bank_active() == 0u && current_role() == CRATE_ROLE_HOST) {
        mp_crate_host_run(&crate.host, &crate.engine, tick);
    }
}

static void send_host(uint32_t tick, mp_crate_send_fn_t send)
{
    uint8_t buffer[MP_CRATE_NOTE_MAX_BYTES];
    size_t  bytes;

    mp_crate_host_end_substep(&crate.host, tick);
    while ((bytes = mp_crate_host_next_fall(&crate.host, buffer, sizeof buffer)) != 0u) {
        bool sent = send(buffer, bytes);

        mp_crate_host_fall_sent(&crate.host, sent);
        if (!sent) {
            break;   /* the fall waits for the next substep, in its order */
        }
    }
    bytes = mp_crate_host_next_note(&crate.host, &crate.engine, tick, buffer, sizeof buffer);
    if (bytes != 0u) {
        mp_crate_host_note_sent(&crate.host, send(buffer, bytes), tick);
    }
}

void mp_crate_send(uint32_t tick, mp_crate_send_fn_t send)
{
    uint8_t      buffer[MP_CRATE_PUSH_BYTES];
    uint32_t     count = 0u;
    crate_role_t now;
    size_t       bytes;

    if (send == NULL) {
        return;
    }
    now = current_role();
    if (now == CRATE_ROLE_HOST) {
        send_host(tick, send);
        return;
    }
    if (now != CRATE_ROLE_CLIENT || !mp_world_mover_count(mp_world_pointer(), &count)) {
        return;
    }
    mp_crate_client_end_substep(&crate.client, tick);
    bytes = mp_crate_client_next_push(&crate.client, (uint16_t)count, mp_world_apply_generation(),
                                      tick, buffer, sizeof buffer);
    if (bytes != 0u) {
        mp_crate_client_push_sent(&crate.client, send(buffer, bytes), tick);
    }
}

bool mp_crate_take(uint8_t sender_slot, const uint8_t *note, size_t bytes)
{
    if (!crate.bound) {
        return false;
    }
    if (mp_bridge_drain_is_client()) {
        return mp_crate_client_take(&crate.client, note, bytes, crate.now);
    }
    return mp_crate_host_take(&crate.host, sender_slot, note, bytes);
}

void mp_crate_note_arrival(void)
{
    mp_crate_host_arrival(&crate.host);
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

static void report_host(const mp_crate_host_stats_t *s)
{
    log_info("  the push blocks (host): %u block(s) in this level (%u of them able to sink), %u "
             "away from where the level put them, %u sunk here; the player of this machine: %u "
             "push(es), %u of them refused by the owner rule; the wishes of the clients: %u "
             "taken, %u reached, %u refused by the engine, %u refused for another owner, %u stale "
             "or of another level; %u fall(s) began here, sent as events",
             (unsigned)s->blocks, (unsigned)s->can_sink, (unsigned)s->away, (unsigned)s->sunk,
             (unsigned)s->own_pushes, (unsigned)s->own_refused, (unsigned)s->taken,
             (unsigned)s->reached, (unsigned)s->engine_refused, (unsigned)s->other_owner,
             (unsigned)s->stale, (unsigned)s->falls);
    log_info("  the push blocks' note (host): %u full and %u change note(s) sent, largest %u "
             "byte(s), %u the channel would not take; entries sent %u; fall events: %u sent, %u "
             "of them waited for the channel",
             (unsigned)s->whole_sent, (unsigned)s->change_sent, (unsigned)s->largest,
             (unsigned)s->note_refused, (unsigned)s->entries_sent, (unsigned)s->falls_sent,
             (unsigned)s->falls_waited);
    log_info("  the push blocks' wishes refused before the engine (host): %u for no push block "
             "(an index out of range, not a crate, or one already sunk), %u for a slot with no "
             "body here, %u torn",
             (unsigned)s->no_block, (unsigned)s->no_body, (unsigned)s->torn);
    log_info("  the push blocks' wishes that went nowhere (host): %u stopped with no progress",
             (unsigned)s->stalled);
}

static void report_client(const mp_crate_client_stats_t *s)
{
    log_info("  the push blocks' note (client): %u full and %u change note(s) taken, %u torn, %u "
             "about another level, %u from before a level change, %u change note(s) taken "
             "before the first full one; entries: %u agreed, %u chased (steps %u), %u jumped "
             "(worst %.2f u), %u held while a block fell, %u sink(s) performed from the host, %u "
             "sink(s) held here for the host (%u of them cancelled), %u where the host has a "
             "block at home this side has sunk, %u older than the one held",
             (unsigned)s->whole_taken, (unsigned)s->change_taken, (unsigned)s->torn,
             (unsigned)s->elsewhere, (unsigned)s->stale_generation,
             (unsigned)s->change_before_whole, (unsigned)s->agreed, (unsigned)s->chased,
             (unsigned)s->chase_steps, (unsigned)s->jumped, (double)s->worst_jump,
             (unsigned)s->held_falling, (unsigned)s->sinks_performed, (unsigned)s->sinks_held,
             (unsigned)s->sinks_cancelled, (unsigned)s->unrepairable, (unsigned)s->older);
    log_info("  the push blocks (this player): %u push call(s), %u let through, %u refused for "
             "another owner, %u refused ahead of the host by %u wish(es), %u refused in the "
             "second after a refusal, %u refused while the block fell or sank; wishes: sent %u, "
             "let-go %u, the channel would not take %u; corrections of this player's own block: "
             "%u, worst %.2f u; fall events: %u performed, %u where the engine answered "
             "otherwise, %u already falling here",
             (unsigned)s->push_calls, (unsigned)s->let_through, (unsigned)s->refused_owner,
             (unsigned)s->refused_ahead, (unsigned)MP_CRATE_WINDOW, (unsigned)s->refused_locked,
             (unsigned)s->refused_falling, (unsigned)s->wishes_sent, (unsigned)s->let_go_sent,
             (unsigned)s->wish_refused, (unsigned)s->corrections, (double)s->worst_correction,
             (unsigned)s->falls_performed, (unsigned)s->falls_otherwise,
             (unsigned)s->falls_already);
    log_info("  the push blocks' sinks replayed here: %u crush(es) left out, %u of them with this "
             "player inside the cylinder",
             (unsigned)crate.crushes_left_out, (unsigned)crate.crushes_on_player);
}

void mp_crate_report(void)
{
    if (!crate.bound) {
        return;
    }
    if (crate.was_host) {
        report_host(&crate.host.stats);
    }
    if (crate.was_client) {
        report_client(&crate.client.stats);
    }
    if (crate.was_host || crate.was_client) {
        log_info("  the push blocks' openers: %u opener call(s) on a sunk push block not sent as "
                 "a trigger",
                 (unsigned)mp_crate_opener_sinks());
    }
}
