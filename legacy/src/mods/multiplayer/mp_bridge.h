/* mp_bridge.h: the network layer married to the engine, proven in one process first.
 *
 * Everything below the engine is finished and proven over a lossy in-memory link: sessions,
 * channels, snapshots, command streams, clocks. What has never existed is the binding, and the
 * binding is where a networked feature dies silently: built, shipped, connected, and never actually
 * carrying the game. So the binding gets its own in-process proof before any packet touches a real
 * wire.
 *
 * The bridge runs a HOST session and a CLIENT session inside the one game process, joined over
 * the loopback network with real loss. Each substep the client authors a command and sends it
 * with redundancy as its unreliable payload; the host reads it through the session, feeds the
 * dedup sink, and hands the newest command to the input split, so the second body moves on input
 * that crossed the wire. After the body has ticked, the host reads the REAL banks into a
 * snapshot, delta encodes it against the baseline the client acknowledged, and sends it back; the
 * client decodes, stores, acknowledges, and then checks the decoded second body against the live
 * one, which in one process is the ground truth sitting right there.
 *
 * What this proves: bank state survives the read into the wire form, the wire form survives loss,
 * and a command authored on the far side of a session drives a real body. What it cannot prove:
 * two processes, real sockets, real clocks. The two UDP roles are that next step, over the same
 * binding with the transport changed under it: each side sends its own body's state every
 * substep and shows the other's as a puppet, sampled out of a history at a render moment the
 * timeline keeps a few ticks behind the newest sample, with the far player's events performed
 * when the render moment reaches the tick they were caught in.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_H
#define MULTIPLAYER_MP_BRIDGE_H

#include "mp_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sets up the loopback, both sessions and the histories, and begins the handshake. `loss_percent`
 * is the link's loss dial; the join itself is proven over the same loss. Refuses when the cells a
 * snapshot needs did not resolve. */
bool mp_bridge_install(uint32_t loss_percent);

/* The same binding over a REAL socket, one role per process. A host binds `port` and simulates
 * the remote player's body from received commands, exactly as the loopback host half does. A
 * client resolves "ip:port" in `address`, joins, sends the LOCAL player's sampled input as its
 * command stream, and drives its second body as a PUPPET from the host's snapshots: position and
 * heading written into the bank, then the commit-only walk, so the engine's own interpolation
 * blends between placements. Refuses when the socket cannot come up or the address does not
 * parse. */
bool mp_bridge_install_udp(bool as_host, const char *address, uint16_t port);

/* The same binding through the relay, for a public session: a host registers `seats` seats and is
 * given a code, a player joins the session `code` ("ABCD-EFGH"). Refuses when no socket to the
 * relay opens or the code does not read. A relay that is slow or out of reach is no refusal: the
 * transport keeps trying and the lobby shows it. */
bool mp_bridge_install_public(bool as_host, const char *code, uint8_t seats);

/* Puts up the transport the menu's settings ask for and no other: direct UDP on the LAN, the
 * relay in public, as mp_settings_transport_for decides. */
bool mp_bridge_install_for(const mp_settings_t *settings);

/* Whether the transport that stands was put up by the menu (above) rather than by the ini's way
 * in. False with no transport; true from inside the menu's door on, where the statement is handed
 * to the session, and taken back where the transport comes down. */
bool mp_bridge_armed_by_menu(void);

/* What the transport that stands was put up for, under the menu's `role`. */
void mp_bridge_armed(uint32_t role, mp_settings_armed_t *out);

/* Takes the socket's transport down so it can be put up again as the other side or on another
 * port: every peer is told goodbye, the lobby closes, the socket is shut and the far side's state
 * is forgotten. False when there is nothing to take down, and for the loopback, which is the
 * ini's test harness and never a choice of the menu. */
bool mp_bridge_uninstall_udp(void);

/* Reliable messages this side has sent and nobody has acknowledged yet, over every peer. The exit
 * waits on it for the host's last word. 0 for the loopback and with no transport. */
uint32_t mp_bridge_reliable_pending(void);

/* The port a host's socket is bound to, 0 for a client, the loopback and no transport. */
uint16_t mp_bridge_bound_port(void);

bool mp_bridge_installed(void);

/* Whether a peer is on the other end right now. Not a latch: a peer can drop and come back. */
bool mp_bridge_joined(void);

/* Arms the client-side spin: the client authors a constant full turn command every substep, so a
 * circling second body is the visible proof the command stream crossed the wire. Without it the
 * client sends stillness, which is itself the decoupling over the wire. */
void mp_bridge_enable_spin(void);

/* Whether the far body's buffer is measured from the stream or held at the value the interpolator
 * was built with. The timeline regulates in ticks and needs no clock for it; what this decides is
 * only whether it may. Set after the bridge stands, and it survives a peer arrival, because the
 * measurement starts over on its own with the history. */
void mp_bridge_set_auto_lag(bool enabled);

/* Which game this side thinks it is playing, handed to both sessions so the handshake can refuse a
 * peer that picked the other one. The bridge does not act on it: what a mode CHANGES is the layer
 * above's business, and what this carries is only the agreement that the two sides picked the same
 * game. Set before a client's first request, which is why it is its own call rather than an
 * argument to the install: the install runs at startup and the mode may still change in the menu.
 * */
void mp_bridge_set_game_mode(uint8_t mode, const char *name);

/* The local player's name, cleaned, for the handshake and the roster. Any time before a peer. */
void mp_bridge_set_player_name(const char *name);

/* The client's target, and the one-shot flag behind that first join from a level. Exposed so
 * the lobby half can begin a join, and begin it AGAIN, without a second copy of either: two
 * cells for one address is how the level path and the lobby path would have drifted apart. */
uint32_t mp_bridge_host_endpoint(void);
void mp_bridge_set_host_endpoint(uint32_t endpoint);
void mp_bridge_clear_connect_pending(void);

/* The password, the seats, the announce and the lobby line are mp_bridge_lobby.h, and so are
 * the join from the lobby and the mode a client's request names. */

/* The input half, called BEFORE the second body's tick: count the substep and stamp the events
 * with it, receive, and decode what the far side sent: the loopback host applies the client's
 * commands to the input split, the UDP host stores the client's body in the history. */
void mp_bridge_tick_pre(void);

/* The APPLYING half, called AFTER the second body's tick. It sends nothing and services nothing;
 * both moved to the substep end below. What it does is take in: receive, hand the reliable notes
 * to their readers, drain the two payload rings, decode the far world into the history, then
 * sample the far body at the render moment and place the puppet; the loopback verifies the
 * decoded world against the built one instead of placing anything.
 *
 * It stays here rather than moving with the sending half because a puppet placed at the end of a
 * substep would be one substep late for the collision pass that has just run. */
void mp_bridge_tick_post(void);

/* The END of a substep, after every task and after the collision pass, which is the last moment
 * anything in this substep can still change the world.
 *
 * It carries the SENDING half: this substep's events, this side's own body state, and the service
 * that puts them on the wire. That half used to sit in the tick above, which is a task slot and
 * therefore runs BEFORE the collision, so everything a hit produced left on the next substep's
 * packet and arrived 31.25 ms late for no reason but where it was read.
 *
 * The applying half deliberately did NOT move with it. See mp_bridge_tick_post.
 *
 * Why the message is enough on its own: a scan of every broadcast call site in the retail image
 * finds 38, and exactly one of them passes 0x0E. After it the engine does two things and no
 * more, it increments the substep counter at 0x004757DB and adds the step to its simulation time
 * at 0x004757E8; the broadcast itself, at 0x004757D3, is BEFORE the increment, so the number
 * stamped here is the substep that just ran and not the next one. A node procedure's return
 * value is dead: both broadcast loops zero their result before returning (0x0046F51B, and
 * 0x0046F45E with 0x0046F49B), and all 38 call sites pass an id of 0. The procedure takes three
 * dwords, counted off the `add esp, 0xC` at all four procedure call sites.
 *
 * The pairing with the task half is not the engine's guarantee. The task runner at 0x0047582A
 * turns back without running a task when its slot count at 0x00868720 is zero, which is the
 * state a level teardown leaves behind, while the broadcast goes out regardless. An unpaired run
 * here would send the previous substep's number a second time, and a repeated stamp is the one
 * error the far side cannot see through, so an unpaired run sends nothing and is counted. In the
 * field the two counts agreed at 1982, 1890, 2543 and 2945 across three level ends; the guard is
 * for the case a census cannot reach. */
void mp_bridge_substep_end(void);

/* Between substeps: receive and service with nothing to send, from the frame hook and from a
 * thread timer, so the session's timeouts and keepalives keep their meaning through a level load
 * or a window drag in which no substep runs. Turns back, counted, while a tick half or a bank
 * window is running. The loopback is pumped as well: its far end is this same process, but its
 * sessions count the wall clock like any other and a long pause would drop them too.
 *
 * The timer fires every thirty milliseconds and is dispatched by the engine's own message pump,
 * so it runs through a level load and through the modal window loops, where no frame is drawn
 * and no substep runs. It is created by the installer on the thread that runs that message loop,
 * because a thread timer belongs to its creating thread, and killed by mp_bridge_leave on the
 * same thread. Whether it can ever be dispatched inside a substep could not be established from
 * the binaries: the engine's one message pump sits outside the frame function, but the graphics
 * wrapper was not read, so both pumps turn back inside a tick half or a bank window and count
 * the refusal. With no payload set a service sends only a keepalive after a hundred milliseconds
 * of silence, so the packet rate is not multiplied.
 *
 * The first build gated these pumps on the UDP roles, on the reasoning that the loopback's far
 * end ticks with this process. Its sessions run on the wall clock like the others, so a menu
 * pause longer than the connected timeout of thirty seconds dropped both loopback peers, and
 * past the rejoin window of sixty the client never asked again: the in-process acceptance could
 * fail on a pause in the menu. */
typedef enum mp_bridge_pump_source {
    MP_BRIDGE_PUMP_FRAME,
    MP_BRIDGE_PUMP_TIMER
} mp_bridge_pump_source_t;

void mp_bridge_pump_idle(mp_bridge_pump_source_t source);

/* The id of the thread timer the installer armed for the pump, so the bridge can kill it when
 * it leaves. Zero for none. */
void mp_bridge_note_pump_timer(uintptr_t timer_id);

void mp_bridge_report(const char *why);

/* On the way out of the process: kills the pump timer and tells the far side this end is leaving,
 * so it frees the slot now instead of at the connected timeout. Nothing without an installed
 * bridge. */
void mp_bridge_leave(void);

#endif /* MULTIPLAYER_MP_BRIDGE_H */
