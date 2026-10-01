/* mp_cadence.c: when the host's world leaves the host and how it reaches a client's replicas. */
#include "unittest.h"

#include "mp_cadence.h"

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_queue_rule.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ==============================================================================================
 * The log, kept. The counters speak only through the report, so the report is what is read.
 * ============================================================================================ */

#define LINES_KEPT 64u
#define LINE_BYTES 1100u

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = kept[kept_count % LINES_KEPT];

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

/* The newest kept line that begins with `head`, or NULL. */
static const char *line_of(const char *head)
{
    size_t count = kept_count < LINES_KEPT ? kept_count : LINES_KEPT;
    size_t i;

    for (i = count; i-- > 0;) {
        if (strncmp(kept[i], head, strlen(head)) == 0) {
            return kept[i];
        }
    }
    return NULL;
}

static bool line_says(const char *head, const char *text)
{
    const char *line = line_of(head);

    return line != NULL && strstr(line, text) != NULL;
}

static void report(bool host)
{
    kept_count = 0;
    mp_cadence_report(host);
}

/* A record at a position, facing a heading in degrees. */
static mp_enemy_record_t record_at(float x, float y, float z, float degrees)
{
    mp_enemy_record_t record;

    memset(&record, 0, sizeof record);
    (void)mp_enemy_wire_put_position(x, &record.value[MP_ENEMY_F_POS_X]);
    (void)mp_enemy_wire_put_position(y, &record.value[MP_ENEMY_F_POS_Y]);
    (void)mp_enemy_wire_put_position(z, &record.value[MP_ENEMY_F_POS_Z]);
    record.value[MP_ENEMY_F_HEADING] = (uint32_t)(int32_t)(degrees * 182.044444f) & 0xFFFFu;
    return record;
}

/* ============================================================================================ */

/* Every line prints before anything was counted, because a nought is what most of them owe, and
 * a line that is absent reads the same as a feature that never ran. */
static void test_every_line_prints_at_nought(void)
{
    ut_section("every line at nought");
    report(false);
    ut_check(line_says("the host's payloads per substep (client): ",
                       "0 substep(s) took none, 0 one, 0 two, 0 three or more; the longest run "
                       "with none 0, the longest run with two or more 0"),
             "the client's payloads per substep print at nought");
    ut_check(line_says("enemies, the records of one replica (client): ",
                       "0 pair(s) one host tick apart, 0 two, 0 three or four, 0 five to eight, "
                       "0 more"),
             "and so do the gaps between the records of one replica");
    ut_check(line_says("enemies, the flush (client): ", "0 write(s) of the newest of two or more"),
             "and the flush");
    ut_check(line_of("the replica of placement") == NULL,
             "with no placement traced there is no trace line");
    report(true);
    ut_check(line_says("the sends on the wall clock (host): ",
                       "0 interval(s) between two substep ends, 0 under 25 ms"),
             "the host's sends print at nought as well");
    ut_check(line_of("the host's payloads per substep") == NULL,
             "and a host prints no client's line");
}

/* The substeps of a client, each with the host's payloads it took. The first ones, before any
 * payload has arrived, are a client waiting for its host and are not counted. */
static void test_the_payloads_per_substep(void)
{
    static const unsigned TAKEN[] = { 0u, 0u, 1u, 1u, 0u, 2u, 0u, 0u, 3u, 2u, 2u, 1u };
    size_t                i;
    unsigned              n;

    ut_section("the host's payloads per client substep");
    for (i = 0; i < sizeof TAKEN / sizeof TAKEN[0]; ++i) {
        for (n = 0; n < TAKEN[i]; ++n) {
            mp_cadence_payload_taken();
        }
        mp_cadence_substep_closed();
    }
    report(false);
    ut_check(line_says("the host's payloads per substep (client): ",
                       "3 substep(s) took none, 3 one, 3 two, 1 three or more; the longest run "
                       "with none 2, the longest run with two or more 3"),
             "each substep in its bucket, from the first payload on, and the longest runs of "
             "none and of two or more");
}

/* The host's sends on the wall clock. A gap of a second or more is a pause, a load or a join and
 * not the cadence, so it starts the chain over rather than going in as an interval. */
static void test_the_sends_on_the_wall_clock(void)
{
    ut_section("the host's sends");
    mp_cadence_send_measured(100u, 0u);          /* the first send has no interval */
    mp_cadence_send_measured(300u, 31250u);
    mp_cadence_send_measured(200u, 20000u);
    mp_cadence_send_measured(1000u, 40000u);
    mp_cadence_send_measured(400u, 2000000u);    /* a pause of two seconds */
    mp_cadence_send_measured(200u, 38000u);
    report(true);
    ut_check(line_says("the sends on the wall clock (host): ",
                       "4 interval(s) between two substep ends, 1 under 25 ms, 2 from 25 to 38 ms, "
                       "1 over 38 ms, the shortest 20 ms, the longest 40 ms"),
             "each interval in its bucket, the pause left out, 38 ms itself still in the middle");
    ut_check(line_says("the sends on the wall clock (host): ",
                       "the census and the encode before the send took 366 us on average and "
                       "1000 us at most"),
             "and the census and the encode of every send, the one after the pause included");
}

static void test_the_records_of_one_replica(void)
{
    static const uint32_t GAPS[] = { 1u, 1u, 2u, 3u, 4u, 5u, 8u, 9u, 300u, 0u };
    size_t                i;

    ut_section("the records of one replica");
    for (i = 0; i < sizeof GAPS / sizeof GAPS[0]; ++i) {
        mp_cadence_record_gap(GAPS[i]);
    }
    report(false);
    ut_check(line_says("enemies, the records of one replica (client): ",
                       "3 pair(s) one host tick apart, 1 two, 2 three or four, 2 five to eight, "
                       "2 more"),
             "each gap in its bucket, a gap of nought with the one tick apart");
}

/* The number written right in front of `phrase` in the newest line that begins with `head`, or
 * -1 when there is none. */
static long number_before(const char *head, const char *phrase)
{
    const char *line = line_of(head);
    const char *at;

    if (line == NULL || (at = strstr(line, phrase)) == NULL || at == line) {
        return -1;
    }
    --at;
    while (at > line && *at == ' ') {
        --at;
    }
    while (at > line && at[-1] >= '0' && at[-1] <= '9') {
        --at;
    }
    return (*at >= '0' && *at <= '9') ? strtol(at, NULL, 10) : -1;
}

/* The flush writes a replica once a substep, the older of two records first. A write is the
 * newest of two or more only when it steps over records that fell, to a third or to a new life;
 * the windows with two or more are counted whatever became of them. A parked replica with no
 * record in a substep has both pairs closed at the head of the flush, and the flush line counts
 * it among the ones with the rotation pair open only when the close did not happen. */
static void test_the_flush(void)
{
    mp_enemy_record_t first  = record_at(10.0f, 10.0f, 0.0f, 90.0f);
    mp_enemy_record_t turned = record_at(10.0f, 10.0f, 0.0f, 100.0f);
    mp_enemy_record_t moved  = record_at(12.0f, 10.0f, 0.0f, 100.0f);
    bool              named[MP_WIRE_KEY_COUNT];

    ut_section("the flush");
    memset(named, 0, sizeof named);
    named[7]                          = true;
    named[9]                          = true;
    named[11]                         = true;
    named[MP_WIRE_KEY_COPY_BASE + 2u] = true;
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    named[9] = false;
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    mp_cadence_records_fell(7u, MP_CADENCE_FELL_THIRD, 1u);    /* a third came for 7 */
    mp_cadence_records_fell(MP_WIRE_KEY_COPY_BASE + 2u, MP_CADENCE_FELL_NEW_LIFE, 2u);
    mp_cadence_records_fell(11u, MP_CADENCE_FELL_LET_GO, 1u);   /* no write steps over this */

    mp_cadence_flush_written(7u, &first, &turned, false);    /* steps over the one that fell */
    mp_cadence_flush_written(9u, &first, &first, true);      /* nothing turned, one substep late */
    mp_cadence_flush_held(11u, true, true, true);            /* refused, its pairs closed */
    mp_cadence_flush_written(MP_WIRE_KEY_COPY_BASE + 2u, NULL, &moved, false);
    mp_cadence_flush_behind(2u);
    mp_cadence_flush_behind(1u);
    mp_cadence_flush_done();

    mp_cadence_flush_held(7u, true, false, false);    /* its last write turned it, and not closed */
    mp_cadence_flush_held(9u, true, true, true);      /* its last write did not turn it */
    mp_cadence_flush_held(12u, false, false, false);  /* carries this player: left alone */
    memset(named, 0, sizeof named);
    named[7] = true;
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    mp_cadence_flush_written(7u, &turned, &moved, false);    /* one record: no window, no step */
    mp_cadence_flush_done();

    report(false);
    ut_check(line_says("enemies, the flush (client): ",
                       "2 write(s) of the newest of two or more records taken for one replica "
                       "since the last substep, 3 record(s) that never reached a body that way"),
             "a write is the newest of two or more when it steps over records that fell: the "
             "one lost to the third and the two to the new life, and not the one a let go took");
    ut_check(line_says("enemies, the flush (client): ",
                       "4 substep(s) a parked replica went without a write, 1 of them with its "
                       "rotation pair open"),
             "the refused write and the three held replicas went without one, and only the one "
             "whose pair was turned and not closed held it open");
    ut_check(line_says("enemies, the pairs the flush closed (client): ",
                       "4 substep(s) a parked replica had no write, 2 rotation pair(s) and 2 "
                       "position pair(s) closed, 0 of the rotation pairs open before; 1 left alone "
                       "because the actor carries this player's body or has none, 1 close(s) that "
                       "would not write"),
             "the pairs line counts the same substeps, what the closes did, and the body that "
             "was not this side's");
    ut_check(line_says("enemies, the second record (client): ",
                       "3 window(s) in which one replica took two or more records, 1 record(s) "
                       "written one substep late, 1 dropped for a third, 2 dropped at a new life, "
                       "1 dropped at a let go or a removal; the longest run one record behind 2 "
                       "substep(s)"),
             "the windows with two records whatever became of them, the late write, what fell "
             "by why, and the longest run behind");
    ut_check(line_says("enemies, the second record, must be 0: ",
                       "0 write(s) of a record for a life other than the table's, 0 substep(s) "
                       "with two writes of one replica"),
             "and the line that must stay at nought prints at nought");
}

/* A pair the head of the flush closed is not open, however the last write turned it; the pairs
 * line keeps what would have been open among the pairs it closed, so a run still shows how often
 * the engine would have drawn a turn again. */
static void test_a_closed_pair_is_not_open(void)
{
    mp_enemy_record_t first  = record_at(10.0f, 10.0f, 0.0f, 90.0f);
    mp_enemy_record_t turned = record_at(10.0f, 10.0f, 0.0f, 120.0f);
    long              open_before;
    long              held_open;

    ut_section("a closed pair is not open");
    report(false);
    open_before = number_before("enemies, the pairs the flush closed (client): ",
                                "of the rotation pairs open before");
    held_open   = number_before("enemies, the flush (client): ",
                                "of them with its rotation pair open");
    mp_cadence_flush_written(20u, &first, &turned, false);
    mp_cadence_flush_done();
    mp_cadence_flush_held(20u, true, true, true);
    mp_cadence_flush_done();
    mp_cadence_flush_held(20u, true, true, true);
    mp_cadence_flush_done();
    report(false);
    ut_checkf(number_before("enemies, the flush (client): ",
                            "of them with its rotation pair open") == held_open,
              "two substeps after a turn, both closed: none left open (%ld)",
              number_before("enemies, the flush (client): ",
                            "of them with its rotation pair open") - held_open);
    ut_checkf(number_before("enemies, the pairs the flush closed (client): ",
                            "of the rotation pairs open before") == open_before + 2,
              "and both counted among the pairs a turn had opened (%ld)",
              number_before("enemies, the pairs the flush closed (client): ",
                            "of the rotation pairs open before") - open_before);
    mp_cadence_flush_held(20u, true, false, true);
    mp_cadence_flush_done();
    report(false);
    ut_check(number_before("enemies, the flush (client): ",
                           "of them with its rotation pair open") ==
                 held_open + 1,
             "a rotation pair the close could not write stays open, and is counted so");

    mp_cadence_flush_fault(MP_CADENCE_FAULT_OTHER_LIFE);
    mp_cadence_flush_fault(MP_CADENCE_FAULT_SECOND_WRITE);
    mp_cadence_flush_fault(MP_CADENCE_FAULT_SECOND_WRITE);
    report(false);
    ut_check(line_says("enemies, the second record, must be 0: ",
                       "1 write(s) of a record for a life other than the table's, 2 substep(s) "
                       "with two writes of one replica"),
             "a fault the flush should never make is counted where it can be seen");
}

/* One key of a client's table, as the flush and the queue drive the cadence. */
typedef struct table_row {
    mp_enemy_queue_t  queue;
    mp_enemy_record_t mirror;
    mp_enemy_record_t written;
    bool              known;
    bool              written_known;
    bool              dirty;
} table_row_t;

static void row_takes(table_row_t *row, size_t key, float x)
{
    bool              named[MP_WIRE_KEY_COUNT];
    uint32_t          fell = 0;
    mp_enemy_queue_take_t take;

    memset(named, 0, sizeof named);
    named[key] = true;
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    take = mp_enemy_queue_take(&row->queue, &row->mirror, row->dirty && row->known, true, true,
                               &fell);
    mp_cadence_records_fell(key, take == MP_ENEMY_QUEUE_THIRD ? MP_CADENCE_FELL_THIRD
                                                              : MP_CADENCE_FELL_LET_GO, fell);
    row->mirror = record_at(x, 0.0f, 0.0f, 0.0f);
    row->known  = true;
    row->dirty  = true;
}

static void row_flushes(table_row_t *row, size_t key)
{
    const mp_enemy_record_t *record;
    bool                     late = false;

    if (row->dirty) {
        record = mp_enemy_queue_next(&row->queue, &row->mirror, &late);
        mp_cadence_flush_written(key, row->written_known ? &row->written : NULL, record, late);
        row->written       = *record;
        row->written_known = true;
        row->dirty         = mp_enemy_queue_handled(&row->queue);
        mp_cadence_flush_behind(mp_enemy_queue_flushed(&row->queue, row->dirty));
    }
    mp_cadence_flush_done();
}

/* A record for every host tick, R1 to R6, and the client's substeps taking one, two, one, two and
 * none of them: the queue writes R1, R2, R3, R5, R6, R4 falls to the third, R3 and R6 come one
 * substep late, and the line says so. */
static void test_the_two_newest_on_the_line(void)
{
    static const char *const SECOND = "enemies, the second record (client): ";
    static const char *const FLUSH  = "enemies, the flush (client): ";
    table_row_t              row;
    long                     windows;
    long                     late;
    long                     third;
    long                     newest;

    ut_section("the two newest records, as the lines tell them");
    memset(&row, 0, sizeof row);
    report(false);
    windows = number_before(SECOND, "window(s) in which");
    late    = number_before(SECOND, "record(s) written one substep late");
    third   = number_before(SECOND, "dropped for a third");
    newest  = number_before(FLUSH, "write(s) of the newest");

    row_takes(&row, 30u, 1.0f);
    row_flushes(&row, 30u);
    row_takes(&row, 30u, 2.0f);
    row_takes(&row, 30u, 3.0f);
    row_flushes(&row, 30u);
    row_takes(&row, 30u, 4.0f);
    row_flushes(&row, 30u);
    row_takes(&row, 30u, 5.0f);
    row_takes(&row, 30u, 6.0f);
    row_flushes(&row, 30u);
    row_flushes(&row, 30u);
    report(false);
    ut_checkf(number_before(SECOND, "window(s) in which") == windows + 2,
              "two windows took two records (%ld)",
              number_before(SECOND, "window(s) in which") - windows);
    ut_checkf(number_before(SECOND, "record(s) written one substep late") == late + 2,
              "R3 and R6 were written one substep late (%ld)",
              number_before(SECOND, "record(s) written one substep late") - late);
    ut_checkf(number_before(SECOND, "dropped for a third") == third + 1,
              "R4 fell to the third (%ld)", number_before(SECOND, "dropped for a third") - third);
    ut_checkf(number_before(FLUSH, "write(s) of the newest") == newest + 1,
              "and the one write that stepped over it, R5, is the newest of two or more (%ld)",
              number_before(FLUSH, "write(s) of the newest") - newest);
    ut_check(line_says(SECOND, "the longest run one record behind 3 substep(s)"),
             "the longest run behind was three flushes");
    ut_check(!row.dirty && !row.queue.held, "and a substep with none caught the delay up");
}

/* One replica followed through its substeps: Qui-Gon, placement 14 in FEDSHIP, by default. */
static void test_the_trace_of_one_replica(void)
{
    mp_enemy_record_t start = record_at(100.0f, 100.0f, 0.0f, 350.0f);
    mp_enemy_record_t next  = record_at(103.0f, 104.0f, 0.0f, 10.0f);
    mp_enemy_record_t last  = record_at(103.5f, 104.0f, 0.0f, 15.0f);
    bool              named[MP_WIRE_KEY_COUNT];

    ut_section("the trace of one replica");
    mp_cadence_set_traced(300);
    ut_check(mp_cadence_traced() == 0u, "a placement past the table traces nothing");
    mp_cadence_set_traced(0);
    ut_check(mp_cadence_traced() == 0u, "and 0 turns the trace off");
    mp_cadence_set_traced(14);
    ut_check(mp_cadence_traced() == 14u, "14 traces placement 14");

    memset(named, 0, sizeof named);
    named[14] = true;
    mp_cadence_flush_written(14u, NULL, &start, false);     /* the first write, nothing to step */
    mp_cadence_flush_done();
    mp_cadence_flush_held(14u, true, true, true);
    mp_cadence_flush_done();
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    mp_cadence_flush_written(14u, &start, &next, false);    /* 5 u and 20 degrees over north */
    mp_cadence_flush_done();
    mp_cadence_records_taken(named, MP_WIRE_KEY_COUNT);
    mp_cadence_flush_written(14u, &next, &last, false);
    mp_cadence_flush_done();
    mp_cadence_flush_written(15u, &start, &last, false);    /* another placement: not traced */
    mp_cadence_flush_done();

    report(false);
    ut_check(line_says("the replica of placement 14 (client): ",
                       "4 substep(s), 3 written, 1 held, 1 with two or more records, the largest "
                       "step 5.00 u, the largest turn 20.0 degree(s)"),
             "its substeps, written and held, the one that took two records, and the largest "
             "step and turn between two writes, the turn the short way round");
}

/* On a host the traced placement's class for each peer, one substep at a time: only a substep in
 * which that peer's view built a block counts, and only while the placement is listed. */
static void test_the_trace_on_a_host(void)
{
    ut_section("the trace on a host");
    mp_cadence_set_traced(14);
    mp_cadence_host_trace(0u, 1u, true, (uint8_t)MP_ENEMY_REACH_NEAR, true);
    mp_cadence_host_trace(0u, 2u, true, (uint8_t)MP_ENEMY_REACH_NEAR, false);
    mp_cadence_host_trace(0u, 2u, true, (uint8_t)MP_ENEMY_REACH_FAR, true);   /* no new block */
    mp_cadence_host_trace(0u, 3u, true, (uint8_t)MP_ENEMY_REACH_MIDDLE, true);
    mp_cadence_host_trace(0u, 4u, false, (uint8_t)MP_ENEMY_REACH_NEAR, false); /* not listed */
    mp_cadence_host_trace(0u, 5u, true, (uint8_t)MP_ENEMY_REACH_NONE, false);
    mp_cadence_host_trace(1u, 1u, true, (uint8_t)MP_ENEMY_REACH_FAR, true);
    report(true);
    ut_check(line_says("the replica of placement 14 to one peer (host): peer 0, ",
                       "4 substep(s) listed, 2 near, 1 middle, 0 far, 1 with no position to "
                       "measure against, 2 with a record of it sent"),
             "peer 0's view counted by class in the substeps it built a block and the placement "
             "was listed");
    ut_check(line_says("the replica of placement 14 to one peer (host): peer 1, ",
                       "1 substep(s) listed, 0 near, 0 middle, 1 far"),
             "and peer 1's on its own line");
    ut_check(line_says("the replica of placement 14 to one peer (host): peer 2, ",
                       "0 substep(s) listed"),
             "and a peer with nothing still prints its line");
}

int main(void)
{
    test_every_line_prints_at_nought();
    test_the_payloads_per_substep();
    test_the_sends_on_the_wall_clock();
    test_the_records_of_one_replica();
    test_the_flush();
    test_a_closed_pair_is_not_open();
    test_the_two_newest_on_the_line();
    test_the_trace_of_one_replica();
    test_the_trace_on_a_host();

    return ut_summary("mp_cadence");
}
