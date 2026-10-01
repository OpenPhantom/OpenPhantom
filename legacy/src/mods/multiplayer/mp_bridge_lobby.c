/* mp_bridge_lobby.c: the menu's side of the bridge. See the header.
 *
 * Split from mp_bridge.c on 2026-09-06, the day it was written, because the bridge was at its
 * size limit and this is a seam: nothing here touches a substep, a bank or a body. It holds two
 * sessions by pointer and speaks through them.
 *
 * SIZE NOTE: over 600 lines. Two subjects, and each is the same one seen from a different side:
 * what a host says about the session, and what a player says about themselves. The friendly fire
 * gate joined them because it is the last question a lobby answers, and it needs the session's
 * game, its rule set and the roster, all three of which are already here. The end of a session
 * joined them for the same reason: this is the file that holds both sessions AND sends the note the
 * ending rides on, and gathering the facts anywhere else would have meant handing the sessions out.
 *
 * The fingerprint comparison left as mp_bridge_content.c when this file reached the limit again,
 * and the announce as mp_bridge_announce.c the time after. The next seam is the friendly fire
 * gate: the game of the session, the team of a slot and the one rule, which read the setup and
 * the roster and write nothing either note carries.
 */
#include "mp_bridge_lobby.h"

#include "mp_bridge.h"
#include "mp_bridge_announce.h"
#include "mp_bridge_content.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby_late.h"
#include "mp_bridge_public.h"
#include "mp_bridge_roster.h"
#include "mp_bridge_savefile.h"
#include "mp_host_settings.h"
#include "mp_lobby.h"
#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_session.h"
#include "mp_udp.h"
#include "mp_wallclock.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How often the host repeats what it has chosen, in milliseconds: the same cadence as the roster
 * and the announce, which is what a screen is worth redrawing at. */
#define SETUP_REPEAT_MS 1000u

typedef struct lobby_state {
    mp_session_t  *host;
    mp_session_t  *client;
    mp_udp_t      *udp;            /* NULL on the loopback, which has no port to announce */
    mp_bridge_drain_t *drain;      /* the reading half, so a lobby can drive it with no substep */
    void (*withdrawn)(void);       /* told when the session is left, by any door */
    uint32_t       withdrawals;
    bool           is_client;
    bool           open;           /* a lobby screen is on show */
    /* Whether anybody has ever been on the far end of this session. The host needs it and the
     * client does not: "nobody is left" and "nobody has come yet" are the same count of peers,
     * and a host that started a level before its first client must not be thrown out of it. */
    bool           had_peer;

    /* What the last diagnostic said, so the line is written when it changes and once a second
     * while nothing is connected, rather than on every tick. */
    uint32_t       diag_last_ms;
    uint32_t       diag_joins;
    uint32_t       diag_denied;
    uint32_t       diag_drops;
    uint8_t        diag_peers;
    uint8_t        diag_deny;
    bool           diag_written;

    mp_lobby_t     lobby;
    bool           lobby_set;
    uint32_t       lobby_sent;
    uint32_t       lobby_unsent;
    uint32_t       lobby_late;     /* owed lines that went out once the handshake was through */
    bool           lobby_owed;     /* the last line could not go out and is owed to the host */
    uint32_t       leaves_seen;    /* the host's leave notices counted when this lobby began */

    /* What everybody is waiting to play, and how it travels. */
    mp_lobby_setup_t setup;
    bool             setup_known;    /* a host chose it, or a client heard it */
    bool             setup_dirty;    /* it changed since the last time it went out */
    uint32_t         setup_last_ms;
    uint32_t         setups_sent;
    uint32_t         setups_taken;
    uint32_t         setups_torn;
    uint32_t         setups_refused;   /* a peer told the host what the session is */
    uint32_t         broadcasts;     /* notes another module handed to the host's channel */
    uint32_t         broadcasts_unsent;
    bool             start_seen;     /* a start this side has not acted on yet */

    /* Which world change this side has already acted on. It is compared for INEQUALITY against
     * what arrives, never for order: the byte wraps, and the change that takes it from 255 back
     * to 0 is as new as any other. */
    uint8_t          generation_acted_on;
    uint8_t        (*difficulty_source)(void);   /* the host's difficulty for the note, plus one */

    /* The friendly fire gate: how often it was asked, how often it refused, and how often it
     * could not tell. The third is the one worth reading. A player whose shots do nothing has no
     * other way of finding out why. */
    uint32_t         damage_asked;
    uint32_t         damage_refused;
    uint32_t         damage_unknown;
} lobby_state_t;

static lobby_state_t lb;

/* The two that are used before they are defined: the tick repeats the setup, and the join resends
 * the lobby line. */
static void send_lobby(void);
static void send_setup(void);

static void send_lobby(void)
{
    uint8_t note[MP_LOBBY_BYTES];

    if (lb.client == NULL || mp_lobby_encode(&lb.lobby, note, sizeof note) != MP_LOBBY_BYTES ||
        !mp_session_is_connected(lb.client) ||
        !mp_session_send_reliable(lb.client, 0, note, sizeof note)) {
        ++lb.lobby_unsent;
        lb.lobby_owed = true;
        return;
    }
    ++lb.lobby_sent;
    lb.lobby_owed = false;
}

/* ---- what the feature and the screens call ------------------------------------------------ */

void mp_bridge_set_lobby(uint8_t team, bool ready)
{
    lb.lobby.team  = team > MP_LOBBY_TEAM_MAX ? (uint8_t)MP_LOBBY_TEAM_MAX : team;
    lb.lobby.ready = ready ? 1u : 0u;
    lb.lobby_set   = true;
    if (lb.is_client) {
        send_lobby();
    } else {
        mp_bridge_roster_set_own(lb.lobby.team, ready, lb.lobby.hero);
    }
}

void mp_bridge_get_lobby(uint8_t *team, bool *ready)
{
    if (team != NULL) {
        *team = lb.lobby.team;
    }
    if (ready != NULL) {
        *ready = lb.lobby.ready != 0u;
    }
}

uint8_t mp_bridge_get_lobby_hero(void)
{
    return lb.lobby.hero;
}

/* ---- what the bridge calls ----------------------------------------------------------------- */

void mp_bridge_lobby_bind(mp_session_t *host, mp_session_t *client, mp_udp_t *udp, bool is_client,
                          mp_bridge_drain_t *drain)
{
    lb.host      = host;
    lb.client    = client;
    lb.udp       = udp;
    lb.drain     = drain;
    lb.is_client = is_client;
    mp_bridge_announce_bind(host, client, udp);
    /* The savegame transfer stands on the same two sessions and is ticked from here, so it is
     * bound from here as well rather than costing the bridge a line it does not have. */
    mp_bridge_savefile_bind(host, client, is_client);
    mp_bridge_content_bind(host, client, is_client);
}

void mp_bridge_lobby_set_open(bool open)
{
    lb.open = open;
}

bool mp_bridge_lobby_connected(void)
{
    if (lb.is_client) {
        return lb.client != NULL && mp_session_is_connected(lb.client);
    }
    return lb.host != NULL && mp_session_peer_count(lb.host) >= 1u;
}

uint32_t mp_bridge_lobby_host_silent_ms(void)
{
    const mp_peer_t *host;

    if (!lb.is_client || lb.client == NULL || !mp_session_is_connected(lb.client)) {
        return 0u;
    }
    host = mp_session_peer(lb.client, 0);
    return host != NULL ? (uint32_t)(lb.client->now_ms - host->last_recv_ms) : 0u;
}

size_t mp_bridge_lobby_population(void)
{
    if (lb.is_client) {
        return (lb.client != NULL && mp_session_is_connected(lb.client)) ? 2u : 1u;
    }
    return lb.host != NULL ? 1u + mp_session_peer_count(lb.host) : 1u;
}

void mp_bridge_lobby_on_join(void)
{
    /* A peer that has just arrived has heard neither side's fingerprint yet. */
    mp_bridge_content_note_join();
    if (lb.is_client && lb.lobby_set) {
        send_lobby();   /* the host that just admitted this side has not heard it yet */
    }
}

/* The reason a refusal gave, as the session's log line says it. What the judge's detail adds, the
 * file or the mod and both builds, mp_bridge_content says in a line of its own. */
static const char *deny_text(mp_deny_reason_t reason)
{
    switch (reason) {
    case MP_DENY_FULL:     return "the session is full";
    case MP_DENY_PROTOCOL: return "a different protocol version";
    case MP_DENY_CONTENT:  return "different game data";
    case MP_DENY_MODE:     return "a different game was picked";
    case MP_DENY_PASSWORD: return "the password did not match";
    case MP_DENY_BEHIND:   return "sent away for falling behind";
    case MP_DENY_MODS:     return "a required mod is missing or another build";
    case MP_DENY_FOREIGN_DLL: return "a DLL the host does not allow";
    default:               return "none";
    }
}

/* Why a handshake that never completes used to be silent, and what this says instead.
 *
 * Every reason a join can fail is already decided and counted one layer down: the session denies
 * with a reason, drops on a timeout and counts both. None of it was ever written to the log, and
 * the bridge's own report is printed at shutdown, which a player who kills a stuck game never
 * reaches. So the field evidence for "the client cannot see the host" was two logs that both
 * stopped at "the bridge stands" and said nothing further, and neither the player nor anybody
 * reading afterwards could tell a refused join from a packet that never arrived.
 *
 * It writes on a CHANGE, and once a second while a lobby is open with nobody on the other end.
 * A connected session that is not changing says nothing at all. */
static void say_what_the_session_is_doing(bool is_host, uint32_t now)
{
    mp_session_t    *session = is_host ? lb.host : lb.client;
    mp_deny_reason_t deny;
    uint32_t         joins;
    uint32_t         denied;
    uint32_t         drops;
    uint8_t          peers;
    bool             changed;

    if (session == NULL || (!lb.open && !lb.setup_known)) {
        return;
    }
    peers  = (uint8_t)mp_session_peer_count(session);
    joins  = mp_session_joins(session);
    denied = mp_session_denied(session);
    drops  = mp_session_drops(session);
    deny   = (uint8_t)mp_session_last_deny(session);

    changed = !lb.diag_written || peers != lb.diag_peers || joins != lb.diag_joins ||
              denied != lb.diag_denied || drops != lb.diag_drops || deny != lb.diag_deny;
    if (!changed && (peers != 0u || now - lb.diag_last_ms < 1000u)) {
        return;
    }
    lb.diag_written = true;
    lb.diag_last_ms = now;
    lb.diag_peers   = peers;
    lb.diag_joins   = joins;
    lb.diag_denied  = denied;
    lb.diag_drops   = drops;
    lb.diag_deny    = (uint8_t)deny;

    if (is_host) {
        log_info("the session as HOST: %u peer(s) on, %u join(s) admitted, %u request(s) refused "
                 "(last: %s), %u dropped", (unsigned)peers, (unsigned)joins, (unsigned)denied,
                 deny_text((mp_deny_reason_t)deny), (unsigned)drops);
    } else {
        log_info("the session as CLIENT: %s, still trying %s, %u join(s), refused %u (last: %s), "
                 "%u dropped", peers != 0u ? "CONNECTED" : "NOT connected",
                 lb.client->connect_wanted ? "yes" : "NO", (unsigned)joins, (unsigned)denied,
                 deny_text((mp_deny_reason_t)deny), (unsigned)drops);
    }
}

void mp_bridge_lobby_tick(bool is_host, uint32_t content)
{
    uint32_t now = mp_wallclock_ms();

    mp_bridge_public_tick();
    say_what_the_session_is_doing(is_host, now);

    /* The content comparison rides this tick because it is the only routine that runs both in a
     * lobby and in a level and is already handed the fingerprint. */
    mp_bridge_content_tick(content, is_host);

    /* A lobby that was backed out of has no room to announce. Without this gate the host went on
     * broadcasting after leaving its own lobby, and mp_bridge_lobby_reset had cleared the setup,
     * so whoever answered the advertisement waited forever on a host that would never choose. An
     * announce outlives the SCREEN on purpose, because a started session keeps its setup and a
     * running host is still a host; it does not outlive the setup. */
    if (is_host && lb.host != NULL && (lb.open || lb.setup_known)) {
        mp_bridge_announce_tick(content, now, lb.open || !mp_bridge_lobby_ended());

        /* The setup is REPEATED rather than sent once, so a player who joins late learns the
         * state of the lobby from the next repeat instead of from a history nobody keeps. */
        if (lb.setup_known && (lb.setup_dirty || now - lb.setup_last_ms >= SETUP_REPEAT_MS)) {
            send_setup();
        }
    }

    /* The host's savegame on its way to the clients. It rides the bulk lane and not the reliable
     * channel the note above uses, so the order of the two no longer matters (it did when both
     * shared one queue and the chunks took every slot ahead of the start flag). It runs in a
     * lobby and inside a level alike, because a client may still be in its lobby while the host
     * is already standing in the world the file describes, and that is exactly the case the old
     * shape could not survive. */
    mp_bridge_savefile_tick(now);

    /* A lobby line that could not go out, because the handshake was not through yet, is owed
     * to the host and goes out as soon as it is. The resend on a join rides a substep, and a
     * lobby has none: without this the host listed a client who had pressed nothing as ready,
     * and could start over them. */
    if (lb.is_client && lb.lobby_owed && lb.lobby_set && lb.client != NULL &&
        mp_session_is_connected(lb.client)) {
        uint32_t before = lb.lobby_sent;

        send_lobby();
        if (lb.lobby_sent != before) {
            ++lb.lobby_late;
        }
    }

    mp_bridge_lobby_late_tick(lb.open && lb.is_client);
    if (!lb.open || lb.drain == NULL) {
        return;
    }
    /* What a substep would have done, and a lobby has none.
     *
     * The handshake completes without any of this, both sessions log their join, but everything
     * that MAKES a lobby is written inside a substep: the roster the player list is drawn from,
     * and the reliable notes that carry it and the host's setup. A lobby that never ran one
     * therefore sat at "connecting" with an empty list while the session underneath it was
     * connected. That was the whole defect.
     *
     * Doing it here is safe for exactly as long as no note of a level is acted on. The idle drain
     * was built without the reliable notes because a note can perform a shot or open a door, and
     * neither belongs outside a substep. A lobby whose host has not started sends nothing else;
     * a client that joins a session whose host already plays is sent everything its level says,
     * so a client's lobby takes only the lobby's own notes and counts the rest. */
    mp_bridge_drain_lobby_notes(lb.drain);
    if (is_host && lb.host != NULL) {
        /* EVERY PUMP, not once a second, because the roster paces ITSELF: it sends when the table
         * changed and otherwise on its own repeat interval. Gating the call as well put a second
         * of delay in front of every change, and the changes are the whole content of a lobby;
         * somebody arriving, somebody picking a side, somebody saying they are ready. A player
         * who joined and then watched an empty list for a second read that as a broken lobby.
         *
         * The substep number is what the roster paces its repeat by, and a lobby has none; the
         * wall clock in the same unit keeps the interval's meaning. */
        (void)mp_bridge_roster_host_tick(lb.host, now / 31u);
    }
}

/* ==============================================================================================
 * The lobby: what everybody is waiting to play.
 * ============================================================================================ */

/* The host's difficulty goes on a COPY. Kept in the setup it would make every choice the menu
 * makes again a change, because the menu builds its note with none, and the next of them would
 * say none to everybody. */
static void send_setup(void)
{
    uint8_t          note[MP_LOBBY_SETUP_BYTES];
    mp_lobby_setup_t said;

    if (lb.host == NULL || !lb.setup_known) {
        return;
    }
    mp_host_settings_send(lb.host);   /* the host's world settings go in front of every setup */
    said = lb.setup;
    said.host_difficulty = lb.difficulty_source != NULL ? lb.difficulty_source() : 0u;
    if (mp_lobby_setup_encode(&said, note, sizeof note) != MP_LOBBY_SETUP_BYTES) {
        return;   /* the encoder refuses what it did not promise; nothing half-formed goes out */
    }
    if (mp_session_broadcast_reliable(lb.host, note, sizeof note) == 0u) {
        return;
    }
    lb.setup_dirty   = false;
    lb.setup_last_ms = mp_wallclock_ms();
    ++lb.setups_sent;
}

/* The host declares the session finished. It rides the setup note, so the flag reaches everyone
 * the host can still reach, repeats until they answer, and needs no message of its own.
 *
 * It is sent immediately rather than left to the once-a-second repeat because the thing being
 * announced is that this side is about to stop sending: a host walking back to its title screen
 * still has a live socket for exactly as long as the process lives, but nobody should have to
 * wait a second to be told, and a host that then quits sends BYE over the top of this anyway. */
void mp_bridge_lobby_end_session(void)
{
    if (lb.host == NULL || lb.is_client || !lb.setup_known) {
        return;
    }
    if ((lb.setup.flags & MP_LOBBY_F_ENDED) != 0u) {
        return;   /* already said, and saying it twice would only cost a packet */
    }
    lb.setup.flags |= (uint8_t)MP_LOBBY_F_ENDED;
    lb.setup_dirty  = true;
    send_setup();
    log_info("the session was declared over; every client is told to return to the menu");
}

/* Whether the setup this side holds says the session is over. On the host that is its own word
 * read back, which is what makes it safe for both roles to ask the same question. */
bool mp_bridge_lobby_ended(void)
{
    return lb.setup_known && (lb.setup.flags & MP_LOBBY_F_ENDED) != 0u;
}

/* Whether this side is a client of somebody else's world that is still being played: joined, and
 * the host has not said the session is over. Every part of the answer is live, so a hull that asks
 * it at every call follows the session rather than the moment it was armed: a client whose host
 * ended the game and who plays on alone in the same process gets the answer "no" without anybody
 * releasing anything, and a client whose host hosts again gets "yes" without anybody arming. */
bool mp_bridge_lobby_client_is_playing(void)
{
    return mp_bridge_drain_is_client() && mp_bridge_joined() && !mp_bridge_lobby_ended();
}

/* The whole judgement, gathered here because this is the file that holds the sessions, and
 * decided in mp_lobby.c because the decision is a rule and not a lookup.
 *
 * `far_side_given_up` is the session's own verdict rather than a clock of this module's: a client
 * clears `connect_wanted` when the host said goodbye AND when the connected timeout runs out,
 * so one field answers the fast case and the slow one. Re-deriving that here would be a second
 * timer disagreeing with the first. */
mp_lobby_over_t mp_bridge_lobby_session_over(bool level_running)
{
    bool started;
    bool present;
    bool given_up;

    if (lb.host == NULL || lb.client == NULL) {
        return MP_LOBBY_OVER_NO;
    }
    started = lb.setup_known && (lb.setup.flags & MP_LOBBY_F_STARTED) != 0u;
    if (lb.is_client) {
        present  = mp_session_is_connected(lb.client);
        given_up = !lb.client->connect_wanted;
    } else {
        present = mp_session_peer_count(lb.host) != 0u;
        if (present) {
            lb.had_peer = true;
        }
        given_up = lb.had_peer;
    }
    /* A host's goodbye is its word as much as the flag is: it is what a host that leaves on
     * purpose sends last, and the flag in front of it may still be on its way. */
    return mp_lobby_session_over(started, level_running, lb.is_client,
                                 mp_bridge_lobby_ended() ||
                                     (lb.is_client && mp_bridge_lobby_host_left()),
                                 present, given_up, mp_bridge_lobby_content_mismatch(),
                                 mp_bridge_lobby_sent_away());
}

/* The host's last notice to this client named the new reason. A client's session keeps the reason
 * of the last refusal until it asks again, so this answers for as long as the ending is shown. */
bool mp_bridge_lobby_sent_away(void)
{
    return lb.is_client && lb.client != NULL && mp_session_last_deny(lb.client) == MP_DENY_BEHIND;
}

void mp_bridge_lobby_set_setup(const mp_lobby_setup_t *setup)
{
    mp_lobby_setup_t next;

    if (setup == NULL) {
        return;
    }
    next = *setup;
    /* The generation is the BRIDGE's count of world changes and not the screen's. The screen
     * builds this note out of its own fields every time the host picks something, so a generation
     * taken from the caller would drop back to zero on every pick, and a client that had already
     * acted on that number would then sit out the next start. */
    next.generation = lb.setup.generation;
    if (lb.setup_known && mp_lobby_setup_equal(&lb.setup, &next)) {
        return;   /* the same choice is not news */
    }
    lb.setup       = next;
    lb.setup_known = true;
    lb.setup_dirty = true;
    send_setup();
}

/* An ending belongs to one session, and forgetting that was a defect with a long reach.
 *
 * The flag is set on the setup note, and the setup note is repeated for as long as a session is
 * known. Nothing cleared it, so the first time a host said "this is over" every note it sent for
 * the rest of the process carried the word: a player who joined afterwards was told the session
 * was finished before it began, and was thrown back to the menu by the note that was supposed to
 * tell them what to load.
 *
 * So both places that describe a NEW session clear it. Describing one is the opposite of ending
 * one, and the two cannot both be true of the same note. */
static void the_session_is_not_over(void)
{
    lb.setup.flags &= (uint8_t)~(uint8_t)MP_LOBBY_F_ENDED;
}

void mp_bridge_lobby_start(void)
{
    if (!lb.setup_known) {
        return;
    }
    lb.setup.flags |= (uint8_t)MP_LOBBY_F_STARTED;
    the_session_is_not_over();
    /* The bit alone cannot be pressed twice. This can: a generation the far side has not acted on
     * is what makes the second start, the loaded savegame and the next level one operation. */
    ++lb.setup.generation;
    lb.setup_dirty = true;
    send_setup();
    log_info("the lobby starts: %s (%s), generation %u", lb.setup.title, lb.setup.level,
             (unsigned)lb.setup.generation);
}

bool mp_bridge_lobby_broadcast(const uint8_t *note, size_t bytes)
{
    if (lb.host == NULL || note == NULL || bytes == 0u ||
        mp_session_broadcast_reliable(lb.host, note, bytes) == 0u) {
        ++lb.broadcasts_unsent;
        return false;
    }
    ++lb.broadcasts;
    return true;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (out == NULL || !lb.setup_known) {
        return false;
    }
    *out = lb.setup;
    return true;
}

bool mp_bridge_lobby_peek_start(mp_lobby_setup_t *out)
{
    if (out == NULL || !lb.start_seen || !lb.setup_known) {
        return false;
    }
    *out = lb.setup;
    return true;
}

uint8_t mp_bridge_lobby_world(void)
{
    return lb.is_client ? lb.generation_acted_on : lb.setup.generation;
}

void mp_bridge_lobby_set_difficulty_source(uint8_t (*source)(void))
{
    lb.difficulty_source = source;
}

bool mp_bridge_lobby_take_start(mp_lobby_setup_t *out)
{
    if (out == NULL || !lb.start_seen || !lb.setup_known) {
        return false;
    }
    /* What is remembered is the generation, not the fact that something was taken. A repeated
     * flag carrying the same generation is then the same start and loads nothing; the next world
     * change raises the generation and is a start again. */
    lb.generation_acted_on = lb.setup.generation;
    lb.start_seen          = false;
    *out = lb.setup;
    mp_bridge_lobby_late_took_start(lb.open);
    return true;
}

bool mp_bridge_lobby_take_setup(const uint8_t *note, size_t bytes)
{
    mp_lobby_setup_t setup;

    if (!mp_lobby_is_setup(note, bytes)) {
        return false;
    }
    /* The host's word, and only the host's. A host used to take this note from any peer and
     * repeat it to all of them, so a changed client could end the session, change the level or
     * start everybody for everybody. It is still recognised, so nothing further down reads it. */
    if (!lb.is_client) {
        ++lb.setups_refused;
        return true;
    }
    if (!mp_lobby_setup_decode(note, bytes, &setup)) {
        ++lb.setups_torn;
        return true;   /* it was addressed to the lobby, and the lobby refused it */
    }
    ++lb.setups_taken;
    if (!lb.setup_known || !mp_lobby_setup_equal(&lb.setup, &setup)) {
        log_info("the host is on %s (%s), %s", setup.title, setup.level,
                 (setup.flags & MP_LOBBY_F_FROM_SAVE) != 0u ? "restored from a savegame"
                                                            : "a fresh level");
    }
    /* The generation replaces the edge. The note is repeated, so the bit itself says nothing; an
     * edge on it fires exactly once per session, which is why a host could send everybody into a
     * level once and never again. What is asked instead is whether this side has already acted on
     * the world change the note names, and that question can be asked of any repeat. */
    if ((setup.flags & MP_LOBBY_F_STARTED) != 0u && (setup.flags & MP_LOBBY_F_ENDED) == 0u &&
        mp_lobby_generation_is_new(lb.generation_acted_on, setup.generation)) {
        lb.start_seen = true;
    } else if ((setup.flags & MP_LOBBY_F_ENDED) != 0u) {
        lb.start_seen = false;   /* an ended session starts nobody, whatever it named last */
    }
    lb.setup       = setup;
    lb.setup_known = true;
    mp_bridge_lobby_late_heard_setup(lb.open);
    return true;
}

/* ==============================================================================================
 * What the team byte is FOR.
 * ============================================================================================ */

/* Which game the SESSION is playing. The host's repeated note is asked first, because that is the
 * session saying what it is, and every side reads the same answer from it; what this machine was
 * configured with is the fallback for a session that was armed from the ini with no lobby in it.
 * Neither of them is guessed at: an unknown game is left unknown and the caller refuses. */
static uint8_t session_mode(void)
{
    if (lb.setup_known) {
        return lb.setup.mode;
    }
    return mp_bridge_announce_game_mode();
}

/* The team of the player on `peer_slot`, out of the table the authority repeats. False when that
 * player is not in it, which is a state and not a licence: a body whose owner nobody has named
 * cannot be judged. */
static bool team_of_slot(uint8_t slot, uint8_t *out)
{
    mp_roster_t table;
    size_t      i;

    if (!mp_bridge_roster_current(&table)) {
        return false;
    }
    for (i = 0; i < table.count && i < MP_ROSTER_MAX_ENTRIES; ++i) {
        if (table.entry[i].slot == slot) {
            *out = table.entry[i].team;
            return true;
        }
    }
    return false;
}

bool mp_bridge_may_damage_peer(uint8_t peer_slot)
{
    mp_rules_t rules;
    uint8_t    mode = session_mode();
    uint8_t    peer_team = (uint8_t)MP_LOBBY_TEAM_NONE;

    ++lb.damage_asked;
    if ((mode != (uint8_t)MP_LOBBY_MODE_COOP && mode != (uint8_t)MP_LOBBY_MODE_TDM) ||
        !team_of_slot(peer_slot, &peer_team)) {
        ++lb.damage_unknown;
        return false;
    }
    /* The rules ride in the host's repeated note, so both sides judge a contact by the same
     * numbers. Before one has arrived this side uses the defaults, which refuse damage between
     * players in both games: a session whose rules are not known yet is not a session in which
     * anybody should be able to kill anybody. */
    if (lb.setup_known) {
        rules = lb.setup.rules;
    } else {
        mp_rules_default(&rules);
    }
    /* mp_rules and not mp_lobby directly: the rule set maps the teams switch onto the team each
     * player is treated as having before the one damage rule is asked, so "teams off" needs no
     * second case anywhere. */
    if (!mp_rules_may_damage(&rules, mode, lb.lobby.team, peer_team)) {
        ++lb.damage_refused;
        return false;
    }
    return true;
}

bool mp_bridge_lobby_gave_up(void)
{
    return lb.is_client && lb.client != NULL && !mp_session_is_connected(lb.client) &&
           !mp_session_connect_wanted(lb.client) &&
           mp_session_last_deny(lb.client) == MP_DENY_NONE && !mp_bridge_public_join_held();
}

/* Gave up, and the reason is the host's own leave notice rather than silence. Both clear the
 * wish to connect, so the count of notices is what tells them apart: one more than when this
 * lobby began means the host went on purpose. */
bool mp_bridge_lobby_host_left(void)
{
    return mp_bridge_lobby_gave_up() && mp_session_leaves(lb.client) != lb.leaves_seen;
}

void mp_bridge_connect_in_lobby(const char *address)
{
    uint32_t endpoint;

    if (lb.client == NULL || (lb.udp == NULL && !mp_bridge_public_bound()) || !lb.is_client ||
        mp_bridge_public_hold_join()) {
        return;
    }
    /* The session's own state decides, not a flag set once at install: a handshake under
     * way or a connected peer means there is nothing to begin. */
    if (mp_session_is_connected(lb.client) ||
        mp_session_peer(lb.client, 0)->state != MP_PEER_FREE) {
        return;
    }
    if (address != NULL && lb.udp != NULL) {
        endpoint = mp_udp_resolve(lb.udp, address);
        if (endpoint == 0u) {
            log_warning("the lobby cannot join: '%s' did not parse as a.b.c.d:port", address);
            return;
        }
        mp_bridge_set_host_endpoint(endpoint);   /* a corrected address on a second try */
    }
    mp_bridge_clear_connect_pending();
    log_info("joining from the lobby, before any level: the request states %u byte(s) of game "
             "data, builds and DLLs outside this release, %s",
             (unsigned)mp_session_statement_bytes(lb.client),
             mp_session_statement_bytes(lb.client) != 0u
                 ? "which the host judges before it makes a slot"
                 : "none, so the host admits it unjudged and the level's content note compares");
    mp_session_connect(lb.client, mp_bridge_host_endpoint());
}

void mp_bridge_set_client_handshake_mode(uint8_t mode)
{
    if (lb.client != NULL) {
        mp_session_set_mode(lb.client, mode);
    }
}

void mp_bridge_lobby_leave(void)
{
    if (lb.client != NULL) {
        mp_session_disconnect(lb.client);
    }
    /* The host too. Its peers used to be left connected and unaware when it backed out of
     * the lobby: keepalives went on from the idle pump, and a client sat on "connected,
     * waiting for the host" with the old level line for as long as it liked. The BYE this
     * sends is what lets a client notice. */
    if (!lb.is_client && lb.host != NULL) {
        mp_session_disconnect(lb.host);
    }
}

void mp_bridge_lobby_withdraw(void)
{
    mp_bridge_lobby_leave();
    mp_bridge_lobby_reset();
    ++lb.withdrawals;
    if (lb.withdrawn != NULL) {
        lb.withdrawn();
    }
}

void mp_bridge_lobby_set_withdrawn_listener(void (*listener)(void))
{
    lb.withdrawn = listener;
}

mp_deny_reason_t mp_bridge_lobby_last_deny(void)
{
    return lb.client != NULL ? mp_session_last_deny(lb.client) : MP_DENY_NONE;
}

uint32_t mp_bridge_lobby_denials(void)
{
    return lb.client != NULL ? mp_session_denied(lb.client) : 0u;
}

void mp_bridge_set_lobby_hero(uint8_t hero)
{
    lb.lobby.hero = hero > MP_LOBBY_HERO_MAX ? (uint8_t)MP_LOBBY_HERO_MAX : hero;
    /* Once, and after the assignment. The first of the two calls this used to make carried the
     * PREVIOUS hero to the host over the reliable channel, so every press cost a note that said
     * the wrong thing and a second that corrected it. */
    mp_bridge_set_lobby(lb.lobby.team, lb.lobby.ready != 0u);
}

void mp_bridge_lobby_reset(void)
{
    /* The count of world changes belongs to the SESSION and outlives the screen. A host that
     * leaves its lobby and opens it again is still the same host on the same socket, and a
     * counter that started over would repeat a number its clients had already acted on, so their
     * next start would be indistinguishable from the last one and they would stay behind.
     *
     * What this side has acted on does go, because in a fresh lobby it has acted on nothing: a
     * client that comes back to a host which is already in a level then follows it in, which is
     * what a repeated note is for. */
    uint8_t generation = lb.setup.generation;

    lb.setup_known         = false;
    lb.setup_dirty         = false;
    lb.start_seen          = false;
    lb.generation_acted_on = 0u;
    /* "Everybody left" needs somebody to have come first, in THIS session. It used to outlive
     * the session, so a host that had once had a client started its next session alone with
     * "all players have left" over the first frame. */
    lb.had_peer            = false;
    lb.lobby_owed          = false;
    mp_bridge_lobby_late_reset();
    mp_bridge_content_reset();
    mp_bridge_roster_forget();
    lb.leaves_seen = lb.client != NULL ? mp_session_leaves(lb.client) : 0u;
    memset(&lb.setup, 0, sizeof lb.setup);
    lb.setup.generation = generation;
}

void mp_bridge_lobby_report(bool is_host, bool is_client)
{
    if (is_host) {
        mp_bridge_announce_report();
    } else if (is_client) {
        log_info("  the lobby note: %u sent, %u unsent, %u sent late once the handshake was "
                 "through", (unsigned)lb.lobby_sent, (unsigned)lb.lobby_unsent,
                 (unsigned)lb.lobby_late);
    }
    if (lb.setup_known) {
        log_info("  the lobby: %s (%s), %u setup(s) sent, %u taken, %u torn, %u refused from a "
                 "client%s; world change %u, this side has acted on %u", lb.setup.title,
                 lb.setup.level, (unsigned)lb.setups_sent, (unsigned)lb.setups_taken,
                 (unsigned)lb.setups_torn, (unsigned)lb.setups_refused,
                 (lb.setup.flags & MP_LOBBY_F_STARTED) != 0u ? ", started" : "",
                 (unsigned)lb.setup.generation, (unsigned)lb.generation_acted_on);
    }
    log_info("  the friendly fire gate: %u contact(s) judged, %u refused by the rule set, %u "
             "refused because the game or the far player's team was not known; %u note(s) "
             "broadcast for another module, %u of them unsent",
             (unsigned)lb.damage_asked, (unsigned)lb.damage_refused, (unsigned)lb.damage_unknown,
             (unsigned)lb.broadcasts, (unsigned)lb.broadcasts_unsent);
    mp_bridge_content_report();
    log_info("  the session was left %u time(s) by withdrawing: disconnected and its setup "
             "forgotten", (unsigned)lb.withdrawals);
}
