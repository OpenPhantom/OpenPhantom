/* A fan the host reports every substep, and one death for it.
 *
 * SIZE NOTE: the engine this test plays is most of the file; the checks are its last third. The
 * seam, when it grows, is that engine into a file of its own, as mp_peers_net is for the sessions.
 *
 * The field run this is written after: a fan kills whoever it finds within reach, every substep,
 * and the host's target hull answered it with a far player who already lay dead. The host reported
 * each contact, the client performed each one around the engine's empty contact slot, and one
 * player entered the same death thirty nine times over two fades and three re-entries.
 *
 * The real modules run here: the dispatcher and its gate, the attribution, the death hull with its
 * entry per life, the rule set, the re-entry and the seat search. The engine under them is played:
 * the task scheduler's delivery with its gate, the player's handler that answers the fan's code
 * with the burning death, the death entry that empties the slot of the task being run, the
 * re-entry's fade and spawn, a flat world, and one host that sees this machine's player a few
 * substeps late. Both ends run in one
 * process: the host's puppet of this player is a far body of this machine, which is what it is on
 * the host, and a report the host drops for it is delivered to this machine's player after the
 * same delay.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_death.h"
#include "mp_body_gate.h"
#include "mp_body_internal.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_damage.h"
#include "mp_damage_entry.h"
#include "mp_death.h"
#include "mp_lobby.h"
#include "mp_placements.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_rules.h"
#include "mp_seat.h"
#include "mp_signatures.h"
#include "mp_signatures_contact.h"
#include "mp_signatures_world.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The log mp_body_gate_neighbours.c keeps: every line a module writes is printed with the test's
 * own lines and held, and a check asks how many lines since the mark contain `first`, and `second`
 * as well unless it is NULL. That is how a check reads the death entry's one line per entry and
 * the landing's warning rather than a counter nothing outside a module can see. */
void   gate_log_mark(void);
size_t gate_log_count(const char *first, const char *second);

/* ==============================================================================================
 * The rule, against the way it was.
 * ============================================================================================ */

/* What delivering a hit the host performed did before the gate: the saved handler, directly,
 * whenever there was one, whatever the slot held. */
static mp_body_delivery_t the_old_delivery(bool slot_known, uint32_t slot, bool task_run_known,
                                           bool handler_known)
{
    (void)slot_known;
    (void)slot;
    (void)task_run_known;
    return handler_known ? MP_BODY_DELIVERY_AROUND : MP_BODY_DELIVERY_NOWHERE;
}

static void check_the_gate(void)
{
    const uint32_t HANDLER = 0x00448369u;

    ut_section("a hit the host performed asks the slot the engine asks");

    ut_check(mp_body_gate_verdict(true, 0u, true, true) == MP_BODY_DELIVERY_EMPTY_SLOT,
             "an empty slot, a corpse or a body on its way back, is delivered nothing");
    ut_check(the_old_delivery(true, 0u, true, true) == MP_BODY_DELIVERY_AROUND,
             "where the old way called the handler on the corpse regardless");
    ut_check(mp_body_gate_verdict(true, 0u, false, true) == MP_BODY_DELIVERY_EMPTY_SLOT,
             "and an empty slot refuses even on a build without task_run");
    ut_check(mp_body_gate_verdict(true, HANDLER, true, true) ==
                 MP_BODY_DELIVERY_THROUGH_TASK_RUN,
             "a slot with the engine's handler or the dispatcher goes through task_run");
    ut_check(mp_body_gate_verdict(true, HANDLER, false, true) == MP_BODY_DELIVERY_AROUND,
             "without task_run the handler is called directly, the one way around the gate");
    ut_check(mp_body_gate_verdict(true, HANDLER, false, false) == MP_BODY_DELIVERY_NOWHERE &&
                 mp_body_gate_verdict(false, HANDLER, true, true) == MP_BODY_DELIVERY_NOWHERE,
             "with neither, or with a slot that does not read, nothing is delivered");
    ut_check(mp_body_gate_verdict(true, HANDLER, true, true) !=
                 the_old_delivery(true, HANDLER, true, true),
             "a living body is hit through the engine's delivery now, not around it");
}

/* ==============================================================================================
 * The engine, as this test plays it.
 * ============================================================================================ */

#define HERO_BLOCK_BYTES  0x400u
#define OBJECT_BYTES      0x200u
#define WORLD_BYTES       0x80u
#define RECORD_MODE       0x60u
#define RECORD_CAUSE      0x364u
#define WORLD_CLOCK       0x54u
#define OBJECT_CLASS      0x04u

/* The fan's contact code, the one the player's handler answers with the burning death. */
#define CODE_BURN 0x1Eu
#define CAUSE_BURN 3

/* The re-entry's fade, from the call to the spawn, as the field measured it. */
#define FADE_SUBSTEPS 33u

/* How many substeps the host sees this player late: the wire and the puppet's buffer. */
#define HOST_LAG 3u

/* The engine's task record, the part a delivery reads. */
typedef struct fake_task {
    uint32_t head[6];
    uint32_t slot;           /* +0x18, the contact slot */
    uint32_t wake_flag;
    uint32_t wake_value;
    uint32_t countdown;
    uint32_t started;
} fake_task_t;

_Static_assert(offsetof(fake_task_t, slot) == 0x18u, "the contact slot sits at +0x18");
_Static_assert(sizeof(fake_task_t) == 0x2Cu, "a task record is 0x2C bytes");

typedef uint32_t(__cdecl *slot_fn_t)(void);

typedef struct fake_engine {
    uint8_t     hero_block[HERO_BLOCK_BYTES];
    uint8_t     player_object[OBJECT_BYTES];
    uint8_t     puppet_object[OBJECT_BYTES];   /* the host's puppet of this player, bank 2 */
    uint8_t     fan_object[OBJECT_BYTES];
    uint8_t     world[WORLD_BYTES];
    uint32_t    world_pointer;
    uint32_t    record_pointer;                /* `pr` */
    fake_task_t player_node;
    fake_task_t mod_task;                      /* the feature's own task, running the substep */
    uint32_t    player_node_pointer;
    uint32_t    running;                       /* the task being run */
    uint32_t    game_mode;
    uint32_t    outcome;
    uint32_t    msg_self;
    uint32_t    msg_other;
    uint32_t    msg_code;
    uint32_t    msg_impact;
    uint8_t     death_descriptor;
    uint8_t     stand_descriptor;

    uint8_t    *death_head;                    /* the death entry, executable */
    uint32_t    deaths;                        /* times the engine's own death ran */
    uint32_t    respawns;
    float       respawned_at[3];
    uint32_t    fade_left;                     /* substeps until the fade spawns the body */
} fake_engine_t;

static fake_engine_t eng;

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t get_u32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

/* task_run: refuse a node with an empty slot, make the node the task being run, run the slot,
 * and put the previous task back. */
static uint32_t __cdecl fake_task_run(uint32_t node)
{
    fake_task_t *task = (fake_task_t *)(uintptr_t)node;
    uint32_t     previous;
    uint32_t     answer;

    if (task == NULL || task->slot == 0u) {
        return 0u;
    }
    previous    = eng.running;
    eng.running = node;
    answer      = ((slot_fn_t)(uintptr_t)task->slot)();
    eng.running = previous;
    return answer & ~2u;
}

/* The death entry's body behind its six byte head: empty the slot of the task being run, raise
 * the dead flag, hang the death descriptor and store the cause. It asks nothing about whether the
 * player is already dead, as the engine's does not. */
static void __cdecl fake_enter_death(int32_t cause)
{
    uint8_t *record = (uint8_t *)(uintptr_t)eng.record_pointer;

    if (eng.running != 0u) {
        ((fake_task_t *)(uintptr_t)eng.running)->slot = 0u;
    }
    put_u32(record + MP_HERO_BLOCK_DEAD, 1u);
    put_u32(record + RECORD_MODE, (uint32_t)(uintptr_t)&eng.death_descriptor);
    put_u32(record + RECORD_CAUSE, (uint32_t)cause);
    ++eng.deaths;
}

/* The player's handler: the fan's code is the burning death. */
static uint32_t __cdecl fake_on_contact(void)
{
    if (eng.msg_code == CODE_BURN) {
        ((void(__cdecl *)(int32_t))(uintptr_t)eng.death_head)(CAUSE_BURN);
    }
    return 1u;
}

static void __cdecl fake_respawn_at(int32_t hero, const float position[3], float heading)
{
    (void)hero;
    (void)heading;
    ++eng.respawns;
    memcpy(eng.respawned_at, position, sizeof eng.respawned_at);
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 4u);
    eng.player_node.slot = 0u;         /* no contact reaches a body on its way back */
    eng.fade_left        = FADE_SUBSTEPS;
}

static void __cdecl fake_set_health(int32_t health)
{
    (void)health;
}

static int32_t __cdecl fake_play_clip(void *object, int32_t clip, int32_t mode)
{
    (void)object;
    (void)clip;
    (void)mode;
    return 0;
}

static void __cdecl fake_enter_stand(void)
{
}

/* The fade ends: the spawn clears the block, stands the body where it was asked and puts the
 * engine's handler back in the player's slot. */
static void fade_step(void)
{
    if (eng.fade_left == 0u || --eng.fade_left != 0u) {
        return;
    }
    put_u32(eng.hero_block + MP_HERO_BLOCK_DEAD, 0u);
    put_u32(eng.hero_block + RECORD_MODE, (uint32_t)(uintptr_t)&eng.stand_descriptor);
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 1u);
    memcpy(eng.hero_block + MP_HERO_BLOCK_POS, eng.respawned_at, sizeof eng.respawned_at);
    eng.player_node.slot = (uint32_t)(uintptr_t)&fake_on_contact;
}

/* A flat floor under every point, nothing overhead, every line walkable. */
static void __cdecl fake_probe_floor(const float position[3], void *ground)
{
    float distance = -position[2];

    memset(ground, 0, 0x88u);
    memcpy(ground, &distance, sizeof distance);
}

static float __cdecl fake_head_clearance(const float position[3], uint16_t mask)
{
    (void)position;
    (void)mask;
    return 0.0f;
}

static float __cdecl fake_walkable_distance(uintptr_t world, const float from[3],
                                            const float to[3])
{
    (void)world;
    (void)from;
    (void)to;
    return 0.0f;
}

/* ---- what the modules ask of the engine ------------------------------------------------------ */

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_PR:               return (uintptr_t)&eng.record_pointer;
    case MP_CELL_HERO_BLOCK:       return (uintptr_t)eng.hero_block;
    case MP_CELL_PLAYER_TASK_NODE: return (uintptr_t)&eng.player_node_pointer;
    case MP_CELL_TASK_SERVICE:     return (uintptr_t)&eng.running;
    case MP_CELL_MSG_SELF:         return (uintptr_t)&eng.msg_self;
    case MP_CELL_MSG_OTHER:        return (uintptr_t)&eng.msg_other;
    case MP_CELL_MSG_CODE:         return (uintptr_t)&eng.msg_code;
    case MP_CELL_MSG_IMPACT:       return (uintptr_t)&eng.msg_impact;
    case MP_CELL_GAME_MODE:        return (uintptr_t)&eng.game_mode;
    case MP_CELL_LEVEL_OUTCOME:    return (uintptr_t)&eng.outcome;
    case MP_CELL_LEVEL:            return (uintptr_t)&eng.world_pointer;
    case MP_CELL_MODE_DEATH_DESC:  return (uintptr_t)&eng.death_descriptor;
    case MP_CELL_MODE_STAND_DESC:  return (uintptr_t)&eng.stand_descriptor;
    default:                       return 0u;
    }
}

bool mp_cells_hero_position(float out[3])
{
    memcpy(out, eng.hero_block + MP_HERO_BLOCK_POS, 3u * sizeof(float));
    return true;
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    switch (site) {
    case MP_SITE_PLR_ENTER_DEATH:    return (uintptr_t)eng.death_head;
    case MP_SITE_BAPOBJ_PLAY_CLIP:   return (uintptr_t)&fake_play_clip;
    case MP_SITE_PLR_ENTER_STAND:    return (uintptr_t)&fake_enter_stand;
    case MP_SITE_STATUS_SET_HEALTH:  return (uintptr_t)&fake_set_health;
    case MP_SITE_PLAYER_RESPAWN_AT:  return (uintptr_t)&fake_respawn_at;
    case MP_SITE_SHOT_SPAWN:         return (uintptr_t)&fake_enter_stand;
    default:                         return 0u;
    }
}

size_t mp_signatures_prologue(mp_site_t site)
{
    return site == MP_SITE_PLR_ENTER_DEATH ? 6u : 0u;
}

/* Whether this build of the engine offers task_run; the first level plays one that does not. */
static bool task_run_resolves;

uintptr_t mp_signatures_contact_address(mp_contact_site_t site)
{
    return task_run_resolves && site == MP_CONTACT_SITE_TASK_RUN ? (uintptr_t)&fake_task_run : 0u;
}

size_t mp_signatures_world_resolve(void)
{
    return (size_t)MP_WORLD_SITE_COUNT;
}

uintptr_t mp_signatures_world_address(mp_world_site_t site)
{
    switch (site) {
    case MP_WORLD_SITE_PROBE_FLOOR:       return (uintptr_t)&fake_probe_floor;
    case MP_WORLD_SITE_HEAD_CLEARANCE:    return (uintptr_t)&fake_head_clearance;
    case MP_WORLD_SITE_WALKABLE_DISTANCE: return (uintptr_t)&fake_walkable_distance;
    default:                              return 0u;
    }
}

bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table)
{
    *world = (uintptr_t)eng.world;
    *count = 0u;
    *table = 0u;
    return true;
}

bool mp_bank_read_at(size_t index, size_t offset, void *out, size_t size)
{
    uint32_t object = (uint32_t)(uintptr_t)eng.puppet_object;

    if (index != 2u || offset != MP_HERO_BLOCK_HACTOR || size != sizeof object) {
        return false;
    }
    memcpy(out, &object, sizeof object);
    return true;
}

/* ==============================================================================================
 * The host, a few substeps behind this player.
 *
 * Bank 2 of this machine stands for this machine's own player as the host shows him: the pose the
 * host resolved for him is his own state HOST_LAG substeps ago, alive or dead in his own words. The
 * fan stands at FAN and touches whoever the host shows within reach of it, every substep, through
 * the handler node of the body it touches.
 * ============================================================================================ */

#define HISTORY 512u

static const float FAN[3]      = { 100.0f, 0.0f, 0.0f };
static const float INTO_FAN[3] = { 100.5f, 0.0f, 0.0f };
static const float HOST_AT[3]  = { 80.0f, 0.0f, 0.0f };
#define FAN_REACH 2.0f
#define MY_SLOT   2u

typedef struct seen {
    bool  alive;
    float position[3];
} seen_t;

typedef struct host_side {
    bool     sees_deaths;           /* the host's pose says dead when this player is */
    seen_t   history[HISTORY];
    uint32_t reports_due[HISTORY];  /* reports the host sent, by the substep they arrive here */
    uint32_t fan_contacts;          /* contacts the fan made on the host's puppet of this player */
    uint32_t reports_sent;
    uint32_t reports_delivered;
    uint32_t reports_performed;     /* of those, delivered to a body that took them */
} host_side_t;

static host_side_t host;
static uint32_t    substep;

static const seen_t *host_view(void)
{
    uint32_t at = substep > HOST_LAG ? substep - HOST_LAG : 0u;

    return &host.history[at % HISTORY];
}

bool mp_bridge_far_pose(size_t bank, mp_bridge_far_reader_t reader, mp_bridge_far_pose_t *out)
{
    const seen_t *seen = host_view();

    (void)reader;
    if (bank != 2u || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->slot = (uint8_t)MY_SLOT;
    memcpy(out->position, seen->position, sizeof out->position);
    out->alive = seen->alive || !host.sees_deaths;
    out->dead  = !out->alive;
    return true;
}

bool mp_bridge_far_pose_stands(const mp_bridge_far_pose_t *pose)
{
    return pose != NULL && pose->alive && !pose->dead;
}

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void on_puppet_hit(size_t bank)
{
    if (bank == 2u) {
        ++host.reports_sent;
        ++host.reports_due[(substep + HOST_LAG) % HISTORY];
    }
}

static void on_death(uint8_t victim_slot, uint8_t killer_slot, uint8_t reason)
{
    mp_death_note_t note;

    note.victim_slot = victim_slot;
    note.killer_slot = killer_slot;
    note.reason      = reason;
    mp_reentry_note_death(&note);
}

/* The fan's message to whoever the host shows beside it: the sender, the receiver and the code,
 * then the delivery to the handler node of that body, as op_message does it. */
static void the_fan_turns(void)
{
    const seen_t *seen = host_view();
    uint32_t      node = get_u32(eng.puppet_object + BAPOBJ_HANDLER_TASK);

    if (distance(seen->position, FAN) > FAN_REACH) {
        return;
    }
    ++host.fan_contacts;
    eng.msg_self  = (uint32_t)(uintptr_t)eng.fan_object;
    eng.msg_other = (uint32_t)(uintptr_t)eng.puppet_object;
    eng.msg_code  = CODE_BURN;
    (void)fake_task_run(node);
}

/* A report arrives here and is performed on this machine's player, from inside the feature's own
 * substep task, as the hit relay performs it. */
static void the_reports_arrive(void)
{
    uint32_t *due = &host.reports_due[substep % HISTORY];

    while (*due != 0u) {
        --*due;
        ++host.reports_delivered;
        eng.msg_self  = 0u;
        eng.msg_other = (uint32_t)(uintptr_t)eng.player_object;
        eng.msg_code  = CODE_BURN;
        eng.running   = (uint32_t)(uintptr_t)&eng.mod_task;
        host.reports_performed += mp_body_run_engine_contact() ? 1u : 0u;
        eng.running   = 0u;
    }
}

static bool player_dead(void)
{
    return get_u32(eng.hero_block + MP_HERO_BLOCK_DEAD) != 0u;
}

/* One substep and the frame drawn after it, in the order the engine and the pumps run. */
static void one_substep(void)
{
    seen_t *now;
    float   seconds;

    ++substep;
    seconds = (float)substep / 32.0f;
    memcpy((uint8_t *)(uintptr_t)eng.world_pointer + WORLD_CLOCK, &seconds, sizeof seconds);
    fade_step();
    now        = &host.history[substep % HISTORY];
    now->alive = !player_dead();
    memcpy(now->position, eng.hero_block + MP_HERO_BLOCK_POS, sizeof now->position);

    the_fan_turns();
    the_reports_arrive();

    mp_reentry_note_peer(0u, HOST_AT, 0.0f, true);
    mp_seat_note_body(0u, HOST_AT, 0.0f, true);
    mp_reentry_tick(substep);
    mp_respawn_tick(substep);
}

static void run_substeps(uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        one_substep();
    }
}

/* A fresh level with this player standing in the fan, the host beside it at HOST_AT, and nothing
 * on the wire. The world record is a different one for every level, the way a load allocates it,
 * and the history starts with this player where he stands. */
static uint8_t worlds[4][WORLD_BYTES];

static void open_a_level(size_t which, bool host_sees_deaths)
{
    size_t i;

    memset(&host, 0, sizeof host);
    host.sees_deaths  = host_sees_deaths;
    eng.world_pointer = (uint32_t)(uintptr_t)worlds[which];
    memset(worlds[which], 0, sizeof worlds[which]);
    eng.game_mode = 2u;
    eng.outcome   = 2u;
    put_u32(eng.hero_block + MP_HERO_BLOCK_DEAD, 0u);
    put_u32(eng.hero_block + RECORD_MODE, (uint32_t)(uintptr_t)&eng.stand_descriptor);
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 1u);
    memcpy(eng.hero_block + MP_HERO_BLOCK_POS, INTO_FAN, sizeof INTO_FAN);
    eng.player_node.slot = (uint32_t)mp_body_dispatcher_address();
    eng.fade_left = 0u;
    eng.deaths    = 0u;
    eng.respawns  = 0u;
    for (i = 0; i < HISTORY; ++i) {
        host.history[i].alive = true;
        memcpy(host.history[i].position, INTO_FAN, sizeof INTO_FAN);
    }
}

static bool standing_beside_the_host(void)
{
    float at[3];

    memcpy(at, eng.hero_block + MP_HERO_BLOCK_POS, sizeof at);
    return !player_dead() && get_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE) == 1u &&
           distance(at, HOST_AT) <= MP_SEAT_RING_FAR + 0.5f;
}

/* ==============================================================================================
 * The runs.
 * ============================================================================================ */

/* The death head the hull detours: push ebp / mov ebp,esp / sub esp,0, which is six bytes on an
 * instruction boundary, then mov esp,ebp / pop ebp and a jump to the death's body in C. */
static bool install_the_engine(void)
{
    static const uint8_t HEAD[14] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x00, 0x8B, 0xE5, 0x5D, 0xE9,
                                      0x00, 0x00, 0x00, 0x00 };
    DWORD      protection = 0;
    int32_t    jump;
    mp_rules_t rules;

    eng.death_head = (uint8_t *)VirtualAlloc(NULL, 4096u, MEM_COMMIT | MEM_RESERVE,
                                             PAGE_READWRITE);
    if (eng.death_head == NULL) {
        return false;
    }
    memcpy(eng.death_head, HEAD, sizeof HEAD);
    jump = (int32_t)((uintptr_t)&fake_enter_death - ((uintptr_t)eng.death_head + sizeof HEAD));
    memcpy(eng.death_head + 10u, &jump, sizeof jump);
    (void)VirtualProtect(eng.death_head, 4096u, PAGE_EXECUTE_READWRITE, &protection);

    eng.record_pointer      = (uint32_t)(uintptr_t)eng.hero_block;
    eng.player_node_pointer = (uint32_t)(uintptr_t)&eng.player_node;
    eng.player_node.slot    = (uint32_t)(uintptr_t)&fake_on_contact;
    put_u32(eng.hero_block + MP_HERO_BLOCK_HACTOR, (uint32_t)(uintptr_t)eng.player_object);
    eng.world_pointer = (uint32_t)(uintptr_t)worlds[3];
    eng.game_mode     = 2u;

    if (!mp_body_install() || !mp_damage_install() || !mp_respawn_install()) {
        return false;
    }
    mp_body_set_death_listener(&on_death);
    mp_body_set_puppet_hit_listener(&on_puppet_hit);
    mp_body_set_bank_slot(0u, (uint8_t)MY_SLOT);
    mp_body_set_bank_slot(1u, 0u);
    mp_respawn_set_landed_listener(&mp_body_arm_dispatcher);
    mp_body_arm_dispatcher();
    mp_body_far_at(2u)->spawned = true;
    mp_body_set_second_is_puppet(true);
    mp_body_gate_give_node(2u, (uint32_t)(uintptr_t)eng.puppet_object);

    memset(&rules, 0, sizeof rules);
    mp_reentry_note_session((uint8_t)MP_LOBBY_MODE_COOP, true, &rules);
    mp_reentry_note_my_slot((uint8_t)MY_SLOT);
    return eng.player_node.slot == (uint32_t)mp_body_dispatcher_address();
}

/* A build whose task_run did not resolve: the hits the host performs are delivered around the
 * engine's gate, so the death entry's own watch is the one thing between the corpse and the
 * reports that were already on their way. */
static void check_a_build_without_task_run(void)
{
    ut_section("a build without task_run: the corpse is refused at the death entry");
    open_a_level(0u, true);
    gate_log_mark();
    /* The player module parked for the first substeps, as a scene parks it: the re-entry waits for
     * its gates, and the reports on their way reach the corpse meanwhile. */
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 0u);
    run_substeps(10u);
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 1u);
    run_substeps(120u);
    ut_checkf(eng.deaths == 1u, "the engine ran its death once for %u report(s) delivered "
              "(it ran %u time(s))", (unsigned)host.reports_delivered, (unsigned)eng.deaths);
    ut_checkf(eng.respawns == 1u, "and the player was asked back once (%u)",
              (unsigned)eng.respawns);
    ut_check(gate_log_count("a death entered here: cause 3", ": entered") == 1u &&
                 gate_log_count("a death entered here: cause 3", "the task being run was another")
                     >= 2u,
             "one entry, and every hit delivered around the gate ran under the feature's own "
             "task, which is where the death's empty slot then went");
    ut_checkf(gate_log_count("on a corpse (the death mode already up)", ": refused") >= 1u,
              "the reports that reached the corpse before the fade were refused at the entry "
              "(%u)", (unsigned)gate_log_count("on a corpse", ": refused"));
    ut_check(standing_beside_the_host(), "and stands again beside the host");
}

/* The field run, repaired: the host stops reporting a player it shows lying dead, and what was on
 * its way meets the empty slot of a corpse. */
static void check_the_fan(void)
{
    ut_section("the fan: one death, one re-entry");
    task_run_resolves = true;
    mp_body_set_second_is_puppet(true);
    open_a_level(1u, true);
    gate_log_mark();
    run_substeps(120u);
    ut_checkf(eng.deaths == 1u, "the engine ran its death once (%u)", (unsigned)eng.deaths);
    ut_check(gate_log_count("a death entered here: cause 3, at 100.50 0.00 0.00, on a living "
                            "body", "the task being run was the player's, a hit from the host "
                                    "was being performed") == 1u &&
                 gate_log_count("a death entered here", NULL) == 1u,
             "one line for one entry: on a living body, under the player's own node, from a hit "
             "the host performed");
    ut_checkf(eng.respawns == 1u, "and the player was asked back once (%u)",
              (unsigned)eng.respawns);
    ut_checkf(host.reports_performed == 1u && host.reports_delivered > 1u,
              "of the %u report(s) that arrived, one was delivered and the rest met an empty "
              "contact slot", (unsigned)host.reports_delivered);
    ut_checkf(host.fan_contacts > host.reports_sent && host.reports_sent <= 2u * HOST_LAG + 1u,
              "the fan touched the host's puppet of this player %u time(s) and %u were reported: "
              "a player the host shows lying dead is reported nothing", (unsigned)host.fan_contacts,
              (unsigned)host.reports_sent);
    ut_check(standing_beside_the_host(), "and he stands again beside the host, out of the fan");
}

/* A host that goes on showing the player standing where he died: the reports that are on their way
 * when the new body stands kill it, which only the host could have known better. What this side
 * owes is one wish for that life, a landing that is not believed, and a different seat. */
static void check_a_host_that_keeps_reporting(void)
{
    float first_seat[3];

    ut_section("a host that keeps reporting: the next life dies once, and not on the same seat");
    open_a_level(2u, false);
    gate_log_mark();
    while (eng.respawns == 0u && host.fan_contacts < 60u) {
        one_substep();
    }
    memcpy(first_seat, eng.respawned_at, sizeof first_seat);
    run_substeps(200u);
    ut_checkf(eng.deaths == 2u, "two lives, two deaths: the first in the fan and one of the new "
              "body at its seat (%u)", (unsigned)eng.deaths);
    ut_checkf(eng.respawns == 2u, "and two re-entries, not one per report (%u)",
              (unsigned)eng.respawns);
    ut_check(gate_log_count("a death entered here", ": entered") == 2u &&
                 gate_log_count("a death entered here", ": refused") == 0u,
             "two entries, both on a living body, and nothing refused: the gate in front of the "
             "handler left the death entry nothing to refuse");
    ut_check(gate_log_count("the body the engine stood up is a corpse again", NULL) == 1u,
             "the landing found the new body dead again and handed nothing on");
    ut_check(gate_log_count("ended a life of this player before the body stood on it", NULL) ==
                 1u,
             "and the seat that ended it is refused to him from now on");
    ut_checkf(distance(eng.respawned_at, first_seat) >= MP_SEAT_LOCK_RADIUS,
              "the second seat is %.2f unit(s) from the one that ended a life",
              (double)distance(eng.respawned_at, first_seat));
    ut_check(standing_beside_the_host(), "and he stands again beside the host");
}

/* A far body in a session has a node of its own, and the spawn of one gives the player's slot back
 * as it found it. */
static void check_the_nodes(void)
{
    uint32_t node = get_u32(eng.puppet_object + BAPOBJ_HANDLER_TASK);
    uint32_t before = mp_body_contacts_total();
    uint32_t kept = 0u;

    ut_section("a node of its own for a far body, and the player's slot kept across a build");
    ut_check(node != 0u && node != (uint32_t)(uintptr_t)&eng.player_node &&
                 ((const fake_task_t *)(uintptr_t)node)->slot ==
                     (uint32_t)mp_body_dispatcher_address(),
             "the far body's handler node is its own, with the dispatcher in its slot");
    eng.player_node.slot = 0u;
    eng.msg_self  = (uint32_t)(uintptr_t)eng.fan_object;
    eng.msg_other = (uint32_t)(uintptr_t)eng.puppet_object;
    eng.msg_code  = 0u;
    (void)fake_task_run((uint32_t)(uintptr_t)&eng.player_node);
    ut_check(mp_body_contacts_total() == before,
             "with this player's slot empty the player's node delivers nothing");
    (void)fake_task_run(node);
    ut_check(mp_body_contacts_total() == before + 1u,
             "and the far body's own node still reaches the dispatcher");

    ut_check(mp_body_gate_keep_local_slot(&kept) && kept == 0u, "an empty slot is kept");
    eng.player_node.slot = (uint32_t)(uintptr_t)&fake_on_contact;   /* what the spawn writes */
    mp_body_gate_put_back_local_slot(kept);
    ut_check(eng.player_node.slot == 0u, "and put back empty after the spawn wrote a handler");
    eng.player_node.slot = (uint32_t)mp_body_dispatcher_address();
    ut_check(mp_body_gate_keep_local_slot(&kept), "a slot with the dispatcher is kept");
    eng.player_node.slot = (uint32_t)(uintptr_t)&fake_on_contact;
    mp_body_gate_put_back_local_slot(kept);
    ut_check(eng.player_node.slot == (uint32_t)mp_body_dispatcher_address(),
             "and put back as the dispatcher");
}

/* A hit the host performs on a living body whose slot is empty is dropped at the gate like one on
 * a corpse. The engine keeps the slot and the life together, so this must never happen, and the
 * report tells the two apart: an empty slot on a living body is a body nothing can hurt. */
static void check_an_empty_slot_on_a_living_body(void)
{
    ut_section("an empty slot on a living body is told apart from a corpse's");
    gate_log_mark();
    mp_body_gate_report();
    ut_check(gate_log_count("met an empty contact slot and did nothing, 0 of them on a living "
                            "body (must be 0)", NULL) == 1u,
             "every empty slot the runs above met was a corpse's or a body's on its way back");

    put_u32(eng.hero_block + MP_HERO_BLOCK_DEAD, 0u);
    put_u32(eng.hero_block + RECORD_MODE, (uint32_t)(uintptr_t)&eng.stand_descriptor);
    put_u32(eng.hero_block + MP_HERO_BLOCK_MODULE_STATE, 1u);
    eng.player_node.slot = 0u;
    eng.msg_self  = 0u;
    eng.msg_other = (uint32_t)(uintptr_t)eng.player_object;
    eng.msg_code  = CODE_BURN;
    ut_check(!mp_body_run_engine_contact(),
             "a hit on a living body with an empty slot does nothing, as the engine's gate does");
    eng.player_node.slot = (uint32_t)mp_body_dispatcher_address();
    gate_log_mark();
    mp_body_gate_report();
    ut_check(gate_log_count("met an empty contact slot and did nothing, 1 of them on a living "
                            "body (must be 0)", NULL) == 1u,
             "and the report names it");
}

int main(void)
{
    bool installed;

    check_the_gate();

    ut_section("the engine this test plays");
    installed = install_the_engine();
    ut_check(installed, "the body, the death hull and the re-entry install over it, and the "
             "dispatcher takes the player's slot");
    if (!installed) {
        return ut_summary("mp_body_gate");
    }
    check_a_build_without_task_run();
    check_the_fan();
    check_a_host_that_keeps_reporting();
    check_the_nodes();
    check_an_empty_slot_on_a_living_body();

    mp_body_report("the end of the test");
    mp_damage_report("the end of the test");
    mp_reentry_report();
    mp_respawn_report();
    return ut_summary("mp_body_gate");
}
