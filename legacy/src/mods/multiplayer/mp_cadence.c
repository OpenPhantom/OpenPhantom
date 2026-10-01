/* mp_cadence.c: when the host's world leaves the host, and how it reaches a client's replicas.
 * See the header. */
#include "mp_cadence.h"

#include "mp_enemy_interest_rule.h"
#include "mp_stopwatch.h"
#include "mp_wire.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The host's sends: the edges of the middle bucket around the 31.25 ms of a substep, and the gap
 * past which two sends are a pause rather than the cadence. */
#define SEND_SHORT_US  25000u
#define SEND_LONG_US   38000u
#define SEND_PAUSE_US  1000000u

/* The payloads of one client substep, and the buckets of a gap in host ticks. */
enum { TOOK_NONE, TOOK_ONE, TOOK_TWO, TOOK_MORE, TOOK_BUCKETS };
enum { GAP_ONE, GAP_TWO, GAP_FOUR, GAP_EIGHT, GAP_MORE, GAP_BUCKETS };

/* A heading on the wire is a turn in 65536 steps. */
#define HEADING_STEPS   65536u
#define HEADING_DEGREES (360.0f / (float)HEADING_STEPS)

/* One placement's class for one peer, on a host. */
typedef struct host_trace {
    uint32_t blocks_seen;   /* the view's block count at the last call */
    uint32_t listed;
    uint32_t by_reach[4];   /* indexed by mp_enemy_reach_t */
    uint32_t sent;
} host_trace_t;

typedef struct cadence_state {
    /* A client's substeps by the payloads they took. */
    uint32_t pending;
    bool     payload_seen;
    uint32_t took[TOOK_BUCKETS];
    uint32_t run_none;
    uint32_t run_many;
    uint32_t longest_none;
    uint32_t longest_many;

    /* A host's sends. */
    uint64_t send_began;      /* the counter when the census began, 0 outside a send */
    uint64_t last_send_end;   /* the counter at the end of the last send, 0 before the first */
    uint32_t intervals;
    uint32_t interval_short;
    uint32_t interval_middle;
    uint32_t interval_long;
    uint32_t shortest_us;
    uint32_t longest_us;
    uint32_t sends;
    uint64_t work_total_us;
    uint32_t work_most_us;

    /* A client's records and its flush. */
    uint32_t gaps[GAP_BUCKETS];
    uint8_t  taken[MP_WIRE_KEY_COUNT];       /* records since the last flush, saturating */
    uint8_t  stepped[MP_WIRE_KEY_COUNT];     /* records the next write steps over, saturating */
    bool     pair_open[MP_WIRE_KEY_COUNT];   /* the last write turned the body */
    uint32_t newest_of_many;   /* writes that stepped over a record that fell */
    uint32_t never_reached;    /* the records they stepped over */
    uint32_t held;
    uint32_t held_open;        /* of them, the rotation pair left open through the substep */

    /* The pairs the flush closed in a substep without a write. */
    uint32_t closed_rotation;
    uint32_t closed_position;
    uint32_t closed_open;      /* of the rotation pairs closed, the ones the last write opened */
    uint32_t left_alone;
    uint32_t unclosed;

    /* The second record of a replica. */
    uint32_t windows_many;
    uint32_t late;
    uint32_t fell[MP_CADENCE_FALLS];
    uint32_t longest_behind;
    uint32_t other_life;
    uint32_t second_write;

    /* The traced placement. */
    uint32_t     traced;
    uint32_t     trace_substeps;
    uint32_t     trace_written;
    uint32_t     trace_held;
    uint32_t     trace_many;
    float        trace_step;
    float        trace_turn;
    host_trace_t host[MP_CADENCE_VIEWS];
} cadence_state_t;

static cadence_state_t cadence;

void mp_cadence_set_traced(int32_t placement)
{
    cadence.traced = 0u;
    if (placement == 0) {
        return;
    }
    if (placement < 0 || placement >= (int32_t)MP_WIRE_KEY_COPY_BASE) {
        log_warning("TraceReplicaPlacement %d names no placement (1 to %u), so no replica is "
                    "traced", (int)placement, (unsigned)(MP_WIRE_KEY_COPY_BASE - 1u));
        return;
    }
    cadence.traced = (uint32_t)placement;
}

uint32_t mp_cadence_traced(void)
{
    return cadence.traced;
}

/* ==============================================================================================
 * A client's substeps.
 * ============================================================================================ */

void mp_cadence_payload_taken(void)
{
    if (cadence.pending < 0xFFFFFFFFu) {
        ++cadence.pending;
    }
}

/* A client's substeps before the first of the host's payloads are a client waiting for its host,
 * and not a gap in the stream. */
void mp_cadence_substep_closed(void)
{
    uint32_t taken = cadence.pending;

    cadence.pending = 0u;
    if (taken == 0u && !cadence.payload_seen) {
        return;
    }
    cadence.payload_seen = true;
    ++cadence.took[taken >= TOOK_MORE ? TOOK_MORE : taken];
    cadence.run_none = taken == 0u ? cadence.run_none + 1u : 0u;
    cadence.run_many = taken >= 2u ? cadence.run_many + 1u : 0u;
    if (cadence.run_none > cadence.longest_none) {
        cadence.longest_none = cadence.run_none;
    }
    if (cadence.run_many > cadence.longest_many) {
        cadence.longest_many = cadence.run_many;
    }
}

/* ==============================================================================================
 * A host's sends.
 * ============================================================================================ */

void mp_cadence_send_begins(void)
{
    cadence.send_began = mp_stopwatch_ticks();
}

void mp_cadence_send_ends(void)
{
    uint64_t now      = mp_stopwatch_ticks();
    uint32_t work     = mp_stopwatch_micros(cadence.send_began, now);
    uint32_t interval = mp_stopwatch_micros(cadence.last_send_end, now);

    cadence.send_began    = 0u;
    cadence.last_send_end = now;
    mp_cadence_send_measured(work, interval);
}

void mp_cadence_send_measured(uint32_t work_us, uint32_t interval_us)
{
    ++cadence.sends;
    cadence.work_total_us += work_us;
    if (work_us > cadence.work_most_us) {
        cadence.work_most_us = work_us;
    }
    if (interval_us == 0u || interval_us >= SEND_PAUSE_US) {
        return;
    }
    ++cadence.intervals;
    if (interval_us < SEND_SHORT_US) {
        ++cadence.interval_short;
    } else if (interval_us <= SEND_LONG_US) {
        ++cadence.interval_middle;
    } else {
        ++cadence.interval_long;
    }
    if (cadence.intervals == 1u || interval_us < cadence.shortest_us) {
        cadence.shortest_us = interval_us;
    }
    if (interval_us > cadence.longest_us) {
        cadence.longest_us = interval_us;
    }
}

void mp_cadence_host_trace(size_t view, uint32_t blocks, bool listed, uint8_t reach, bool sent)
{
    host_trace_t *trace;
    bool          built;

    if (cadence.traced == 0u || view >= MP_CADENCE_VIEWS) {
        return;
    }
    trace              = &cadence.host[view];
    built              = blocks != trace->blocks_seen;
    trace->blocks_seen = blocks;
    if (!built || !listed) {
        return;
    }
    ++trace->listed;
    ++trace->by_reach[reach & 3u];
    trace->sent += sent ? 1u : 0u;
}

/* ==============================================================================================
 * A client's records and its flush.
 * ============================================================================================ */

void mp_cadence_records_taken(const bool *named, size_t keys)
{
    size_t key;

    for (key = 0; named != NULL && key < keys && key < MP_WIRE_KEY_COUNT; ++key) {
        if (named[key] && cadence.taken[key] < 0xFFu) {
            ++cadence.taken[key];
        }
    }
}

void mp_cadence_forget_records(void)
{
    memset(cadence.taken, 0, sizeof cadence.taken);
}

void mp_cadence_record_gap(uint32_t gap)
{
    if (gap <= 1u) {
        ++cadence.gaps[GAP_ONE];
    } else if (gap == 2u) {
        ++cadence.gaps[GAP_TWO];
    } else if (gap <= 4u) {
        ++cadence.gaps[GAP_FOUR];
    } else if (gap <= 8u) {
        ++cadence.gaps[GAP_EIGHT];
    } else {
        ++cadence.gaps[GAP_MORE];
    }
}

static uint32_t field(const mp_enemy_record_t *record, size_t index)
{
    return record->value[index] & 0xFFFFu;
}

/* Whether a write leaves the body's rotation pair apart. The write puts the rotation the body had
 * in front of the one this record names, so the pair is open when the record turns the body: its
 * heading, and for a flyer its pitch or roll, differ from the record written before. A first write
 * opens it as well, because the slot's rotation before it is nothing this record said. */
static bool pair_opens(const mp_enemy_record_t *before, const mp_enemy_record_t *now)
{
    if (before == NULL) {
        return true;
    }
    if (field(before, MP_ENEMY_F_HEADING) != field(now, MP_ENEMY_F_HEADING)) {
        return true;
    }
    return (now->value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_FLYER_POSE) != 0u &&
           (field(before, MP_ENEMY_F_PITCH) != field(now, MP_ENEMY_F_PITCH) ||
            field(before, MP_ENEMY_F_ROLL) != field(now, MP_ENEMY_F_ROLL));
}

/* The traced replica's step and turn between two writes, the turn the short way round. */
static void trace_motion(const mp_enemy_record_t *before, const mp_enemy_record_t *now)
{
    float    step = 0.0f;
    uint32_t turn;
    size_t   axis;

    if (before == NULL) {
        return;
    }
    for (axis = 0; axis < 3u; ++axis) {
        float d = mp_enemy_wire_get_position(now->value[MP_ENEMY_F_POS_X + axis]) -
                  mp_enemy_wire_get_position(before->value[MP_ENEMY_F_POS_X + axis]);

        step += d * d;
    }
    step = sqrtf(step);
    turn = (field(now, MP_ENEMY_F_HEADING) - field(before, MP_ENEMY_F_HEADING)) & 0xFFFFu;
    if (turn > HEADING_STEPS / 2u) {
        turn = HEADING_STEPS - turn;
    }
    if (step > cadence.trace_step) {
        cadence.trace_step = step;
    }
    if ((float)turn * HEADING_DEGREES > cadence.trace_turn) {
        cadence.trace_turn = (float)turn * HEADING_DEGREES;
    }
}

static uint8_t saturated(uint32_t value)
{
    return (uint8_t)(value > 0xFFu ? 0xFFu : value);
}

void mp_cadence_records_fell(size_t key, mp_cadence_fall_t why, uint32_t count)
{
    if ((unsigned)why >= (unsigned)MP_CADENCE_FALLS || count == 0u) {
        return;
    }
    cadence.fell[why] += count;
    if (why != MP_CADENCE_FELL_LET_GO && key < MP_WIRE_KEY_COUNT) {
        cadence.stepped[key] = saturated((uint32_t)cadence.stepped[key] + count);
    }
}

/* One window of a replica with two or more records in it, counted at the write or the refusal it
 * ends with, whatever the queue made of it. */
static void count_window(size_t key)
{
    cadence.windows_many += cadence.taken[key] >= 2u ? 1u : 0u;
}

void mp_cadence_flush_written(size_t key, const mp_enemy_record_t *before,
                              const mp_enemy_record_t *now, bool late)
{
    uint32_t taken;

    if (key >= MP_WIRE_KEY_COUNT || now == NULL) {
        return;
    }
    taken = cadence.taken[key];
    count_window(key);
    if (cadence.stepped[key] != 0u) {
        ++cadence.newest_of_many;
        cadence.never_reached += cadence.stepped[key];
        cadence.stepped[key] = 0u;
    }
    cadence.late += late ? 1u : 0u;
    cadence.pair_open[key] = pair_opens(before, now);
    if (key == cadence.traced && cadence.traced != 0u) {
        ++cadence.trace_substeps;
        ++cadence.trace_written;
        cadence.trace_many += taken >= 2u ? 1u : 0u;
        trace_motion(before, now);
    }
}

/* `pair_open` stays what the last write made it: it says whether the pair WOULD stand open, so a
 * substep closed here still counts among the ones the saw tooth would have drawn. */
void mp_cadence_flush_held(size_t key, bool ours, bool rotation_closed, bool position_closed)
{
    if (key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    ++cadence.held;
    count_window(key);
    cadence.held_open += (cadence.pair_open[key] && !rotation_closed) ? 1u : 0u;
    cadence.closed_rotation += rotation_closed ? 1u : 0u;
    cadence.closed_position += position_closed ? 1u : 0u;
    cadence.closed_open += (cadence.pair_open[key] && rotation_closed) ? 1u : 0u;
    cadence.left_alone += ours ? 0u : 1u;
    cadence.unclosed += (ours && !(rotation_closed && position_closed)) ? 1u : 0u;
    if (key == cadence.traced && cadence.traced != 0u) {
        ++cadence.trace_substeps;
        ++cadence.trace_held;
    }
}

void mp_cadence_flush_behind(uint32_t run)
{
    if (run > cadence.longest_behind) {
        cadence.longest_behind = run;
    }
}

void mp_cadence_flush_fault(mp_cadence_fault_t fault)
{
    if (fault == MP_CADENCE_FAULT_OTHER_LIFE) {
        ++cadence.other_life;
    } else if (fault == MP_CADENCE_FAULT_SECOND_WRITE) {
        ++cadence.second_write;
    }
}

void mp_cadence_flush_done(void)
{
    memset(cadence.taken, 0, sizeof cadence.taken);
    memset(cadence.stepped, 0, sizeof cadence.stepped);
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

static void report_host(void)
{
    size_t view;

    log_info("the sends on the wall clock (host): %u interval(s) between two substep ends, %u "
             "under 25 ms, %u from 25 to 38 ms, %u over 38 ms, the shortest %u ms, the longest %u "
             "ms; the census and the encode before the send took %u us on average and %u us at "
             "most",
             (unsigned)cadence.intervals, (unsigned)cadence.interval_short,
             (unsigned)cadence.interval_middle, (unsigned)cadence.interval_long,
             (unsigned)(cadence.shortest_us / 1000u), (unsigned)(cadence.longest_us / 1000u),
             (unsigned)(cadence.sends != 0u ? cadence.work_total_us / cadence.sends : 0u),
             (unsigned)cadence.work_most_us);
    for (view = 0; cadence.traced != 0u && view < MP_CADENCE_VIEWS; ++view) {
        const host_trace_t *trace = &cadence.host[view];

        log_info("the replica of placement %u to one peer (host): peer %u, %u substep(s) listed, "
                 "%u near, %u middle, %u far, %u with no position to measure against, %u with a "
                 "record of it sent",
                 (unsigned)cadence.traced, (unsigned)view, (unsigned)trace->listed,
                 (unsigned)trace->by_reach[MP_ENEMY_REACH_NEAR],
                 (unsigned)trace->by_reach[MP_ENEMY_REACH_MIDDLE],
                 (unsigned)trace->by_reach[MP_ENEMY_REACH_FAR],
                 (unsigned)trace->by_reach[MP_ENEMY_REACH_NONE], (unsigned)trace->sent);
    }
}

static void report_client(void)
{
    log_info("the host's payloads per substep (client): %u substep(s) took none, %u one, %u two, "
             "%u three or more; the longest run with none %u, the longest run with two or more %u",
             (unsigned)cadence.took[TOOK_NONE], (unsigned)cadence.took[TOOK_ONE],
             (unsigned)cadence.took[TOOK_TWO], (unsigned)cadence.took[TOOK_MORE],
             (unsigned)cadence.longest_none, (unsigned)cadence.longest_many);
    log_info("enemies, the records of one replica (client): %u pair(s) one host tick apart, %u "
             "two, %u three or four, %u five to eight, %u more",
             (unsigned)cadence.gaps[GAP_ONE], (unsigned)cadence.gaps[GAP_TWO],
             (unsigned)cadence.gaps[GAP_FOUR], (unsigned)cadence.gaps[GAP_EIGHT],
             (unsigned)cadence.gaps[GAP_MORE]);
    log_info("enemies, the flush (client): %u write(s) of the newest of two or more records taken "
             "for one replica since the last substep, %u record(s) that never reached a body that "
             "way; %u substep(s) a parked replica went without a write, %u of them with its "
             "rotation pair open",
             (unsigned)cadence.newest_of_many, (unsigned)cadence.never_reached,
             (unsigned)cadence.held, (unsigned)cadence.held_open);
    log_info("enemies, the pairs the flush closed (client): %u substep(s) a parked replica had no "
             "write, %u rotation pair(s) and %u position pair(s) closed, %u of the rotation pairs "
             "open before; %u left alone because the actor carries this player's body or has "
             "none, %u close(s) that would not write",
             (unsigned)cadence.held, (unsigned)cadence.closed_rotation,
             (unsigned)cadence.closed_position, (unsigned)cadence.closed_open,
             (unsigned)cadence.left_alone, (unsigned)cadence.unclosed);
    log_info("enemies, the second record (client): %u window(s) in which one replica took two or "
             "more records, %u record(s) written one substep late, %u dropped for a third, %u "
             "dropped at a new life, %u dropped at a let go or a removal; the longest run one "
             "record behind %u substep(s)",
             (unsigned)cadence.windows_many, (unsigned)cadence.late,
             (unsigned)cadence.fell[MP_CADENCE_FELL_THIRD],
             (unsigned)cadence.fell[MP_CADENCE_FELL_NEW_LIFE],
             (unsigned)cadence.fell[MP_CADENCE_FELL_LET_GO], (unsigned)cadence.longest_behind);
    log_info("enemies, the second record, must be 0: %u write(s) of a record for a life other than "
             "the table's, %u substep(s) with two writes of one replica",
             (unsigned)cadence.other_life, (unsigned)cadence.second_write);
    if (cadence.traced != 0u) {
        log_info("the replica of placement %u (client): %u substep(s), %u written, %u held, %u "
                 "with two or more records, the largest step %.2f u, the largest turn %.1f "
                 "degree(s)",
                 (unsigned)cadence.traced, (unsigned)cadence.trace_substeps,
                 (unsigned)cadence.trace_written, (unsigned)cadence.trace_held,
                 (unsigned)cadence.trace_many, (double)cadence.trace_step,
                 (double)cadence.trace_turn);
    }
}

void mp_cadence_report(bool host)
{
    if (host) {
        report_host();
    } else {
        report_client();
    }
}
