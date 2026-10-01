/* mp_bridge.c: host and client in one process, joined over the lossy loopback, carrying the game.
 *
 * Three shapes of one pump (loopback, UDP host, UDP client) are kept together on purpose: the mode
 * gates are two-line guards inside shared halves, and splitting by role would copy the pump into
 * three files that must never drift. What has left is what had its own state: the puppet's placing
 * and dressing (mp_puppet.c), the render-time sampling (mp_interp.c, mp_timeline.c), the world's
 * two ends and its snapshots (mp_bridge_world.c), the loopback's command path
 * (mp_bridge_command.c), and putting the transport up and down (mp_bridge_install.c).
 *
 * SIZE NOTE: over 600 lines. The reason is the three shapes above: each half carries the mode gates
 * of all three, and why each gate sits where it does is prose the maintainer needs in front of the
 * code. The install, the sockets and taking the transport down again left for mp_bridge_install.c,
 * the cut this note had named, and mp_bridge_shared.h is its price. The statement a join is judged
 * by, which read the sessions and nothing of the three halves, left for mp_bridge_statement.c after
 * it. The next seam is the report's gathering at the end of this file, which copies the state into
 * a record and touches no half either.
 *
 * One time base. The sessions and the channels run on the wall clock, which keeps counting
 * through a level load and a window drag; the far body runs on the substep count through the
 * timeline, which is the honest clock for a body that moves once per substep. Nothing here reads
 * the operating system's tick count: its sixteen millisecond steps were the stutter of the third
 * field run.
 *
 * THREE entry points and two pumps, and which one a thing belongs in is the whole design. The
 * pre-tick half counts the substep and stamps the events, then receives and decodes what the far
 * side sent. The post-tick half APPLIES: it takes in, drains, and places the puppet where the
 * collision pass that just ran can see it. The substep end SENDS, because it is the last moment
 * anything in the substep can still change the world, and everything a hit produced would
 * otherwise leave on the next substep. Between substeps, and through a level load in which no
 * substep runs, the
 * frame hook and a thread timer receive and service with nothing to send, so the timeouts and
 * the keepalives keep their meaning; both turn back while a tick half or a bank window runs.
 *
 * Everything sized like a session lives in static storage. Two sessions are over seven megabytes
 * between them and the loopback ring another three hundred kilobytes, far past any stack.
 */
#include "mp_bridge.h"
#include "mp_bridge_shared.h"

#include "mp_actions.h"
#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_command.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_roster.h"
#include "mp_enemy_relay.h"
#include "mp_npc_copies_bridge.h"
#include "mp_npc_shot_relay.h"
#include "mp_target.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_burst.h"
#include "mp_enemy_sync.h"
#include "mp_pickup_relay.h"
#include "mp_range_gate.h"
#include "mp_stopwatch.h"
#include "mp_dialog_relay.h"
#include "mp_quest_relay.h"
#include "mp_scratch_bind.h"
#include "mp_hit_relay.h"
#include "mp_level_state.h"
#include "mp_scene_client.h"
#include "mp_script_sound.h"
#include "mp_scene_host.h"
#include "mp_scratch_wire.h"
#include "mp_bridge_world.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_statement.h"
#include "mp_cells.h"
#include "mp_crate.h"
#include "mp_bridge_report.h"
#include "mp_events.h"
#include "mp_loopback.h"
#include "mp_puppet.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_udp.h"
#include "mp_wallclock.h"
#include "mp_wire.h"
#include "mp_world.h"
#include "mp_world_apply.h"
#include "mp_world_event.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How long the in-process handshake may take before the watchpost calls it a finding. Generous:
 * the join itself runs over the lossy link. */
#define BRIDGE_JOIN_DEADLINE_SUBSTEPS 640u

static mp_bridge_state_t     bridge;

static mp_loopback_t         bridge_net;
static mp_udp_t              bridge_udp;
static mp_relay_transport_t  bridge_relay;
static mp_session_t          bridge_host;
static mp_session_t          bridge_client;
static mp_transport_t        bridge_host_transport;
static mp_transport_t        bridge_client_transport;

/* The loopback's command path, its own module; the far bodies' histories are mp_bridge_far's. */
static mp_bridge_command_t   bridge_command;

bool mp_bridge_installed(void)
{
    return mp_armed_transport();
}

void mp_bridge_enable_spin(void)
{
    mp_bridge_command_enable_spin(&bridge_command);
}

void mp_bridge_set_auto_lag(bool enabled)
{
    mp_bridge_far_set_auto_lag(enabled);
    log_info("the far body's buffer is %s: %s",
             enabled ? "measured from the stream" : "held at the value it was built with",
             enabled ? "a clean wire settles it low, a gap in the stream raises it at once"
                     : "every run pays the same three ticks, whatever the wire does");
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* Everything that indexes the far side's ticks or latches what was last applied from it, run on
 * every arrival: a restarted peer counts from tick one, which an old history refuses. */
void mp_bridge_reset_peer_state(void)
{
    mp_bridge_world_reset();
    mp_bridge_far_reset();   /* every far body's history fresh, the run's lag choice kept */
    mp_bridge_command_reset(&bridge_command);
    mp_puppet_reset_all();
    mp_actions_clear();
    mp_world_clear();
}

const mp_bridge_shared_t *mp_bridge_shared(void)
{
    static const mp_bridge_shared_t shared = {
        &bridge, &bridge_net, &bridge_udp, &bridge_relay, &bridge_host, &bridge_client,
        &bridge_host_transport, &bridge_client_transport
    };

    return &shared;
}

void mp_bridge_set_game_mode(uint8_t mode, const char *name)
{
    bridge.game_mode = mode;
    /* The name is HANDED IN rather than derived. What a mode is called is the feature's knowledge,
     * and a bridge that looked it up would have to be edited every time a mode is added. It is a
     * string literal from the caller and outlives the run. */
    bridge.game_mode_name = (name != NULL) ? name : "unnamed";
    /* Both, because which of the two is live depends on the role and the mode has to be named on
     * whichever one speaks. Setting the other costs a byte. */
    mp_session_set_mode(&bridge_host, mode);
    mp_session_set_mode(&bridge_client, mode);
    mp_bridge_lobby_note_mode(mode);
}

uint32_t mp_bridge_host_endpoint(void)              { return bridge.host_endpoint; }
void mp_bridge_set_host_endpoint(uint32_t endpoint)  { bridge.host_endpoint = endpoint; }
void mp_bridge_clear_connect_pending(void)           { bridge.connect_pending = false; }

void mp_bridge_set_player_name(const char *name)
{
    mp_bridge_roster_set_name(name);
    mp_session_set_name(&bridge_host, mp_bridge_roster_name());
    mp_session_set_name(&bridge_client, mp_bridge_roster_name());
}

bool mp_bridge_joined(void)
{
    return mp_armed_transport() && bridge.joined;
}

void mp_bridge_note_pump_timer(uintptr_t timer_id)
{
    /* One timer for a transport. A second arming of the same transport used to leave the first
     * timer running for the life of the process, beside the new one. */
    if (bridge.pump_timer != 0u && bridge.pump_timer != timer_id) {
        KillTimer(NULL, (UINT_PTR)bridge.pump_timer);
    }
    bridge.pump_timer = timer_id;
}

/* ==============================================================================================
 * The pump: receive and service, on the one clock.
 * ============================================================================================ */

static void receive_all(void)
{
    uint32_t now = mp_wallclock_ms();

    switch (bridge.mode) {
    case MP_BRIDGE_LOOPBACK:
        mp_loopback_pump(&bridge_net, now);
        mp_session_receive(&bridge_host, now);
        mp_session_receive(&bridge_client, now);
        break;
    case MP_BRIDGE_UDP_HOST:
        mp_session_receive(&bridge_host, now);
        break;
    case MP_BRIDGE_UDP_CLIENT:
    default:
        mp_session_receive(&bridge_client, now);
        break;
    }
}

static void service_all(void)
{
    uint32_t now = mp_wallclock_ms();

    switch (bridge.mode) {
    case MP_BRIDGE_LOOPBACK:
        mp_session_service(&bridge_host, now);
        mp_session_service(&bridge_client, now);
        break;
    case MP_BRIDGE_UDP_HOST:
        mp_session_service(&bridge_host, now);
        break;
    case MP_BRIDGE_UDP_CLIENT:
    default:
        mp_session_service(&bridge_client, now);
        break;
    }
}

/* Between substeps. Nothing is set as a payload here, so a service sends only a keepalive after
 * a stretch of silence; the point is that the timeouts and the far side's keepalives are seen
 * while no substep runs. The loopback is pumped here like the UDP roles: its far end is this
 * same process, but its sessions count the wall clock like any other, and a menu pause longer
 * than the connected timeout would otherwise drop both of its ends and never bring them back. A
 * pump that finds a tick half or a bank window running turns back: the sessions are not written
 * from two places at once, and the count says whether the engine ever
 * dispatched the timer inside a substep, which was the assumption this guard measures.
 *
 * The drain runs here too, and it is the half that was missing: a pump that only receives fills
 * a ring nothing empties. What it takes is counted apart, so the report says whether this ever
 * ran rather than only that it was installed.
 *
 * Since the sending half moved to the substep end, the guard no longer covers the whole substep.
 * The gap between the task half finishing and the substep end starting is inside a substep and
 * has this flag clear, so a pump landing there would be counted as an ordinary one and would call
 * itself a pump between substeps, which it would not be.
 *
 * The gap is left open deliberately. Nothing that reaches this function can land in it: the frame
 * hook is a detour on the end of rendering and the timer is dispatched by the window message
 * pump, and neither runs while the substep loop is turning. Closing it would mean a second flag
 * spanning both halves, and that flag's failure mode is worse than the gap it removes, because
 * the halves are no longer guaranteed to come in pairs: one missing substep end would leave it
 * set and kill the idle pump for good, in exactly the situation where the pump is the only thing
 * still servicing the session. */
void mp_bridge_pump_idle(mp_bridge_pump_source_t source)
{
    uint32_t taken;

    if (!mp_armed_transport()) {
        return;
    }
    if (bridge.in_tick) {
        ++bridge.pumps_refused;
        if (!bridge.pump_refusal_logged) {
            bridge.pump_refusal_logged = true;
            log_warning("an idle pump (%s) ran inside a tick half and turned back; later ones "
                        "are counted", source == MP_BRIDGE_PUMP_TIMER ? "timer" : "frame");
        }
        return;
    }
    if (source == MP_BRIDGE_PUMP_TIMER) {
        ++bridge.timer_pumps;
        /* A timer pump with no substep for a second or more is a load, a pause or a film going on
         * answering the wire. Whether it does is what decides whether a host that holds messages
         * for this side ever sends it away, so it is counted rather than assumed. */
        if (bridge.substep != 0u && mp_wallclock_ms() - bridge.last_substep_ms >= 1000u) {
            uint32_t stalled = mp_wallclock_ms() - bridge.last_substep_ms;

            ++bridge.stall_pumps;
            if (bridge.longest_pumped_stall_ms < stalled) {
                bridge.longest_pumped_stall_ms = stalled;
            }
        }
    } else {
        ++bridge.frame_pumps;
    }
    receive_all();
    taken = mp_bridge_drain_between_substeps(&bridge.drain);
    if (taken != 0u) {
        ++bridge.idle_drains;
        bridge.idle_payloads += taken;
    }
    service_all();
    mp_bridge_lobby_tick(bridge.mode == MP_BRIDGE_UDP_HOST, bridge.content);
}

static void announce_join(void)
{
    log_info("the bridge joined on substep %u: the handshake crossed the %s",
             (unsigned)bridge.substep,
             bridge.mode == MP_BRIDGE_LOOPBACK ? "lossy loopback" : "wire");
    /* The two instances are pixel-identical twins otherwise, so the caption says which is which. */
    if (bridge.mode != MP_BRIDGE_LOOPBACK) {
        mp_bridge_report_caption(bridge.mode == MP_BRIDGE_UDP_HOST);
    }
}

/* The live joined state, not a latch: a peer can drop and come back. On every arrival the command
 * sink and the acknowledged baseline start over, because a restarted client has neither. An
 * arrival is counted by the sessions' joins, not by the edge of the joined state: a client that
 * restarts and is replaced in place by the host's parallel handshake never shows an edge, and
 * its ticks start over all the same. A host that still has somebody else starts nothing global
 * over: the drain starts the newcomer's own bank over on its connection, and the rest is the
 * others' state. */
static bool joined(void)
{
    bool     now_joined;
    uint32_t joins = mp_session_joins(&bridge_host) + mp_session_joins(&bridge_client);

    switch (bridge.mode) {
    case MP_BRIDGE_LOOPBACK:
        now_joined = mp_session_is_connected(&bridge_client) &&
                     mp_session_peer_count(&bridge_host) == 1u;
        break;
    case MP_BRIDGE_UDP_HOST:
        now_joined = mp_session_peer_count(&bridge_host) >= 1u;
        break;
    case MP_BRIDGE_UDP_CLIENT:
    default:
        now_joined = mp_session_is_connected(&bridge_client);
        break;
    }
    if (now_joined && joins != bridge.joins_seen) {
        bridge.joins_seen = joins;
        if (bridge.mode != MP_BRIDGE_UDP_HOST || !bridge.joined ||
            mp_session_peer_count(&bridge_host) == 1u) {
            mp_bridge_reset_peer_state();
        }
        announce_join();
        mp_bridge_lobby_on_join();   /* a client tells the host its lobby line again */
    }
    if (now_joined != bridge.joined) {
        bridge.joined = now_joined;
        if (!now_joined) {
            log_warning("the bridge lost its peer on substep %u; a client gives its host up, "
                        "a host waits", (unsigned)bridge.substep);

            /* And the replicas are let go at once. A parked actor is stepped over before its tick
             * AND before the removal decision, so one still parked with no wire behind it stands
             * still for the rest of the level. Losing the peer is exactly the moment the local
             * simulation should take its enemies back. */
            mp_enemy_sync_release_all();
        }
    }
    /* A UDP peer may legitimately arrive minutes late (the other machine is still in a menu), so
     * only the loopback, whose far end is this same process, has a deadline. */
    if (!now_joined && bridge.mode == MP_BRIDGE_LOOPBACK &&
        bridge.substep > BRIDGE_JOIN_DEADLINE_SUBSTEPS && !bridge.join_failed_logged) {
        bridge.join_failed_logged = true;
        log_error("%u substeps and the in-process handshake has not completed, which the loss "
                  "dial cannot explain; the bridge is idle", (unsigned)bridge.substep);
    }
    return now_joined;
}

/* ==============================================================================================
 * The two tick halves.
 * ============================================================================================ */

/* How one reliable message goes out, for this file's own sending and for the modules that decide
 * what to send for themselves. The loopback has no far player to perform anything, so it reports
 * success without sending and its queues drain all the same. */
static bool send_reliable(const uint8_t *bytes, size_t count)
{
    mp_session_t *session = bridge.mode == MP_BRIDGE_UDP_HOST ? &bridge_host : &bridge_client;

    if (bridge.mode == MP_BRIDGE_LOOPBACK) {
        return true;
    }
    /* EVERY peer, not peer zero: a host with three clients used to send every shot, door, death
     * and pickup to the first and nothing to the other two. Unsent means it reached nobody. */
    if (mp_session_broadcast_reliable(session, bytes, count) == 0u) {
        ++bridge.events_unsent;   /* the channel is full; the event waits, the ring ages it */
        return false;
    }
    ++bridge.events_sent;
    return true;
}

/* A hit the host saw on a far player concerns the machine of that player alone. Sent or held, never
 * dropped for a full channel, because nothing repeats a hit, and never overtaking what that peer is
 * owed already (mp_session_send_or_hold). */
static mp_hit_relay_addressed_t send_to_slot(uint8_t slot, const uint8_t *bytes, size_t count)
{
    size_t           peer = 0;
    const mp_peer_t *seat;

    if (!mp_session_peer_of_slot(slot, &peer)) {
        return MP_HIT_ADDRESSED_NO_PEER;
    }
    seat = mp_session_peer(&bridge_host, peer);
    if (seat == NULL || seat->state != MP_PEER_CONNECTED) {
        return MP_HIT_ADDRESSED_NO_PEER;
    }
    return mp_session_send_or_hold(&bridge_host, peer, bytes, count) ? MP_HIT_ADDRESSED_SENT
                                                                    : MP_HIT_ADDRESSED_REFUSED;
}

/* The local player's moments since the last substep, and then the map's own, which the map puts
 * out itself because it knows which of its messages may be dropped when the channel is full and
 * which must wait. The body's moments go first: they are what a watching player sees. */
static void send_events(void)
{
    mp_event_t event;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];

    while (mp_actions_peek(&event)) {
        size_t bytes;

        event.source_slot = (uint8_t)bridge.drain.my_slot;   /* whose moment this is */
        bytes = mp_event_encode(&event, buffer, sizeof buffer);
        if (bytes != 0u && !send_reliable(buffer, bytes)) {
            return;
        }
        (void)mp_actions_pop(&event);
    }
    mp_world_send(bridge.substep, &send_reliable);
    /* The push blocks: a host's note and falls, a client's wishes. */
    mp_crate_send(bridge.substep, &send_reliable);
    /* And what the host's scripts switched on its level, from the side that owns the level, and
     * the scene it plays for everybody. */
    if (bridge.mode == MP_BRIDGE_UDP_HOST) {
        mp_level_state_send(bridge.substep, mp_world_apply_generation(), &send_reliable);
        mp_scene_host_send(bridge.substep, &send_reliable);
    }

    /* The campaign last: the body's moments are what a watching player sees, and a bank sweep
     * would otherwise fill the channel ahead of them for a quarter of a second at a time. */
    mp_scratch_wire_tick(bridge.substep, &send_reliable);

    /* And the two smallest, last, in the order a player meets them: the shared story a
     * conversation earns, and the answer the host gave. Neither may wait behind a bank sweep. */
    mp_quest_relay_tick(bridge.substep, &send_reliable);
    mp_dialog_relay_tick();

    /* The five with no tick of their own. Each reports from inside an engine call, the hit from
     * the contact handler, the removal from the enemy tick, the pickup from the player's own
     * pickup, the conversation from the engine's speak entry, an NPC's bolt from the shot
     * entry, so what they need from here is only somewhere to send, handed over once a
     * substep. */
    mp_hit_relay_set_send(&send_reliable);
    mp_hit_relay_set_send_to_slot(bridge.mode == MP_BRIDGE_UDP_HOST ? &send_to_slot : NULL);
    mp_hit_relay_set_slot(bridge.drain.my_slot);
    mp_enemy_relay_set_send(&send_reliable);
    mp_npc_shot_relay_set_send(&send_reliable);
    mp_pickup_relay_set_send(&send_reliable);
    mp_pickup_relay_set_slot((uint8_t)bridge.drain.my_slot);
    mp_dialog_relay_set_send(&send_reliable);
}

/* Where the far body of bank `bank` stands on this machine, out of that bank's block. A substep
 * old, which for a radius test is nothing. None while its player's pose is another world's: the
 * body stands where the last pose of this world left it, and a player still in another level wakes
 * nothing in this one. */
static bool far_body_position(size_t bank, float out[3])
{
    mp_bridge_far_pose_t pose;

    if (!mp_body_exists_at(bank) || !mp_bridge_far_pose(bank, MP_FAR_READER_RANGE_GATE, &pose)) {
        return false;
    }
    return mp_bank_read_at(bank, MP_HERO_BLOCK_POS, out, 3u * sizeof(float));
}

void mp_bridge_tick_pre(void)
{
    if (!mp_armed_transport()) {
        return;
    }
    mp_stopwatch_enter_substep();   /* and the engine's tasks ahead of this one end */
    mp_stopwatch_enter(MP_WATCH_TICK_PRE);
    bridge.in_tick = true;
    ++bridge.substep;
    bridge.last_substep_ms = mp_wallclock_ms();
    mp_actions_set_tick(bridge.substep);   /* every moment caught in this substep carries it */
    mp_world_set_tick(bridge.substep);
    mp_npc_shot_relay_set_tick(bridge.substep);
    mp_enemy_burst_set_tick(bridge.substep);
    bridge.drain.tick = bridge.substep;    /* and every moment a host passes on */

    /* The host's map state, before anything in this tick reads a mover. The engine's own player
     * slot runs earlier still, so what keeps a corrected platform from moving the player standing
     * on it is the settling integration the applier makes rather than this placement. */
    mp_world_apply_pending();
    mp_crate_apply_pending(bridge.substep);
    mp_bridge_statement_learn(true);

    /* Which level this substep runs in, told to the enemy sync once per substep: a block from
     * another level is then refused rather than applied to bodies that happen to share an index,
     * and no level open here means no block is applied and none is built. Read here, inside a
     * substep, because a substep is the one moment the world is certainly whole. */
    {
        uint16_t level = 0;
        bool     known = mp_enemy_spawn_level_identity(&level);

        mp_enemy_sync_set_level(known, level);
        mp_level_state_set_level(known, level);
    }
    mp_pickup_relay_tick(bridge.substep);

    if (bridge.mode == MP_BRIDGE_LOOPBACK) {
        mp_bridge_command_author(&bridge_command, &bridge_client);
    }
    receive_all();
    if (joined()) {
        /* The receiving half of the client-to-host direction. The loopback keeps the command
         * path, which is what the in-process acceptance proved; the UDP host stores the
         * client's state for the sampling after its own tick. */
        if (bridge.mode == MP_BRIDGE_LOOPBACK) {
            mp_bridge_command_drain(&bridge_command, &bridge_host);
        } else if (bridge.mode == MP_BRIDGE_UDP_HOST) {
            mp_bridge_drain_client_state(&bridge.drain);
        }
    }
    mp_stopwatch_leave(MP_WATCH_TICK_PRE);
    bridge.in_tick = false;
}

void mp_bridge_tick_post(void)
{
    if (!mp_armed_transport()) {
        return;
    }
    mp_stopwatch_enter(MP_WATCH_TICK_POST);
    bridge.in_tick = true;
    bridge.tick_ran_this_substep = true;

    /* The sending half now hangs off a message from the engine rather than off this task, and a
     * message that never arrives would make this whole feature mute: nothing would leave, and the
     * only symptom would be a peer that never sees anything. So the absence is measured against
     * the one clock that is certainly running, this task's own substep counter, and said once.
     *
     * A second of substeps is the threshold because the message is sent once per substep by the
     * engine's own runner: after thirty two of them without one, it is not late, it is absent. */
    if (bridge.substep > 32u && bridge.substep_ends == 0u && !bridge.substep_end_missing_logged) {
        bridge.substep_end_missing_logged = true;
        log_error("%u substeps have run and the end of a substep has not been reached once, so "
                  "NOTHING is being sent: the head module node is not hearing the engine's "
                  "substep message. The far side will see this side stand still",
                  (unsigned)bridge.substep);
    }

    /* THE APPLYING HALF, and it stays in the task on purpose.
     *
     * The engine runs the substep as an ascending list of task slots and then broadcasts one
     * message to its modules, and this task is slot 4 while the collision is a listener on that
     * message. So the task is EARLIER than the end of the substep, and everything that has to be
     * seen by something inside this substep belongs here: the puppet is placed before the pair
     * pass runs, and a mover that comes due opens before anything collides with it.
     *
     * Moving this half to the end would not have gained a single consumer a tick and would have
     * cost the collision one, because the two enemy and player slots run BEFORE this task either
     * way. The sending half is the one with something to gain, and it has moved. */
    receive_all();
    if (joined()) {
        mp_bridge_drain_reliable_notes(&bridge.drain);
        if (bridge.mode == MP_BRIDGE_UDP_HOST) {
            mp_bridge_drain_client_state(&bridge.drain);
            /* The host's level sees every player: each far body's activation pass, from inside
             * the substep like the engine's own. */
            mp_enemy_relay_set_far_body(&far_body_position);
            mp_range_gate_set_far_body(&far_body_position);
            mp_pickup_relay_set_far_body(&far_body_position);
            /* The far bodies the range gate will be asked about until the next substep. Once
             * here, not once per call: the engine's activation scan asks the gate for every
             * eligible placement, once a FRAME, and a far body moves once a substep. */
            mp_stopwatch_enter(MP_WATCH_RANGE_GATE);
            mp_range_gate_refresh();
            mp_stopwatch_leave(MP_WATCH_RANGE_GATE);
            mp_enemy_relay_tick(bridge.substep);
            mp_target_tick(bridge.substep);
            mp_scene_host_tick(bridge.substep);   /* the doors of this substep are heard */
        } else {
            mp_bridge_drain_host_world(&bridge.drain);
            /* The bodies the host's blocks named and this machine has not got, created NOW and
             * not where the block was decoded: the same drain runs from the idle pump, which
             * runs from the message loop and can find the engine halfway through a level load.
             * Here a substep is running, so the world is whole, and the collision pass that
             * follows this task already sees the new replica, parked and in place. */
            (void)mp_enemy_sync_flush();
            (void)mp_level_state_flush();
            (void)mp_enemy_sync_spawn_pending();
            (void)mp_world_event_flush();   /* behind the bodies: an event needs its replica */
            mp_script_sound_flush();        /* the music the host holds and the loops it wants */
            mp_scene_client_tick(bridge.substep);   /* after the notes of this substep */
        }
        /* Both UDP roles place their second body from the peer's own state; only the loopback,
         * which proves the command path, does not. */
        if (bridge.mode != MP_BRIDGE_LOOPBACK) {
            mp_bridge_drain_place_puppet(&bridge.drain);
            /* After the puppet, on the render tick the puppet has just resolved, and outside the
             * bank window it opened: a mover belongs to the map and nothing about it is banked. */
            mp_world_run_due();
            /* A host pushes its clients' blocks here, where the body test sees every puppet
             * where it stands and no bank window is open. */
            mp_crate_run_due(bridge.substep);
            /* And a client's copies of the host's NPC bolts, on the same render tick. */
            mp_npc_shot_relay_run_due(MP_BRIDGE_DRAIN_FAR_BANK);
        }
    } else if (bridge.mode == MP_BRIDGE_UDP_HOST) {
        /* A host whose clients have all gone still steps its scene: a door it hears then begins
         * nothing, and a hold that stood when the last one left falls with its fade given back.
         * Stepped only while somebody was joined, it held its scene's actor for good. */
        mp_scene_host_tick(bridge.substep);
    }
    mp_stopwatch_leave(MP_WATCH_TICK_POST);
    bridge.in_tick = false;
}

void mp_bridge_substep_end(void)
{
    if (!mp_armed_transport()) {
        return;
    }
    mp_stopwatch_enter(MP_WATCH_SUBSTEP_END);
    bridge.in_tick = true;
    ++bridge.substep_ends;

    /* The two halves now hang off different things: this one off a message the engine broadcasts,
     * the other off a task slot. They are not guaranteed to come in pairs. The scheduler turns
     * back without running a single task when its slot count is zero, which is what a level
     * teardown leaves behind, while the broadcast goes out regardless; this half would then run
     * alone and send the SAME substep number twice, because the number is counted in the task.
     *
     * A repeated stamp is the one thing the far side's timeline cannot see through: it reads two
     * different bodies as one moment. So the send is skipped and counted rather than sent stale. */
    if (!bridge.tick_ran_this_substep) {
        mp_stopwatch_leave(MP_WATCH_SUBSTEP_END);
        mp_stopwatch_leave(MP_WATCH_SUBSTEP);
        ++bridge.substep_ends_without_tick;
        bridge.in_tick = false;
        return;
    }
    bridge.tick_ran_this_substep = false;

    /* The NPC copies, joined or not: the walk and the host's own overlay wait for nobody. */
    mp_npc_copies_bridge_substep(bridge.joined, &send_reliable,
                                 bridge.mode == MP_BRIDGE_UDP_HOST ? &bridge_host : NULL);

    /* THE SENDING HALF, at the end of the substep rather than in the middle of it.
     *
     * It used to sit in the task, which runs before the collision, so everything the collision
     * produced left on the NEXT substep's packet: a hit, the knock it applies, the damage and the
     * death all reached the far side 31.25 ms late, and the lateness looked like the network
     * rather than like the sampling point. The player's own contact handler writes its knock
     * velocity, its skip-collide flag, its drop timer, its damage and its death after the task has
     * already run.
     *
     * This substep's events first and its state second, so the two share the packet the service
     * sends now: an event caught in substep S rides with the state of S, whose fire overlay is
     * live. The host reads the post-tick banks; the client sends its own body with the newest host
     * tick it holds in front.
     *
     * Then a receive and then the service, in that order: the receive takes whatever has arrived
     * while this substep ran so it is not left lying until the next one, and the service puts the
     * payload set above on the wire at once. */
    if (bridge.joined) {
        ++bridge.joined_substeps;
        send_events();
        if (bridge.mode == MP_BRIDGE_UDP_CLIENT) {
            mp_bridge_world_send_own(&bridge_client, bridge.drain.my_slot, bridge.substep);
        } else {
            /* Who is here, once a second and on every change, on the reliable channel, and before
             * the world: the world sizes its enemy block by what is due on the channel, and a
             * roster queued after it would never be counted and would ride the next packet. */
            if (bridge.mode == MP_BRIDGE_UDP_HOST) {
                (void)mp_bridge_roster_host_tick(&bridge_host, bridge.substep);
            }
            mp_bridge_world_send(&bridge_host, bridge.substep);
        }
    }
    /* What the hulls saw an actor start or lose in this substep is the census's of this substep
     * or nobody's: a host with no peer runs no census, and a start kept past it would be put on
     * whatever actor holds that address when one runs again. */
    mp_world_event_census_done();
    mp_enemy_burst_census_done();
    receive_all();
    service_all();
    mp_stopwatch_leave(MP_WATCH_SUBSTEP_END);
    mp_stopwatch_leave(MP_WATCH_SUBSTEP);
    bridge.in_tick = false;
}

void mp_bridge_leave(void)
{
    if (!mp_armed_transport()) {
        return;
    }
    if (bridge.pump_timer != 0u) {
        KillTimer(NULL, (UINT_PTR)bridge.pump_timer);
        bridge.pump_timer = 0u;
    }
    /* A host's last word before its goodbye. It will hardly be acknowledged before the process
     * ends, and the goodbye after it is what a client reads as the host ending the session. */
    mp_bridge_lobby_end_session();
    mp_session_disconnect(&bridge_host);
    mp_session_disconnect(&bridge_client);
    mp_bridge_lobby_close();
    if (bridge.public_net) {
        mp_relay_transport_farewell(&bridge_relay);   /* the relay frees the session or the seat */
    }

    /* The campaign mirror belongs to the session that built it. A second session inheriting the
     * first one's mirror would believe the far side already holds bytes it has never seen, and
     * the difference it would have to notice is exactly the one it just forgot. */
    mp_scratch_wire_reset();
    mp_enemy_sync_reset();
}

void mp_bridge_report(const char *why)
{
    mp_bridge_report_run_t run;

    if (!mp_armed_transport()) {
        return;
    }
    memset(&run, 0, sizeof run);
    run.timer_pumps       = bridge.timer_pumps;
    run.frame_pumps       = bridge.frame_pumps;
    run.pumps_refused     = bridge.pumps_refused;
    run.idle_drains       = bridge.idle_drains;
    run.idle_payloads     = bridge.idle_payloads;
    run.stall_pumps       = bridge.stall_pumps;
    run.longest_pumped_stall_ms = bridge.longest_pumped_stall_ms;
    run.substep_ends      = bridge.substep_ends;
    run.ends_without_tick = bridge.substep_ends_without_tick;
    run.joined_substeps   = bridge.joined_substeps;
    run.content           = bridge.content;
    run.content_decided   = bridge.content_decided;

    run.shape = bridge.mode == MP_BRIDGE_LOOPBACK
                    ? "loopback"
                    : (bridge.mode == MP_BRIDGE_UDP_HOST ? "UDP host" : "UDP client");
    run.game_mode_name    = bridge.game_mode_name;
    run.joined            = bridge.joined;
    run.commands_sent     = bridge_command.sent;
    run.commands_applied  = bridge_command.applied;
    run.commands_refused  = bridge_command.refusals;
    run.states_in         = bridge.drain.states_in;
    run.puppet_applies    = bridge.drain.puppet_applies;
    run.events_sent       = bridge.events_sent;
    run.events_unsent     = bridge.events_unsent;
    run.events_in         = bridge.drain.events_in;
    run.events_unplaced   = bridge.drain.events_unplaced;
    run.notes_refused     = bridge.drain.notes_refused;
    run.substep           = bridge.substep;
    run.is_loopback       = bridge.mode == MP_BRIDGE_LOOPBACK;
    run.is_host           = bridge.mode == MP_BRIDGE_UDP_HOST;
    run.loopback_delivered = bridge_net.delivered;
    run.loopback_dropped   = bridge_net.dropped;

    mp_bridge_report_all(why, &run, &bridge_host, &bridge_client,
                         bridge.public_net ? NULL : &bridge_udp);
    if (bridge.public_net) {
        mp_relay_transport_report(&bridge_relay);
    }
}
