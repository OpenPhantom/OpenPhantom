/* mp_session_hold.c: every reliable send of a session, and the hold behind each peer's channel.
 *
 * A broadcast is one send per peer, and each peer's queue is its own; this is what ENet,
 * GameNetworkingSockets and yojimbo do, and what this session did not. Before the hold, a
 * broadcast one channel refused while another took it was counted as sent, and the player behind
 * the full channel simply never saw it. Six callers do not repeat a refused note, so for them that
 * was a loss. What a channel cannot take now is held here for that peer alone and handed on as
 * room returns, and the one thing that may not happen is a message overtaking another on its way
 * to the same peer: a single send is refused while anything is held for its peer.
 *
 * With two players nothing changes. A message is held only when some other peer took it, or when
 * a peer is held for already; a lone peer whose channel is full is refused exactly as it always
 * was, and every caller that repeats a note still repeats it. The one deliberate difference is
 * mp_session_send_or_hold, which holds where a caller with no second chance used to lose, and
 * mp_session_broadcast_or_hold, which is that send for every peer.
 *
 * A peer that cannot be held for is sent away with its own reason, the way Quake 3 drops a client
 * whose reliable commands overflow ("Server command overflow") and GameNetworkingSockets refuses a
 * send past its buffer with k_EResultLimitExceeded. It is the peer that goes, never the message for
 * everybody else.
 *
 * A broadcast of one of the nine state notes (mp_state_note_rule) is the sender's own newest
 * state, and it goes as that, in the channel and in the hold alike: an equal copy on its way is
 * enough, and a newer one replaces the older rather than queueing behind it. A single send, and the
 * send that holds, pass what they are given on as an event, whatever its tag: the relays use them
 * for notes of several senders, and two senders' digests are not one state.
 */
#include "mp_session_hold.h"

#include "mp_session_packets.h"

#include "mp_channel.h"
#include "mp_hold.h"
#include "mp_session.h"
#include "mp_state_note_rule.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_peer_t *connected_peer(mp_session_t *session, size_t peer_index)
{
    if (peer_index >= MP_SESSION_MAX_PEERS ||
        session->peers[peer_index].state != MP_PEER_CONNECTED) {
        return NULL;
    }
    return &session->peers[peer_index];
}

/* Said once per peer that goes, because it is the one line that names the machine and the reason
 * together; the report counts the rest. */
static void send_away(mp_session_t *session, mp_peer_t *peer, const char *why)
{
    log_warning("peer %u (%s) is sent away for falling behind: %s; %u message(s) were held for "
                "it, the oldest for %u ms, and the next went to everybody else",
                (unsigned)(peer - session->peers), peer->name, why,
                (unsigned)mp_hold_count(&peer->hold),
                (unsigned)mp_hold_oldest_age_ms(&peer->hold, session->now_ms));
    mp_session_send_away_behind(session, peer);
}

/* Every kind this session sends as a state needs a row of its own in each channel: a kind that
 * finds every row taken cannot wait behind its shrunk copy and is refused instead. */
_Static_assert(MP_STATE_NOTE_KINDS <= MP_CHANNEL_STATE_KINDS,
               "a channel keeps a row for every kind of state note");

/* One note on its way: an event (kind 0), or the newest copy of a state with the key it has
 * without its clock. */
typedef struct note {
    const void *data;
    size_t      bytes;
    uint8_t     kind;
    uint32_t    key;
} note_t;

static note_t an_event(const void *data, size_t bytes)
{
    note_t note;

    note.data  = data;
    note.bytes = bytes;
    note.kind  = 0u;
    note.key   = 0u;
    return note;
}

/* Whether the peer's channel would take the note now, and taking it; one decision for both. */
static bool channel_would_take(const mp_peer_t *peer, const note_t *note)
{
    if (note->kind == 0u) {
        return mp_channel_can_send(&peer->channel, note->bytes);
    }
    return mp_channel_state_would_take(&peer->channel, note->kind, note->key, note->bytes);
}

static bool channel_take(mp_peer_t *peer, const note_t *note)
{
    if (note->kind == 0u) {
        return mp_channel_send(&peer->channel, note->data, note->bytes);
    }
    return mp_channel_send_state(&peer->channel, note->kind, note->key, note->data,
                                 note->bytes) != MP_CHANNEL_STATE_REFUSED;
}

/* A host only. A client has one peer, the host, and a note it cannot send now is either repeated
 * by its caller or was never the kind a client passes on; there is nobody behind whom to wait.
 *
 * A state is held as its newest copy: when the newest copy already held, or else the newest on its
 * way in the channel, says the same, this one is left out; otherwise the one held is marked dead
 * and this goes behind everything, so the peer gets the newest state once and in order. */
static bool hold_note(mp_session_t *session, mp_peer_t *peer, const note_t *note)
{
    mp_hold_entry_t entry;
    uint32_t        newest = 0u;

    if (session->role != MP_SESSION_HOST) {
        return false;
    }
    if (note->kind != 0u) {
        bool known = mp_hold_newest_key(&peer->hold, note->kind, &newest) ||
                     mp_channel_state_newest_key(&peer->channel, note->kind, &newest);

        if (known && newest == note->key) {
            mp_channel_state_count_unchanged(&peer->channel, note->kind);
            return true;
        }
        (void)mp_hold_supersede(&peer->hold, note->kind);
    }
    entry.kind    = note->kind;
    entry.key     = note->key;
    entry.held_ms = session->now_ms;
    entry.bytes   = note->bytes;
    if (mp_hold_put(&peer->hold, &entry, note->data, session->now_ms)) {
        return true;
    }
    send_away(session, peer, "its hold could take no more");
    return false;
}

bool mp_session_send_reliable(mp_session_t *session, size_t peer_index, const void *data,
                              size_t bytes)
{
    mp_peer_t *peer = connected_peer(session, peer_index);

    if (peer == NULL || !mp_hold_empty(&peer->hold)) {
        return false;
    }
    return mp_channel_send(&peer->channel, data, bytes);
}

bool mp_session_send_or_hold(mp_session_t *session, size_t peer_index, const void *data,
                             size_t bytes)
{
    mp_peer_t *peer = connected_peer(session, peer_index);
    note_t     note = an_event(data, bytes);

    if (peer == NULL || bytes > MP_CHANNEL_MESSAGE_BYTES || (data == NULL && bytes > 0u)) {
        return false;   /* no channel carries it, so no hold may keep it either */
    }
    if (mp_hold_empty(&peer->hold) && channel_take(peer, &note)) {
        return true;
    }
    if (!hold_note(session, peer, &note)) {
        return false;
    }
    ++session->singles_held;
    return true;
}

/* One send or hold per peer, so a peer sent away for a full hold costs only its own copy. */
size_t mp_session_broadcast_or_hold(mp_session_t *session, const void *data, size_t bytes)
{
    size_t reached = 0;
    size_t i;

    if (session == NULL) {
        return 0u;
    }
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state == MP_PEER_CONNECTED &&
            mp_session_send_or_hold(session, i, data, bytes)) {
            ++reached;
        }
    }
    return reached;
}

size_t mp_session_broadcast_reliable(mp_session_t *session, const void *data, size_t bytes)
{
    bool   direct[MP_SESSION_MAX_PEERS];
    bool   any_direct = false;
    bool   any_held   = false;
    bool   held_some  = false;
    size_t reached    = 0;
    size_t i;
    note_t note = an_event(data, bytes);

    if (bytes > MP_CHANNEL_MESSAGE_BYTES || (data == NULL && bytes > 0u)) {
        return 0u;
    }
    /* The sender's own state, if it is one: the only place a state is recognised as such. */
    (void)mp_state_note_classify((const uint8_t *)data, bytes, &note.kind, &note.key);
    /* Asked of every peer before anything is queued anywhere, so a refusal leaves nothing behind
     * on any peer: the caller that repeats it would otherwise send some peers the note twice. */
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = &session->peers[i];

        direct[i] = false;
        if (peer->state != MP_PEER_CONNECTED) {
            continue;
        }
        if (!mp_hold_empty(&peer->hold)) {
            any_held = true;
            continue;
        }
        direct[i]  = channel_would_take(peer, &note);
        any_direct = any_direct || direct[i];
    }
    if (!any_direct && !any_held) {
        return 0u;
    }
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        mp_peer_t *peer = &session->peers[i];

        if (peer->state != MP_PEER_CONNECTED) {
            continue;
        }
        if (direct[i]) {
            reached += channel_take(peer, &note) ? 1u : 0u;
            continue;
        }
        if (hold_note(session, peer, &note)) {
            ++reached;
            held_some = true;
        }
    }
    if (held_some) {
        ++session->broadcasts_partial;
    }
    return reached;
}

size_t mp_session_due_bytes(mp_session_t *session, size_t peer_index, size_t limit)
{
    mp_peer_t *peer = connected_peer(session, peer_index);

    if (peer == NULL) {
        return 0u;
    }
    /* Emptied first, so the enemy block is sized with what the hold hands the channel now, and
     * not with a channel that looks quieter than the next packet will be. */
    mp_session_hold_flush(session, peer);
    mp_channel_state_promote(&peer->channel);
    return mp_channel_due_bytes(&peer->channel, session->now_ms, limit);
}

void mp_session_hold_flush(mp_session_t *session, mp_peer_t *peer)
{
    uint8_t         message[MP_CHANNEL_MESSAGE_BYTES];
    mp_hold_entry_t entry;
    note_t          note;

    while (mp_hold_oldest(&peer->hold, &entry, message, sizeof message)) {
        note       = an_event(message, entry.bytes);
        note.kind  = entry.kind;
        note.key   = entry.key;
        if (!channel_take(peer, &note)) {
            return;   /* the rest waits for the next acknowledged packet, in order */
        }
        mp_hold_pop(&peer->hold, session->now_ms);
    }
}

bool mp_session_hold_overdue(mp_session_t *session, mp_peer_t *peer)
{
    if (mp_hold_empty(&peer->hold) ||
        mp_hold_oldest_age_ms(&peer->hold, session->now_ms) < MP_SESSION_HOLD_AGE_MS ||
        (uint32_t)(session->now_ms - peer->last_recv_ms) >= MP_SESSION_PUMPING_MS) {
        return false;
    }
    send_away(session, peer, "its oldest held message waited too long while it went on sending");
    return true;
}

void mp_session_hold_remember(mp_session_t *session, const mp_peer_t *peer)
{
    mp_session_dropped_t *slot = &session->dropped[0];
    size_t                i;

    /* A free place, or else the one whose answering ends first. */
    for (i = 0; i < MP_SESSION_DROPPED_RECENT; ++i) {
        mp_session_dropped_t *candidate = &session->dropped[i];

        if (!candidate->used) {
            slot = candidate;
            break;
        }
        if ((int32_t)(candidate->until_ms - slot->until_ms) < 0) {
            slot = candidate;
        }
    }
    slot->used          = true;
    slot->connection_id = peer->connection_id;
    slot->client_salt   = peer->client_salt;
    slot->until_ms      = session->now_ms + MP_SESSION_DROP_ANSWER_MS;
    slot->answered_ms   = session->now_ms;
}

void mp_session_hold_answer(mp_session_t *session, uint32_t endpoint, uint64_t connection_id)
{
    size_t i;

    for (i = 0; i < MP_SESSION_DROPPED_RECENT; ++i) {
        mp_session_dropped_t *slot = &session->dropped[i];

        if (!slot->used) {
            continue;
        }
        if ((int32_t)(session->now_ms - slot->until_ms) >= 0) {
            slot->used = false;
            continue;
        }
        if (slot->connection_id != connection_id) {
            continue;
        }
        /* At the pace a handshake packet is repeated, so a client sending thirty packets a second
         * gets four answers, not thirty. The answer is smaller than what drew it. */
        if ((uint32_t)(session->now_ms - slot->answered_ms) >= MP_SESSION_RESEND_MS) {
            slot->answered_ms = session->now_ms;
            mp_session_send_notice(session, endpoint, slot->client_salt, MP_DENY_BEHIND);
            ++session->behind_answers;
        }
        return;
    }
}

void mp_session_hold_totals(const mp_session_t *session, mp_session_hold_totals_t *out)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (session == NULL) {
        return;
    }
    out->broadcasts_partial = session->broadcasts_partial;
    out->singles_held       = session->singles_held;
    out->dropped_behind     = session->dropped_behind;
    out->answers            = session->behind_answers;
    /* Every slot, not every connected peer: a hold keeps its counters until a peer connects to
     * that slot again, like the channel beside it, so a peer that was sent away is still here. */
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_hold_t *hold = &session->peers[i].hold;

        out->held       += hold->held;
        out->held_bytes += hold->held_bytes;
        out->delivered  += hold->delivered;
        if (out->deepest < (uint32_t)hold->most_live) {
            out->deepest      = (uint32_t)hold->most_live;
            out->deepest_peer = (uint32_t)i;
        }
        if (out->longest_ms < hold->longest_ms) {
            out->longest_ms = hold->longest_ms;
        }
    }
}

void mp_session_state_totals(const mp_session_t *session, uint8_t kind,
                             mp_channel_state_kind_t *out)
{
    size_t i;
    size_t j;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->kind = kind;
    for (i = 0; session != NULL && i < MP_SESSION_MAX_PEERS; ++i) {
        for (j = 0; j < MP_CHANNEL_STATE_KINDS; ++j) {
            const mp_channel_state_kind_t *row = &session->peers[i].channel.state_kinds[j];

            if (row->kind != kind) {
                continue;
            }
            out->sent      += row->sent;
            out->replaced  += row->replaced;
            out->shrunk    += row->shrunk;
            out->unchanged += row->unchanged;
            out->waited    += row->waited;
        }
    }
}

bool mp_session_peer_dropped_behind(const mp_session_t *session, size_t index)
{
    return session != NULL && index < MP_SESSION_MAX_PEERS &&
           session->peers[index].dropped_behind;
}
