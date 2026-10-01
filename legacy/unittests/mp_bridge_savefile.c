/* mp_bridge_savefile.c: the two ends of the savegame transfer, each driven against a far end the
 * test plays by hand over the loopback.
 *
 * SIZE NOTE: over 600 lines, because the last section brings its own network. The
 * loopback has two endpoints and the total budget over all peers is a property of three, so a
 * mailbox with four endpoints is written here; the seam, should this grow, is that mailbox, which
 * the session's own mailbox test would share.
 *
 * Why this file exists. unittests/mp_session.c proves the lane and the codec by running the whole
 * slice and mask protocol, written a second time, inside the test. What it cannot see is the
 * module that runs the protocol in the game: the per peer record on the host, when it is reset and
 * when it is not, and the client's decision about when to stop saying what it holds. A control
 * pass over the savegame's own lane found two defects there, both invisible to a green suite
 * because no test compiled the file:
 *
 *   - a completed record for a peer that has LEFT was never cleared, so the next player to land
 *     on that slot sent masks for ever and was never sent a slice;
 *   - a client that had written the file stopped talking at once, so the host never received the
 *     full mask, never called the transfer complete, and resent the last slice for good.
 *
 * The module holds one end per process, so the two halves run one after the other on the same
 * pair of sessions. A savegame is a file on disk here as in the game: the host end reads one this
 * test writes under save\\, and the client end writes save\\MPJOIN.SAV into the working directory,
 * which is the build tree under ctest.
 */
#include "unittest.h"

#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_savefile.h"
#include "mp_saves.h"
#include "mp_session.h"
#include "mp_session_bulk.h"
#include "mp_transport.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_FILE      "save\\UT_BRIDGE.SAV"
#define TEST_FILE_SIZE (20u * 1024u + 333u)

/* Static: a session is over two megabytes now that every peer carries a bulk ring, and the
 * loopback ring is another three hundred kilobytes. */
static mp_loopback_t  s_net;
static mp_session_t   s_host;
static mp_session_t   s_client;
static mp_transport_t s_host_t;
static mp_transport_t s_client_t;
static uint8_t        s_file[TEST_FILE_SIZE];
static mp_savefile_assembly_t s_far_assembly;   /* the hand played client's */

static uint32_t s_id;
static uint16_t s_count;

static void make_the_file(void)
{
    HANDLE handle;
    DWORD  written = 0;
    size_t i;

    for (i = 0; i < sizeof s_file; ++i) {
        s_file[i] = (uint8_t)(i * 7u + (i >> 9));
    }
    s_id    = mp_savefile_digest(s_file, sizeof s_file);
    s_count = mp_savefile_chunk_count((uint32_t)sizeof s_file);

    (void)CreateDirectoryA(MP_SAVES_FOLDER, NULL);
    handle = CreateFileA(TEST_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         NULL);
    ut_check(handle != INVALID_HANDLE_VALUE, "the test's savegame can be written under save\\");
    if (handle != INVALID_HANDLE_VALUE) {
        (void)WriteFile(handle, s_file, (DWORD)sizeof s_file, &written, NULL);
        CloseHandle(handle);
    }
}

static bool connect_pair(uint32_t *now)
{
    int tick;

    for (tick = 0; tick < 400; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

static void fresh_pair(uint32_t *now, uint32_t seed)
{
    mp_loopback_conditions_t cond;

    memset(&cond, 0, sizeof cond);
    cond.delay_ms = 16u;
    mp_loopback_init(&s_net, &cond, seed);
    s_host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_t, 0x111u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, 0x222u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    ut_check(connect_pair(now), "host and client are connected");
}

/* One step of the world: the wire moves, both sessions run, the module under test ticks. */
static void step(uint32_t *now)
{
    *now += 16u;
    mp_loopback_pump(&s_net, *now);
    mp_session_update(&s_host, *now);
    mp_session_update(&s_client, *now);
    mp_bridge_savefile_tick(*now);
}

/* ============================================================================================
 * THE HOST END. The test is the client: it sends masks and takes slices.
 * ============================================================================================ */

static void far_client_send_mask(void)
{
    uint8_t note[MP_SAVEFILE_ACK_BYTES];
    size_t  length = mp_savefile_ack_encode(s_id, s_count, s_far_assembly.have, note, sizeof note);

    (void)mp_session_send_bulk(&s_client, 0, note, length);
}

/* Takes every slice waiting for the hand played client; how many arrived. */
static uint32_t far_client_take_slices(void)
{
    uint8_t  note[MP_SESSION_BULK_BYTES];
    size_t   bytes = 0;
    uint32_t taken = 0;

    while (mp_session_read_bulk(&s_client, 0, note, sizeof note, &bytes)) {
        mp_savefile_chunk_t chunk;

        if (mp_savefile_chunk_decode(note, bytes, &chunk)) {
            (void)mp_savefile_assembly_take(&s_far_assembly, &chunk);
            ++taken;
        }
    }
    return taken;
}

/* Runs the transfer from an empty mask until the hand played client holds the file, or gives up.
 * Returns the tick it completed on, or -1. */
static int run_a_transfer_as_the_client(uint32_t *now)
{
    uint32_t last_mask_ms = 0;
    int      tick;

    mp_savefile_assembly_reset(&s_far_assembly);
    (void)mp_savefile_assembly_open(&s_far_assembly, s_id, (uint32_t)sizeof s_file);
    far_client_send_mask();
    last_mask_ms = *now;
    for (tick = 0; tick < 1500; ++tick) {
        step(now);
        (void)far_client_take_slices();
        if (*now - last_mask_ms >= 100u) {
            far_client_send_mask();
            last_mask_ms = *now;
        }
        if (s_far_assembly.complete) {
            return tick;
        }
    }
    return -1;
}

/* After the file is whole the client says so, a few times, and the host must fall silent. */
static void say_it_is_whole_and_count_what_still_arrives(uint32_t *now, uint32_t *arrived)
{
    int tick;

    *arrived = 0;
    for (tick = 0; tick < 40; ++tick) {          /* 640 ms: several masks, several rests */
        if (tick % 6 == 0) {
            far_client_send_mask();               /* the full mask, said again on the timer */
        }
        step(now);
        if (tick >= 20) {
            *arrived += far_client_take_slices();   /* only what comes after the host has heard */
        } else {
            (void)far_client_take_slices();
        }
    }
}

static void check_the_host_end(void)
{
    uint32_t now = 0;
    uint32_t save_id = 0;
    uint32_t save_bytes = 0;
    uint32_t late = 0;
    int      done;

    ut_section("the host end: a client that asks is sent the file, and the file is whole");

    fresh_pair(&now, 0xA11CEu);
    mp_bridge_savefile_bind(&s_host, &s_client, false);
    ut_check(mp_bridge_savefile_offer(TEST_FILE, &save_id, &save_bytes),
             "the host reads the file the lobby chose");
    ut_checkf(save_id == s_id && save_bytes == (uint32_t)sizeof s_file,
              "and names it by its digest and size: %08X / %u", (unsigned)save_id,
              (unsigned)save_bytes);

    done = run_a_transfer_as_the_client(&now);
    ut_checkf(done >= 0, "the hand played client holds the whole file after %d tick(s)", done);
    ut_check(s_far_assembly.complete && memcmp(s_far_assembly.bytes, s_file, sizeof s_file) == 0,
             "byte for byte");

    ut_section("and once the client says it has everything, the host stops sending");
    say_it_is_whole_and_count_what_still_arrives(&now, &late);
    ut_checkf(late == 0u,
              "no slice arrives after the host has heard the full mask, %u did", (unsigned)late);

    /* The first of the two defects above. The record for slot 0 says complete. The client
     * leaves; another joins and, as the lowest free slot, lands on slot 0 as well. It has nothing
     * and says so. The host has to send, and did not, because a completed record was never
     * cleared for a peer that had gone and never re-armed by a mask that named nothing. */
    ut_section("a NEW client on the same slot, after the first one left, is sent the file again");

    mp_session_disconnect(&s_client);
    {
        int tick;

        for (tick = 0; tick < 60 && mp_session_peer_count(&s_host) != 0u; ++tick) {
            now += 16u;
            mp_loopback_pump(&s_net, now);
            mp_session_update(&s_host, now);
            mp_session_update(&s_client, now);
            mp_bridge_savefile_tick(now);
        }
        ut_check(mp_session_peer_count(&s_host) == 0u, "the host saw the first client leave");
    }
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, 0x333u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    ut_check(connect_pair(&now), "a second client connects");
    ut_check(mp_session_peer(&s_host, 0)->state == MP_PEER_CONNECTED,
             "and, as the lowest free slot, it is peer 0, the slot whose record says complete");

    done = run_a_transfer_as_the_client(&now);
    ut_checkf(done >= 0,
              "the second client is sent the file: whole after %d tick(s) (-1 means the host never "
              "sent a slice, because a completed record from the player before was never cleared)",
              done);

    /* And a host that opens a new session on the same file: the records must not survive it. */
    ut_section("a new session on the same savegame starts every record afresh");
    mp_bridge_savefile_bind(&s_host, &s_client, false);
    ut_check(mp_bridge_savefile_offer(TEST_FILE, &save_id, &save_bytes),
             "the same file is offered again");
    done = run_a_transfer_as_the_client(&now);
    ut_checkf(done >= 0, "and the client on slot 0 is sent it again, whole after %d tick(s)", done);
}

/* ============================================================================================
 * THE CLIENT END. The test is the host: it takes masks and sends what they do not name.
 * ============================================================================================ */

static uint8_t  s_far_acked[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
static uint32_t s_far_masks_in;
static bool     s_far_saw_full_mask;

/* Reads every mask the hand played host has been sent. */
static void far_host_take_masks(void)
{
    uint8_t note[MP_SESSION_BULK_BYTES];
    size_t  bytes = 0;

    while (mp_session_read_bulk(&s_host, 0, note, sizeof note, &bytes)) {
        const uint8_t *mask  = NULL;
        uint32_t       id    = 0;
        uint16_t       count = 0;

        if (mp_savefile_ack_decode(note, bytes, &id, &count, &mask) && id == s_id &&
            count == s_count) {
            ++s_far_masks_in;
            memcpy(s_far_acked, mask, MP_SAVEFILE_MASK_BYTES_FOR(count));
            if (mp_savefile_mask_count(mask, count) == count) {
                s_far_saw_full_mask = true;
            }
        }
    }
}

/* Sends up to four slices the newest mask does not name, oldest index first. A host that has
 * not seen a full mask keeps offering the slices it has not seen acknowledged, which is exactly
 * what the real host does, and exactly why the client must go on answering. */
static void far_host_send_unacked(void)
{
    uint8_t  note[MP_SAVEFILE_CHUNK_BYTES];
    uint16_t index;
    uint32_t laid = 0;

    for (index = 0; index < s_count && laid < 4u; ++index) {
        size_t length;

        if (mp_savefile_mask_has(s_far_acked, index)) {
            continue;
        }
        length = mp_savefile_chunk_encode(s_id, s_file, (uint32_t)sizeof s_file, index, note,
                                          sizeof note);
        if (length > 0u && mp_session_send_bulk(&s_host, 0, note, length)) {
            ++laid;
        }
    }
}

static void feed_the_client_a_setup(void)
{
    mp_lobby_setup_t setup;
    uint8_t          note[256];
    size_t           length;

    memset(&setup, 0, sizeof setup);
    setup.mode       = 1u;
    setup.flags      = (uint8_t)MP_LOBBY_F_FROM_SAVE;
    setup.save_id    = s_id;
    setup.save_bytes = (uint32_t)sizeof s_file;
    memcpy(setup.level, "level\\espa.b3d", 15u);
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length > 0u, "a setup note that names the savegame encodes");
    ut_check(mp_bridge_lobby_take_setup(note, length), "and the client's lobby takes it");
}

static void check_the_client_end(void)
{
    uint32_t now = 0;
    uint32_t masks_after_ready = 0;
    int      tick;
    int      ready_on = -1;

    ut_section("the client end: it asks with a mask, assembles, writes, and says it is whole");

    fresh_pair(&now, 0xC11E17u);
    (void)DeleteFileA(MP_SAVES_JOIN_PATH);
    /* Bound the way a real client is bound: the lobby as a client, which binds this module
     * with it. Binding this module alone left the lobby a host, and a host takes no setup
     * note from anybody. */
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, true, NULL);
    feed_the_client_a_setup();

    memset(s_far_acked, 0, sizeof s_far_acked);
    s_far_masks_in      = 0;
    s_far_saw_full_mask = false;

    for (tick = 0; tick < 1500 && ready_on < 0; ++tick) {
        step(&now);
        far_host_take_masks();
        if (tick % 6 == 0) {
            far_host_send_unacked();   /* about every hundred milliseconds, like the real rest */
        }
        if (mp_bridge_savefile_ready(s_id, (uint32_t)sizeof s_file)) {
            ready_on = tick;
        }
    }
    ut_checkf(ready_on >= 0, "the client holds the file on disk after %d tick(s)", ready_on);
    ut_checkf(s_far_masks_in > 0u, "and the hand played host was sent %u mask(s) on the way",
              (unsigned)s_far_masks_in);

    /* The second of the two defects above. The file is written and the client is ready, but
     * the host has not necessarily heard the full mask, the last one may have been lost, or the
     * client may have become whole between two of its own timer ticks. A host that has not heard
     * goes on resending the last slice. The client MUST go on answering while that happens, or
     * the host resends for the rest of the connection and reports a transfer nobody finished. */
    ut_section("after the file is written the client goes on answering until the host has heard");

    s_far_saw_full_mask = false;
    {
        uint32_t before = s_far_masks_in;

        for (tick = 0; tick < 60; ++tick) {          /* about a second */
            step(&now);
            far_host_take_masks();
            if (!s_far_saw_full_mask && tick % 6 == 0) {
                far_host_send_unacked();              /* the host has not heard: it keeps asking */
            }
        }
        masks_after_ready = s_far_masks_in - before;
    }
    ut_checkf(masks_after_ready > 0u,
              "the client sent %u mask(s) after it was ready (0 means it fell silent the moment "
              "the file was written, and the host can never learn to stop)",
              (unsigned)masks_after_ready);
    ut_check(s_far_saw_full_mask,
             "and one of them named every slice, which is the only sentence that ends a transfer");

    ut_section("and once the host has stopped, so does the client");
    {
        uint32_t before;
        int      quiet;

        for (quiet = 0; quiet < 160; ++quiet) {       /* well past BULK_QUIET_MS with no slice */
            step(&now);
        }
        far_host_take_masks();
        before = s_far_masks_in;
        for (quiet = 0; quiet < 30; ++quiet) {
            step(&now);
        }
        far_host_take_masks();
        ut_checkf(s_far_masks_in == before,
                  "no mask arrives once the host has been silent for long enough, %u did",
                  (unsigned)(s_far_masks_in - before));
    }

    (void)DeleteFileA(MP_SAVES_JOIN_PATH);
}

/* ============================================================================================
 * Many clients at once. The loopback has two endpoints, so this section brings its own network: a
 * mailbox with a host and three clients, delivering in order and losing nothing, and a count of
 * every slice the host puts on the wire, so the test can say per tick how many left.
 *
 * Three clients because the property is about a TOTAL: with two, the peers' own budgets never
 * add up to more than twice one of them, and a cap of twice the per peer budget would be proven
 * by nothing. Three at their own budget of four are twelve, and the cap says eight.
 * ============================================================================================ */

#define BOX_CLIENTS 3u
#define BOX_SLOTS   512u

typedef struct box_packet {
    uint32_t to;
    uint32_t from;
    size_t   bytes;
    uint8_t  data[MP_CHANNEL_PACKET_BYTES];
} box_packet_t;

typedef struct box {
    box_packet_t queue[BOX_SLOTS];
    size_t       count;
    uint32_t     slices_from_host;   /* bulk datagrams from endpoint 0 that carry a slice */
} box_t;

typedef struct box_binding {
    box_t   *box;
    uint32_t endpoint;
} box_binding_t;

static box_t                  s_box;
static box_binding_t          s_box_binding[1u + BOX_CLIENTS];
static mp_transport_t         s_box_transport[1u + BOX_CLIENTS];
static mp_session_t           s_clients[BOX_CLIENTS];
static mp_savefile_assembly_t s_far_assemblies[BOX_CLIENTS];

static bool box_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    box_binding_t *binding = (box_binding_t *)context;
    box_packet_t  *slot;
    const uint8_t *data = (const uint8_t *)packet;

    if (binding->box->count == BOX_SLOTS || bytes > sizeof slot->data) {
        return false;
    }
    slot = &binding->box->queue[binding->box->count++];
    slot->to    = endpoint;
    slot->from  = binding->endpoint;
    slot->bytes = bytes;
    memcpy(slot->data, data, bytes);
    /* A slice: a bulk datagram from the host whose note reads as a chunk. Masks and payload
     * packets are not counted, so the number is the lane's slices and nothing else. */
    if (binding->endpoint == 0u && bytes > MP_SESSION_PAYLOAD_HEADER &&
        data[4] == (uint8_t)MP_SESSION_BULK_TYPE &&
        mp_savefile_is_chunk(data + MP_SESSION_PAYLOAD_HEADER, bytes - MP_SESSION_PAYLOAD_HEADER)) {
        ++binding->box->slices_from_host;
    }
    return true;
}

static size_t box_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    box_binding_t *binding = (box_binding_t *)context;
    box_t         *box = binding->box;
    size_t         index;

    for (index = 0; index < box->count; ++index) {
        if (box->queue[index].to == binding->endpoint) {
            size_t bytes = box->queue[index].bytes;

            if (bytes > capacity) {
                return 0;
            }
            memcpy(buffer, box->queue[index].data, bytes);
            *from = box->queue[index].from;
            memmove(&box->queue[index], &box->queue[index + 1u],
                    (box->count - index - 1u) * sizeof box->queue[0]);
            --box->count;
            return bytes;
        }
    }
    return 0;
}

static void box_build(void)
{
    size_t i;

    memset(&s_box, 0, sizeof s_box);
    for (i = 0; i < 1u + BOX_CLIENTS; ++i) {
        s_box_binding[i].box      = &s_box;
        s_box_binding[i].endpoint = (uint32_t)i;
        memset(&s_box_transport[i], 0, sizeof s_box_transport[i]);
        s_box_transport[i].context = &s_box_binding[i];
        s_box_transport[i].send    = &box_send;
        s_box_transport[i].recv    = &box_recv;
    }
}

static void box_step_sessions(uint32_t now)
{
    size_t c;

    mp_session_update(&s_host, now);
    for (c = 0; c < BOX_CLIENTS; ++c) {
        mp_session_update(&s_clients[c], now);
    }
}

static void far_clients_send_masks(void)
{
    size_t c;

    for (c = 0; c < BOX_CLIENTS; ++c) {
        uint8_t note[MP_SAVEFILE_ACK_BYTES];
        size_t  length = mp_savefile_ack_encode(s_id, s_count, s_far_assemblies[c].have, note,
                                                sizeof note);

        (void)mp_session_send_bulk(&s_clients[c], 0, note, length);
    }
}

static bool far_clients_take_slices_and_say_if_all_whole(void)
{
    bool   all = true;
    size_t c;

    for (c = 0; c < BOX_CLIENTS; ++c) {
        uint8_t note[MP_SESSION_BULK_BYTES];
        size_t  bytes = 0;

        while (mp_session_read_bulk(&s_clients[c], 0, note, sizeof note, &bytes)) {
            mp_savefile_chunk_t chunk;

            if (mp_savefile_chunk_decode(note, bytes, &chunk)) {
                (void)mp_savefile_assembly_take(&s_far_assemblies[c], &chunk);
            }
        }
        all = all && s_far_assemblies[c].complete;
    }
    return all;
}

/* Three clients on the box, each connected and each saying it holds nothing, and a host holding
 * the file. */
static void box_open(uint32_t *now)
{
    uint32_t save_id = 0;
    uint32_t save_bytes = 0;
    size_t   c;
    int      tick;

    box_build();
    mp_session_init(&s_host, MP_SESSION_HOST, &s_box_transport[0], 0x111u);
    for (c = 0; c < BOX_CLIENTS; ++c) {
        mp_session_init(&s_clients[c], MP_SESSION_CLIENT, &s_box_transport[1u + c],
                        0x1000u + (uint32_t)c);
        mp_session_connect(&s_clients[c], 0u);
    }
    for (tick = 0; tick < 400 && mp_session_peer_count(&s_host) != BOX_CLIENTS; ++tick) {
        *now += 16u;
        box_step_sessions(*now);
    }
    ut_checkf(mp_session_peer_count(&s_host) == BOX_CLIENTS, "the host holds %u client(s), not %u",
              (unsigned)BOX_CLIENTS, (unsigned)mp_session_peer_count(&s_host));

    mp_bridge_savefile_bind(&s_host, NULL, false);
    ut_check(mp_bridge_savefile_offer(TEST_FILE, &save_id, &save_bytes), "the host holds the file");
    for (c = 0; c < BOX_CLIENTS; ++c) {
        mp_savefile_assembly_reset(&s_far_assemblies[c]);
        (void)mp_savefile_assembly_open(&s_far_assemblies[c], s_id, (uint32_t)sizeof s_file);
    }
    far_clients_send_masks();   /* all three say "I have nothing" in the same tick */
}

static void check_the_budget_over_all_peers(void)
{
    uint32_t now = 0;
    uint32_t last_mask_ms = 0;
    uint32_t most_in_a_tick = 0;
    uint32_t ticks_at_the_total = 0;
    bool     all_whole = false;
    size_t   c;
    int      tick;

    ut_section("three clients asking at once: no tick puts more than the total budget on the wire");
    box_open(&now);
    last_mask_ms = now;

    for (tick = 0; tick < 1500 && !all_whole; ++tick) {
        now += 16u;
        s_box.slices_from_host = 0;
        box_step_sessions(now);
        mp_bridge_savefile_tick(now);
        if (s_box.slices_from_host > most_in_a_tick) {
            most_in_a_tick = s_box.slices_from_host;
        }
        if (s_box.slices_from_host == MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK) {
            ++ticks_at_the_total;
        }
        all_whole = far_clients_take_slices_and_say_if_all_whole();
        if (now - last_mask_ms >= 100u) {
            far_clients_send_masks();
            last_mask_ms = now;
        }
    }
    ut_checkf(all_whole, "all three clients hold the whole file after %d tick(s)", tick);
    for (c = 0; c < BOX_CLIENTS; ++c) {
        ut_checkf(memcmp(s_far_assemblies[c].bytes, s_file, sizeof s_file) == 0,
                  "client %u byte for byte", (unsigned)c);
    }
    ut_checkf(most_in_a_tick <= MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK,
              "THE TOTAL HOLDS: the most slices any tick put on the wire was %u, the total budget "
              "is %u (three peers at their own budget of %u with no total would be %u)",
              (unsigned)most_in_a_tick, (unsigned)MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK,
              (unsigned)MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK,
              (unsigned)(BOX_CLIENTS * MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK));
    ut_checkf(ticks_at_the_total > 0u,
              "and the total was reached in %u tick(s), so it was a limit that bound and not a "
              "number nobody met", (unsigned)ticks_at_the_total);

    for (c = 0; c < BOX_CLIENTS; ++c) {
        mp_session_disconnect(&s_clients[c]);
    }
}

/* The pump that ticks the lane runs at the frame rate, which nothing bounds. A thousand ticks a
 * second used to lay eight slices each, as often as the rest let the same slices go again; the
 * second's budget holds whatever the tick. Nobody acknowledges here, so every slice is due again
 * after each rest and the lane would lay all it may. */
static void check_the_pace_under_a_fast_pump(void)
{
    uint32_t now = 0;
    uint32_t laid = 0;
    size_t   c;
    int      tick;

    ut_section("a host ticked a thousand times a second lays no more than a second's budget");
    box_open(&now);
    for (tick = 0; tick < 1000; ++tick) {
        now += 1u;
        s_box.slices_from_host = 0;
        box_step_sessions(now);
        mp_bridge_savefile_tick(now);
        laid += s_box.slices_from_host;
    }
    ut_checkf(laid <= MP_BRIDGE_SAVEFILE_SLICES_PER_SECOND +
                          MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK,
              "%u slice(s) in one second of a thousand ticks, against %u a second and a first "
              "tick of %u", (unsigned)laid, (unsigned)MP_BRIDGE_SAVEFILE_SLICES_PER_SECOND,
              (unsigned)MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK);
    ut_checkf(laid >= BOX_CLIENTS * (uint32_t)s_count,
              "and every client was sent every slice at least once: %u of %u", (unsigned)laid,
              (unsigned)(BOX_CLIENTS * (uint32_t)s_count));
    for (c = 0; c < BOX_CLIENTS; ++c) {
        mp_session_disconnect(&s_clients[c]);
    }
}

int main(void)
{
    make_the_file();
    check_the_host_end();
    check_the_client_end();
    check_the_budget_over_all_peers();
    check_the_pace_under_a_fast_pump();
    (void)DeleteFileA(TEST_FILE);

    return ut_summary("mp_bridge_savefile");
}
