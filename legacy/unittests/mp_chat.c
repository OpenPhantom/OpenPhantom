/* mp_chat.c: the chat of a session as the module runs it, with the far side played by hand.
 *
 * A listen host and two clients on real localhost sockets in this one process. The host reads its
 * clients through the bridge's own drain, so the line in the drain that hands a say to the chat is
 * part of what is tested; the clients read their lines by hand through the codec, until each in
 * turn is read through a client's drain of its own.
 *
 *   - a say from the client at peer index i becomes one line naming slot i + 1 and the name that
 *     client gave, for every player, the one who said it included, and in the host's history;
 *   - the host's own line goes the same way, as slot 0 under the host's name;
 *   - a line a client writes is refused before the chat sees it, and a torn say goes nowhere;
 *   - the host answers five at once from one slot and not a sixth; the typing side holds its own
 *     fourth back, and a text of spaces or of a byte no line may carry is refused as empty;
 *   - the history keeps the newest thirty two, oldest first and newest last;
 *   - a line for players whose channels are full is held for each of them and reaches them once
 *     there is room, and no copy of it counts as unsent;
 *   - a client's half through the drain of a client: its say leaves with a fresh serial, a line of
 *     its own and one of another player's are kept, a torn one is not, and a say the channel does
 *     not take costs no line of the bucket;
 *   - a second client, slot 2, read through a drain bound before its slot byte: until the byte
 *     arrives no line is its own, afterwards its own slot's line came back and slot 1's is shown;
 *   - the one exit forgets it all, and without a session nothing is said; a say or a line that
 *     still arrives after it counts as after the session's end and as nothing else;
 *   - last, a copy for a player whose hold is full as well is the one copy unsent, and that
 *     player is sent away while the others get the line.
 *
 * Every count of the two report lines is read after each part, against the number of lines that
 * part said, sent and received.
 *
 * Skips cleanly on a machine with no Winsock, like the socket tests do.
 *
 * SIZE NOTE: over 600 lines. Every part runs on the same three sessions and the one chat module of
 * the process, in order, because each part starts from the state the one before left: the buckets,
 * the history, the counts and the slots the drains were told. Split into two programs, each would
 * carry its own copy of the room and the readers, which is most of the first two hundred lines.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_roster.h"
#include "mp_channel.h"
#include "mp_chat.h"
#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_hold.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_udp.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 2
#define STEP_MS 5u
#define SETTLE  30

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_session_t      s_host;
static mp_session_t      s_client[CLIENTS];
static mp_udp_t          s_host_udp;
static mp_udp_t          s_client_udp[CLIENTS];
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport[CLIENTS];
static mp_bridge_drain_t s_drain;
static mp_bridge_drain_t s_client_drain[CLIENTS];
static char              s_address[32];

static const char *const NAMES[CLIENTS] = { "Ann", "Bob" };

/* What each client heard, and what the host heard when the test plays the host by hand. */
typedef struct heard {
    unsigned            lines;
    unsigned            others;   /* anything that was not a line */
    mp_chat_line_note_t last;
} heard_t;

static heard_t s_heard[CLIENTS];

typedef struct host_heard {
    unsigned           says;
    mp_chat_say_note_t last;
} host_heard_t;

static host_heard_t s_host_heard;

/* Whether the host is the chat's, read through the bridge's drain, or the test's, read by hand,
 * and the same for each client. */
static bool s_host_drained = true;
static bool s_client_drained[CLIENTS];

static void read_client(int which)
{
    uint8_t             note[MP_CHANNEL_MESSAGE_BYTES];
    size_t              bytes = 0;
    mp_chat_line_note_t line;

    while (mp_session_read_reliable(&s_client[which], 0, note, sizeof note, &bytes)) {
        if (mp_chat_line_decode(note, bytes, &line)) {
            ++s_heard[which].lines;
            s_heard[which].last = line;
        } else {
            ++s_heard[which].others;
        }
    }
}

static void read_host_by_hand(void)
{
    uint8_t            note[MP_CHANNEL_MESSAGE_BYTES];
    size_t             bytes = 0;
    size_t             peer;
    mp_chat_say_note_t say;

    for (peer = 0; peer < MP_SESSION_MAX_PEERS; ++peer) {
        while (mp_session_read_reliable(&s_host, peer, note, sizeof note, &bytes)) {
            if (mp_chat_say_decode(note, bytes, &say)) {
                ++s_host_heard.says;
                s_host_heard.last = say;
            }
        }
    }
}

/* One step of the room: the host's session and its reader, then every client's. */
static void step(uint32_t *now)
{
    int i;

    *now += STEP_MS;
    mp_session_update(&s_host, *now);
    if (s_host_drained) {
        mp_bridge_drain_reliable_notes(&s_drain);
    } else {
        read_host_by_hand();
    }
    for (i = 0; i < CLIENTS; ++i) {
        mp_session_update(&s_client[i], *now);
        if (s_client_drained[i]) {
            mp_bridge_drain_reliable_notes(&s_client_drain[i]);
        } else {
            read_client(i);
        }
    }
}

static void settle(uint32_t *now, int steps)
{
    int i;

    for (i = 0; i < steps; ++i) {
        step(now);
    }
}

static bool connect_everybody(uint32_t *now)
{
    int i;
    int round;

    s_host_transport = mp_udp_transport(&s_host_udp);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xC4A7u);
    text_format(s_address, sizeof s_address, "127.0.0.1:%u",
                (unsigned)mp_udp_local_port(&s_host_udp));
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_HOST, &s_host, NULL);
    for (i = 0; i < CLIENTS; ++i) {
        if (!mp_udp_init(&s_client_udp[i], 0)) {
            return false;
        }
        s_client_transport[i] = mp_udp_transport(&s_client_udp[i]);
        mp_session_init(&s_client[i], MP_SESSION_CLIENT, &s_client_transport[i],
                        0xC0u + (uint32_t)i);
        mp_session_set_name(&s_client[i], NAMES[i]);
        mp_session_connect(&s_client[i], mp_udp_resolve(&s_client_udp[i], s_address));
        for (round = 0; round < 400 && !mp_session_is_connected(&s_client[i]); ++round) {
            step(now);
        }
    }
    settle(now, SETTLE);
    return mp_session_is_connected(&s_client[0]) && mp_session_is_connected(&s_client[1]) &&
           mp_session_peer_count(&s_host) == 2u;
}

/* A say as a client's chat puts it out, sent by hand. */
static bool client_says(int which, uint8_t serial, const char *text)
{
    mp_chat_say_note_t say;
    uint8_t            note[MP_CHAT_SAY_MAX_BYTES];
    size_t             bytes;

    memset(&say, 0, sizeof say);
    say.serial = serial;
    say.length = (uint8_t)strlen(text);
    memcpy(say.text, text, say.length);
    bytes = mp_chat_say_encode(&say, note, sizeof note);
    return bytes != 0u && mp_session_send_reliable(&s_client[which], 0, note, bytes);
}

/* The host's line to one client, sent by hand, whole or with its length byte one too many. */
static bool host_sends_line(size_t peer, uint8_t slot, uint8_t serial, const char *name,
                            const char *text, bool torn)
{
    mp_chat_line_note_t line;
    uint8_t             note[MP_CHAT_LINE_MAX_BYTES];
    size_t              bytes;

    memset(&line, 0, sizeof line);
    line.slot   = slot;
    line.serial = serial;
    memcpy(line.name, name, strlen(name));
    line.length = (uint8_t)strlen(text);
    memcpy(line.text, text, line.length);
    bytes = mp_chat_line_encode(&line, note, sizeof note);
    if (torn) {
        note[MP_CHAT_LINE_HEAD_BYTES - 1u] = (uint8_t)(line.length + 1u);
    }
    return bytes != 0u && mp_session_send_reliable(&s_host, peer, note, bytes);
}

/* The one reliable byte a host tells each client, its world slot, sent by hand. */
static bool host_tells_slot(size_t peer, uint8_t slot)
{
    return mp_session_send_reliable(&s_host, peer, &slot, sizeof slot);
}

/* Messages nobody reads into one channel until it takes no more, and how many it took. */
static unsigned fill_channel(mp_session_t *session, size_t peer)
{
    uint8_t  junk[8];
    unsigned stuffed = 0u;

    memset(junk, 0x7Eu, sizeof junk);
    while (stuffed < 1000u && mp_session_send_reliable(session, peer, junk, sizeof junk)) {
        ++stuffed;
    }
    return stuffed;
}

static bool heard_line(int which, uint8_t slot, const char *name, const char *text)
{
    const mp_chat_line_note_t *line = &s_heard[which].last;

    return line->slot == slot && strcmp(line->name, name) == 0 && strcmp(line->text, text) == 0;
}

static bool newest_is(uint8_t slot, const char *name, const char *text)
{
    const mp_chat_line_t *newest[1];

    return mp_chat_newest(newest, 1u) == 1u && newest[0]->slot == slot &&
           strcmp(newest[0]->name, name) == 0 && strcmp(newest[0]->text, text) == 0;
}

/* Every count of the two report lines since `before`, against what the part that ran should have
 * made of them. The fields in the order of mp_chat.h and mp_chat_rule.h: said, sent, held back,
 * empty, unsent, came back, shown, torn, after the end; and taken, unsound, too fast, no seat,
 * said here, lines out, copies, copies unsent. */
static void check_counts(const mp_chat_counts_t *before, const mp_chat_side_counts_t *want_side,
                         const mp_chat_host_counts_t *want_host, const char *what)
{
    mp_chat_counts_t      now;
    mp_chat_side_counts_t side;
    mp_chat_host_counts_t host;

    mp_chat_counts(&now);
    side.said          = now.side.said - before->side.said;
    side.sent          = now.side.sent - before->side.sent;
    side.held_back     = now.side.held_back - before->side.held_back;
    side.empty         = now.side.empty - before->side.empty;
    side.unsent        = now.side.unsent - before->side.unsent;
    side.came_back     = now.side.came_back - before->side.came_back;
    side.shown         = now.side.shown - before->side.shown;
    side.torn          = now.side.torn - before->side.torn;
    side.after_end     = now.side.after_end - before->side.after_end;
    host.taken         = now.host.taken - before->host.taken;
    host.unsound       = now.host.unsound - before->host.unsound;
    host.too_fast      = now.host.too_fast - before->host.too_fast;
    host.no_seat       = now.host.no_seat - before->host.no_seat;
    host.said_here     = now.host.said_here - before->host.said_here;
    host.lines_out     = now.host.lines_out - before->host.lines_out;
    host.copies        = now.host.copies - before->host.copies;
    host.copies_unsent = now.host.copies_unsent - before->host.copies_unsent;

    ut_checkf(memcmp(&side, want_side, sizeof side) == 0,
              "%s; this side %u %u %u %u %u %u %u %u %u, want %u %u %u %u %u %u %u %u %u", what,
              side.said, side.sent, side.held_back, side.empty, side.unsent, side.came_back,
              side.shown, side.torn, side.after_end, want_side->said, want_side->sent,
              want_side->held_back, want_side->empty, want_side->unsent, want_side->came_back,
              want_side->shown, want_side->torn, want_side->after_end);
    ut_checkf(memcmp(&host, want_host, sizeof host) == 0,
              "%s; the host %u %u %u %u %u %u %u %u, want %u %u %u %u %u %u %u %u", what,
              host.taken, host.unsound, host.too_fast, host.no_seat, host.said_here,
              host.lines_out, host.copies, host.copies_unsent, want_host->taken,
              want_host->unsound, want_host->too_fast, want_host->no_seat, want_host->said_here,
              want_host->lines_out, want_host->copies, want_host->copies_unsent);
}

static void check_what_the_players_say(uint32_t *now)
{
    /* Eight said by the host's player: one, four of which three went, and three empty. Seven of
     * the clients' taken, a torn say and the sixth of a burst refused; eleven lines out, one to
     * each of two players. The host's own sent, came back and said here agree at four. */
    static const mp_chat_side_counts_t side = { 8u, 4u, 1u, 3u, 0u, 4u, 7u, 0u, 0u };
    static const mp_chat_host_counts_t host = { 7u, 1u, 1u, 0u, 4u, 11u, 22u, 0u };
    mp_chat_counts_t before;
    unsigned         i;
    unsigned         heard_before[CLIENTS];
    unsigned         sent = 0u;
    unsigned         held = 0u;

    ut_section("a say from a client becomes a line for every player, the one who said it included");
    mp_bridge_roster_set_name("Hosty");
    mp_chat_bind(&s_host, NULL, false);
    mp_chat_counts(&before);

    ut_check(client_says(0, 7u, "hello"), "the first client says a line");
    settle(now, SETTLE);
    ut_check(s_heard[0].lines == 1u && s_heard[1].lines == 1u,
             "each player got it once, the one who said it included");
    ut_check(heard_line(0, 1u, "Ann", "hello") && heard_line(1, 1u, "Ann", "hello") &&
                 s_heard[1].last.serial == 7u,
             "naming slot 1, the first client's name and its serial");
    ut_check(newest_is(1u, "Ann", "hello"), "and the host shows it in its own history");

    ut_check(client_says(1, 1u, "  hi there  "), "the second client says a line with spaces");
    settle(now, SETTLE);
    ut_check(s_heard[0].lines == 2u && s_heard[1].lines == 2u &&
                 heard_line(0, 2u, "Bob", "hi there") && heard_line(1, 2u, "Bob", "hi there"),
             "every player got slot 2, the second client's name, the text without its spaces");

    ut_check(mp_chat_say("from the host", 13u, *now) == MP_CHAT_SAY_SENT,
             "the host's own player says a line");
    ut_check(newest_is(MP_CHAT_HOST_SLOT, "Hosty", "from the host"),
             "it is in the host's history at once");
    settle(now, SETTLE);
    ut_check(s_heard[0].lines == 3u && s_heard[1].lines == 3u &&
                 heard_line(0, 0u, "Hosty", "from the host") &&
                 heard_line(1, 0u, "Hosty", "from the host"),
             "and every client got it as slot 0 under the host's name");

    ut_section("what the host refuses");
    {
        mp_chat_line_note_t forged;
        uint8_t             note[MP_CHAT_LINE_MAX_BYTES];
        size_t              bytes;

        memset(&forged, 0, sizeof forged);
        forged.slot   = 0u;
        forged.serial = 1u;
        memcpy(forged.name, "Hosty", 5u);
        forged.length = 4u;
        memcpy(forged.text, "fake", 4u);
        bytes = mp_chat_line_encode(&forged, note, sizeof note);
        ut_check(bytes != 0u && mp_session_send_reliable(&s_client[0], 0, note, bytes),
                 "the first client writes a line in the host's name");
        note[0] = (uint8_t)MP_CHAT_SAY_TAG;
        note[1] = 1u;
        note[2] = 9u;   /* a length the message does not have */
        ut_check(mp_session_send_reliable(&s_client[0], 0, note, 8u), "and a torn say");
    }
    settle(now, SETTLE);
    ut_checkf(s_drain.notes_refused == 1u,
              "the line was refused before any reader saw it (%u refused)",
              (unsigned)s_drain.notes_refused);
    ut_check(s_heard[0].lines == 3u && s_heard[1].lines == 3u &&
                 newest_is(MP_CHAT_HOST_SLOT, "Hosty", "from the host"),
             "and neither it nor the torn say reached anybody or the host's history");

    ut_section("how fast");
    settle(now, 1000);   /* five seconds: every bucket full again */
    heard_before[0] = s_heard[0].lines;
    heard_before[1] = s_heard[1].lines;
    for (i = 0u; i < 6u; ++i) {
        ut_checkf(client_says(1, (uint8_t)(10u + i), "fast"), "the second client says %u", i);
    }
    settle(now, SETTLE);
    ut_checkf(s_heard[0].lines - heard_before[0] == MP_CHAT_PACE_HOST_BURST &&
                  s_heard[1].lines - heard_before[1] == MP_CHAT_PACE_HOST_BURST,
              "six at once from one client: the host's burst of five went to everybody (%u, %u)",
              s_heard[0].lines - heard_before[0], s_heard[1].lines - heard_before[1]);

    settle(now, 1000);
    heard_before[0] = s_heard[0].lines;
    for (i = 0u; i < 4u; ++i) {
        mp_chat_say_result_t said = mp_chat_say("mine", 4u, *now);

        sent += said == MP_CHAT_SAY_SENT ? 1u : 0u;
        held += said == MP_CHAT_SAY_TOO_FAST ? 1u : 0u;
    }
    settle(now, SETTLE);
    ut_checkf(sent == MP_CHAT_PACE_CLIENT_BURST && held == 1u &&
                  s_heard[0].lines - heard_before[0] == MP_CHAT_PACE_CLIENT_BURST,
              "four at once from the host's own player: three go, the fourth is held back and "
              "never sent (%u, %u)", sent, held);
    ut_check(mp_chat_say("   ", 3u, *now) == MP_CHAT_SAY_EMPTY &&
                 mp_chat_say("a\\b", 3u, *now) == MP_CHAT_SAY_EMPTY,
             "spaces alone, and a backslash, are refused as empty before the bucket is asked");
    {
        char long_text[MP_CHAT_TEXT_MAX + 2u];

        memset(long_text, 'w', sizeof long_text);
        ut_check(mp_chat_say(long_text, MP_CHAT_TEXT_MAX + 1u, *now) == MP_CHAT_SAY_EMPTY,
                 "and a text a byte longer than a line is refused, not cut");
    }
    check_counts(&before, &side, &host, "the counts of the listen host so far");
}

static void check_the_history(uint32_t *now)
{
    static const mp_chat_side_counts_t side = { 40u, 40u, 0u, 0u, 0u, 40u, 0u, 0u, 0u };
    static const mp_chat_host_counts_t host = { 0u, 0u, 0u, 0u, 40u, 40u, 80u, 0u };
    const mp_chat_line_t *lines[MP_CHAT_HISTORY + 8u];
    mp_chat_counts_t      before;
    size_t                count;
    unsigned              i;
    char                  text[16];

    ut_section("the history");
    count = mp_chat_newest(lines, 6u);
    ut_checkf(count == 6u && strcmp(lines[5]->text, "mine") == 0 &&
                  lines[5]->slot == MP_CHAT_HOST_SLOT && lines[0]->slot == 2u,
              "the six newest, oldest first and newest last (%u)", (unsigned)count);
    mp_chat_counts(&before);
    for (i = 0u; i < 40u; ++i) {
        *now += 1000u;
        text_format(text, sizeof text, "line %u", i);
        (void)mp_chat_say(text, strlen(text), *now);
        settle(now, 2);
    }
    count = mp_chat_newest(lines, MP_CHAT_HISTORY + 8u);
    ut_checkf(count == MP_CHAT_HISTORY && strcmp(lines[MP_CHAT_HISTORY - 1u]->text, "line 39") ==
                                              0 &&
                  strcmp(lines[0]->text, "line 8") == 0,
              "the newest thirty two and no more, the oldest dropped first (%u)",
              (unsigned)count);
    ut_checkf(lines[MP_CHAT_HISTORY - 1u]->shown_ms == *now - 2u * STEP_MS,
              "dated with the time it was said at (%u)",
              (unsigned)lines[MP_CHAT_HISTORY - 1u]->shown_ms);
    ut_check(mp_chat_newest(NULL, 3u) == 0u, "and nowhere to put them is none");
    check_counts(&before, &side, &host, "forty said a second apart, every one out to both");
}

/* The listen host's broadcast used to ask every channel first and hold nothing when none could
 * take the line; with every player's channel full the line was then gone for all of them, while
 * the host showed it in its own history. */
static void check_a_full_channel(uint32_t *now)
{
    static const mp_chat_side_counts_t side = { 1u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u };
    static const mp_chat_host_counts_t host = { 0u, 0u, 0u, 0u, 1u, 1u, 2u, 0u };
    mp_chat_counts_t         before;
    mp_session_hold_totals_t held_before;
    mp_session_hold_totals_t held_after;
    unsigned                 stuffed[CLIENTS];
    unsigned                 heard_before[CLIENTS];
    int                      i;

    ut_section("a line for players whose channels are full");
    settle(now, 1000);
    for (i = 0; i < CLIENTS; ++i) {
        stuffed[i]      = fill_channel(&s_host, (size_t)i);
        heard_before[i] = s_heard[i].lines;
    }
    mp_chat_counts(&before);
    mp_session_hold_totals(&s_host, &held_before);
    ut_checkf(stuffed[0] < 1000u && stuffed[1] < 1000u &&
                  mp_chat_say("held", 4u, *now) == MP_CHAT_SAY_SENT,
              "with both channels full (%u and %u messages) the host's player says a line",
              stuffed[0], stuffed[1]);
    mp_session_hold_totals(&s_host, &held_after);
    ut_checkf(held_after.singles_held - held_before.singles_held == 2u,
              "the line is held for each of the two players (%u)",
              (unsigned)(held_after.singles_held - held_before.singles_held));
    check_counts(&before, &side, &host, "two copies, none of them unsent");
    settle(now, 200);
    ut_check(s_heard[0].lines == heard_before[0] + 1u && s_heard[1].lines == heard_before[1] + 1u &&
                 heard_line(0, 0u, "Hosty", "held") && heard_line(1, 0u, "Hosty", "held"),
             "and once the channels have room, both players get it");
}

static void check_the_hosts_exit(uint32_t *now)
{
    static const mp_chat_side_counts_t side = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u };
    static const mp_chat_host_counts_t host = { 0u };
    const mp_chat_line_t *lines[4];
    mp_chat_counts_t      before;

    ut_section("the host's exit");
    mp_chat_forget();
    ut_check(mp_chat_newest(lines, 4u) == 0u, "the one exit of a session forgets every line");
    ut_check(mp_chat_say("after", 5u, *now) == MP_CHAT_SAY_NO_SESSION,
             "and with no session bound nothing is said");

    /* The transport outlives the exit by up to a second, and the drain reads on until it falls. */
    mp_chat_counts(&before);
    ut_check(client_says(0, 20u, "bye"), "a client's say still arrives after the exit");
    settle(now, SETTLE);
    check_counts(&before, &side, &host,
                 "it counts as after the session's end, not as a say from no seat");
}

static void check_a_client(uint32_t *now)
{
    /* Five said: three sent, the fourth at once held back, one empty. Its own line came back, one
     * of another player's shown, a torn one refused. Then a fifth sent line lost to the full
     * channel and three more sent; last, a line that came after the exit. */
    static const mp_chat_side_counts_t said = { 5u, 3u, 1u, 1u, 0u, 1u, 1u, 1u, 0u };
    static const mp_chat_side_counts_t full = { 9u, 6u, 1u, 1u, 1u, 1u, 1u, 1u, 0u };
    static const mp_chat_side_counts_t late = { 9u, 6u, 1u, 1u, 1u, 1u, 1u, 1u, 1u };
    static const mp_chat_host_counts_t host = { 0u };
    const mp_chat_line_t *lines[4];
    mp_chat_counts_t      before;
    unsigned              i;
    unsigned              stuffed;
    unsigned              sent = 0u;

    ut_section("a client's half, through the drain of a client");
    s_host_drained      = false;
    s_client_drained[0] = true;
    mp_bridge_drain_bind(&s_client_drain[0], MP_BRIDGE_UDP_CLIENT, NULL, &s_client[0]);
    mp_chat_bind(NULL, &s_client[0], true);
    mp_chat_counts(&before);
    ut_check(host_tells_slot(0u, 1u), "the host tells the first client its slot, 1");
    settle(now, 1000);
    ut_check(mp_bridge_drain_slot_told() && mp_bridge_drain_my_slot() == 1u,
             "and the client's drain took it");

    ut_check(mp_chat_say("Gruesse", 7u, *now) == MP_CHAT_SAY_SENT, "the client says a line");
    settle(now, SETTLE);
    ut_check(s_host_heard.says == 1u && s_host_heard.last.serial == 1u &&
                 strcmp(s_host_heard.last.text, "Gruesse") == 0,
             "the host got a say with serial 1 and the text, and nothing else of the client's");
    ut_check(mp_chat_newest(lines, 4u) == 0u,
             "the client keeps nothing of its own until the host's line comes back");
    ut_check(mp_chat_say("  ", 2u, *now) == MP_CHAT_SAY_EMPTY, "spaces alone are refused");
    ut_check(mp_chat_say("two", 3u, *now) == MP_CHAT_SAY_SENT &&
                 mp_chat_say("three", 5u, *now) == MP_CHAT_SAY_SENT &&
                 mp_chat_say("four", 4u, *now) == MP_CHAT_SAY_TOO_FAST,
             "the fourth at once is held back by the client's own bucket");
    settle(now, SETTLE);
    ut_check(s_host_heard.says == 3u && s_host_heard.last.serial == 3u,
             "the host got the second and the third with the next serials");

    ut_check(host_sends_line(0u, 1u, 1u, "Ann", "Gruesse", false) &&
                 host_sends_line(0u, 2u, 4u, "Bob", "hallo", false) &&
                 host_sends_line(0u, 2u, 5u, "Bob", "torn", true),
             "the host sends the client's own line, another player's and a torn one");
    settle(now, SETTLE);
    ut_check(mp_chat_newest(lines, 4u) == 2u && lines[0]->slot == 1u &&
                 strcmp(lines[0]->text, "Gruesse") == 0 && lines[1]->slot == 2u &&
                 strcmp(lines[1]->name, "Bob") == 0 && strcmp(lines[1]->text, "hallo") == 0,
             "the client keeps its own line and the other player's, and not the torn one");
    ut_check(lines[1]->shown_ms != 0u && lines[1]->shown_ms <= *now,
             "dated with the client's session clock when it arrived");
    check_counts(&before, &said, &host, "the counts of the client so far");

    ut_section("a say the channel does not take");
    settle(now, 1000);
    stuffed = fill_channel(&s_client[0], 0u);
    ut_checkf(stuffed < 1000u && mp_chat_say("lost", 4u, *now) == MP_CHAT_SAY_UNSENT,
              "with the channel to the host full (%u messages), a say is unsent", stuffed);
    settle(now, SETTLE * 4);
    for (i = 0u; i < 3u; ++i) {
        sent += mp_chat_say("again", 5u, *now) == MP_CHAT_SAY_SENT ? 1u : 0u;
    }
    ut_checkf(sent == 3u,
              "and it cost the bucket nothing: three at once go once the channel is free (%u)",
              sent);
    check_counts(&before, &full, &host, "one unsent, three more sent");

    ut_section("a client's exit");
    mp_chat_forget();
    ut_check(host_sends_line(0u, 1u, 9u, "Ann", "late", false),
             "the host's line still arrives after the client's exit");
    settle(now, SETTLE);
    check_counts(&before, &late, &host, "it counts as after the session's end, not as torn");
    ut_check(mp_chat_newest(lines, 4u) == 0u && mp_chat_say("gone", 4u, *now) ==
                                                    MP_CHAT_SAY_NO_SESSION,
             "the one exit forgets a client's lines as well, and keeps none that came after it");
    ut_check(mp_chat_take_line(1u, NULL, 0u) == false, "and nothing at all is no line");
}

/* Slot 1 is the default a client holds until its host tells it otherwise, and slot 1 is also the
 * first client's real slot. A drain bound before the slot byte must not take slot 1's lines for
 * its own, and must take its own slot's once the byte is there. */
static void check_a_second_client(uint32_t *now)
{
    static const mp_chat_side_counts_t unseated = { 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u };
    static const mp_chat_side_counts_t seated   = { 1u, 1u, 0u, 0u, 0u, 1u, 2u, 0u, 0u };
    static const mp_chat_host_counts_t host = { 0u };
    const mp_chat_line_t *lines[4];
    mp_chat_counts_t      before;
    unsigned              says;

    ut_section("a second client, slot 2, read through a drain bound before its slot byte");
    s_client_drained[0] = false;
    s_client_drained[1] = true;
    mp_bridge_drain_bind(&s_client_drain[1], MP_BRIDGE_UDP_CLIENT, NULL, &s_client[1]);
    mp_chat_bind(NULL, &s_client[1], true);
    mp_chat_counts(&before);
    settle(now, SETTLE);
    ut_check(!mp_bridge_drain_slot_told() && mp_bridge_drain_my_slot() == 1u,
             "before its slot byte the drain holds the default slot 1, which is the first "
             "client's");

    ut_check(host_sends_line(1u, 1u, 30u, "Ann", "before the slot", false),
             "a line of slot 1 arrives first");
    settle(now, SETTLE);
    check_counts(&before, &unseated, &host,
                 "it is shown as another player's, not taken for this side's own");

    ut_check(host_tells_slot(1u, 2u) && host_sends_line(1u, 2u, 1u, "Bob", "mine", false) &&
                 host_sends_line(1u, 1u, 31u, "Ann", "yours", false),
             "then the slot byte, a line of slot 2 and one of slot 1");
    settle(now, SETTLE);
    ut_check(mp_bridge_drain_slot_told() && mp_bridge_drain_my_slot() == 2u,
             "the drain took slot 2");
    says = s_host_heard.says;
    ut_check(mp_chat_say("hi", 2u, *now) == MP_CHAT_SAY_SENT, "the second client says a line");
    settle(now, SETTLE);
    ut_check(s_host_heard.says == says + 1u && s_host_heard.last.serial == 1u &&
                 strcmp(s_host_heard.last.text, "hi") == 0,
             "the host got it with serial 1");
    check_counts(&before, &seated, &host,
                 "its own slot's line came back, both lines of slot 1 were shown");
    ut_check(mp_chat_newest(lines, 4u) == 3u && lines[0]->slot == 1u && lines[1]->slot == 2u &&
                 strcmp(lines[1]->name, "Bob") == 0 && strcmp(lines[2]->text, "yours") == 0,
             "and all three are in its history, in the order they came");
    mp_chat_forget();
}

/* A copy is unsent only where neither the channel nor the hold took it: the player whose hold is
 * full as well is sent away in the call, and every other player gets the line. */
static void check_a_full_hold(uint32_t *now)
{
    static const mp_chat_side_counts_t side = { 1u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u };
    static const mp_chat_host_counts_t host = { 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u };
    uint8_t          filler[MP_CHAT_LINE_HEAD_BYTES + 4u];   /* as long as the line "last" */
    const mp_peer_t *peer;
    mp_chat_counts_t before;
    unsigned         held = 0u;
    unsigned         heard_before;

    ut_section("a copy for a player whose hold is full as well");
    s_client_drained[1] = false;
    mp_chat_bind(&s_host, NULL, false);
    settle(now, 200);
    peer = mp_session_peer(&s_host, 1u);
    memset(filler, 0x7Eu, sizeof filler);
    (void)fill_channel(&s_host, 1u);
    while (held < 10000u && peer != NULL && mp_hold_fits(&peer->hold, sizeof filler) &&
           mp_session_send_or_hold(&s_host, 1u, filler, sizeof filler)) {
        ++held;
    }
    heard_before = s_heard[0].lines;
    mp_chat_counts(&before);
    ut_checkf(held > 0u && mp_chat_say("last", 4u, *now) == MP_CHAT_SAY_SENT,
              "with the second client's channel and hold full (%u held), the host's player says "
              "a line", held);
    check_counts(&before, &side, &host, "one copy out, and one unsent");
    ut_check(mp_session_peer_count(&s_host) == 1u && mp_session_peer_dropped_behind(&s_host, 1u),
             "the second client was sent away for falling behind");
    settle(now, SETTLE);
    ut_check(s_heard[0].lines == heard_before + 1u && heard_line(0, 0u, "Hosty", "last"),
             "and the first client got the line all the same");
    mp_chat_report();
}

int main(void)
{
    uint32_t now = 0;

    ut_section("a listen host and two clients in one process");
    if (!mp_udp_init(&s_host_udp, 0)) {
        printf("no winsock on this machine, nothing to test\n");
        return ut_summary("mp_chat");
    }
    if (!connect_everybody(&now)) {
        ut_check(false, "both clients joined the host");
        return ut_summary("mp_chat");
    }
    ut_check(true, "both clients joined the host");
    check_what_the_players_say(&now);
    check_the_history(&now);
    check_a_full_channel(&now);
    check_the_hosts_exit(&now);
    check_a_client(&now);
    check_a_second_client(&now);
    check_a_full_hold(&now);

    mp_udp_shutdown(&s_client_udp[0]);
    mp_udp_shutdown(&s_client_udp[1]);
    mp_udp_shutdown(&s_host_udp);
    return ut_summary("mp_chat");
}
