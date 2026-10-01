/* mp_npc_copies_bridge.c: the NPC copies' protocol in the running game. See the header. */
#include "mp_npc_copies_bridge.h"

#include "mp_arena.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_relay.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_sync.h"
#include "mp_events.h"
#include "mp_lobby.h"
#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_npc_copies_run.h"
#include "mp_session.h"
#include "mp_target.h"
#include "mp_wallclock.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_SESSION_MAX_PEERS + 1u <= NPC_SPAWN_WORLD_SLOTS,
               "every peer's slot and the host's fit the contract's world slots");
_Static_assert(MP_ENEMY_SPAWN_ACTOR_RESERVE == NPC_SPAWN_POOL_RESERVE,
               "the receiver's spawner and the copies keep the same slots free");
_Static_assert(MP_TARGET_SLOTS >= NPC_SPAWN_KEY_END, "the aim remembers every copy's key");
_Static_assert(MP_ENEMY_SYNC_KEYS >= NPC_SPAWN_KEY_END, "the enemy block names every copy's key");

/* The corpse sweep counts censuses, 32 a second. */
#define CENSUSES_PER_SECOND 32u

typedef struct copies_bridge {
    mp_npc_copies_run_t     run;
    npc_spawn_wish_record_t wishes;
    uint8_t                 world;          /* the generation of the world entered */
    bool                    entered;        /* a level began since the last new world */
    uint32_t                unentered;      /* substeps with a level no level begin announced */
    bool (*send)(const uint8_t *bytes, size_t count);
    mp_session_t           *host;
    uint32_t                reads_failed;   /* substeps with no overlay's record to read */
    uint32_t                corpses;        /* a host's copy that died, made a corpse here */
    uint32_t                anchors_said;   /* the flyers' anchors published */
    uint32_t                anchors_refused;
    uint32_t                anchors_to_host;    /* a seated far slot resolved to the host's point */
    uint32_t                anchors_own;        /* a seated far slot with its player's own point */
    /* The two numbers the overlay decides by, as they were last said. */
    bool                    said_record;
    uint32_t                said_cap;
    uint32_t                said_slot;
    uint32_t                said_epoch;
    /* Who stands where this substep, read once for both of its readers: the aim, which follows a
     * copy's owner, and the flyers' anchors. Two readings of one fact drift apart, and then a
     * copy runs to the host while its flyer hovers over the owner. */
    bool                    stands[NPC_SPAWN_WORLD_SLOTS];
    bool                    seated[NPC_SPAWN_WORLD_SLOTS];
    uint32_t                body[NPC_SPAWN_WORLD_SLOTS];
    float                   point[NPC_SPAWN_WORLD_SLOTS][3];
    bool                    armed_logged;
} copies_bridge_t;

static copies_bridge_t bridge;

/* A new session or world has nobody standing yet: a body of the last one is not a place to go. */
static void forget_the_players(void)
{
    memset(bridge.stands, 0, sizeof bridge.stands);
    memset(bridge.seated, 0, sizeof bridge.seated);
    memset(bridge.body, 0, sizeof bridge.body);
    memset(bridge.point, 0, sizeof bridge.point);
}

static const char *role_word(void)
{
    if (bridge.run.role == MP_NPC_RUN_HOST) {
        return "host";
    }
    return bridge.run.role == MP_NPC_RUN_CLIENT ? "client" : "session that has ended";
}

static void publish(void)
{
    const npc_spawn_grant_record_t *record = mp_npc_copies_run_record(&bridge.run);

    if (!bridge.run.dirty) {
        return;
    }
    mp_npc_copies_run_published(&bridge.run, npc_spawn_note_publish_grants(record));
    /* And what the overlay will read out of it, whenever those numbers move. They are the whole of
     * "does this session run the copies" on the other side of the note: a host names a cap, a
     * client its world slot, and a record carrying neither reads over there as a session that runs
     * none, which greys the spawner's group with nothing on this side to say why. */
    if (bridge.said_record && bridge.said_cap == record->cap &&
        bridge.said_slot == record->own_slot && bridge.said_epoch == record->epoch) {
        return;
    }
    bridge.said_record = true;
    bridge.said_cap    = record->cap;
    bridge.said_slot   = record->own_slot;
    bridge.said_epoch  = record->epoch;
    log_info("the npc copies, the record the overlay reads: the %s, cap %u, world slot %u, "
             "epoch %u, published %u time(s)", role_word(), (unsigned)record->cap,
             (unsigned)record->own_slot, (unsigned)record->epoch,
             (unsigned)bridge.run.counters.published);
}

/* The aim forgets what it knew of a key handed to a new life. */
static void forget_the_granted(void)
{
    uint32_t k = 0;

    while (mp_npc_copies_run_next_granted(&bridge.run, &k)) {
        mp_target_forget(MP_WIRE_KEY_COPY_BASE + k);
    }
}

/* A client's overlay built a copy: it is parked before the engine can tick it, because a replica
 * that runs its own script can be killed here and then removed past the overlay. */
static void on_spawned(uint32_t index, uintptr_t actor)
{
    bool parked = mp_enemy_bind_park(actor, true);

    mp_npc_copies_run_spawned(&bridge.run, index - MP_WIRE_KEY_COPY_BASE, actor, parked);
}

/* A removal the host sent for a copy. A corpse is not a removal: the replica of this very life is
 * handed back to be made one. Anything else is the table's, which asks the overlay to remove it. */
static uintptr_t on_copy_removed(uint32_t k, uint8_t generation, uint8_t reason)
{
    const mp_npc_copies_client_t *table   = mp_npc_copies_run_client_table(&bridge.run);
    uintptr_t                     replica = 0u;

    if (table != NULL) {
        replica = mp_npc_copies_client_replica(table, k, generation);
    }
    if (reason == MP_EVENT_REMOVE_LEAVE_CORPSE) {
        bridge.corpses += replica != 0u ? 1u : 0u;
        return replica;
    }
    mp_npc_copies_run_removed(&bridge.run, k, generation, replica != 0u);
    return 0u;
}

/* Who stands where, once a substep on the host. The host's slot stands while its hero block can
 * be read, which is where the overlay put every flyer before a session could name another player;
 * a far slot stands while the bank that shows it has a pose of a player who stands and a body to
 * go to. */
static void see_the_players(void)
{
    uint8_t s;

    forget_the_players();
    bridge.stands[0] = mp_cells_hero_position(bridge.point[0]);
    for (s = 1u; s < NPC_SPAWN_WORLD_SLOTS; ++s) {
        size_t               bank   = mp_bridge_far_bank_of_slot(s);
        uint32_t             handle = 0;
        mp_bridge_far_pose_t pose;

        bridge.seated[s] = bank != 0u;
        if (bank == 0u || !mp_bridge_far_pose(bank, MP_FAR_READER_OTHER, &pose) ||
            !mp_bridge_far_pose_stands(&pose) ||
            !mp_body_exists_at(bank) ||
            !mp_bank_read_at(bank, MP_HERO_BLOCK_HACTOR, &handle, sizeof handle) || handle == 0u) {
            continue;
        }
        bridge.stands[s] = true;
        bridge.body[s]   = handle;
        memcpy(bridge.point[s], pose.position, sizeof bridge.point[s]);
    }
}

/* The body of the player of world slot `slot`, for the aim, out of this substep's reading. Only
 * far slots are asked: the host's is the engine's own answer. */
static bool owner_stands(uint8_t slot, uint32_t *body)
{
    if (slot == 0u || slot >= NPC_SPAWN_WORLD_SLOTS || !bridge.stands[slot]) {
        return false;
    }
    *body = bridge.body[slot];
    return true;
}

/* The tables, the park and the removal go to the modules that need them, or are taken back. */
static void hand_out_the_tables(void)
{
    bool client = bridge.run.role == MP_NPC_RUN_CLIENT;

    mp_enemy_sync_set_copies(mp_npc_copies_run_host_table(&bridge.run),
                             mp_npc_copies_run_client_table(&bridge.run));
    mp_target_set_copies(mp_npc_copies_run_host_table(&bridge.run),
                         client ? NULL : &owner_stands);
    mp_arena_set_spawn_listener(client ? &on_spawned : NULL);
    mp_enemy_relay_set_copy_listener(client ? &on_copy_removed : NULL);
}

void mp_npc_copies_bridge_arm(bool over_a_socket, bool as_client, uint32_t cap,
                              uint32_t corpse_seconds)
{
    mp_npc_run_role_t role = !over_a_socket ? MP_NPC_RUN_OFF
                                            : (as_client ? MP_NPC_RUN_CLIENT : MP_NPC_RUN_HOST);

    if (role == MP_NPC_RUN_OFF) {
        mp_npc_copies_bridge_disarm();
        return;
    }
    bridge.entered = false;
    forget_the_players();
    mp_npc_copies_run_arm(&bridge.run, role, (uint16_t)cap, corpse_seconds * CENSUSES_PER_SECOND,
                          MP_NPC_RUN_ORPHAN_BLOCKS);
    hand_out_the_tables();
    publish();
    if (!bridge.armed_logged) {
        bridge.armed_logged = true;
        if (as_client) {
            log_info("the npc copies run as the client; how many live at once is the host's to "
                     "allow, and this machine is told its world slot a moment later");
        } else {
            log_info("the npc copies run as the host, with the cap from NpcCopiesMax: %u",
                     (unsigned)cap);
        }
    }
}

void mp_npc_copies_bridge_disarm(void)
{
    bridge.entered = false;
    forget_the_players();
    mp_npc_copies_run_disarm(&bridge.run);
    publish();
    mp_enemy_sync_set_copies(NULL, NULL);
    mp_target_set_copies(NULL, NULL);
    mp_arena_set_spawn_listener(NULL);
    mp_enemy_relay_set_copy_listener(NULL);
}

void mp_npc_copies_bridge_new_world(void)
{
    bridge.entered = false;
    forget_the_players();
    mp_npc_copies_run_new_world(&bridge.run);
    publish();
}

void mp_npc_copies_bridge_level_begin(void)
{
    mp_lobby_setup_t setup;

    bridge.world   = mp_bridge_lobby_setup(&setup) ? setup.generation : 0u;
    bridge.entered = true;
}

static void census_visit(uintptr_t actor, void *user)
{
    uint32_t key = 0;

    (void)user;
    if (mp_enemy_bind_index(actor, &key) && mp_wire_key_is_copy(key)) {
        mp_npc_copies_run_census_saw(&bridge.run, key - MP_WIRE_KEY_COPY_BASE,
                                     mp_enemy_bind_is_live(actor, key, NULL));
    }
}

static bool broadcast(void *user, const uint8_t *bytes, size_t count)
{
    (void)user;
    return bridge.send != NULL && bridge.send(bytes, count);
}

/* A refusal for the one client that asked. One that has gone since the run checked is not waited
 * for. */
static bool send_to(void *user, uint8_t slot, const uint8_t *bytes, size_t count)
{
    size_t i;

    (void)user;
    for (i = 0; bridge.host != NULL && i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = mp_session_peer(bridge.host, i);

        if (peer != NULL && peer->state == MP_PEER_CONNECTED &&
            mp_session_slot_of_peer(i) == slot) {
            return mp_session_send_reliable(bridge.host, i, bytes, count);
        }
    }
    return true;
}

static void connections_by_slot(uint64_t *connection_of)
{
    size_t i;

    memset(connection_of, 0, NPC_SPAWN_WORLD_SLOTS * sizeof connection_of[0]);
    for (i = 0; bridge.host != NULL && i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = mp_session_peer(bridge.host, i);
        uint8_t          slot = mp_session_slot_of_peer(i);

        if (peer != NULL && peer->state == MP_PEER_CONNECTED && slot < NPC_SPAWN_WORLD_SLOTS) {
            connection_of[slot] = peer->connection_id;
        }
    }
}

/* The point each world slot's copies fly to, for the overlay's flyers: the
 * host's own from its hero block, which is where the overlay put every flyer before a session
 * could name another player, and a far player's from the pose resolved for them this substep. Only
 * the host's copies think; a client's are parked, so a client publishes none. */
static void publish_anchors(void)
{
    npc_spawn_anchor_record_t record;
    uint16_t                  stands = 0;
    uint8_t                   s;

    for (s = 0; s < NPC_SPAWN_WORLD_SLOTS; ++s) {
        if (bridge.stands[s]) {
            stands = (uint16_t)(stands | (1u << s));
        }
        if (s != 0u && bridge.seated[s]) {
            if (bridge.stands[s]) {
                ++bridge.anchors_own;
            } else {
                ++bridge.anchors_to_host;
            }
        }
    }
    mp_npc_copies_anchors((const float(*)[3])bridge.point, stands,
                          mp_npc_copies_run_record(&bridge.run)->epoch, &record);
    if (npc_spawn_note_publish_anchors(&record)) {
        ++bridge.anchors_said;
    } else {
        ++bridge.anchors_refused;
    }
}

void mp_npc_copies_bridge_substep(bool joined, bool (*send)(const uint8_t *bytes, size_t count),
                                  mp_session_t *host)
{
    mp_npc_copies_run_in_t in;
    uint64_t               connection_of[NPC_SPAWN_WORLD_SLOTS];
    uint16_t               level    = 0;
    uint32_t               capacity = 0;
    uint32_t               chain;
    bool                   complete = false;

    if (bridge.run.role == MP_NPC_RUN_OFF) {
        return;
    }
    bridge.send = send;
    bridge.host = bridge.run.role == MP_NPC_RUN_HOST ? host : NULL;

    /* The census first: the answers read below and the decisions after them see the pool as the
     * overlay's last frame left it. */
    mp_npc_copies_run_census_begin(&bridge.run);
    chain = mp_enemy_bind_walk_whole(&census_visit, NULL, &complete, &capacity);
    mp_npc_copies_run_census_end(&bridge.run, complete, chain, capacity);

    connections_by_slot(connection_of);
    memset(&in, 0, sizeof in);
    in.joined        = joined;
    /* A level no level begin announced is one being left: the fade after a session's end, or
     * the last substeps before a load. */
    in.level_known = mp_enemy_spawn_level_identity(&level);
    if (in.level_known && !bridge.entered) {
        in.level_known = false;
        ++bridge.unentered;
    }
    in.level         = level;
    in.world         = bridge.world;
    in.own_slot      = mp_bridge_drain_slot_told() ? mp_bridge_drain_my_slot() : 0u;
    in.can_park      = mp_arena_installed() && mp_enemy_bind_installed();
    in.connection_of = connection_of;
    in.now_ms        = mp_wallclock_ms();
    in.broadcast     = &broadcast;
    in.send_to       = &send_to;
    if (npc_spawn_note_read_wishes(&bridge.wishes)) {
        in.wishes = &bridge.wishes;
    } else {
        ++bridge.reads_failed;   /* no overlay, or a torn read that succeeds next frame */
    }
    (void)mp_npc_copies_run_substep(&bridge.run, &in);
    publish();
    forget_the_granted();
    if (bridge.run.role == MP_NPC_RUN_HOST) {
        see_the_players();
        publish_anchors();
    }
}

bool mp_npc_copies_bridge_take(uint8_t slot, uint64_t connection, const uint8_t *note,
                               size_t bytes)
{
    bool ours = mp_npc_copies_run_take(&bridge.run, slot, connection, note, bytes,
                                       mp_wallclock_ms());

    if (ours) {
        forget_the_granted();
    }
    return ours;
}

static uint32_t refused_total(const mp_npc_copies_counters_t *c)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_NPC_COPIES_REASONS; ++i) {
        total += c->refused[i];
    }
    return total;
}

static uint32_t refused_here(const mp_npc_copies_run_counters_t *r)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_NPC_COPIES_REASONS; ++i) {
        total += r->refused_here[i];
    }
    return total;
}

static void report_host(void)
{
    const mp_npc_copies_counters_t     *h = &bridge.run.host.counters;
    const mp_npc_copies_run_counters_t *r = &bridge.run.counters;

    log_info("the npc copies (the host): %u wish(es) from its overlay, %u from clients (%u in the "
             "lobby), %u of another epoch; %u granted, %u refused (%u at the cap, %u pool, %u no "
             "free key, %u another level or world, %u no builder, %u unsound, %u too fast); %u "
             "announced, %u repeated; %u never seen alive, %u built dead, %u died unannounced; "
             "%u removal(s), %u with nothing to remove; %u owner change(s); %u ridden; %u "
             "recancelled, %u stuck; %u corpse(s) swept; %u foreign, %u double key(s) (must be "
             "0), %u incomplete walk(s)",
             (unsigned)r->wishes_read, (unsigned)r->wire_taken, (unsigned)r->wire_in_lobby,
             (unsigned)r->wishes_stale, (unsigned)h->granted, (unsigned)refused_total(h),
             (unsigned)h->refused[NPC_SPAWN_REFUSED_CAP],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_POOL],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_NO_KEY],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_NO_LEVEL],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_NO_BUILDER],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_UNSOUND],
             (unsigned)h->refused[NPC_SPAWN_REFUSED_TOO_FAST], (unsigned)r->announced,
             (unsigned)r->repeated, (unsigned)h->never_seen, (unsigned)h->built_dead,
             (unsigned)h->died_unannounced, (unsigned)h->removals, (unsigned)h->removals_empty,
             (unsigned)h->owners_changed, (unsigned)h->ridden, (unsigned)h->recancelled,
             (unsigned)h->stuck, (unsigned)h->corpses_swept, (unsigned)h->foreign,
             (unsigned)h->doubles, (unsigned)h->incomplete);
    log_info("the npc copies (the host), the notes: %u grant(s) its overlay could not build, "
             "answers %u unmatched, %u open, %u lost, %u to refusals; the record published %u "
             "time(s), %u refused, %u substep(s) with the ring full; %u refusal(s) sent to "
             "clients, %u dropped for an asker gone, %u overflowed, %u too-fast unsaid; %u send(s) "
             "the channel refused, %u grant(s) failed, %u torn, %u of the wrong role, %u "
             "substep(s) without an overlay's record, %u with a level never begun; refusals to "
             "its own overlay %u (%u no builder, %u unsound, %u another level), %u overflowed, "
             "%u serial(s) left",
             (unsigned)h->not_built, (unsigned)h->answers_unmatched, (unsigned)h->answers_open,
             (unsigned)h->answers_lost, (unsigned)r->answers_to_refusals, (unsigned)r->published,
             (unsigned)r->publish_refused, (unsigned)r->ring_full,
             (unsigned)r->wire_refusals_sent, (unsigned)r->wire_refusals_dropped,
             (unsigned)r->wire_overflow, (unsigned)r->too_fast_quiet, (unsigned)r->unsent,
             (unsigned)r->grant_failed, (unsigned)r->wire_torn, (unsigned)r->wire_wrong_role,
             (unsigned)bridge.reads_failed, (unsigned)bridge.unentered, (unsigned)refused_here(r),
             (unsigned)r->refused_here[NPC_SPAWN_REFUSED_NO_BUILDER],
             (unsigned)r->refused_here[NPC_SPAWN_REFUSED_UNSOUND],
             (unsigned)r->refused_here[NPC_SPAWN_REFUSED_NO_LEVEL], (unsigned)r->own_overflow,
             (unsigned)r->serials_spent);
    log_info("the npc copies (the host), the flyers' anchors: published %u time(s), %u refused "
             "by the note; a seated player's slot %u time(s) over that player and %u time(s) over "
             "the host, for a player absent or dead",
             (unsigned)bridge.anchors_said, (unsigned)bridge.anchors_refused,
             (unsigned)bridge.anchors_own, (unsigned)bridge.anchors_to_host);
}

static void report_client(void)
{
    const mp_npc_copies_client_counters_t *c = &bridge.run.client.counters;
    const mp_npc_copies_run_counters_t    *r = &bridge.run.counters;

    log_info("the npc copies (a client): %u wish(es) sent, %u held for the channel, %u of another "
             "epoch; %u entries taken (%u repeats, %u waited), %u kept back with no overlay, %u "
             "with no parking, %u of another level or world; %u built, %u could not build, %u "
             "parked, %u park(s) failed, %u build(s) under a key not handed here, %u replica(s) "
             "handed back to be made a corpse; given up: %u orphan, %u removed, %u removed "
             "unbuilt, %u released, %u another life, %u unparked; %u lost here",
             (unsigned)r->wishes_sent, (unsigned)r->wishes_held, (unsigned)r->wishes_stale,
             (unsigned)c->entries, (unsigned)c->repeats, (unsigned)c->waited,
             (unsigned)r->entries_no_panel, (unsigned)r->entries_no_parking,
             (unsigned)r->entries_stale, (unsigned)c->built, (unsigned)c->not_built,
             (unsigned)r->parked, (unsigned)r->park_failed, (unsigned)r->spawned_unknown,
             (unsigned)bridge.corpses,
             (unsigned)c->given_up[MP_NPC_GIVE_UP_ORPHAN],
             (unsigned)c->given_up[MP_NPC_GIVE_UP_DESPAWN],
             (unsigned)c->given_up[MP_NPC_GIVE_UP_TOMBSTONE],
             (unsigned)c->given_up[MP_NPC_GIVE_UP_RELEASE],
             (unsigned)c->given_up[MP_NPC_GIVE_UP_GENERATION],
             (unsigned)c->given_up[MP_NPC_GIVE_UP_PARKING], (unsigned)c->lost_here);
    log_info("the npc copies (a client), the notes: %u refusal(s) from the host (%u unmatched, %u "
             "not for this slot), %u said here, %u overflowed; %u wish(es) expired, %u pushed "
             "out; answers %u unmatched, %u open, %u lost, %u to refusals; the record published "
             "%u time(s), %u refused, %u substep(s) with the ring full; %u torn, %u of the wrong "
             "role, %u substep(s) without an overlay's record, %u with a level never begun; said "
             "here before asking: %u another level or none, %u no parking, %u unsound; %u "
             "overflowed, %u serial(s) left, %u dut(y/ies) it could not write",
             (unsigned)c->refusals, (unsigned)c->refusals_unmatched,
             (unsigned)c->refusals_not_mine, (unsigned)c->refusals_said_here,
             (unsigned)c->refusals_overflow, (unsigned)c->wishes_expired,
             (unsigned)c->wishes_pushed_out, (unsigned)c->answers_unmatched,
             (unsigned)c->answers_open, (unsigned)c->answers_lost,
             (unsigned)r->answers_to_refusals, (unsigned)r->published,
             (unsigned)r->publish_refused, (unsigned)r->ring_full, (unsigned)r->wire_torn,
             (unsigned)r->wire_wrong_role, (unsigned)bridge.reads_failed,
             (unsigned)bridge.unentered, (unsigned)r->refused_here[NPC_SPAWN_REFUSED_NO_LEVEL],
             (unsigned)r->refused_here[NPC_SPAWN_REFUSED_NO_PARKING],
             (unsigned)r->refused_here[NPC_SPAWN_REFUSED_UNSOUND], (unsigned)r->own_overflow,
             (unsigned)r->serials_spent, (unsigned)r->duties_dropped);
}

void mp_npc_copies_bridge_report(void)
{
    if (bridge.run.role == MP_NPC_RUN_HOST) {
        report_host();
    } else if (bridge.run.role == MP_NPC_RUN_CLIENT) {
        report_client();
    }
}
