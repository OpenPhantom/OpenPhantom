/* mp_bridge_lobby.h: the menu's side of the bridge: password, seats, the announce, the lobby line.
 *
 * What the screens decide reaches the sessions through here, and what a host says about itself
 * on the LAN leaves from here. It is its own file rather than a part of mp_bridge.c because none
 * of it touches a substep: it writes a session field before a peer arrives, or sends one note
 * when a button is pressed, or broadcasts once a second from the idle pump.
 *
 * The bridge binds it to its two sessions once they exist and tells it three things after that:
 * which game was named (for the announce's mode bit), that a peer arrived (a client resends its
 * lobby line), and that a tick passed (the announce). The public setters below are the ones the
 * feature and the screens call; the ones prefixed mp_bridge_lobby_ are the bridge's.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_LOBBY_H
#define MULTIPLAYER_MP_BRIDGE_LOBBY_H

#include "mp_bridge_drain.h"
#include "mp_lobby.h"
#include "mp_session.h"
#include "mp_udp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- what the feature and the screens call ------------------------------------------------ */

/* The password: what a host asks for, what a client offers. Empty is none. Before a peer. */
void mp_bridge_set_password(const char *password);

/* How many may sit in the session, the host itself included, 2..MP_SESSION_MAX_PEERS+1. A host
 * refuses the next request past it with the same FULL a full session answers. */
void mp_bridge_set_slots(uint8_t slots);

/* Whether a host says so on the LAN once a second, and by what name (mp_announce). The password
 * bit and the mode bit come from the other setters and are recomputed on every change, so the
 * order of the calls does not matter. */
void mp_bridge_set_announce(bool enabled, const char *session_name);

/* This side's team and ready, for the roster: a host writes its own line, a client sends the
 * lobby note (mp_lobby) to the host now and again on every arrival. */
void mp_bridge_set_lobby(uint8_t team, bool ready);
void mp_bridge_get_lobby(uint8_t *team, bool *ready);

/* This side's hero, 0..MP_LOBBY_HERO_MAX. The setter is further down beside the lobby's own
 * calls; the getter is here because it answers the same question as the two above and because
 * without one the chosen hero could be written and never read, which is what it was. */
uint8_t mp_bridge_get_lobby_hero(void);

/* ---- the lobby ------------------------------------------------------------------------------ */

/* HOST: what everybody is waiting to play. Setting it starts the repeat; setting the same thing
 * again changes nothing and sends nothing. */
void mp_bridge_lobby_set_setup(const mp_lobby_setup_t *setup);

/* HOST: the moment has come. Sets the started bit in the repeat and RAISES THE GENERATION, which
 * is what every client is watching for. The host loads the level itself; this only tells the
 * others.
 *
 * The generation is why this may be called more than once in a session. The started bit is an edge
 * that has already fired: once it is set it is never cleared, so a second load of the same level
 * looked exactly like the first and everybody could be sent into a level exactly once. A raised
 * generation is a state, so loading again, loading a savegame and going on to the next level are
 * one operation with three reasons. */
void mp_bridge_lobby_start(void);

/* HOST: puts a note on the reliable channel to every peer, and answers whether anything took it.
 * Generic on purpose: the lobby has three notes of its own that go this way, and the round's
 * repeated table is a fourth that belongs to a module which may not be linked beside this one. */
bool mp_bridge_lobby_broadcast(const uint8_t *note, size_t bytes);

/* What this side knows about the session, host or client. False before a host has chosen or a
 * client has heard. */
bool mp_bridge_lobby_setup(mp_lobby_setup_t *out);

/* CLIENT: whether a start this side has not acted on has arrived. It answers true once per
 * GENERATION rather than once per session: a repeated flag must not load the same level twice, and
 * a host that changes the world again must be able to take everybody with it. */
bool mp_bridge_lobby_take_start(mp_lobby_setup_t *out);
/* CLIENT: the same question without the answer being consumed, for a side that has to wait for
 * something before it can act on a start, the host's savegame, above all, and must not let the
 * generation be marked acted on until it does. */
bool mp_bridge_lobby_peek_start(mp_lobby_setup_t *out);

/* The world change this side has last acted on: a host's own generation, a client's the one whose
 * start it took. The world a level begins in is this one. */
uint8_t mp_bridge_lobby_world(void);

/* HOST: where the difficulty the setup note says comes from, 0 for "none" and otherwise the
 * difficulty plus one. Asked on every send, on a copy of the note, so a choice the menu makes
 * again is compared against what the host chose and not against the engine's cell. NULL says none,
 * which is what a machine without the engine says. */
void mp_bridge_lobby_set_difficulty_source(uint8_t (*source)(void));

/* Whether a contact between this machine's player and the player on `peer_slot` is allowed to
 * hurt. This is where the team byte finally has a consumer: the game, the rule set and the two
 * teams are brought together here and handed to the one rule that decides it.
 *
 * It answers FALSE when it cannot tell, which is deliberate. An unknown game or a peer nobody has
 * put in a roster are both states in which the alternative is to let players kill each other in a
 * co-operative campaign, and a refused contact is counted and reported while a wrong kill is not
 * noticed at all. */
bool mp_bridge_may_damage_peer(uint8_t peer_slot);

/* From the drain: the two notes the lobby reads. A fingerprint names the peer it came from, since
 * a host holds each client's against its own. */
bool mp_bridge_lobby_take_setup(const uint8_t *note, size_t bytes);
bool mp_bridge_lobby_take_content(size_t peer_index, const uint8_t *note, size_t bytes);

/* Once a level is up and this side knows its own fingerprint: sends it, and compares whatever the
 * far side sent. A client whose fingerprint differs from the host's LEAVES. A host names it, and
 * in a deathmatch sends that client away with the reason (mp_trust.h), because a client that
 * differs and stays is one that was changed not to leave. */
void mp_bridge_lobby_content_settled(uint32_t fingerprint, bool is_host);

/* Whether the far side's content was compared and disagreed. For the screen's error line. */
bool mp_bridge_lobby_content_mismatch(void);

/* CLIENT: leave the session, keeping the bridge installed. What a player backing out of a lobby
 * does, and what a failed content comparison does by itself. */
void mp_bridge_lobby_leave(void);

/* Client only: the join was tried for the whole window and nobody answered. The band used
 * to say it was connecting before, during and after, so waiting and giving up read alike. */
bool mp_bridge_lobby_gave_up(void);

/* Client only: gave up because the host said goodbye, not because it went silent. The band
 * told a host that had left as one that did not answer. */
bool mp_bridge_lobby_host_left(void);

/* Begins, or begins again, the client's join at `address`, re-read every time so the address
 * the menu holds NOW is the one asked. Nothing happens while a handshake is under way or a
 * peer is connected. It used to hang on a flag set once, at install, so every join after the
 * first in one process asked nobody and the band said it was connecting for good. */
void mp_bridge_connect_in_lobby(const char *address);

/* What the CLIENT names as its game in the request, apart from what the lobby shows. Zero
 * means the mode was never heard; the host's check steps aside for a zero and the host's
 * game is played. The lobby line and the local switches keep reading the effective mode. */
void mp_bridge_set_client_handshake_mode(uint8_t mode);

/* Why the last join was refused, MP_DENY_NONE when it was not. The one thing a player could never
 * learn before: a refused join simply did not happen. */
mp_deny_reason_t mp_bridge_lobby_last_deny(void);

/* Client only: the host sent this side away for falling behind (MP_DENY_BEHIND). */
bool mp_bridge_lobby_sent_away(void);

/* How many joins this client has had refused since its session began. The lobby asks a refused
 * player for the password, and asks about every refusal once: a count tells a refusal that has
 * been answered from one that has just arrived, which the reason on its own cannot. */
uint32_t mp_bridge_lobby_denials(void);

/* This side's hero, without touching its team or its ready. */
void mp_bridge_set_lobby_hero(uint8_t hero);

/* Whether a lobby screen is on show. It changes what the idle pump does: a lobby has no substeps,
 * and the joined state, the roster and the reliable notes are all written inside one. While this
 * is set, the tick does that work itself, which is safe for exactly as long as no note of a level
 * is acted on: a client's lobby takes only the lobby's own notes, because a host that already
 * plays sends it everything its level says. It is not simply always on because inside a level a
 * note can perform a shot or open a door, and neither belongs outside a substep. */
void mp_bridge_lobby_set_open(bool open);

/* Whether somebody is on the other end, asked of the sessions directly rather than of the
 * bridge's own joined flag, which only a substep updates, and a lobby has none. */
bool mp_bridge_lobby_connected(void);

/* How long the host has said nothing, on a client that is connected to one; 0 otherwise. The
 * picture says so from the second second on, long before the connected timeout decides. */
uint32_t mp_bridge_lobby_host_silent_ms(void);

/* How many are in the session, this side included. What the lobby shows before a level. */
size_t mp_bridge_lobby_population(void);

/* Forgets the session's lobby state, so a second lobby does not start on the first one's answers.
 * */
void mp_bridge_lobby_reset(void);

/* Leaves the session by whichever door: the lobby's BACK, the title screen a client reaches
 * from a level, and a host starting a level after its session has ended. Disconnects AND
 * forgets the setup, then tells the listener, so every door leaves the same state behind. */
void mp_bridge_lobby_withdraw(void);

/* Who is told that the session was left, once per withdrawal. The feature puts the game back
 * to the campaign's rules there, which this layer cannot reach. */
void mp_bridge_lobby_set_withdrawn_listener(void (*listener)(void));

/* ---- ending a session ------------------------------------------------------------------------ */

/* Host only: say that this session is finished. The word rides the setup note that already
 * repeats once a second, so it reaches a client inside a level without a message of its own, and
 * it covers the one departure no timeout can see, a host that leaves its level for the title
 * screen while its socket stays open and answering. */
void mp_bridge_lobby_end_session(void);

/* Whether the setup this side holds carries that word. */
bool mp_bridge_lobby_ended(void);

/* Whether this side is a client in a session that is joined and has not ended: the one answer a
 * rule that must follow the session, and not the moment it was armed, can hang on. */
bool mp_bridge_lobby_client_is_playing(void);

/* Whether this session is over, and why. The facts are read here; the rule is mp_lobby.c's.
 * `level_running` is the caller's, because whether the engine is in a level is not the bridge's
 * to know. Safe to call every frame: it answers MP_LOBBY_OVER_NO until a session has actually
 * started. */
mp_lobby_over_t mp_bridge_lobby_session_over(bool level_running);

/* ---- what the bridge calls ----------------------------------------------------------------- */

void mp_bridge_lobby_bind(mp_session_t *host, mp_session_t *client, mp_udp_t *udp, bool is_client,
                          mp_bridge_drain_t *drain);
void mp_bridge_lobby_note_mode(uint8_t game_mode);
void mp_bridge_lobby_on_join(void);
void mp_bridge_lobby_tick(bool is_host, uint32_t content);
void mp_bridge_lobby_close(void);
void mp_bridge_lobby_report(bool is_host, bool is_client);

#endif /* MULTIPLAYER_MP_BRIDGE_LOBBY_H */
