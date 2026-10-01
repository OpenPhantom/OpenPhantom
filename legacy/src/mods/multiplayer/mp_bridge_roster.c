/* mp_bridge_roster.c: who is here, told to everyone, and read back. */
#include "mp_bridge_roster.h"

#include "mp_events.h"
#include "mp_lobby.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A host repeats an unchanged roster this often, in substeps: once a second at 32 Hz, which is
 * the rate a ping display is worth reading at and a fifth of a kilobyte. */
#define ROSTER_REPEAT_SUBSTEPS 32u

/* How long a refused note waits before it is offered again, in substeps: a quarter second.
 *
 * The channel refuses when its queue of unacknowledged messages is full, and that queue empties on
 * an ACKNOWLEDGEMENT, never on a retry. Offering the same note again on the next substep therefore
 * cannot succeed; it only spends another decision and, while a savegame was crossing, another
 * chance for a chunk to take the slot instead. A field run counted 3277 such offers against 41
 * that went out. */
#define ROSTER_RETRY_SUBSTEPS 8u

typedef struct bridge_roster_state {
    char        name[MP_ROSTER_NAME_MAX];
    char        asset[MP_EVENT_ASSET_MAX];                      /* what this side is wearing */
    char        peer_asset[MP_SESSION_MAX_PEERS][MP_EVENT_ASSET_MAX];
    uint8_t     kind;                                        /* and what kind of name it is */
    uint8_t     peer_kind[MP_SESSION_MAX_PEERS];
    /* How big each body is drawn, in the hundredths the wire carries; 0 is its own size. The
     * authority's table is where a player who joins later reads it, the same hole the asset
     * field fills for what everybody wears. */
    uint16_t    scale;
    uint16_t    peer_scale[MP_SESSION_MAX_PEERS];
    uint8_t     own_worn;        /* the hero this side wears, plus one; 0 = nobody has said */
    uint8_t     peer_worn[MP_SESSION_MAX_PEERS];
    mp_lobby_t  own;             /* the host's own team and ready */
    mp_lobby_t  peer[MP_SESSION_MAX_PEERS];   /* what each peer last said; ready until it says */
    bool        peer_spoke[MP_SESSION_MAX_PEERS];
    bool        own_spoke;
    uint32_t    lobby_taken;
    uint32_t    lobby_torn;
    uint32_t    lobby_forgotten;   /* slots whose player left, taking their line with them */
    bool        have;            /* `current` holds a roster */
    mp_roster_t current;
    mp_roster_t last_sent;
    bool        sent_once;
    uint32_t    last_sent_substep;
    bool        refused_once;        /* a send was refused and the retry is waiting out its rest */
    uint32_t    refused_substep;
    uint32_t    sent;
    uint32_t    unsent;              /* the channel had no room */
    uint32_t    nobody;              /* due, but no peer is connected: not a failure */
    uint32_t    held;                /* due, but resting after a refusal */
    uint32_t    partial;             /* went out, but not every peer's channel had room */
    uint32_t    taken;
    uint32_t    torn;
    uint32_t    changes;
} bridge_roster_state_t;

static bridge_roster_state_t roster;

/* Two callers set it: the installation from the ini, and the menu after its apply, which is newer
 * than the ini's copy the DLL read at start. The order at the menu's apply is install, arm, then
 * the menu's name, so the menu wins. */
void mp_bridge_roster_set_name(const char *name)
{
    mp_roster_name_clean(name, roster.name);
}

const char *mp_bridge_roster_name(void)
{
    if (roster.name[0] == '\0') {
        mp_roster_name_clean(NULL, roster.name);
    }
    return roster.name;
}

/* There was a second comparison here, and it was half the defect.
 *
 * `names_moved` decided what to log, `mp_roster_equal` decided what to send, and the two answered
 * different questions about the same tables: the sender's included the round trip and was
 * therefore true almost every substep, the logger's left out the appearance and was therefore
 * false when a player changed model. Worse, neither ran against what was actually SENT, because a
 * refused send left `last_sent` behind, so after the first refusal the logger answered "changed"
 * for ever and wrote twelve hundred copies of a one player roster into a field log.
 *
 * One predicate now, mp_roster_differs_to_a_player, and it is asked against the table that
 * really went out. Two ways into the same state share the rule, or one of them
 * is a path nobody checked. The round trip is left out of it on purpose: it moves every second,
 * is sent all the same, and logged 821 lines in one run when it was not. */

/* One line per entry, so the log reads like the screen will. */
static void log_roster(const mp_roster_t *table, const char *why)
{
    size_t i;

    log_info("the roster (%s): %u player(s)", why, (unsigned)table->count);
    for (i = 0; i < table->count; ++i) {
        const mp_roster_entry_t *e = &table->entry[i];

        log_info("    slot %u  %-15s  team %u  hero %u  %s  ping %u ms%s%s", (unsigned)e->slot,
                 e->name, (unsigned)e->team, (unsigned)e->hero,
                 e->ready ? "ready" : "not ready", (unsigned)e->rtt_ms,
                 e->asset[0] == '\0' ? ""
                     : e->asset_kind == MP_SKIN_MODEL ? "  model " : "  actor ",
                 e->asset);
    }
}

/* How a peer looks: the worn hero, the size, the asset and its kind. Forgotten as one, in one
 * place, by both ways out: the peer leaving and the session being left. */
static void forget_peer_look(size_t i)
{
    roster.peer_worn[i]  = 0u;
    roster.peer_scale[i] = 0u;
    roster.peer_kind[i]  = 0u;
    memset(roster.peer_asset[i], 0, sizeof roster.peer_asset[i]);
}

/* The worn hero when one has been said, the pick until then. */
static uint8_t shown_hero(uint8_t worn_plus_one, uint8_t chosen)
{
    return worn_plus_one != 0u ? (uint8_t)(worn_plus_one - 1u) : chosen;
}

/* The table the host says: its own row first, then one for every connected peer, and a seat
 * that has emptied forgets whoever sat in it. */
static void build_the_table(mp_session_t *host, mp_roster_t *table)
{
    size_t i;

    memset(table, 0, sizeof *table);
    table->entry[0].slot  = 0u;
    table->entry[0].team  = roster.own.team;
    table->entry[0].ready = roster.own_spoke ? roster.own.ready : 1u;
    table->entry[0].hero  = shown_hero(roster.own_worn, roster.own.hero);
    memcpy(table->entry[0].asset, roster.asset, sizeof table->entry[0].asset);
    table->entry[0].asset_kind = roster.kind;
    table->entry[0].scale = roster.scale;
    memcpy(table->entry[0].name, mp_bridge_roster_name(), MP_ROSTER_NAME_MAX);
    table->count = 1u;
    for (i = 0; i < MP_SESSION_MAX_PEERS && table->count < MP_ROSTER_MAX_ENTRIES; ++i) {
        const mp_peer_t   *peer = mp_session_peer(host, i);
        mp_roster_entry_t *e;
        uint32_t           rtt;

        if (peer == NULL || peer->state != MP_PEER_CONNECTED) {
            /* An empty slot remembers nobody. Whoever takes it next says their own team,
             * ready and hero, and until they do they are the default rather than the player
             * who left: a new player used to wear the last one's team, and in a deathmatch the
             * two machines then judged the same hit by two different teams. */
            forget_peer_look(i);   /* what the last player wore is not the next's */
            if (roster.peer_spoke[i]) {
                roster.peer_spoke[i] = false;
                memset(&roster.peer[i], 0, sizeof roster.peer[i]);
                ++roster.lobby_forgotten;
            }
            continue;
        }
        e = &table->entry[table->count++];
        e->slot  = mp_session_slot_of_peer(i);   /* the slot the host told this peer */
        /* In until they say otherwise: nothing gates on ready yet, and a player who has
         * crossed the handshake is here. */
        e->team  = roster.peer[i].team;
        e->ready = roster.peer_spoke[i] ? roster.peer[i].ready : 1u;
        e->hero  = shown_hero(roster.peer_worn[i], roster.peer[i].hero);
        memcpy(e->asset, roster.peer_asset[i], sizeof e->asset);
        e->asset_kind = roster.peer_kind[i];
        e->scale = roster.peer_scale[i];
        rtt = mp_session_peer_rtt_ms(host, i);
        e->rtt_ms = rtt > 65535u ? 65535u : (uint16_t)rtt;
        mp_roster_name_clean(mp_session_peer_name(host, i), e->name);
    }
}

bool mp_bridge_roster_host_tick(mp_session_t *host, uint32_t substep)
{
    mp_roster_t table;
    uint8_t     note[MP_ROSTER_BYTES];
    size_t      bytes;
    size_t      reached;
    bool        due;
    bool        changed;

    if (host == NULL) {
        return false;
    }
    build_the_table(host, &table);

    changed = !roster.sent_once || mp_roster_differs_to_a_player(&table, &roster.last_sent);
    due     = changed || substep - roster.last_sent_substep >= ROSTER_REPEAT_SUBSTEPS;
    roster.current = table;
    roster.have    = true;
    if (!due) {
        return false;
    }
    /* Nobody to tell is not a failure, and counting it as one made the two numbers that matter
     * unreadable. A host sitting alone in its lobby has no connected peer, so the broadcast
     * reaches nought of them every single substep. That is the ordinary state of a lobby waiting
     * for someone, not a channel that refused anything.
     *
     * The table is recorded as what was said, to nobody, so `changed` is false on the next
     * substep and this counts once per repeat rather than thirty two times a second, which is
     * what a reader takes "quiet" to mean. A join changes the count, so the first table after
     * one is a change and goes out at once. */
    if (mp_session_peer_count(host) == 0u) {
        ++roster.nobody;
        roster.last_sent         = table;
        roster.sent_once         = true;
        roster.last_sent_substep = substep;
        return false;
    }
    if (roster.refused_once &&
        (uint32_t)(substep - roster.refused_substep) < ROSTER_RETRY_SUBSTEPS) {
        ++roster.held;
        return false;
    }
    /* The encoded length is what goes out, not the buffer's.
     *
     * The note became variable length when a padded one of 866 bytes turned out to need a packet
     * with under 304 bytes of payload, which an in-level enemy block never leaves. (The number
     * read 317 until 2026-09-09, when the channel was found to be sizing itself against 1200
     * bytes it never had; the carrier leaves it 1187, so the room is 1174 less the payload.) The
     * buffer
     * stayed at its full size, as a buffer should, and the send went on quoting `sizeof note`.
     *
     * The receiver's recogniser tests the length against the count the note declares, exactly, as
     * every recogniser on this channel does. So a table of two players arrived claiming two and
     * measuring eight hundred and sixty six, matched nothing, and was dropped without being
     * counted as torn, because it was never recognised as a roster at all. A field run showed a
     * host sending fourteen and a client taking none, and the visible half of that was a lobby
     * list with nobody in it and a far body wearing the wrong hero. */
    bytes   = mp_roster_encode(&table, note, sizeof note);
    reached = bytes == MP_ROSTER_BYTES_FOR(table.count)
                  ? mp_session_broadcast_reliable(host, note, bytes)
                  : 0u;
    if (reached == 0u) {
        ++roster.unsent;
        roster.refused_once    = true;
        roster.refused_substep = substep;
        return false;
    }
    /* A broadcast that reached some peers and not others is recorded as sent, because it was,
     * and the peer whose channel had no room gets the table on the next repeat. That is a
     * second's delay for one player, and it is counted so a run can say it happened; without
     * the count a partial failure is invisible by construction. */
    if (reached < mp_session_peer_count(host)) {
        ++roster.partial;
    }
    /* The line is written for what WENT OUT, never for what was merely built: a log that says a
     * roster changed when nobody was told is how twelve hundred lines of a one player lobby got
     * into a field log and buried the counters underneath them. */
    if (changed) {
        ++roster.changes;
        log_roster(&table, "built");
    }
    roster.refused_once      = false;
    roster.last_sent         = table;
    roster.sent_once         = true;
    roster.last_sent_substep = substep;
    ++roster.sent;
    return true;
}

bool mp_bridge_roster_take(const uint8_t *note, size_t bytes)
{
    mp_roster_t table;

    if (!mp_roster_is_roster(note, bytes)) {
        return false;
    }
    if (!mp_roster_decode(note, bytes, &table)) {
        ++roster.torn;
        return true;
    }
    ++roster.taken;
    if (!roster.have || mp_roster_differs_to_a_player(&roster.current, &table)) {
        ++roster.changes;
        log_roster(&table, "received");
    }
    roster.current = table;
    roster.have    = true;
    return true;
}

/* The kind that goes with a cleaned asset: none for an asset that is not known, and a kind this
 * build does not know makes the asset not known, because read as an actor it builds a body. */
static void keep_asset(char field[MP_EVENT_ASSET_MAX], uint8_t *field_kind, const char *asset,
                       uint8_t kind)
{
    mp_roster_asset_clean(kind <= (uint8_t)MP_SKIN_KIND_MAX ? asset : NULL, field);
    *field_kind = field[0] != '\0' ? kind : (uint8_t)MP_SKIN_CHARACTER;
}

void mp_bridge_roster_set_own_asset(const char *asset, uint8_t kind)
{
    keep_asset(roster.asset, &roster.kind, asset, kind);
}

void mp_bridge_roster_set_own_scale(uint16_t scale)
{
    roster.scale = mp_wire_scale_is_sound(scale) ? scale : 0u;
}

void mp_bridge_roster_set_own_hero(uint8_t hero)
{
    roster.own_worn = hero <= (uint8_t)MP_LOBBY_HERO_MAX ? (uint8_t)(hero + 1u) : 0u;
}

void mp_bridge_roster_note_peer_scale(size_t peer_index, uint16_t scale)
{
    if (peer_index < MP_SESSION_MAX_PEERS) {
        roster.peer_scale[peer_index] = mp_wire_scale_is_sound(scale) ? scale : 0u;
    }
}

void mp_bridge_roster_note_peer_hero(size_t peer_index, uint8_t hero)
{
    if (peer_index < MP_SESSION_MAX_PEERS) {
        roster.peer_worn[peer_index] = hero <= (uint8_t)MP_LOBBY_HERO_MAX
                                           ? (uint8_t)(hero + 1u) : 0u;
    }
}

void mp_bridge_roster_note_peer_asset(size_t peer_index, const char *asset, uint8_t kind)
{
    if (peer_index < MP_SESSION_MAX_PEERS) {
        keep_asset(roster.peer_asset[peer_index], &roster.peer_kind[peer_index], asset, kind);
    }
}

void mp_bridge_roster_set_own(uint8_t team, bool ready, uint8_t hero)
{
    roster.own.team  = team > MP_LOBBY_TEAM_MAX ? (uint8_t)MP_LOBBY_TEAM_MAX : team;
    roster.own.ready = ready ? 1u : 0u;
    roster.own.hero  = hero > MP_LOBBY_HERO_MAX ? (uint8_t)MP_LOBBY_HERO_MAX : hero;
    roster.own_spoke = true;
}

bool mp_bridge_roster_take_lobby(size_t peer_index, const uint8_t *note, size_t bytes)
{
    mp_lobby_t lobby;

    if (!mp_lobby_is_lobby(note, bytes)) {
        return false;
    }
    if (!mp_lobby_decode(note, bytes, &lobby) || peer_index >= MP_SESSION_MAX_PEERS) {
        ++roster.lobby_torn;
        return true;
    }
    roster.peer[peer_index]       = lobby;
    roster.peer_spoke[peer_index] = true;
    ++roster.lobby_taken;
    return true;
}

/* A session that was left takes its table with it. A client kept the last host's list into its
 * next lobby, where it stood under "connecting" as though those players were there, and the line
 * saying it waits for the host never came back. A host rebuilds its own on the next pump. */
void mp_bridge_roster_forget(void)
{
    size_t i;

    roster.have = false;
    /* And how its players looked. A client writes its host's look under peer 0, where no lobby
     * note ever clears it, and hosting next it gave that look to its own first peer. */
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        forget_peer_look(i);
    }
}

bool mp_bridge_roster_current(mp_roster_t *out)
{
    if (out == NULL || !roster.have) {
        return false;
    }
    *out = roster.current;
    return true;
}

void mp_bridge_roster_report(const mp_session_t *client, bool is_host)
{
    /* The three reasons a due roster did not go out, apart rather than in one heap. They say
     * opposite things: `nobody` is a quiet lobby and needs nothing; `unsent` is a channel with no
     * room and points at whoever else is filling it; `held` is this module resting after such a
     * refusal and should be a small multiple of `unsent`. One number for all three read 3277 and
     * said none of it. */
    log_info("  the roster: %u sent (%u of them reaching only some of the peers), %u the channel "
             "had no room for, %u with nobody to tell, %u held back while resting, %u taken, %u "
             "torn, %u change(s); this player is '%s'",
             (unsigned)roster.sent, (unsigned)roster.partial, (unsigned)roster.unsent,
             (unsigned)roster.nobody, (unsigned)roster.held, (unsigned)roster.taken,
             (unsigned)roster.torn, (unsigned)roster.changes, mp_bridge_roster_name());
    log_info("  the lobby: %u note(s) taken, %u torn, %u forgotten when their player left; "
             "this side team %u, %s", (unsigned)roster.lobby_taken, (unsigned)roster.lobby_torn,
             (unsigned)roster.lobby_forgotten, (unsigned)roster.own.team,
             roster.own_spoke ? (roster.own.ready ? "ready" : "not ready") : "never said");
    if (roster.have) {
        log_roster(&roster.current, is_host ? "as built here" : "as the host sent it");
    }
    if (!is_host && client != NULL && mp_session_is_connected(client)) {
        log_info("    this side's own round trip to the host: %u ms",
                 (unsigned)mp_session_peer_rtt_ms(client, 0u));
    }
}
