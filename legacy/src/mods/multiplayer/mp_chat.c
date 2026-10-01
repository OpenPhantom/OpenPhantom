/* mp_chat.c: the chat of a session on this machine. See the header. */
#include "mp_chat.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_roster.h"
#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_roster.h"
#include "mp_session.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_CHAT_HOST_SLOT == MP_BRIDGE_HOST_SLOT, "the listen host's player is slot 0");

/* Module state because a process runs one session at a time. */
typedef struct chat_state {
    mp_session_t         *host;
    mp_session_t         *client;
    bool                  bound;
    bool                  is_client;
    bool                  was_host;    /* for the report: this machine has been a host */
    uint8_t               serial;      /* this side's newest say, 1..255, and 0 before the first */
    mp_chat_pace_t        own_pace;    /* this side's player, held back before a line is sent */
    mp_chat_host_t        host_rule;   /* a host's bucket for every slot, and its counts */
    mp_chat_line_t        history[MP_CHAT_HISTORY];
    size_t                next;        /* where the next line goes */
    size_t                count;
    mp_chat_side_counts_t side;
} chat_state_t;

static chat_state_t chat;

void mp_chat_bind(mp_session_t *host, mp_session_t *client, bool is_client)
{
    chat.host      = host;
    chat.client    = client;
    chat.is_client = is_client;
    chat.bound     = is_client ? client != NULL : host != NULL;
    if (chat.bound && !is_client) {
        chat.was_host = true;
    }
}

/* A line into the history, read back out of the bytes every player got, so this machine shows
 * exactly what the others show. Its own lines are told from the others' by the slot, which only
 * the host writes. */
static void keep(const mp_chat_line_note_t *note, uint8_t own_slot, uint32_t now_ms)
{
    mp_chat_line_t *line = &chat.history[chat.next];

    memset(line, 0, sizeof *line);
    line->slot = note->slot;
    memcpy(line->name, note->name, sizeof line->name);
    line->name[sizeof line->name - 1u] = '\0';
    memcpy(line->text, note->text, note->length);
    line->shown_ms = now_ms;
    chat.next = (chat.next + 1u) % MP_CHAT_HISTORY;
    if (chat.count < MP_CHAT_HISTORY) {
        ++chat.count;
    }
    if (note->slot == own_slot) {
        ++chat.side.came_back;
    } else {
        ++chat.side.shown;
    }
}

/* The host rule on one say, and if it makes a line, the line into this host's own history and out
 * to every connected player, the one who said it included. Every player is a queue of its own, and
 * nobody says a line twice: a copy a player's channel cannot take now is held for that player and
 * handed on as room returns. A copy is unsent only for a player whose hold is full as well, who is
 * sent away for it; the others get the line all the same. */
static mp_chat_host_verdict_t answer(uint8_t slot, const char *name, const uint8_t *say,
                                     size_t say_bytes, uint32_t now_ms)
{
    uint8_t                line[MP_CHAT_LINE_MAX_BYTES];
    size_t                 bytes = 0;
    size_t                 peers;
    size_t                 reached;
    mp_chat_line_note_t    note;
    mp_chat_host_verdict_t verdict;

    verdict = mp_chat_rule_host_line(&chat.host_rule, say, say_bytes, slot, name, now_ms, line,
                                     sizeof line, &bytes);
    if (verdict != MP_CHAT_HOST_LINE) {
        return verdict;
    }
    if (mp_chat_line_decode(line, bytes, &note)) {
        keep(&note, MP_CHAT_HOST_SLOT, now_ms);
    }
    peers   = mp_session_peer_count(chat.host);
    reached = mp_session_broadcast_or_hold(chat.host, line, bytes);
    mp_chat_rule_host_sent(&chat.host_rule, peers, reached);
    return verdict;
}

/* This side's own line, once it has passed its own bucket: a client sends it to its host and keeps
 * nothing until the host's line comes back; a host answers it at once, as slot 0 under its own
 * name. */
static mp_chat_say_result_t send_own(const char *text, size_t length, uint32_t now_ms)
{
    mp_chat_say_note_t say;
    uint8_t            note[MP_CHAT_SAY_MAX_BYTES];
    size_t             bytes;

    memset(&say, 0, sizeof say);
    say.serial = (uint8_t)(chat.serial % 255u + 1u);
    say.length = (uint8_t)length;
    memcpy(say.text, text, length);
    bytes = mp_chat_say_encode(&say, note, sizeof note);
    if (bytes == 0u) {
        ++chat.side.empty;
        return MP_CHAT_SAY_EMPTY;
    }
    if (!chat.is_client) {
        /* The host's bucket is wider than this side's own, so it holds nothing back here; the
         * two other answers are listed so that every say still ends in exactly one count. */
        chat.serial = say.serial;
        switch (answer(MP_CHAT_HOST_SLOT, mp_bridge_roster_name(), note, bytes, now_ms)) {
        case MP_CHAT_HOST_LINE:
            ++chat.side.sent;
            return MP_CHAT_SAY_SENT;
        case MP_CHAT_HOST_TOO_FAST:
            ++chat.side.held_back;
            return MP_CHAT_SAY_TOO_FAST;
        default:
            ++chat.side.empty;
            return MP_CHAT_SAY_EMPTY;
        }
    }
    if (mp_session_broadcast_reliable(chat.client, note, bytes) == 0u) {
        mp_chat_pace_give_back(&chat.own_pace, MP_CHAT_PACE_CLIENT_BURST,
                               MP_CHAT_PACE_CLIENT_EVERY_MS);
        ++chat.side.unsent;
        return MP_CHAT_SAY_UNSENT;
    }
    chat.serial = say.serial;
    ++chat.side.sent;
    return MP_CHAT_SAY_SENT;
}

mp_chat_say_result_t mp_chat_say(const char *text, size_t length, uint32_t now_ms)
{
    char line[MP_CHAT_TEXT_MAX + 1u];

    if (!chat.bound || (chat.is_client && !mp_session_is_connected(chat.client))) {
        return MP_CHAT_SAY_NO_SESSION;
    }
    ++chat.side.said;
    if (text == NULL || length > MP_CHAT_TEXT_MAX) {
        ++chat.side.empty;
        return MP_CHAT_SAY_EMPTY;
    }
    memcpy(line, text, length);
    line[length] = '\0';
    length = mp_chat_rule_trim(line, length);
    if (!mp_chat_rule_text_is_sound(line, length)) {
        ++chat.side.empty;
        return MP_CHAT_SAY_EMPTY;
    }
    if (!mp_chat_pace_take(&chat.own_pace, now_ms, MP_CHAT_PACE_CLIENT_BURST,
                           MP_CHAT_PACE_CLIENT_EVERY_MS)) {
        ++chat.side.held_back;
        return MP_CHAT_SAY_TOO_FAST;
    }
    return send_own(line, length, now_ms);
}

/* Only a host answers a say.
 *
 * The exit of a session forgets the chat at once, while the transport stays up until the level
 * has ended or a second has passed, and the drain goes on reading until it falls. A say that
 * arrives in that window, typically the last line somebody said before the host left, is claimed
 * and counted as after the session's end: nobody is here to answer it any more, and counting it
 * against the player's seat would read like a defect on the way.
 *
 * A say that reaches a machine bound as a client has no seat on this machine to come from, since
 * a client holds none, and is counted with the ones from no seat. The drain hands a say only to a
 * host, and the chat is bound beside the drain, so that would be a binding and a drain that
 * disagree. */
bool mp_chat_take_say(size_t peer_index, uint8_t sender_slot, const uint8_t *note, size_t bytes)
{
    if (!mp_chat_is_say(note, bytes)) {
        return false;
    }
    if (!chat.bound) {
        ++chat.side.after_end;
        return true;
    }
    if (chat.is_client) {
        ++chat.host_rule.counts.no_seat;
        return true;
    }
    (void)answer(sender_slot, mp_session_peer_name(chat.host, peer_index), note, bytes,
                 chat.host->now_ms);
    return true;
}

/* A line that arrives after the session's end, in the same window as above, is claimed and counted
 * as after the end rather than as torn: typically it is the echo of the line this side's player
 * said on the way out, and nothing was wrong with it. A line that reaches a machine bound as a host
 * has no client clock to be dated by and is refused with the torn ones; the drain hands a line only
 * to a client, so that too is a binding and a drain that disagree. */
bool mp_chat_take_line(uint8_t own_slot, const uint8_t *note, size_t bytes)
{
    mp_chat_line_note_t line;

    if (!mp_chat_is_line(note, bytes)) {
        return false;
    }
    if (!chat.bound) {
        ++chat.side.after_end;
        return true;
    }
    if (!chat.is_client || !mp_chat_line_decode(note, bytes, &line)) {
        ++chat.side.torn;
        return true;
    }
    keep(&line, own_slot, chat.client->now_ms);
    return true;
}

size_t mp_chat_newest(const mp_chat_line_t **out, size_t max)
{
    size_t shown = chat.count < max ? chat.count : max;
    size_t i;

    if (out == NULL) {
        return 0u;
    }
    for (i = 0; i < shown; ++i) {
        out[i] = &chat.history[(chat.next + MP_CHAT_HISTORY - shown + i) % MP_CHAT_HISTORY];
    }
    return shown;
}

void mp_chat_forget(void)
{
    chat.host      = NULL;
    chat.client    = NULL;
    chat.bound     = false;
    chat.is_client = false;
    chat.serial    = 0u;
    chat.next      = 0u;
    chat.count     = 0u;
    memset(&chat.own_pace, 0, sizeof chat.own_pace);
    memset(chat.host_rule.pace, 0, sizeof chat.host_rule.pace);
    memset(chat.history, 0, sizeof chat.history);
}

void mp_chat_counts(mp_chat_counts_t *out)
{
    if (out == NULL) {
        return;
    }
    out->side     = chat.side;
    out->host     = chat.host_rule.counts;
    out->was_host = chat.was_host;
}

void mp_chat_report(void)
{
    mp_chat_counts_t             counts;
    const mp_chat_side_counts_t *side = &counts.side;
    const mp_chat_host_counts_t *host = &counts.host;

    mp_chat_counts(&counts);
    log_info("the chat on this side: %u line(s) said, %u sent to the host, %u held back by the "
             "pace, %u refused as empty, %u unsent for a full channel; %u of this side's own came "
             "back from the host; %u line(s) from others shown, %u torn line(s) refused, %u after "
             "the session's end",
             (unsigned)side->said, (unsigned)side->sent, (unsigned)side->held_back,
             (unsigned)side->empty, (unsigned)side->unsent, (unsigned)side->came_back,
             (unsigned)side->shown, (unsigned)side->torn, (unsigned)side->after_end);
    if (!counts.was_host) {
        return;
    }
    log_info("the chat on the host: %u line(s) said by clients taken, %u refused (%u unsound, %u "
             "too fast, %u from no seat), %u said here; %u line(s) sent out as %u copies, %u "
             "copies unsent",
             (unsigned)host->taken, (unsigned)(host->unsound + host->too_fast + host->no_seat),
             (unsigned)host->unsound, (unsigned)host->too_fast, (unsigned)host->no_seat,
             (unsigned)host->said_here, (unsigned)host->lines_out, (unsigned)host->copies,
             (unsigned)host->copies_unsent);
}
