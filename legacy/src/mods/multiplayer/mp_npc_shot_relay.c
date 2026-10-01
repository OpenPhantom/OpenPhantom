/* mp_npc_shot_relay.c: the host's NPC bolts, repeated on every client. See the header. */
#include "mp_npc_shot_relay.h"

#include "mp_armed.h"
#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_cells.h"
#include "mp_enemy_spawn.h"
#include "mp_hit_relay.h"
#include "mp_npc_shot.h"
#include "mp_npc_shot_impact.h"
#include "mp_npc_shot_zap.h"
#include "mp_signatures.h"
#include "mp_world.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *(__cdecl *shot_spawn_fn_t)(int32_t kind, const void *muzzle, float pitch, float yaw,
                                        int32_t shooter_class);

/* The projectile row's handler pointer, null for a plain bolt. */
#define SHOT_ROW_HANDLER 0x3Cu
/* A shot node's body object, which is the sender a contact names, and the kind the engine made,
 * after its own remaps (an ally's rocket becomes the player's). */
#define SHOT_NODE_OBJECT 0xA0u
#define SHOT_NODE_KIND   0x80u
/* The side a body and a shot carry, which a sabre rewrites when it turns a bolt back. */
#define OBJECT_SIDE      0x08u
/* A shot's body names its node back here; an object that closes the round trip is a live shot. */
#define OBJECT_OWNER     0xA0u
/* The kind an object has when it is no shot, past every row of the table. */
#define NO_KIND          0xFFFFFFFFu

/* A client holds this many bolts for its replay; a full queue drops the oldest. */
#define QUEUE_SLOTS 32u
/* A host sends at most this many bolts a substep; the rest stay on the host, hits and all. */
#define SENDS_PER_SUBSTEP 8u

/* Within one lifetime a host remembers at most its budget of bolts a substep and one sub shot for
 * each of them, the impact of a remembered bolt making at most one and a sub shot having no impact
 * of its own; a client fires what the host sent, plus a queue it may release at once. The memory
 * holds all of them, so no bolt still flying is pushed out while its copy can still hurt. */
_Static_assert(MP_NPC_SHOT_MEMORY >=
                   2u * (SENDS_PER_SUBSTEP * (MP_NPC_SHOT_LIFETIME + 1u) + QUEUE_SLOTS),
               "the bolt memory holds every bolt and sub shot young enough to answer");

typedef struct npc_shot_relay {
    bool                        installed;
    bool                        host;
    bool                        firing;       /* this module is calling the engine itself */
    mp_npc_shot_relay_send_fn_t send;
    uint32_t                    tick;
    uint32_t                    sends_tick;   /* the substep `tick_sends` counts for */
    uint32_t                    tick_sends;
    uintptr_t                   shot_table;
    shot_spawn_fn_t             spawn;
    mp_npc_shot_memory_t        memory;

    mp_npc_shot_t               queue[QUEUE_SLOTS];
    size_t                      queue_head;
    size_t                      queue_count;

    uint64_t                    kinds_sent;
    uint64_t                    kinds_kept;   /* the kinds the rule refused, by number */
    uint64_t                    kinds_fired;
    uint32_t                    sent;
    uint32_t                    allies_sent;  /* of them, an ally's: class 1 out of the AI */
    uint32_t                    unsent;
    uint32_t                    over_budget;
    uint32_t                    kept;         /* a kind that stays on the host */
    uint32_t                    alone;        /* nobody joined, or no level open */
    uint32_t                    taken;
    uint32_t                    torn;
    uint32_t                    other_level;
    uint32_t                    queue_dropped;
    uint32_t                    fired;
    uint32_t                    fire_refused; /* the engine made no bolt */
    uint32_t                    stale;
    uint32_t                    local;        /* bolts this client's own actors fired */
    uint32_t                    hits_left;    /* contacts by a remembered bolt, not reported */

    uint64_t                    sub_kinds;    /* the kinds of the sub shots tied, by number */
    uint32_t                    sub_tied;     /* sub shots remembered as their bolt's child */
    uint32_t                    sub_other;    /* made by the impact of a bolt not remembered */
    uint32_t                    sub_orphans;  /* a sub shot kind outside every impact */
    uint32_t                    forgotten;    /* entries dropped for a new shot on the object */
    uint32_t                    displaced;    /* young entries a full memory wrote over */
    uint32_t                    unreadable;   /* a side or a kind that did not read */
} npc_shot_relay_t;

static npc_shot_relay_t relay;

/* `sub_shots_tied` is the one condition a kind can add: the thermal detonator travels only where
 * the fireball of its impact is tied to it. */
static bool kind_travels(uint32_t kind, bool sub_shots_tied)
{
    uint32_t handler = 0;

    if (kind >= MP_NPC_SHOT_KINDS || relay.shot_table == 0u ||
        !memory_try_read_u32(relay.shot_table + kind * MP_CELLS_SHOT_ROW_BYTES + SHOT_ROW_HANDLER,
                         &handler)) {
        return false;
    }
    return mp_npc_shot_kind_travels(kind, handler != 0u, sub_shots_tied);
}

/* The side an object carries now, and the kind of the shot it flies as: NO_KIND when the object
 * is no live shot, which is an object whose node does not name it back. Read under the structured
 * handler, because this runs for every contact a remembered fireball's ring posts. */
static bool made_of(uint32_t object, uint32_t *side, uint32_t *kind)
{
    uint32_t node = 0u;
    uint32_t back = 0u;

    *kind = NO_KIND;
    if (object == 0u || !memory_try_read((uintptr_t)object + OBJECT_SIDE, side, sizeof *side)) {
        return false;
    }
    if (memory_try_read((uintptr_t)object + OBJECT_OWNER, &node, sizeof node) && node != 0u &&
        memory_try_read((uintptr_t)node + SHOT_NODE_OBJECT, &back, sizeof back) &&
        back == object) {
        (void)memory_try_read((uintptr_t)node + SHOT_NODE_KIND, kind, sizeof *kind);
    }
    return true;
}

/* Remembers a bolt or a sub shot by what the engine made: its side after the spawn, the zero of a
 * detonator included, and its kind. A side of 1 is not remembered, see mp_npc_shot_rememberable. */
static void remember_made(uint32_t object)
{
    uint32_t side = 0u;
    uint32_t kind = NO_KIND;

    if (!made_of(object, &side, &kind) || kind >= MP_NPC_SHOT_KINDS) {
        ++relay.unreadable;
        return;
    }
    if (mp_npc_shot_rememberable(side) &&
        mp_npc_shot_remember(&relay.memory, object, (uint8_t)side, (uint8_t)kind, relay.tick)) {
        ++relay.displaced;
    }
}

/* Whether `object` is a remembered bolt or sub shot that is still the NPC's, asked now. */
static bool still_npcs_now(uint32_t object)
{
    uint32_t side = 0u;
    uint32_t kind = NO_KIND;

    return made_of(object, &side, &kind) &&
           mp_npc_shot_still_npcs(&relay.memory, object, side, kind, relay.tick);
}

/* The shot hull's listener for every shot made, on both sides. The object's old entries go first:
 * the engine hands an object out again once its shot is freed. Then a sub shot made inside an
 * impact is tied to the bolt the impact runs for, when that bolt is remembered and still the
 * NPC's; the fireball of a detonator is the one that hurts, and a copy of it hurts on the
 * client's own machine, so its hits here are that machine's to decide. */
static void note_made(int32_t kind, int32_t shooter_class, uint32_t object)
{
    uint32_t          parent    = 0u;
    bool              in_impact;
    mp_npc_shot_sub_t verdict;

    if (!relay.installed || object == 0u || !mp_armed_transport()) {
        return;
    }
    relay.forgotten += (uint32_t)mp_npc_shot_forget(&relay.memory, object, relay.tick);
    in_impact = mp_npc_shot_impact_parent(&parent);
    verdict   = mp_npc_shot_sub_verdict(in_impact, in_impact && still_npcs_now(parent),
                                        shooter_class, (uint32_t)kind);
    if (verdict == MP_NPC_SHOT_SUB_TIED) {
        ++relay.sub_tied;
        if (kind >= 0 && kind < (int32_t)MP_NPC_SHOT_KINDS) {
            relay.sub_kinds |= (uint64_t)1u << (uint32_t)kind;
        }
        remember_made(object);
    } else if (verdict == MP_NPC_SHOT_SUB_OF_ANOTHER) {
        ++relay.sub_other;
    } else if (verdict == MP_NPC_SHOT_SUB_ORPHAN && mp_npc_shot_impact_installed()) {
        ++relay.sub_orphans;   /* without the hull every sub shot would land here */
    }
}

/* The shot hull's NPC listener. On a client it only counts: a bolt one of its own unparked actors
 * fired is that machine's, and nothing here repeats it. */
static void note_shot(int32_t kind, const float muzzle[3], float pitch, float yaw,
                      int32_t shooter_class, uint32_t object)
{
    mp_npc_shot_t shot;
    uint8_t       note[MP_NPC_SHOT_BYTES];
    uint16_t      level = 0;

    if (!relay.installed || relay.firing || muzzle == NULL || !mp_armed_transport()) {
        return;
    }
    if (!relay.host) {
        ++relay.local;
        return;
    }
    /* The zap stays here as a bolt, and its arcs go as a world event once its actor is known. */
    if (kind == (int32_t)MP_NPC_SHOT_PLAYER_ZAP) {
        mp_npc_shot_zap_note(object);
    }
    if (kind < 0 || shooter_class < 1 || shooter_class > 255 ||
        !kind_travels((uint32_t)kind, mp_npc_shot_impact_installed())) {
        ++relay.kept;
        if (kind >= 0 && kind < (int32_t)MP_NPC_SHOT_KINDS) {
            relay.kinds_kept |= (uint64_t)1u << (uint32_t)kind;
        }
        return;
    }
    if (relay.send == NULL || !mp_bridge_joined() || !mp_enemy_spawn_level_identity(&level)) {
        ++relay.alone;
        return;
    }
    if (relay.sends_tick != relay.tick) {
        relay.sends_tick = relay.tick;
        relay.tick_sends = 0u;
    }
    if (relay.tick_sends >= SENDS_PER_SUBSTEP) {
        ++relay.over_budget;
        return;
    }
    memset(&shot, 0, sizeof shot);
    shot.tick          = relay.tick;
    shot.level         = level;
    shot.kind          = (uint8_t)kind;
    shot.shooter_class = (uint8_t)shooter_class;
    memcpy(shot.muzzle, muzzle, sizeof shot.muzzle);
    shot.pitch = pitch;
    shot.yaw   = yaw;
    /* Remembered only once it is on its way: a bolt the clients never heard of is reported by
     * the host when it hits, as every bolt was before. */
    if (mp_npc_shot_encode(&shot, note, sizeof note) == 0u || !relay.send(note, sizeof note)) {
        ++relay.unsent;
        return;
    }
    ++relay.tick_sends;
    ++relay.sent;
    relay.allies_sent += shooter_class == 1 ? 1u : 0u;
    relay.kinds_sent |= (uint64_t)1u << (uint32_t)kind;
    /* By the side the engine left, which is the class for every kind but the detonator. An ally's
     * bolt reads 1 and is not remembered (see mp_npc_shot_rememberable); an ally's detonator reads
     * 0 and is, because a copy of it hurts the client where the client sits. */
    remember_made(object);
}

bool mp_npc_shot_relay_take(const uint8_t *note, size_t bytes)
{
    mp_npc_shot_t shot;
    size_t        slot;

    if (!mp_npc_shot_is(note, bytes)) {
        return false;
    }
    if (!relay.installed || relay.host) {
        return true;   /* ours by tag; a host fires nothing it is told */
    }
    if (!mp_npc_shot_decode(note, bytes, &shot)) {
        ++relay.torn;
        return true;
    }
    ++relay.taken;
    if (relay.queue_count == QUEUE_SLOTS) {
        relay.queue_head = (relay.queue_head + 1u) % QUEUE_SLOTS;
        --relay.queue_count;
        ++relay.queue_dropped;
    }
    slot = (relay.queue_head + relay.queue_count) % QUEUE_SLOTS;
    relay.queue[slot] = shot;
    ++relay.queue_count;
    return true;
}

/* The copy, through the engine's own entry and so through the hull, which hands this module's own
 * spawn back to it and is told to ignore it. */
static void fire(const mp_npc_shot_t *shot)
{
    void    *node;
    uint32_t object = 0;
    float    muzzle[3];

    /* Tied or not here, a detonator the host sent is fired: the copy hurts this machine's own
     * player, which is this machine's to decide, and the host does not mirror what it sent. The
     * tie only matters where hits are mirrored, and that is the host. */
    if (!kind_travels(shot->kind, true)) {
        ++relay.fire_refused;
        return;
    }
    memcpy(muzzle, shot->muzzle, sizeof muzzle);
    relay.firing = true;
    mp_body_note_npc_replay(true);
    node = relay.spawn((int32_t)shot->kind, muzzle, shot->pitch, shot->yaw,
                       (int32_t)shot->shooter_class);
    mp_body_note_npc_replay(false);
    relay.firing = false;
    if (node == NULL || !memory_try_read_u32((uintptr_t)node + SHOT_NODE_OBJECT, &object) ||
        object == 0u) {
        ++relay.fire_refused;
        return;
    }
    ++relay.fired;
    relay.kinds_fired |= (uint64_t)1u << shot->kind;
    /* An ally's bolt carries a player's side, so it stays out of the memory, which tells a bolt
     * partly by its side (see note_shot), and is named in the shot owners as not this player's: a
     * contact of it is never reported as this player's hit, and a shot of this player's own that
     * reuses the object is this player's again. An ally's detonator reads 0 and is remembered. */
    if (shot->shooter_class == 1u) {
        mp_hit_relay_note_far_shot(object);
    }
    remember_made(object);
}

void mp_npc_shot_relay_run_due(size_t clock)
{
    uint32_t now   = 0;
    uint16_t level = 0;
    bool     known;
    bool     have_level;

    if (!relay.installed || relay.host || relay.queue_count == 0u) {
        return;
    }
    known      = mp_world_render_tick(clock, &now);
    have_level = mp_enemy_spawn_level_identity(&level);
    while (relay.queue_count != 0u) {
        const mp_npc_shot_t *shot = &relay.queue[relay.queue_head];
        mp_npc_shot_due_t    due  = mp_npc_shot_due(shot->tick, now, known);

        if (due == MP_NPC_SHOT_WAIT) {
            return;   /* in the host's order, so every later one waits as well */
        }
        if (!have_level || shot->level != level) {
            ++relay.other_level;
        } else if (due == MP_NPC_SHOT_STALE) {
            ++relay.stale;
        } else {
            fire(shot);
        }
        relay.queue_head = (relay.queue_head + 1u) % QUEUE_SLOTS;
        --relay.queue_count;
    }
}

bool mp_npc_shot_relay_npc_owned(uint32_t object)
{
    if (!relay.installed || object == 0u || !still_npcs_now(object)) {
        return false;
    }
    ++relay.hits_left;
    return true;
}

void mp_npc_shot_relay_set_host(bool host)
{
    relay.host = host;
}

void mp_npc_shot_relay_set_send(mp_npc_shot_relay_send_fn_t send)
{
    relay.send = send;
}

/* A new substep: the zaps of the one before have been stored by their actors by now. */
void mp_npc_shot_relay_set_tick(uint32_t tick)
{
    relay.tick = tick;
    if (relay.installed && relay.host) {
        mp_npc_shot_zap_resolve();
    }
}

bool mp_npc_shot_relay_install(void)
{
    if (relay.installed) {
        return true;
    }
    relay.shot_table = mp_cells_address(MP_CELL_SHOT_TABLE);
    relay.spawn      = (shot_spawn_fn_t)mp_signatures_address(MP_SITE_SHOT_SPAWN);
    if (relay.shot_table == 0u || relay.spawn == NULL) {
        log_warning("the NPCs' bolts cannot travel: the %s did not resolve, so a client sees none "
                    "of the host's bolts while their hits still reach it",
                    relay.shot_table == 0u ? "projectile table" : "engine's shot entry");
        return false;
    }
    mp_npc_shot_memory_clear(&relay.memory);
    mp_body_set_npc_shot_listener(&note_shot);
    mp_body_set_shot_made_listener(&note_made);
    relay.installed = true;
    (void)mp_npc_shot_impact_install((uintptr_t)relay.spawn);
    (void)mp_npc_shot_zap_install();
    log_info("the NPCs' bolts travel: a bolt the host's actors fire goes to every client, where "
             "the engine fires it when the replay reaches it, and the machine a player sits at "
             "decides whether that player is hit");
    return true;
}

/* The kinds a mask names, as numbers. */
static void kinds_text(uint64_t mask, char *out, size_t size)
{
    size_t   used = 0;
    uint32_t kind;

    out[0] = '\0';
    for (kind = 0; kind < MP_NPC_SHOT_KINDS && used + 4u < size; ++kind) {
        if ((mask & ((uint64_t)1u << kind)) != 0u) {
            used += used == 0u ? text_format(out + used, size - used, "%u", (unsigned)kind)
                               : text_format(out + used, size - used, " %u", (unsigned)kind);
        }
    }
    if (used == 0u) {
        text_format(out, size, "none");
    }
}

/* The sub shots and the memory's own upkeep, one line for either side. A tied sub shot is one the
 * memory holds as its bolt's child; one without a parent is a ring or a fireball the hull saw
 * made outside every impact, which the bytes say cannot happen. */
static void report_sub_shots(void)
{
    char sub_kinds[128];
    char impact[96];

    kinds_text(relay.sub_kinds, sub_kinds, sizeof sub_kinds);
    if (mp_npc_shot_impact_installed()) {
        text_format(impact, sizeof impact, "hulled at %08X, %u impact(s) seen",
                    (unsigned)mp_npc_shot_impact_site(), (unsigned)mp_npc_shot_impact_seen());
    } else {
        text_format(impact, sizeof impact, "NOT hulled on this side");
    }
    log_info("  the NPCs' bolts' sub-shots (%s): %u tied to the remembered bolt whose impact made "
             "them (kinds: %s), %u made by the impact of a bolt not remembered here, %u without a "
             "parent; %u remembered entr(ies) dropped when the engine made a new shot on the "
             "object, %u young entr(ies) a full memory wrote over, %u bolt(s) whose side or kind "
             "did not read; shot_impact %s, so the thermal detonator %s",
             relay.host ? "the host" : "a client", (unsigned)relay.sub_tied, sub_kinds,
             (unsigned)relay.sub_other, (unsigned)relay.sub_orphans, (unsigned)relay.forgotten,
             (unsigned)relay.displaced, (unsigned)relay.unreadable, impact,
             mp_npc_shot_impact_installed() ? "travels"
             : relay.host                   ? "stays here"
                                            : "is fired here untied, the host deciding");
}

void mp_npc_shot_relay_report(void)
{
    char kinds[128];

    if (!relay.installed) {
        return;
    }
    mp_npc_shot_zap_report();
    report_sub_shots();
    if (relay.host) {
        char kept_kinds[128];

        kinds_text(relay.kinds_sent, kinds, sizeof kinds);
        kinds_text(relay.kinds_kept, kept_kinds, sizeof kept_kinds);
        log_info("  the NPCs' bolts (the host): %u sent (%u of them an ally's), %u unsent, %u "
                 "over the budget of %u a substep, %u of a kind that stays here (kinds kept: %s), "
                 "%u with nobody "
                 "joined or no level; kinds sent: %s; %u hit(s) on a far player left to that "
                 "player's machine",
                 (unsigned)relay.sent, (unsigned)relay.allies_sent, (unsigned)relay.unsent,
                 (unsigned)relay.over_budget,
                 (unsigned)SENDS_PER_SUBSTEP, (unsigned)relay.kept, kept_kinds,
                 (unsigned)relay.alone,
                 kinds, (unsigned)relay.hits_left);
        return;
    }
    kinds_text(relay.kinds_fired, kinds, sizeof kinds);
    log_info("  the NPCs' bolts (a client): %u taken, %u torn, %u fired, %u the engine made "
             "nothing of, %u too late, %u about another level, %u dropped from a full queue; "
             "kinds fired: %s; %u hit(s) on the host's actors left to the host; %u bolt(s) "
             "fired by this side's own actors",
             (unsigned)relay.taken, (unsigned)relay.torn, (unsigned)relay.fired,
             (unsigned)relay.fire_refused, (unsigned)relay.stale, (unsigned)relay.other_level,
             (unsigned)relay.queue_dropped, kinds, (unsigned)relay.hits_left,
             (unsigned)relay.local);
}
