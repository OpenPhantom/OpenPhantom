/* The dedicated server, end to end, with no game: one server and two clients on real localhost
 * sockets in this one process. What has to hold is the whole point of the server: both clients
 * join, each is told its own world slot, each sends its own state, and each receives a world in
 * which the OTHER player's body stands at the other slot with the position that was sent. On top
 * of that, a shot one client fires reaches the other client once, never comes back to its
 * sender and names the sender's slot on the server's clock; a player's own death goes on and is
 * counted, while a note no player sends, a hit on another player and a death written for another
 * player go nowhere; the world leaves
 * once per 31.25 ms tick under a loop that runs six times faster, with no tick stamped twice;
 * and a client that restarts from the same address is told its slot again. A line of chat one
 * client says comes back to both as a line naming its slot and its name, under the same rule a
 * listen host keeps: a torn say and the sixth of a burst are refused, a line a client writes is a
 * note no player sends, and every line goes out as one copy per player; a newcomer on a seat
 * somebody just left says its first line at once, with a bucket of its own. Skips cleanly on a
 * machine with no Winsock, like the socket tests do.
 */
#include "unittest.h"

#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_events.h"
#include "mp_hit_relay.h"
#include "mp_lobby.h"
#include "mp_npc_copy_wire.h"
#include "mp_payload_prefix.h"
#include "mp_server.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_transport.h"
#include "mp_udp.h"
#include "mp_wire.h"

#include "common/text.h"

#include <stdio.h>
#include <string.h>

static mp_server_t  s_server;
static mp_session_t s_client[2];
static mp_udp_t     s_server_udp;
static mp_udp_t     s_client_udp[2];

/* The transports outlive the sessions that hold pointers to them, so they are statics too. */
static mp_transport_t s_server_transport;
static mp_transport_t s_client_transport[2];


/* The loop and the clock. Five milliseconds is a little over six steps per tick, so a server
 * that sent a world per step would show it at once; the window is a whole second, thirty-two
 * ticks, with one of slack at either end for where its edges fall against the tick boundaries. */
#define STEP_MS            5u
#define STEPS              800
#define EVENT_AT_MS        1000u
#define WINDOW_OPEN_MS     2000u
#define WINDOW_CLOSE_MS    3000u
#define RESTART_AT_MS      3000u
#define TICKS_PER_WINDOW   32u
#define WINDOW_SLACK       1u

/* A reliable message no reader decodes, the size a shot had before the moments named their
 * player: no player sends such a thing, and the server passes it on to nobody. */
#define EVENT_BYTES 22u

/* A real shot, with the tick the sender counted and a slot it has no business naming. */
#define SHOT_AT_MS     (EVENT_AT_MS + 10u)
#define SHOT_OWN_TICK  4321u
#define SHOT_OWN_SLOT  9u

/* Three notes client 0 has no business sending, and one it has: a hit on client 1, a death it
 * wrote for client 1, and its own death at client 1's hand. */
#define PLAYER_HIT_AT_MS  (EVENT_AT_MS + 15u)
#define FORGED_DEATH_AT_MS (EVENT_AT_MS + 20u)
#define OWN_DEATH_AT_MS   (EVENT_AT_MS + 25u)
#define COPY_WISH_AT_MS   (EVENT_AT_MS + 30u)

/* Client 0's chat: one line, a torn say, a line it writes itself, and then seven says one step
 * apart, of which the server's bucket, one line already gone, takes four. */
#define CHAT_AT_MS        (EVENT_AT_MS + 40u)
#define CHAT_TORN_AT_MS   (EVENT_AT_MS + 45u)
#define CHAT_FORGED_AT_MS (EVENT_AT_MS + 50u)
#define CHAT_BURST_FROM_MS (EVENT_AT_MS + 55u)
#define CHAT_BURST        7u
#define CHAT_NAME         "Ann"

typedef struct client_view {
    uint32_t slot;
    bool     seen_other;
    float    other_x;
    uint32_t events;           /* relayed messages of the event's size read */
    uint32_t shots;            /* real shots decoded */
    uint8_t  shot_source;      /* the slot the last one named */
    uint32_t shot_tick;
    uint32_t tick_at_shot;     /* the newest world tick this client held when it came */
    uint32_t setups;           /* setup notes read, which only the server may write */
    uint32_t player_hits;      /* hits on this player, which no client may write */
    uint32_t deaths;           /* deaths read */
    uint8_t  death_victim;     /* the slot the last one named */
    bool     have_tick;
    uint32_t last_tick;
    uint32_t duplicate_ticks;  /* a world stamped with the tick of the one before it */
    uint32_t worlds_refused;   /* payloads that did not split and decode as a world */
    uint32_t chat_lines;       /* lines of chat decoded */
    mp_chat_line_note_t chat_last;
} client_view_t;

/* The body goes in the slot the server told this client, and in slot 1 until it has, which is
 * the bridge's default against a listen host; the server refuses those first payloads and
 * takes the ones after the slot byte. The prefix is the codec's, the one every client writes: the
 * newest tick of the server's worlds this client holds and the bits of the ones before it. */
static void send_own_state(int which, uint32_t tick, float x, uint32_t slot, uint32_t held)
{
    uint8_t          payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t    own;
    mp_wire_body_t   body;
    mp_payload_ack_t ack;
    size_t           bytes = 0;

    memset(&body, 0, sizeof body);
    body.position[0] = x;
    body.alive       = true;
    body.hero        = (uint8_t)(which + 1);
    mp_snapshot_clear(&own);
    own.tick = tick;
    mp_snapshot_set_body(&own, slot != 0u ? slot : 1u, &body);
    ack.newest = held;
    ack.bits   = held > 1u ? 0x1u : 0u;
    if (mp_payload_put_ack(payload, sizeof payload, &ack) &&
        mp_snapshot_encode(&own, NULL, payload + MP_PAYLOAD_ACK_BYTES,
                           sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes)) {
        mp_session_set_payload(&s_client[which], 0, payload, bytes + MP_PAYLOAD_ACK_BYTES);
    }
}

static void note_world(client_view_t *view, const mp_snapshot_t *world)
{
    size_t slot;

    if (view->have_tick && world->tick == view->last_tick) {
        ++view->duplicate_ticks;
    }
    view->have_tick = true;
    view->last_tick = world->tick;
    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        if (slot != view->slot && mp_snapshot_has_body(world, slot)) {
            view->seen_other = true;
            view->other_x    = world->body[slot].position[0];
        }
    }
}

static void read_client(int which, client_view_t *view)
{
    uint8_t    buffer[MP_SESSION_PAYLOAD_BYTES];
    size_t          bytes = 0;
    mp_event_t      shot;
    mp_death_note_t death;

    while (mp_session_read_reliable(&s_client[which], 0, buffer, sizeof buffer, &bytes)) {
        if (mp_chat_line_decode(buffer, bytes, &view->chat_last)) {
            ++view->chat_lines;
        } else if (bytes == 1u) {
            view->slot = buffer[0];
        } else if (bytes == EVENT_BYTES) {
            ++view->events;
        } else if (mp_event_decode(buffer, bytes, &shot) && shot.kind == MP_EVENT_SHOT) {
            ++view->shots;
            view->shot_source  = shot.source_slot;
            view->shot_tick    = shot.tick;
            view->tick_at_shot = view->last_tick;
        } else if (mp_lobby_is_setup(buffer, bytes)) {
            ++view->setups;
        } else if (bytes == MP_PLAYER_HIT_BYTES && buffer[0] == (uint8_t)MP_EVENT_PLAYER_HIT) {
            ++view->player_hits;
        } else if (mp_death_decode(buffer, bytes, &death)) {
            ++view->deaths;
            view->death_victim = death.victim_slot;
        }
    }
    /* The world as a client's bridge splits it: the enemy length first, the snapshot behind the
     * block it names. This used to decode the payload as a bare snapshot, which is what the server
     * sent, and so passed while every real client refused it. */
    while (mp_session_read_payload(&s_client[which], 0, buffer, sizeof buffer, &bytes)) {
        uint32_t      baseline_tick = 0;
        mp_snapshot_t world;
        size_t        enemy_bytes = 0;
        size_t        at          = 0;

        if (mp_payload_split_world(buffer, bytes, &enemy_bytes, &at) && enemy_bytes == 0u &&
            mp_snapshot_baseline_tick(buffer + at, bytes - at, &baseline_tick) &&
            baseline_tick == 0u && mp_snapshot_decode(buffer + at, bytes - at, NULL, &world)) {
            note_world(view, &world);
        } else {
            ++view->worlds_refused;
        }
    }
}

static void start_client(int which, uint32_t seed, const char *address)
{
    mp_session_init(&s_client[which], MP_SESSION_CLIENT, &s_client_transport[which], seed);
    if (which == 0) {
        mp_session_set_name(&s_client[which], CHAT_NAME);
    }
    mp_session_connect(&s_client[which], mp_udp_resolve(&s_client_udp[which], address));
}

/* A say as a client's chat puts it out, or a torn one whose length byte says one more than the
 * text it carries. */
static bool client_says(int which, uint8_t serial, const char *text, bool torn)
{
    mp_chat_say_note_t say;
    uint8_t            note[MP_CHAT_SAY_MAX_BYTES];
    size_t             bytes;

    memset(&say, 0, sizeof say);
    say.serial = serial;
    say.length = (uint8_t)strlen(text);
    memcpy(say.text, text, say.length);
    bytes = mp_chat_say_encode(&say, note, sizeof note);
    if (torn) {
        note[2] = (uint8_t)(say.length + 1u);
    }
    return bytes != 0u && mp_session_send_reliable(&s_client[which], 0, note, bytes);
}

/* A line of chat a client writes, which only the server may. */
static bool client_forges_a_line(int which)
{
    mp_chat_line_note_t line;
    uint8_t             note[MP_CHAT_LINE_MAX_BYTES];
    size_t              bytes;

    memset(&line, 0, sizeof line);
    line.slot   = 2u;
    line.serial = 1u;
    memcpy(line.name, "Bob", 3u);
    line.length = 6u;
    memcpy(line.text, "forged", 6u);
    bytes = mp_chat_line_encode(&line, note, sizeof note);
    return bytes != 0u && mp_session_send_reliable(&s_client[which], 0, note, bytes);
}

/* Steps of the room after the main run: the server, then each client's state, session and
 * reader. */
static void run_steps(uint32_t *now, int steps, client_view_t *view)
{
    int step;
    int i;

    for (step = 0; step < steps; ++step) {
        *now += STEP_MS;
        mp_server_tick(&s_server, *now);
        for (i = 0; i < 2; ++i) {
            if (mp_session_is_connected(&s_client[i])) {
                send_own_state(i, *now, (i == 0) ? 100.0f : 200.0f, view[i].slot,
                               view[i].last_tick);
            }
            mp_session_update(&s_client[i], *now);
            read_client(i, &view[i]);
        }
    }
}

/* A seat that changes hands. Client 0 says five lines at once, which empties its seat's bucket at
 * the server, leaves, and a fresh session takes the same seat a few steps later. Its first line
 * has to be taken: a newcomer does not inherit the pace of the one who left. All of it runs inside
 * the 700 ms the emptied bucket would need for its next line, or the check would pass without the
 * reset. */
static void check_a_seat_changes_hands(uint32_t *now, const char *address, client_view_t *view)
{
    uint32_t taken;
    uint32_t too_fast;
    uint32_t burst_at;
    unsigned said = 0u;
    int      i;

    ut_section("a seat that changes hands");
    run_steps(now, 1000, view);   /* five seconds: the burst of the main run is refilled */
    taken    = s_server.chat.counts.taken;
    too_fast = s_server.chat.counts.too_fast;
    burst_at = *now;
    for (i = 0; i < (int)MP_CHAT_PACE_HOST_BURST; ++i) {
        said += client_says(0, (uint8_t)(20 + i), "last words", false) ? 1u : 0u;
    }
    run_steps(now, 4, view);
    ut_checkf(said == MP_CHAT_PACE_HOST_BURST &&
                  s_server.chat.counts.taken - taken == MP_CHAT_PACE_HOST_BURST,
              "client 0 said five at once, which empties its seat's bucket (%u taken)",
              (unsigned)(s_server.chat.counts.taken - taken));
    mp_session_disconnect(&s_client[0]);
    run_steps(now, 4, view);
    ut_check(mp_server_connected(&s_server) == 1u, "and left");

    view[0].slot = 0u;
    start_client(0, 0xF0u, address);
    for (i = 0; i < 60 && view[0].slot == 0u; ++i) {
        run_steps(now, 1, view);
    }
    ut_check(mp_server_connected(&s_server) == 2u && view[0].slot == 1u,
             "a newcomer took the same seat, slot 1");
    ut_check(client_says(0, 1u, "hello, I am new", false), "and says a line at once");
    run_steps(now, 4, view);
    ut_checkf(*now - burst_at < MP_CHAT_PACE_HOST_EVERY_MS,
              "all of it %u ms after the burst, inside one interval of the bucket",
              (unsigned)(*now - burst_at));
    ut_checkf(s_server.chat.counts.taken - taken == MP_CHAT_PACE_HOST_BURST + 1u &&
                  s_server.chat.counts.too_fast == too_fast,
              "its line was taken: it did not inherit the empty bucket of the one who left "
              "(%u taken, %u too fast)", (unsigned)(s_server.chat.counts.taken - taken),
              (unsigned)(s_server.chat.counts.too_fast - too_fast));
}

/* What client 0 sent over the main run, counted as it went. */
typedef struct sent_counts {
    uint32_t event;
    uint32_t setup;
    uint32_t shot;
    uint32_t notes;
    uint32_t copy_wishes;
    uint32_t chat;
} sent_counts_t;

/* Client 0's scripted messages, each at its own moment of the main run. */
static void client_0_sends(uint32_t now, sent_counts_t *sent)
{
    if (now == EVENT_AT_MS && mp_session_is_connected(&s_client[0])) {
        uint8_t event[EVENT_BYTES];

        memset(event, 0xA5, sizeof event);
        if (mp_session_send_reliable(&s_client[0], 0, event, sizeof event)) {
            ++sent->event;
        }
    }
    if (now == EVENT_AT_MS + STEP_MS && mp_session_is_connected(&s_client[0])) {
        mp_lobby_setup_t forged;
        uint8_t          note[MP_LOBBY_SETUP_BYTES];

        memset(&forged, 0, sizeof forged);
        forged.mode  = (uint8_t)MP_LOBBY_MODE_TDM;
        forged.flags = (uint8_t)MP_LOBBY_F_ENDED;
        memcpy(forged.level, "level\\maul.b3d", 15u);
        if (mp_lobby_setup_encode(&forged, note, sizeof note) == MP_LOBBY_SETUP_BYTES &&
            mp_session_send_reliable(&s_client[0], 0, note, sizeof note)) {
            ++sent->setup;
        }
    }
    if (now == SHOT_AT_MS && mp_session_is_connected(&s_client[0])) {
        mp_event_t shot;
        uint8_t    note[MP_EVENT_MAX_BYTES];
        size_t     length;

        memset(&shot, 0, sizeof shot);
        shot.kind        = MP_EVENT_SHOT;
        shot.tick        = SHOT_OWN_TICK;
        shot.source_slot = SHOT_OWN_SLOT;
        length = mp_event_encode(&shot, note, sizeof note);
        if (length != 0u && mp_session_send_reliable(&s_client[0], 0, note, length)) {
            ++sent->shot;
        }
    }
    if (now == PLAYER_HIT_AT_MS && mp_session_is_connected(&s_client[0])) {
        uint8_t note[MP_PLAYER_HIT_BYTES];

        memset(note, 0, sizeof note);
        note[0] = (uint8_t)MP_EVENT_PLAYER_HIT;
        note[1] = 2u;   /* client 1's slot */
        note[2] = 0xFFu;   /* the attacker's key, two bytes: nobody */
        note[3] = 0xFFu;
        note[5] = 0x10u;   /* the impact code */
        if (mp_session_send_reliable(&s_client[0], 0, note, sizeof note)) {
            ++sent->notes;
        }
    }
    if ((now == FORGED_DEATH_AT_MS || now == OWN_DEATH_AT_MS) &&
        mp_session_is_connected(&s_client[0])) {
        mp_death_note_t death;
        uint8_t         note[MP_EVENT_DEATH_BYTES];

        death.victim_slot = now == OWN_DEATH_AT_MS ? 1u : 2u;
        death.killer_slot = now == OWN_DEATH_AT_MS ? 2u : 1u;
        death.reason      = MP_DEATH_BY_HIT;
        if (mp_death_encode(&death, note, sizeof note) &&
            mp_session_send_reliable(&s_client[0], 0, note, sizeof note)) {
            ++sent->notes;
        }
    }
    if (now == COPY_WISH_AT_MS && mp_session_is_connected(&s_client[0])) {
        mp_npc_copy_wish_t wish;
        uint8_t            note[MP_NPC_COPY_WISH_BYTES];

        memset(&wish, 0, sizeof wish);
        wish.kind   = NPC_SPAWN_WISH_REMOVE_OWN;
        wish.serial = 1u;
        if (mp_npc_copy_wish_encode(&wish, note, sizeof note) == sizeof note &&
            mp_session_send_reliable(&s_client[0], 0, note, sizeof note)) {
            ++sent->copy_wishes;
        }
    }
    if (mp_session_is_connected(&s_client[0])) {
        if (now == CHAT_AT_MS) {
            sent->chat += client_says(0, 1u, "hello there", false) ? 1u : 0u;
        }
        if (now == CHAT_TORN_AT_MS) {
            sent->chat += client_says(0, 2u, "torn", true) ? 1u : 0u;
        }
        if (now == CHAT_FORGED_AT_MS) {
            sent->chat += client_forges_a_line(0) ? 1u : 0u;
        }
        if (now >= CHAT_BURST_FROM_MS &&
            now < CHAT_BURST_FROM_MS + CHAT_BURST * STEP_MS) {
            sent->chat += client_says(0, 3u, "again and again", false) ? 1u : 0u;
        }
    }
}

static void check_the_bodies_and_the_notes(const client_view_t *view,
                                           const sent_counts_t *sent)
{
    ut_check(mp_server_connected(&s_server) == 2u, "both clients are connected at the end");
    ut_check(view[0].slot == 1u && view[1].slot == 2u,
             "each client was told its own world slot, in join order");
    ut_check(view[0].seen_other && view[1].seen_other, "each client saw the other player's body");
    ut_near(view[0].other_x, 200.0, 0.01, "client 0 sees client 1 where client 1 stands");
    ut_near(view[1].other_x, 100.0, 0.01, "client 1 sees client 0 where client 0 stands");
    ut_check(s_server.peers[1].states_received > 0u,
             "client 1's state landed in slot 2, the slot it was told, not in slot 1");

    ut_check(sent->event == 1u, "client 0 queued one reliable message once it was connected");
    ut_check(view[1].events == 0u && view[0].events == 0u,
             "a message of a kind no player sends reached nobody");
    ut_check(sent->shot == 1u && view[1].shots == 1u && view[0].shots == 0u,
             "a shot client 0 fired reached client 1 once and never came back");
    ut_checkf(view[1].shot_source == 1u,
              "naming slot 1, the slot the server told client 0, not the %u it wrote",
              SHOT_OWN_SLOT);
    ut_checkf(view[1].shot_tick != SHOT_OWN_TICK &&
                  view[1].shot_tick >= view[1].tick_at_shot &&
                  view[1].shot_tick <= view[1].tick_at_shot + 2u,
              "on the server's clock, tick %u beside a world at %u, not client 0's own %u",
              (unsigned)view[1].shot_tick, (unsigned)view[1].tick_at_shot, SHOT_OWN_TICK);
    ut_check(sent->notes == 3u, "client 0 sent a hit on client 1, a death for it and its own");
    ut_check(view[1].player_hits == 0u,
             "the hit client 0 wrote on client 1 never reached it: that is the server's to say");
    ut_check(view[1].deaths == 1u && view[1].death_victim == 1u,
             "client 0's own death reached client 1, the one it wrote for client 1 did not");
    ut_check(s_server.deaths_seen == 1u, "and the server counted only the true one");
    ut_check(sent->copy_wishes == 1u && s_server.copy_wishes == 1u && s_server.relayed == 2u,
             "a wish for an NPC copy is counted at the server and passed on to nobody");
    ut_check(s_server.notes_refused == 4u,
             "four notes refused: the message, the hit, the death in another's name and a line "
             "of chat a client wrote");
    ut_check(s_server.relayed == 2u && s_server.relay_refused == 0u,
             "the server counted two relays, the shot and the death, and no refusal");
    ut_check(sent->setup == 1u && s_server.setups_refused == 1u && view[1].setups == 0u,
             "a setup note one client wrote was dropped rather than passed to the other");
}

/* `worlds` is how many the server sent in the second the window spans. */
static void check_the_clock_and_the_chat(const client_view_t *view,
                                         const sent_counts_t *sent, uint32_t worlds)
{
    ut_checkf(worlds >= TICKS_PER_WINDOW - WINDOW_SLACK &&
                  worlds <= TICKS_PER_WINDOW + WINDOW_SLACK,
              "one world per 31.25 ms tick under a 5 ms loop: %u in a second, not 200",
              (unsigned)worlds);
    ut_checkf(view[0].worlds_refused == 0u && view[1].worlds_refused == 0u,
              "every world the server sent split as a client's bridge splits one, the enemy "
              "length first (%u and %u refused)", (unsigned)view[0].worlds_refused,
              (unsigned)view[1].worlds_refused);
    ut_check(view[0].duplicate_ticks == 0u && view[1].duplicate_ticks == 0u,
             "no two consecutive worlds carry the same tick");

    ut_check(sent->chat == 3u + CHAT_BURST, "client 0 said its lines of chat");
    ut_checkf(view[0].chat_lines == 5u && view[1].chat_lines == 5u,
              "the first line and four of the burst came back to both clients, the one who said "
              "them included (%u, %u)", (unsigned)view[0].chat_lines,
              (unsigned)view[1].chat_lines);
    ut_check(view[1].chat_last.slot == 1u && strcmp(view[1].chat_last.name, CHAT_NAME) == 0 &&
                 strcmp(view[1].chat_last.text, "again and again") == 0 &&
                 view[0].chat_last.slot == 1u,
             "naming slot 1, the slot the server told client 0, and the name it gave");
    ut_check(s_server.chat.counts.taken == 5u && s_server.chat.counts.unsound == 1u &&
                 s_server.chat.counts.too_fast == 3u && s_server.chat.counts.no_seat == 0u &&
                 s_server.chat.counts.said_here == 0u,
             "the server took five, refused the torn say and three past its bucket, and said "
             "nothing itself");
    ut_check(s_server.chat.counts.lines_out == 5u && s_server.chat.counts.copies == 10u &&
                 s_server.chat.counts.copies_unsent == 0u,
             "five lines went out as ten copies, one for each player, none unsent");

    ut_check(mp_session_replaced(&s_server.session) == 1u,
             "the restarted client was replaced in place rather than given a second slot");
}

int main(void)
{
    char          address[32];
    client_view_t view[2];
    sent_counts_t sent;
    uint32_t      now = 0;
    uint32_t      worlds_at_open = 0;
    uint32_t      worlds_at_close = 0;
    int           step;
    int           i;

    ut_section("a dedicated server and two clients in one process");

    if (!mp_udp_init(&s_server_udp, 0)) {
        printf("no winsock on this machine, nothing to test\n");
        return ut_summary("mp_dedicated");
    }
    s_server_transport = mp_udp_transport(&s_server_udp);
    mp_server_init(&s_server, &s_server_transport, 0xD5u);
    text_format(address, sizeof address, "127.0.0.1:%u",
                (unsigned)mp_udp_local_port(&s_server_udp));

    memset(view, 0, sizeof view);
    memset(&sent, 0, sizeof sent);
    for (i = 0; i < 2; ++i) {
        ut_checkf(mp_udp_init(&s_client_udp[i], 0), "client %d gets a socket", i);
        s_client_transport[i] = mp_udp_transport(&s_client_udp[i]);
        start_client(i, 0xC0u + (uint32_t)i, address);
    }

    for (step = 0; step < STEPS; ++step) {
        now += STEP_MS;
        mp_server_tick(&s_server, now);

        if (now == WINDOW_OPEN_MS) {
            worlds_at_open = s_server.worlds_sent;
        }
        if (now == WINDOW_CLOSE_MS) {
            worlds_at_close = s_server.worlds_sent;
        }
        if (now == RESTART_AT_MS) {
            /* The same socket and address, a fresh session: the server must replace the peer in
             * place and tell the newcomer its slot again. */
            view[1].slot = 0;
            start_client(1, 0xE1u, address);
        }
        for (i = 0; i < 2; ++i) {
            if (mp_session_is_connected(&s_client[i])) {
                send_own_state(i, now, (i == 0) ? 100.0f : 200.0f, view[i].slot,
                               view[i].last_tick);
            }
            if (i == 0) {
                client_0_sends(now, &sent);
            }
            mp_session_update(&s_client[i], now);
            read_client(i, &view[i]);
        }
    }

    check_the_bodies_and_the_notes(view, &sent);
    check_the_clock_and_the_chat(view, &sent, worlds_at_close - worlds_at_open);

    check_a_seat_changes_hands(&now, address, view);

    mp_udp_shutdown(&s_client_udp[0]);
    mp_udp_shutdown(&s_client_udp[1]);
    mp_udp_shutdown(&s_server_udp);
    return ut_summary("mp_dedicated");
}
