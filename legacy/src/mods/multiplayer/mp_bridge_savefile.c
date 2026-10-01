/* mp_bridge_savefile.c: the host holds the file and answers; the client asks, assembles, writes.
 * See the header.
 *
 * SIZE NOTE: over 600 lines, and most of the excess is the paragraph below rather than
 * code, three field runs and three wrong repairs are written down here because the next person to
 * change a number needs to read them first. The two ends share the held file, the counters and the
 * report, and neither is meaningful without the other; the seam, should this grow, is the
 * Windows file handling (offer and write_the_file), which touches no wire at all.
 */
#include "mp_bridge_savefile.h"

#include "mp_bridge_lobby.h"
#include "mp_channel.h"
#include "mp_lobby.h"
#include "mp_savefile.h"
#include "mp_saves.h"
#include "mp_wallclock.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= The transfer, and the four ways it was wrong =========================
 *
 * It now runs on the session's BULK LANE and no longer on the reliable channel. What follows is
 * why, because three repairs were made to the old shape and every one of them was a guess about
 * a number when the shape itself was the fault: a share of the channel's queue for the chunks,
 * then the counter that share was measured against, then the number of chunks a packet can seat.
 *
 * The old shape. The host held a CURSOR per peer and laid chunks into the ordered reliable
 * channel from it. The client asked "send me file X from chunk N", which rewound that cursor. A
 * transfer was finished when the cursor ran off the end.
 *
 * What killed it. A reliable message is offered a seat only in what the packet has left after the
 * unreliable payload is reserved. In a level that payload is a snapshot plus a block of enemies,
 * most of a packet, so a chunk of 1043 bytes was offered 200 and never fitted. It did not fail:
 * it was SKIPPED, silently, every packet, for ever, and since the channel is ordered, everything
 * queued behind it stopped as well. The field said 70 of 78 chunks, the lobby band said 89 %, the
 * roster could not send 3277 times, and the host reported the transfer COMPLETE, because its
 * cursor had run off the end while eight chunks sat unseatable in its queue.
 *
 * The three repairs before that, each of which looked right and shipped. A share of the queue
 * for the chunks fixed a jammed setup note and left the transfer at the mercy of everybody
 * else's traffic, because the count it was measured against was the whole channel's pending
 * messages and not the transfer's own: the moment anything else kept eight messages in flight
 * the transfer stopped dead, and it did so the first time the conversation relay and the shared
 * story began repeating from a host in its restored level, which the player reported as a new
 * bug in those two features. Correcting the counter's name was a true finding about the comment
 * and said nothing about the number. Widening the number on the strength of that permitted forty
 * kilobytes in flight and collapsed the transfer to eleven of forty five slices.
 *
 * What is built now, and it is the ordinary answer to this problem rather than a new idea. The
 * slices go out as their own datagrams: no order, no queue, no competition with a snapshot. The
 * CLIENT answers with a bitmask of every slice it holds, the whole mask, every time, so no
 * acknowledgement needs to arrive and none needs reliability of its own. The host walks its
 * slices round robin, skips what the mask names and what it sent within a rest, and sends the
 * rest. A transfer is finished when the MASK IS FULL, which only the receiver can say.
 *
 * The four properties that follow from that, each answering one of the old failures:
 *   - a lost slice is a NAMED slice rather than a silence, so nothing waits on a guess;
 *   - the host cannot believe a transfer finished, because the client decides;
 *   - the block cannot block anything: the control channel never carries it;
 *   - and there is no window and no share to get wrong, because nothing queues.
 */

/* The two per tick budgets are in the header, with their reasons. What is checked here is the
 * byte arithmetic the total's reason rests on: at the total, the lane puts fewer bytes on the
 * wire in a tick than the game's own connected packets to every peer slot may. If a slice grows,
 * the envelope grows or the peer count grows, the claim in the header stops being true and the
 * build says so before a comment goes stale. The bandwidth itself is not derived from anything
 * here: the tick is the idle pump's and its rate is measured in the report. */
_Static_assert(MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK *
                       (MP_SESSION_PAYLOAD_HEADER + MP_SAVEFILE_CHUNK_BYTES) <=
                   MP_SESSION_MAX_PEERS * MP_CHANNEL_PACKET_BYTES / 2u,
               "the lane at its total budget would put more than half of what the game's own "
               "packets may on the wire in one tick; the reason for the total in the header no "
               "longer holds");
_Static_assert(MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK >= MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK,
               "a total under the per peer budget would cut every single transfer short");

/* How long a slice rests after being sent before it is sent again, and the floor under that rest.
 * The classic form for this is a round trip and a quarter, so an acknowledgement of the copy that
 * is out has time to arrive before a second one is spent; the floor keeps a link that reports no
 * round trip yet from resending everything at the tick rate. */
#define BULK_RESEND_FLOOR_MS 100u

/* How often the receiver says what it holds, and how long after the last slice it stops saying
 * it. The rest above is the reason for the first: a mask that arrives every 100 ms is never more
 * than one rest stale, so the host wastes at most one copy of a slice that had already arrived. */
#define BULK_ACK_MS   100u
#define BULK_QUIET_MS 2000u

/* What the compiler can check is not left to a paragraph.
 *
 * A slice travels alone in its own datagram now, so the old arithmetic about how many fitted
 * beside a snapshot is gone with the shape it belonged to. One thing still has to hold, and it is
 * the one the whole lane rests on: a slice must fit in a bulk note. If a slice ever grows or the
 * lane ever shrinks, the build stops rather than the transfer. */
_Static_assert(MP_SAVEFILE_CHUNK_BYTES <= MP_SESSION_BULK_BYTES,
               "a savegame slice no longer fits in one bulk note, so it cannot be sent at all");
_Static_assert(MP_SAVEFILE_ACK_BYTES <= MP_SESSION_BULK_BYTES,
               "the acknowledging mask no longer fits in one bulk note");

/* What the host knows about one peer's transfer. There is no cursor any more in the old sense:
 * `next` is only where the round robin walk resumes, so that a scan interrupted by the per tick
 * budget does not start from the beginning again and pour every attempt into the first slices. */
typedef struct savefile_cursor {
    bool     active;        /* this peer has asked, by sending a mask */
    bool     complete;      /* its mask named every slice: the receiver says so, not this side */
    uint16_t next;          /* where the round robin walk resumes */
    uint16_t acked_count;
    uint32_t last_said_ms;  /* when the report line for this peer was last written */
    uint8_t  acked[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
    uint8_t  ever_sent[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
    uint32_t sent_ms[MP_SAVEFILE_MAX_CHUNKS];   /* per slice, so a rest is per slice */
} savefile_cursor_t;

typedef struct savefile_state {
    mp_session_t *host;
    mp_session_t *client;
    bool          is_client;

    /* HOST: the file the lobby chose. */
    bool     held;
    uint32_t held_id;
    uint32_t held_bytes;
    char     held_name[MP_SAVES_FILE_MAX];
    uint8_t  held_file[MP_SAVEFILE_MAX_BYTES];
    savefile_cursor_t cursor[MP_SESSION_MAX_PEERS];
    mp_savefile_pace_t pace;         /* the lane's slices a second, over all peers */

    uint32_t offers;
    uint32_t offers_refused;
    uint32_t acks_in;                 /* masks that arrived from a client */
    uint32_t acks_for_another_file;
    uint32_t acks_torn;               /* a mask that did not describe the file it named */
    uint32_t slices_sent;
    uint32_t slices_resent;           /* sent again because a mask had not named them */
    uint32_t slices_resting;          /* skipped: the copy that is out is younger than the rest */
    uint32_t sends_refused;           /* the lane refused it, which means the peer went away */
    uint32_t slices_at_the_budget;    /* peer walks that laid the whole per peer budget */
    uint32_t ticks_at_the_total_budget; /* ticks that laid the whole budget over ALL peers */
    uint32_t ticks_held_by_the_pace;  /* ticks the second's budget held under the tick's */
    uint32_t transfers_done;          /* every slice ACKNOWLEDGED, said by the receiver */
    bool     other_file_logged;

    /* The tick rate, measured. The pacing is a count of slices per tick, and the tick is the
     * bridge's idle pump, whose rate nothing in the tree states: one field run's host counted
     * 2915 timer pumps against 9104 frame pumps, so the rate is the frame rate plus the timer's.
     * These three turn "four slices a tick" into a rate in the report: how many ticks laid a
     * slice, and the wall clock from the first such tick to the last. */
    uint32_t ticks_with_a_slice;
    uint32_t first_slice_tick_ms;
    uint32_t last_slice_tick_ms;
    size_t   first_peer;              /* where the walk over the peers starts this tick */

    /* CLIENT: the file the setup names. */
    mp_savefile_assembly_t assembly;
    uint32_t last_chunk_ms;
    uint32_t last_ack_ms;
    uint32_t written_id;
    uint32_t written_bytes;
    bool     wanted_logged;

    uint32_t acks_sent;
    uint32_t acks_unsent;
    uint32_t chunks_in;
    uint32_t bulk_notes_in;           /* everything the lane handed over, of any kind */
    uint32_t bulk_notes_strange;      /* neither a slice nor a mask, or for the wrong side */
    uint32_t stray_reliable_notes;    /* a note of the OLD shape, on the channel it left */
    uint32_t writes;
    uint32_t write_faults;
} savefile_state_t;

/* Static rather than on any stack: it carries two copies of a file. */
static savefile_state_t sf;

void mp_bridge_savefile_bind(mp_session_t *host, mp_session_t *client, bool is_client)
{
    sf.host      = host;
    sf.client    = client;
    sf.is_client = is_client;
    /* A bridge bound again is a new session on the same process; a file written for the last one
     * stays valid by its name, but a half assembled one does not, and neither does anything
     * remembered about a peer. A record that said "complete" for whoever held slot 0 last time
     * kept the next player on that slot from ever being sent a slice. */
    mp_savefile_assembly_reset(&sf.assembly);
    memset(sf.cursor, 0, sizeof sf.cursor);
    memset(&sf.pace, 0, sizeof sf.pace);
}

const char *mp_bridge_savefile_path(void)
{
    return MP_SAVES_JOIN_PATH;
}

/* ==============================================================================================
 * The host's end.
 * ============================================================================================ */

static void forget_every_cursor(void)
{
    memset(sf.cursor, 0, sizeof sf.cursor);
}

bool mp_bridge_savefile_offer(const char *file, uint32_t *save_id, uint32_t *save_bytes)
{
    HANDLE handle;
    DWORD  size;
    DWORD  read = 0;

    if (save_id != NULL) {
        *save_id = 0u;
    }
    if (save_bytes != NULL) {
        *save_bytes = 0u;
    }
    if (file == NULL || file[0] == '\0') {
        return false;
    }
    /* The same choice offered again is the same file: what the note carries has to stay what
     * the clients already compared against, so it is not read a second time. */
    if (sf.held && _stricmp(sf.held_name, file) == 0) {
        if (save_id != NULL) {
            *save_id = sf.held_id;
        }
        if (save_bytes != NULL) {
            *save_bytes = sf.held_bytes;
        }
        return true;
    }
    ++sf.offers;
    sf.held = false;
    forget_every_cursor();

    handle = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        ++sf.offers_refused;
        log_warning("the savegame %s could not be opened (error %lu), so the clients begin from "
                    "the level alone", file, (unsigned long)GetLastError());
        return false;
    }
    size = GetFileSize(handle, NULL);
    if (size == INVALID_FILE_SIZE || mp_savefile_chunk_count(size) == 0u) {
        CloseHandle(handle);
        ++sf.offers_refused;
        log_warning("the savegame %s is %lu bytes, which the transfer does not carry, so the "
                    "clients begin from the level alone", file, (unsigned long)size);
        return false;
    }
    if (!ReadFile(handle, sf.held_file, size, &read, NULL) || read != size) {
        CloseHandle(handle);
        ++sf.offers_refused;
        log_warning("the savegame %s could not be read whole (%lu of %lu bytes), so the clients "
                    "begin from the level alone", file, (unsigned long)read, (unsigned long)size);
        return false;
    }
    CloseHandle(handle);

    sf.held       = true;
    sf.held_id    = mp_savefile_digest(sf.held_file, size);
    sf.held_bytes = size;
    memset(sf.held_name, 0, sizeof sf.held_name);
    memcpy(sf.held_name, file, strlen(file) < sizeof sf.held_name ? strlen(file)
                                                                   : sizeof sf.held_name - 1u);
    if (save_id != NULL) {
        *save_id = sf.held_id;
    }
    if (save_bytes != NULL) {
        *save_bytes = sf.held_bytes;
    }
    log_info("the savegame %s is held for the clients: %lu bytes in %u chunk(s), named %08X",
             file, (unsigned long)size, (unsigned)mp_savefile_chunk_count(size),
             (unsigned)sf.held_id);
    return true;
}

void mp_bridge_savefile_withdraw(void)
{
    if (sf.held) {
        log_info("the savegame %s is no longer offered: the lobby chose a level", sf.held_name);
    }
    sf.held = false;
    forget_every_cursor();
}

/* A mask from a client: what it holds, and therefore what it is still asking for. It is both the
 * request and the acknowledgement, so a peer that was silent and a peer that is halfway through are
 * the same case and neither needs a state on this side that the other does not have. */
static void take_ack(size_t peer_index, uint32_t file_id, uint16_t count, const uint8_t *mask)
{
    savefile_cursor_t *cursor;
    uint16_t           held;

    if (peer_index >= MP_SESSION_MAX_PEERS) {
        return;
    }
    ++sf.acks_in;
    if (!sf.held || file_id != sf.held_id) {
        ++sf.acks_for_another_file;
        if (!sf.other_file_logged) {
            sf.other_file_logged = true;
            log_warning("a client asked for savegame %08X and this side holds %s, so nothing is "
                        "sent; later such requests are counted", (unsigned)file_id,
                        sf.held ? "another" : "none");
        }
        return;
    }
    /* The count the mask declares has to be the count THIS file has, or the bits do not mean what
     * this side would read them as. A client that is one setup note behind can say so. */
    if (count != mp_savefile_chunk_count(sf.held_bytes)) {
        ++sf.acks_torn;
        return;
    }
    cursor = &sf.cursor[peer_index];
    held   = mp_savefile_mask_count(mask, count);
    /* The mask is the truth and the record is only a memory. A record that says complete
     * belongs to whoever held this slot last; the peer sending this mask may be somebody else
     * who landed on the same slot, or the same player back without the file. A mask that names
     * less than everything is therefore a transfer to start, whatever the record remembers;
     * and a mask that names everything from a peer with no record is a peer that already holds
     * the file, which is not a transfer and is not counted as one. The first form of this
     * function trusted the record over the mask, and the second player to join was never sent
     * a slice. */
    if (!cursor->active) {
        if (held == count) {
            if (!cursor->complete) {
                memset(cursor, 0, sizeof *cursor);
                cursor->complete = true;
                log_info("peer %u already holds the savegame: all %u slice(s), nothing to send",
                         (unsigned)peer_index, (unsigned)count);
            }
            memcpy(cursor->acked, mask, MP_SAVEFILE_MASK_BYTES_FOR(count));
            cursor->acked_count = held;
            return;
        }
        memset(cursor, 0, sizeof *cursor);
        cursor->active = true;
        log_info("peer %u is asking for the savegame: %u slice(s) to send, it holds %u",
                 (unsigned)peer_index, (unsigned)count, (unsigned)held);
    }
    memcpy(cursor->acked, mask, MP_SAVEFILE_MASK_BYTES_FOR(count));
    cursor->acked_count = held;
    if (held == count) {
        /* The only place a transfer is called finished, and the receiver is what says it. The old
         * shape said it when its own cursor ran off the end, which is why a run could report one
         * completed transfer while the client sat at 89 per cent. */
        cursor->complete = true;
        cursor->active   = false;
        ++sf.transfers_done;
        log_info("the savegame is COMPLETE on peer %u: it acknowledged all %u slice(s), %lu bytes",
                 (unsigned)peer_index, (unsigned)count, (unsigned long)sf.held_bytes);
    }
}

/* A slice rests a round trip and a quarter before a second copy is spent on it, and never less
 * than the floor. A link that has not measured a round trip yet reports nought and gets the
 * floor, which is what the floor is for. */
static uint32_t resend_rest_ms(uint32_t rtt_ms)
{
    uint32_t rest = rtt_ms + rtt_ms / 4u;

    return rest < BULK_RESEND_FLOOR_MS ? BULK_RESEND_FLOOR_MS : rest;
}

/* Round robin, not from the front. Walking from the first missing slice every tick would spend
 * every attempt on the same few and starve the tail; resuming where the last walk stopped spreads
 * the sending over the whole file, which is what makes one lost slice cost one resend rather than
 * a second pass over everything behind it.
 *
 * `allowance` is how many slices this walk may lay: the per peer budget, or what is left of the
 * total over all peers, whichever is smaller. Returns how many it laid, so the caller can keep
 * the total. */
static uint32_t send_to_one_peer(size_t peer_index, uint16_t count, uint32_t now_ms,
                                 uint32_t rest_ms, uint32_t allowance)
{
    savefile_cursor_t *cursor  = &sf.cursor[peer_index];
    uint32_t           laid    = 0;
    uint16_t           scanned = 0;
    uint16_t           first   = 0;
    bool               any     = false;

    while (scanned < count && laid < allowance) {
        uint16_t index = (uint16_t)((cursor->next + scanned) % count);
        uint8_t  note[MP_SAVEFILE_CHUNK_BYTES];
        size_t   length;

        ++scanned;
        if (mp_savefile_mask_has(cursor->acked, index)) {
            continue;   /* the receiver named it: never sent again */
        }
        if (mp_savefile_mask_has(cursor->ever_sent, index) &&
            (uint32_t)(now_ms - cursor->sent_ms[index]) < rest_ms) {
            ++sf.slices_resting;
            continue;   /* a copy is out and has not had time to be acknowledged */
        }
        length = mp_savefile_chunk_encode(sf.held_id, sf.held_file, sf.held_bytes, index, note,
                                          sizeof note);
        if (length == 0u || !mp_session_send_bulk(sf.host, peer_index, note, length)) {
            ++sf.sends_refused;
            break;
        }
        if (mp_savefile_mask_has(cursor->ever_sent, index)) {
            ++sf.slices_resent;
        }
        mp_savefile_mask_set(cursor->ever_sent, index);
        cursor->sent_ms[index] = now_ms;
        ++sf.slices_sent;
        ++laid;
        if (!any) {
            first = index;
            any   = true;
        }
    }
    cursor->next = (uint16_t)((cursor->next + scanned) % count);
    /* What is going out and what is still owed, once a second for as long as anything goes out.
     * The first form of this line fired only inside the last four slices, so the one stall this
     * was written for, seventy of seventy eight, would have printed nothing. A stall is now
     * readable without another field run: this side names the lowest slice the peer has not
     * acknowledged, the client names the lowest it is missing, and the two lines meet. */
    if (any && (cursor->last_said_ms == 0u ||
                (uint32_t)(now_ms - cursor->last_said_ms) >= 1000u)) {
        cursor->last_said_ms = now_ms;
        log_info("the savegame to peer %u: %u slice(s) sent this tick from index %u; %u of %u "
                 "acknowledged, the lowest not is %u",
                 (unsigned)peer_index, (unsigned)laid, (unsigned)first,
                 (unsigned)cursor->acked_count, (unsigned)count,
                 (unsigned)mp_savefile_mask_first_missing(cursor->acked, count));
    }
    return laid;
}

/* The total over all peers is kept here, and the walk over the peers starts one slot further
 * along every tick. Without the rotation the total would be spent on the lowest slots every
 * time and a third client asking at once would be sent nothing until one of the first two had
 * finished; with it, every asking peer gets its turn inside a few ticks. The records of peers
 * that have gone are cleared on the same walk whatever the budget has left, because that has
 * nothing to do with sending.
 *
 * Measured over a mailbox with a host and three clients all sending an empty mask in the same
 * tick: before the total existed the most slices any tick put on the wire was twelve; with it,
 * eight, reached in six ticks, and all three clients whole after eleven ticks against seven
 * without, which is the price. */
static void host_tick(uint32_t now_ms)
{
    size_t   walked;
    uint32_t allowed;
    uint32_t left;
    uint32_t laid_total = 0;

    if (sf.host == NULL || !sf.held) {
        return;
    }
    /* The second's budget first, then the tick's: the pace holds at most the per tick total. */
    allowed = mp_savefile_pace_fill(&sf.pace, now_ms, MP_BRIDGE_SAVEFILE_SLICES_PER_SECOND,
                                    MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK);
    left    = allowed;
    for (walked = 0; walked < MP_SESSION_MAX_PEERS; ++walked) {
        size_t             peer_index = (sf.first_peer + walked) % MP_SESSION_MAX_PEERS;
        savefile_cursor_t *cursor     = &sf.cursor[peer_index];
        const mp_peer_t   *peer       = mp_session_peer(sf.host, peer_index);
        uint16_t           count      = mp_savefile_chunk_count(sf.held_bytes);
        uint32_t           allowance;
        uint32_t           laid;

        if (peer == NULL || peer->state != MP_PEER_CONNECTED) {
            /* Gone, and the record goes with it whether it said active or complete. The first
             * form tested active first and so never cleared a completed record, which then
             * belonged to nobody and blocked the next player on the slot. A peer that comes
             * back asks again by sending a mask, and it starts from what it actually holds
             * rather than from anything remembered here. */
            if (cursor->active || cursor->complete) {
                memset(cursor, 0, sizeof *cursor);
            }
            continue;
        }
        if (!cursor->active || count == 0u || left == 0u) {
            continue;
        }
        allowance = left < MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK
                        ? left : MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK;
        laid = send_to_one_peer(peer_index, count, now_ms,
                                resend_rest_ms(mp_session_peer_rtt_ms(sf.host, peer_index)),
                                allowance);
        left       -= laid;
        laid_total += laid;
        if (laid == MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK) {
            ++sf.slices_at_the_budget;   /* its own budget stopped it, not the total */
        }
    }
    sf.first_peer = (sf.first_peer + 1u) % MP_SESSION_MAX_PEERS;
    mp_savefile_pace_spend(&sf.pace, laid_total);
    if (laid_total == 0u) {
        return;
    }
    if (allowed < MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK && laid_total == allowed) {
        ++sf.ticks_held_by_the_pace;
    }
    if (laid_total == MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK) {
        ++sf.ticks_at_the_total_budget;
    }
    if (sf.ticks_with_a_slice == 0u) {
        sf.first_slice_tick_ms = now_ms;
    }
    sf.last_slice_tick_ms = now_ms;
    ++sf.ticks_with_a_slice;
}

/* ==============================================================================================
 * The client's end.
 * ============================================================================================ */

/* What this side holds, said in full, and it is also the asking. A mask of all zeroes means "I
 * have none of this file", so a client that just learned the name and a client that is eight
 * slices short send the same sentence and the host needs no state to tell them apart. Nothing is
 * remembered about what was said before, because the whole mask goes every time: one that arrives
 * makes every one that was lost irrelevant. */
static void send_ack(uint32_t now_ms)
{
    uint8_t note[MP_SAVEFILE_ACK_BYTES];
    size_t  length;

    sf.last_ack_ms = now_ms;
    length = mp_savefile_ack_encode(sf.assembly.file_id, sf.assembly.count, sf.assembly.have, note,
                                    sizeof note);
    if (sf.client == NULL || length == 0u || !mp_session_is_connected(sf.client) ||
        !mp_session_send_bulk(sf.client, 0, note, length)) {
        ++sf.acks_unsent;
        return;
    }
    ++sf.acks_sent;
}

/* The file to disk, under the one name the start module restores from. Written whole and once
 * per file; a write that fails is counted and tried again on the next tick, because the disk is
 * the only part of this that can fail after the bytes have been proven. */
static void write_the_file(void)
{
    HANDLE handle;
    DWORD  written = 0;

    (void)CreateDirectoryA(MP_SAVES_FOLDER, NULL);   /* exists on every installation; harmless */
    handle = CreateFileA(MP_SAVES_JOIN_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        ++sf.write_faults;
        return;
    }
    if (!WriteFile(handle, sf.assembly.bytes, sf.assembly.total, &written, NULL) ||
        written != sf.assembly.total) {
        CloseHandle(handle);
        ++sf.write_faults;
        return;
    }
    CloseHandle(handle);
    sf.written_id    = sf.assembly.file_id;
    sf.written_bytes = sf.assembly.total;
    ++sf.writes;
    log_info("the host's savegame is on disk as %s: %lu bytes, named %08X, ready to be restored",
             MP_SAVES_JOIN_PATH, (unsigned long)sf.written_bytes, (unsigned)sf.written_id);
}

static void client_tick(uint32_t now_ms)
{
    mp_lobby_setup_t setup;

    if (sf.client == NULL || !mp_bridge_lobby_setup(&setup)) {
        return;
    }
    if ((setup.flags & MP_LOBBY_F_FROM_SAVE) == 0u || setup.save_id == 0u ||
        setup.save_bytes == 0u) {
        return;   /* a fresh level, or a host that could not read its own file */
    }
    if (mp_bridge_savefile_ready(setup.save_id, setup.save_bytes)) {
        /* The file is on disk, and the host may not know it yet. The last mask may have been
         * lost, or this side became whole between two ticks of its own timer. A host that has
         * not heard goes on resending the last slice, and every slice it sends refreshes
         * last_chunk_ms here, so answering for as long as slices keep arriving is exactly as
         * long as the host needs. The first form of this branch returned at once and the host
         * never learned to stop: it resent one slice for the life of the connection and
         * reported a transfer nobody had finished. A file held from an
         * EARLIER session has no open assembly and says nothing, which is right: that host was
         * never asked. */
        if (sf.assembly.open && sf.assembly.file_id == setup.save_id &&
            (uint32_t)(now_ms - sf.last_chunk_ms) < BULK_QUIET_MS &&
            (uint32_t)(now_ms - sf.last_ack_ms) >= BULK_ACK_MS) {
            send_ack(now_ms);
        }
        return;
    }
    if (!mp_savefile_assembly_open(&sf.assembly, setup.save_id, setup.save_bytes)) {
        if (!sf.wanted_logged) {
            sf.wanted_logged = true;
            log_warning("the host names a savegame of %lu bytes, which the transfer does not "
                        "carry, so this side begins from the level alone",
                        (unsigned long)setup.save_bytes);
        }
        return;
    }
    if (!sf.wanted_logged) {
        sf.wanted_logged = true;
        log_info("the host restores a savegame named %08X (%lu bytes) that this side does not "
                 "hold, so it is asked for", (unsigned)setup.save_id,
                 (unsigned long)setup.save_bytes);
    }
    if (sf.assembly.complete && sf.written_id != sf.assembly.file_id) {
        write_the_file();
    }
    /* The mask goes on a timer, not on a silence, and that is the whole change to this end.
     *
     * The old form asked once and then only after two seconds of hearing nothing, because a
     * request rewound the host's cursor and asking twice cost a second pass over the file. A mask
     * costs nothing to repeat: it is under two dozen bytes, it names everything, and the host acts
     * on the newest one it has. So it is said ten times a second while a transfer is running, and
     * every lost one is made irrelevant by the next.
     *
     * What happens AFTER the file is whole is the ready branch above: it goes on answering for as
     * long as slices keep arriving, which is how the host learns to stop. */
    if (!sf.assembly.complete ||
        (uint32_t)(now_ms - sf.last_chunk_ms) < BULK_QUIET_MS) {
        if (sf.last_ack_ms == 0u || (uint32_t)(now_ms - sf.last_ack_ms) >= BULK_ACK_MS) {
            send_ack(now_ms);
        }
    }
}

/* ==============================================================================================
 * Both ends.
 * ============================================================================================ */

/* One note of the lane. Both kinds are recognised on both sides and each is acted on by exactly
 * the side it is addressed to, so a note that arrives at the wrong end is counted rather than
 * mistaken for something else. */
static void take_bulk_note(size_t peer_index, const uint8_t *note, size_t bytes, uint32_t now_ms)
{
    mp_savefile_chunk_t chunk;
    const uint8_t      *mask    = NULL;
    uint32_t            file_id = 0;
    uint16_t            count   = 0;

    ++sf.bulk_notes_in;
    if (mp_savefile_is_chunk(note, bytes)) {
        if (sf.is_client && mp_savefile_chunk_decode(note, bytes, &chunk)) {
            ++sf.chunks_in;
            if (mp_savefile_assembly_take(&sf.assembly, &chunk)) {
                sf.last_chunk_ms = now_ms;
            }
            return;
        }
        ++sf.bulk_notes_strange;
        return;
    }
    if (mp_savefile_is_ack(note, bytes)) {
        if (!sf.is_client && mp_savefile_ack_decode(note, bytes, &file_id, &count, &mask)) {
            take_ack(peer_index, file_id, count, mask);
            return;
        }
        ++sf.bulk_notes_strange;
        return;
    }
    ++sf.bulk_notes_strange;
}

/* The module that owns the traffic empties its own lane, and it does it from the one call that runs
 * both in a lobby and inside a level. Hanging it off the reliable drain instead would have given
 * the rule two call sites, which is the shape that has already cost this feature five defects: two
 * ways into the same state that did not share one rule. */
static void drain_the_lane(uint32_t now_ms)
{
    mp_session_t *session = sf.is_client ? sf.client : sf.host;
    uint8_t       note[MP_SESSION_BULK_BYTES];
    size_t        bytes = 0;
    size_t        peers = sf.is_client ? 1u : (size_t)MP_SESSION_MAX_PEERS;
    size_t        peer;

    if (session == NULL) {
        return;
    }
    for (peer = 0; peer < peers; ++peer) {
        while (mp_session_read_bulk(session, peer, note, sizeof note, &bytes)) {
            take_bulk_note(peer, note, bytes, now_ms);
        }
    }
}

/* The reliable channel no longer carries anything of this module. The recogniser stays so that a
 * note from a build of the old shape is answered with silence and a count rather than being read
 * by whatever module happens to look at it next. */
bool mp_bridge_savefile_take_note(size_t peer_index, const uint8_t *note, size_t bytes)
{
    (void)peer_index;
    if (mp_savefile_is_request(note, bytes) || mp_savefile_is_chunk(note, bytes) ||
        mp_savefile_is_ack(note, bytes)) {
        ++sf.stray_reliable_notes;   /* not the lane's: two lanes, two counters */
        return true;
    }
    return false;
}

void mp_bridge_savefile_tick(uint32_t now_ms)
{
    drain_the_lane(now_ms);
    if (sf.is_client) {
        client_tick(now_ms);
    } else {
        host_tick(now_ms);
    }
}

bool mp_bridge_savefile_ready(uint32_t save_id, uint32_t save_bytes)
{
    return save_id != 0u && sf.written_id == save_id && sf.written_bytes == save_bytes;
}

uint32_t mp_bridge_savefile_percent(void)
{
    return mp_savefile_assembly_percent(&sf.assembly);
}

void mp_bridge_savefile_report(void)
{
    size_t peer_index;

    if (sf.is_client) {
        /* Which slices are missing, which no run before this one could say. The old line said
           "asked 18 time(s)" and nothing about what was still wanted, so a stall could only be
           diagnosed by guessing. Now the two sides name the same indices and the lines meet. */
        uint16_t missing = sf.assembly.open
                               ? (uint16_t)(sf.assembly.count - sf.assembly.received)
                               : 0u;

        log_info("  the host's savegame: %u mask(s) sent (%u unsent), %u slice(s) arrived, %u "
                 "placed, %u repeated, %u refused, %u file(s) rejected by their name; written %u "
                 "time(s) with %u fault(s); this side holds %08X of %lu bytes",
                 (unsigned)sf.acks_sent, (unsigned)sf.acks_unsent, (unsigned)sf.chunks_in,
                 (unsigned)sf.assembly.chunks_taken,
                 (unsigned)sf.assembly.chunks_repeated, (unsigned)sf.assembly.chunks_refused,
                 (unsigned)sf.assembly.files_rejected, (unsigned)sf.writes,
                 (unsigned)sf.write_faults, (unsigned)sf.written_id,
                 (unsigned long)sf.written_bytes);
        if (sf.assembly.open && !sf.assembly.complete) {
            log_warning("  the savegame is NOT WHOLE here: %u of %u slice(s) are missing, the "
                        "lowest is %u. Every mask this side sends names them, so a host that is "
                        "listening cannot be unaware of them",
                        (unsigned)missing, (unsigned)sf.assembly.count,
                        (unsigned)mp_savefile_mask_first_missing(sf.assembly.have,
                                                                 sf.assembly.count));
        }
        log_info("  the savegame lane: %u note(s) taken, %u of them neither a slice nor a mask, "
                 "%u lost to a full ring; %u note(s) of the old shape arrived on the reliable "
                 "channel instead",
                 (unsigned)sf.bulk_notes_in, (unsigned)sf.bulk_notes_strange,
                 (unsigned)(sf.client != NULL ? mp_session_bulk_overrun(sf.client) : 0u),
                 (unsigned)sf.stray_reliable_notes);
        return;
    }
    log_info("  the savegame for the clients: %s%s, %u offer(s) with %u refused; %u mask(s) in, "
             "%u for a file this side does not hold, %u torn; %u slice(s) sent of which %u were "
             "repeats, %u skipped while a copy was still out, %u the lane refused, %u tick(s) at "
             "the budget of %u; %u transfer(s) the RECEIVER called complete; %u note(s) of the "
             "old shape arrived on the reliable channel",
             sf.held ? sf.held_name : "none held", sf.held ? "" : " (a fresh level)",
             (unsigned)sf.offers, (unsigned)sf.offers_refused, (unsigned)sf.acks_in,
             (unsigned)sf.acks_for_another_file, (unsigned)sf.acks_torn,
             (unsigned)sf.slices_sent, (unsigned)sf.slices_resent, (unsigned)sf.slices_resting,
             (unsigned)sf.sends_refused, (unsigned)sf.slices_at_the_budget,
             (unsigned)MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK, (unsigned)sf.transfers_done,
             (unsigned)sf.stray_reliable_notes);
    /* The three readings this line is for. Repeats far above nought means the wire is losing
       slices and the rest is too short for this link. A large resting count with few sent means
       the acknowledgements are not arriving, so look at the client's mask counter, not here. Many
       ticks at the budget means the transfer is running as fast as it is allowed to, which is the
       only number that could ever justify raising it. */
    if (sf.ticks_with_a_slice != 0u) {
        /* The rate, measured rather than assumed. The per tick budget was once explained as "four
           slices thirty two times a second"; the tick is the idle pump's and nobody had measured
           it. The span is wall clock, first tick that laid a slice to the last, so a transfer
           that paused between two clients reads slower than it ran, which is the honest error. */
        uint32_t span_ms = sf.last_slice_tick_ms - sf.first_slice_tick_ms;
        uint32_t per_second_ticks  = span_ms != 0u ? sf.ticks_with_a_slice * 1000u / span_ms : 0u;
        uint32_t per_second_slices = span_ms != 0u ? sf.slices_sent * 1000u / span_ms : 0u;

        log_info("  the transfer ticked %u time(s) over %u ms, about %u tick(s) and %u slice(s) a "
                 "second; %u tick(s) reached the total budget of %u over all peers, %u were "
                 "held to the budget of %u a second",
                 (unsigned)sf.ticks_with_a_slice, (unsigned)span_ms, (unsigned)per_second_ticks,
                 (unsigned)per_second_slices, (unsigned)sf.ticks_at_the_total_budget,
                 (unsigned)MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK,
                 (unsigned)sf.ticks_held_by_the_pace,
                 (unsigned)MP_BRIDGE_SAVEFILE_SLICES_PER_SECOND);
    }
    for (peer_index = 0; peer_index < MP_SESSION_MAX_PEERS; ++peer_index) {
        const savefile_cursor_t *cursor = &sf.cursor[peer_index];
        uint16_t                 count  = mp_savefile_chunk_count(sf.held_bytes);

        if (!cursor->active || count == 0u) {
            continue;
        }
        log_warning("  peer %u is STILL WAITING for the savegame: it has acknowledged %u of %u "
                    "slice(s), the lowest it has not is %u",
                    (unsigned)peer_index, (unsigned)cursor->acked_count, (unsigned)count,
                    (unsigned)mp_savefile_mask_first_missing(cursor->acked, count));
    }
}
