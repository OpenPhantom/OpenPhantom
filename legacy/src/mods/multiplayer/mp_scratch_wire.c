/* mp_scratch_wire.c: when the world scratchpad goes out, and what happens to one that arrives. */
#include "mp_scratch_wire.h"

#include "mp_channel.h"
#include "mp_scratch.h"
#include "mp_scratch_bind.h"
#include "mp_wire.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The budget one bank message may spend. The channel's own maximum less this module's header;
 * whatever is left over resumes on the next period, which is what the cursor is for. */
#define BANK_BUDGET (MP_CHANNEL_MESSAGE_BYTES - MP_SCRATCH_WIRE_HEADER)

typedef struct scratch_wire_state {
    bool                 host;
    uint32_t             generation;         /* transitions seen on this machine, as a SENDER */
    uint32_t             newest_generation;  /* the highest a sender has named, as a RECEIVER */
    mp_scratch_cursor_t  cursor;
    uint8_t              mirror[MP_SCRATCH_BANK_BYTES];
    int32_t              ai_flag[MP_SCRATCH_AI_SLOTS];
    int32_t              ai_previous[MP_SCRATCH_AI_SLOTS];
    float                ai_expiry[MP_SCRATCH_AI_SLOTS];
    bool                 ai_known;
    uint32_t             bank_sent;
    uint32_t             bank_bytes_sent;
    uint32_t             bank_applied;
    uint32_t             ai_sent;
    uint32_t             ai_applied;
    uint32_t             refused_generation;
    uint32_t             refused_role;
    uint32_t             refused_malformed;
    bool                 generation_logged;
    uint32_t             resets;
    uint32_t             empty_sweeps;
    uint32_t             bank_unreadable;
    uint32_t             send_refused;

    /* A sweep of the whole bank for a player who has just entered the host's world, from the
     * arrival to the message that reaches the end of the bank. */
    bool                 arrival_sweep;
    uint32_t             arrival_bytes;      /* bank bytes the sweep has to carry */
    uint32_t             arrival_messages;   /* the messages it took so far */
    uint32_t             arrivals;           /* arrivals over the run */
} scratch_wire_state_t;

static scratch_wire_state_t wire;

/* The counters survive this, for the same reason the enemy sync's do: a reset runs on a level
 * change and the report is written at a level end, so clearing them here made every field report
 * read as though nothing had ever crossed the wire. */
void mp_scratch_wire_reset(void)
{
    wire.generation      = 0u;
    wire.newest_generation = 0u;
    wire.cursor.at       = 0u;
    wire.cursor.pending  = 0u;
    wire.ai_known        = false;
    memset(wire.mirror, 0, sizeof wire.mirror);
    memset(wire.ai_flag, 0, sizeof wire.ai_flag);
    memset(wire.ai_previous, 0, sizeof wire.ai_previous);
    memset(wire.ai_expiry, 0, sizeof wire.ai_expiry);
    wire.arrival_sweep = false;
    ++wire.resets;
}

void mp_scratch_wire_set_host(bool host)
{
    wire.host = host;
}

/* ==============================================================================================
 * The sending side.
 * ============================================================================================ */

/* A transition puts the cursor back to the beginning and forgets the mirror, so the sweep that
 * follows describes the whole bank rather than the difference against a bank that is gone. The
 * generation moves with it, which is what lets a receiver refuse anything still in flight. */
static void take_transition(void)
{
    if (!mp_scratch_bind_take_resync()) {
        return;
    }
    ++wire.generation;
    wire.cursor.at      = 0;
    wire.cursor.pending = 0;
    wire.ai_known       = false;
    wire.arrival_sweep  = false;   /* the bank an arrival was owed is gone with the level */
    memset(wire.mirror, 0, sizeof wire.mirror);
}

/* A player has entered the host's world and holds whatever its own level begin and scripts left in
 * its bank. The encoder sends only the bytes that differ from the mirror, so a mirror emptied as a
 * transition empties it would never carry the host's zeros, and a byte the newcomer holds at 1
 * where the host holds 0 would stay 1. The mirror is set to the complement of the live bank
 * instead, which makes every byte differ once, zeros included, and the sweep that follows carries
 * the whole bank. The blackboard goes whole as well, and the generation moves as a transition moves
 * it, so a delta encoded before this is refused by everybody rather than applied over the sweep.
 *
 * What goes out is absolute, so the players who were already here take the same values again. */
void mp_scratch_wire_note_arrival(void)
{
    uint8_t  live[MP_SCRATCH_BANK_BYTES];
    uint32_t bytes = 0u;
    size_t   i;

    if (!wire.host || !mp_scratch_bind_read_bank(live)) {
        return;
    }
    for (i = 0; i < MP_SCRATCH_BANK_BYTES; ++i) {
        wire.mirror[i] = (uint8_t)~live[i];
        bytes += mp_scratch_is_per_hero((uint32_t)i) ? 0u : 1u;
    }
    ++wire.generation;
    wire.cursor.at        = 0;
    wire.cursor.pending   = 0;
    wire.ai_known         = false;
    wire.arrival_sweep    = true;
    wire.arrival_bytes    = bytes;
    wire.arrival_messages = 0u;
    ++wire.arrivals;
}

/* The sweep an arrival began has reached the end of the bank: said once, with what it took. */
static void close_arrival_sweep(void)
{
    wire.arrival_sweep = false;
    log_info("the campaign bank goes out whole for an arrival: %u byte(s) in %u message(s), and "
             "the blackboard, under generation %u", (unsigned)wire.arrival_bytes,
             (unsigned)wire.arrival_messages, (unsigned)wire.generation);
}

static void send_bank(mp_scratch_wire_send_fn_t send)
{
    uint8_t             live[MP_SCRATCH_BANK_BYTES];
    uint8_t             message[MP_CHANNEL_MESSAGE_BYTES];
    mp_wire_writer_t    writer;
    mp_scratch_cursor_t next;
    size_t              bytes = 0;

    if (!mp_scratch_bind_read_bank(live)) {
        ++wire.bank_unreadable;   /* three silent returns once made this look like idleness */
        return;
    }
    mp_wire_writer_init(&writer, message, sizeof message);
    (void)mp_wire_put_u8(&writer, (uint8_t)MP_SCRATCH_TAG_BANK);
    (void)mp_wire_put_u32(&writer, wire.generation);

    if (!mp_scratch_encode_bank(wire.mirror, live, &wire.cursor,
                                message + MP_SCRATCH_WIRE_HEADER, BANK_BUDGET, &bytes, &next)) {
        return;
    }

    /* The commit takes the cursor the ENCODE produced as its second cursor, because that is
     * what says how far the range it covered reaches. Both arguments used to be the old cursor,
     * which made the two ends of the range equal: the copy loop ran zero times, the mirror never
     * advanced a byte, and the cursor stayed at zero for the life of the session. Every message
     * would then have been a full re-send rather than a difference, and `pending` would have been
     * a number about nothing. */
    if (bytes <= 2u && next.pending == 0u) {
        mp_scratch_commit_bank(wire.mirror, live, &wire.cursor, &next);
        wire.cursor = next;
        ++wire.empty_sweeps;
        if (wire.arrival_sweep) {
            close_arrival_sweep();
        }
        return;
    }
    if (!send(message, MP_SCRATCH_WIRE_HEADER + bytes)) {
        ++wire.send_refused;
        return;   /* the channel is full; the cursor has not moved, so nothing was lost */
    }
    mp_scratch_commit_bank(wire.mirror, live, &wire.cursor, &next);
    wire.cursor = next;
    ++wire.bank_sent;
    wire.bank_bytes_sent += (uint32_t)bytes;
    if (wire.arrival_sweep) {
        ++wire.arrival_messages;
        if (wire.cursor.at >= MP_SCRATCH_BANK_BYTES) {
            close_arrival_sweep();
        }
    }
}

/* The blackboard is 48 bytes and travels whole or not at all, because a delta over twelve dwords
 * would cost more in headers than it saved. It is compared against what was last sent rather than
 * against a clock: a running timer's ABSOLUTE expiry does not change while it runs, so an
 * unchanged blackboard really is unchanged and costs no packet. */
static void send_ai(mp_scratch_wire_send_fn_t send)
{
    int32_t          flag[MP_SCRATCH_AI_SLOTS];
    int32_t          previous[MP_SCRATCH_AI_SLOTS];
    float            expiry[MP_SCRATCH_AI_SLOTS];
    uint8_t          message[MP_SCRATCH_WIRE_AI_BYTES];
    mp_wire_writer_t writer;
    float            now = 0.0f;
    size_t           bytes;

    if (!mp_scratch_bind_read_ai(flag, previous, expiry)) {
        return;
    }
    if (!mp_scratch_bind_world_now(&now)) {
        return;   /* no level, so no clock to measure the remaining times against */
    }
    if (wire.ai_known &&
        memcmp(flag, wire.ai_flag, sizeof flag) == 0 &&
        memcmp(previous, wire.ai_previous, sizeof previous) == 0 &&
        memcmp(expiry, wire.ai_expiry, sizeof expiry) == 0) {
        return;
    }

    mp_wire_writer_init(&writer, message, sizeof message);
    (void)mp_wire_put_u8(&writer, (uint8_t)MP_SCRATCH_TAG_AI);
    (void)mp_wire_put_u32(&writer, wire.generation);
    bytes = mp_scratch_encode_ai(flag, previous, expiry, now,
                                 message + MP_SCRATCH_WIRE_HEADER,
                                 sizeof message - MP_SCRATCH_WIRE_HEADER);
    if (bytes == 0u || !send(message, MP_SCRATCH_WIRE_HEADER + bytes)) {
        return;
    }
    memcpy(wire.ai_flag, flag, sizeof flag);
    memcpy(wire.ai_previous, previous, sizeof previous);
    memcpy(wire.ai_expiry, expiry, sizeof expiry);
    wire.ai_known = true;
    ++wire.ai_sent;
}

void mp_scratch_wire_tick(uint32_t substep, mp_scratch_wire_send_fn_t send)
{
    if (!mp_scratch_bind_installed() || send == NULL) {
        return;
    }
    take_transition();
    if (!wire.host) {
        return;
    }

    /* Off the period, unless the cursor is mid sweep: a resync should not take two seconds to
     * cross 1250 bytes when the channel could carry it in a few substeps. */
    if ((substep % MP_SCRATCH_WIRE_PERIOD) != 0u && wire.cursor.pending == 0u &&
        wire.cursor.at >= MP_SCRATCH_BANK_BYTES) {
        return;
    }
    if (wire.cursor.at >= MP_SCRATCH_BANK_BYTES) {
        wire.cursor.at = 0;   /* the sweep wraps; steady play then costs an empty compare */
    }
    send_bank(send);
    send_ai(send);
}

/* ==============================================================================================
 * The receiving side.
 * ============================================================================================ */

/* The generation is a SEQUENCE, not a number the two machines are expected to share.
 *
 * It used to be compared for equality against this machine's own transition count, and the first
 * field run showed exactly why that cannot work: the host reported generation 1 and the client 0,
 * because each counts the transitions IT has seen and they do not start together. A client that
 * joins mid game has seen fewer, permanently, so every bank message would have been refused for
 * the life of the session and the campaign would silently never have travelled.
 *
 * What the guard is actually for is narrow: a delta encoded before the host's transition and
 * delivered after it. The host resets its own mirror on a transition and sweeps the whole bank, so
 * everything sent afterwards is absolute; the only thing to refuse is a message from BEFORE the
 * newest generation this machine has already seen. */
static bool generation_agrees(uint32_t theirs)
{
    if (theirs >= wire.newest_generation) {
        wire.newest_generation = theirs;
        return true;
    }
    ++wire.refused_generation;
    if (!wire.generation_logged) {
        wire.generation_logged = true;
        log_warning("a campaign message describes transition %u and this side has already seen %u, "
                    "so it was refused: it was encoded before a level change and arrived after "
                    "one, and applying it would write progress into a bank that is gone",
                    (unsigned)theirs, (unsigned)wire.newest_generation);
    }
    return false;
}

static bool take_bank(const uint8_t *note, size_t bytes)
{
    uint8_t want[MP_SCRATCH_BANK_BYTES];
    size_t  written = 0;
    size_t  changed = 0;

    if (!mp_scratch_bind_read_bank(want)) {
        ++wire.refused_malformed;
        return true;
    }
    if (!mp_scratch_decode_bank(want, note + MP_SCRATCH_WIRE_HEADER,
                                bytes - MP_SCRATCH_WIRE_HEADER, &changed)) {
        ++wire.refused_malformed;
        return true;
    }
    if (!mp_scratch_bind_write_bank(want, &written)) {
        ++wire.refused_malformed;
        return true;
    }
    ++wire.bank_applied;
    (void)written;
    return true;
}

static bool take_ai(const uint8_t *note, size_t bytes)
{
    mp_scratch_ai_t ai;

    if (!mp_scratch_decode_ai(note + MP_SCRATCH_WIRE_HEADER, bytes - MP_SCRATCH_WIRE_HEADER,
                              &ai) ||
        !mp_scratch_bind_write_ai(&ai)) {
        ++wire.refused_malformed;
        return true;
    }
    ++wire.ai_applied;
    return true;
}

bool mp_scratch_wire_take_message(const uint8_t *note, size_t bytes)
{
    mp_wire_reader_t reader;
    uint8_t          tag        = 0;
    uint32_t         generation = 0;

    if (note == NULL || bytes <= MP_SCRATCH_WIRE_HEADER) {
        return false;
    }
    if (note[0] != MP_SCRATCH_TAG_BANK && note[0] != MP_SCRATCH_TAG_AI) {
        return false;
    }

    /* Taken from here on, whatever happens to it. It is ours by tag, and handing it back would
     * only have the event decoder try to read a campaign delta as a body action. */
    mp_wire_reader_init(&reader, note, bytes);
    (void)mp_wire_get_u8(&reader, &tag);
    (void)mp_wire_get_u32(&reader, &generation);

    /* The level belongs to the host, so a host has nothing to learn here. Refusing rather than
     * applying is what stops a mistaken mode from letting one client rewrite the campaign of
     * everyone in the session. */
    if (wire.host) {
        ++wire.refused_role;
        return true;
    }
    if (!mp_scratch_bind_installed() || !generation_agrees(generation)) {
        return true;
    }
    return tag == MP_SCRATCH_TAG_BANK ? take_bank(note, bytes) : take_ai(note, bytes);
}

void mp_scratch_wire_report(void)
{
    uint32_t transitions = 0;
    uint32_t bank_bytes  = 0;
    uint32_t ai_writes   = 0;

    mp_scratch_bind_counters(&transitions, &bank_bytes, &ai_writes, NULL);
    log_info("world scratchpad: %s, generation %u, %u transitions, %u reset(s) | sent %u bank "
             "messages of %u "
             "bytes and %u blackboards | applied %u banks (%u bytes into the engine) and %u "
             "blackboards | refused %u on generation, %u on role, %u malformed | %u sweep(s) "
             "found nothing, %u unreadable, %u refused by a full channel | %u arrival(s) "
             "sent the whole bank",
             wire.host ? "describing" : "applying", (unsigned)wire.generation,
             (unsigned)transitions, (unsigned)wire.resets, (unsigned)wire.bank_sent,
             (unsigned)wire.bank_bytes_sent,
             (unsigned)wire.ai_sent, (unsigned)wire.bank_applied, (unsigned)bank_bytes,
             (unsigned)wire.ai_applied, (unsigned)wire.refused_generation,
             (unsigned)wire.refused_role, (unsigned)wire.refused_malformed,
             (unsigned)wire.empty_sweeps, (unsigned)wire.bank_unreadable,
             (unsigned)wire.send_refused, (unsigned)wire.arrivals);
}
