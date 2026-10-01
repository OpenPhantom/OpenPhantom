/* mp_enemy_queue_rule.c: the two newest records of one replica, written one substep apart.
 *
 * The rule is driven here the way the enemy table drives it: a record taken replaces the mirror
 * and leaves it unwritten, and a flush writes what the queue names and says whether the mirror
 * still waits. The records are told apart by their x alone.
 */
#include "unittest.h"

#include "mp_enemy_queue_rule.h"
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One key of the table, as far as the rule sees it. */
typedef struct row {
    mp_enemy_queue_t  queue;
    mp_enemy_record_t mirror;
    bool              known;
    bool              dirty;
    uint8_t           life;

    float    written[16];   /* the x of every record written, in order */
    size_t   writes;
    uint32_t fell[5];       /* indexed by mp_enemy_queue_take_t */
    uint32_t late;
    uint32_t longest;
} row_t;

static mp_enemy_record_t record_at(float x, uint8_t life)
{
    mp_enemy_record_t record;

    memset(&record, 0, sizeof record);
    (void)mp_enemy_wire_put_position(x, &record.value[MP_ENEMY_F_POS_X]);
    record.value[MP_ENEMY_F_GENERATION] = life;
    return record;
}

static void begin(row_t *row)
{
    memset(row, 0, sizeof *row);
    row->life = 1u;
}

/* A record taken by the table: the rule first, then the mirror, as the table's apply does. */
static mp_enemy_queue_take_t take(row_t *row, float x, uint8_t life, bool lands)
{
    mp_enemy_queue_take_t result;
    uint32_t              fell = 0;

    result = mp_enemy_queue_take(&row->queue, &row->mirror, row->dirty && row->known,
                                 row->known && row->life == life, lands, &fell);
    row->fell[result] += fell;
    row->mirror = record_at(x, life);
    row->known  = true;
    row->life   = life;
    row->dirty  = row->dirty || lands;
    return result;
}

/* One flush of the key. */
static void flush(row_t *row)
{
    const mp_enemy_record_t *record;
    bool                     late = false;
    uint32_t                 run;

    if (!row->dirty) {
        return;
    }
    record = mp_enemy_queue_next(&row->queue, &row->mirror, &late);
    if (row->writes < sizeof row->written / sizeof row->written[0]) {
        row->written[row->writes] = mp_enemy_wire_get_position(record->value[MP_ENEMY_F_POS_X]);
    }
    ++row->writes;
    row->late += late ? 1u : 0u;
    row->dirty = mp_enemy_queue_handled(&row->queue);
    run        = mp_enemy_queue_flushed(&row->queue, row->dirty);
    if (run > row->longest) {
        row->longest = run;
    }
}

static bool wrote(const row_t *row, const float *want, size_t count)
{
    size_t i;

    if (row->writes != count) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (row->written[i] != want[i]) {
            return false;
        }
    }
    return true;
}

static void test_one_and_two(void)
{
    static const float ONE[] = { 1.0f };
    static const float TWO[] = { 1.0f, 2.0f, 3.0f };
    row_t              row;

    ut_section("one record, then two in one window");
    begin(&row);
    ut_check(take(&row, 1.0f, 1u, true) == MP_ENEMY_QUEUE_ALONE,
             "a first record has nothing in front of it");
    flush(&row);
    ut_check(wrote(&row, ONE, 1u) && row.late == 0u, "and is written at the next flush, not late");
    flush(&row);
    ut_check(row.writes == 1u, "a flush with nothing new writes nothing");

    ut_check(take(&row, 2.0f, 1u, true) == MP_ENEMY_QUEUE_ALONE &&
                 take(&row, 3.0f, 1u, true) == MP_ENEMY_QUEUE_KEPT,
             "of two in one window the older is kept, because the body has not been given it");
    flush(&row);
    ut_check(wrote(&row, TWO, 2u) && row.dirty, "the flush writes the older and the newer waits");
    flush(&row);
    ut_check(wrote(&row, TWO, 3u) && !row.dirty && row.late == 1u,
             "the next writes the newer, one substep late, and nothing waits any more");
    ut_check(row.longest == 1u && row.queue.behind == 0u, "the run behind was one flush long");
}

/* A record for every host tick, R1 to R6, and the client's substeps taking one, two, one and two
 * of them. Writing only the newest of each window steps the body R4 to R6; dropping every record
 * but the newest past two would step it R3 to R6; keeping the two newest never steps it further
 * than it stepped without the queue. */
static void test_the_two_newest(void)
{
    static const float WANT[] = { 1.0f, 2.0f, 3.0f, 5.0f, 6.0f };
    row_t              row;

    ut_section("the two newest records stay, one tick apart at the most");
    begin(&row);
    (void)take(&row, 1.0f, 1u, true);
    flush(&row);
    (void)take(&row, 2.0f, 1u, true);
    (void)take(&row, 3.0f, 1u, true);
    flush(&row);
    ut_check(take(&row, 4.0f, 1u, true) == MP_ENEMY_QUEUE_KEPT,
             "R4 arrives with R3 still unwritten: R3 is kept");
    flush(&row);
    ut_check(take(&row, 5.0f, 1u, true) == MP_ENEMY_QUEUE_KEPT &&
                 take(&row, 6.0f, 1u, true) == MP_ENEMY_QUEUE_THIRD,
             "R5 keeps R4, and R6 arriving with both unwritten drops R4");
    ut_check(row.fell[MP_ENEMY_QUEUE_THIRD] == 1u, "one record fell to the third");
    flush(&row);
    flush(&row);
    ut_checkf(wrote(&row, WANT, 5u),
              "written: R1, R2, R3, R5, R6 (%u writes, the fourth %.0f)", (unsigned)row.writes,
              (double)(row.writes > 3u ? row.written[3] : 0.0f));
    ut_checkf(row.late == 2u,
              "R3 and R6 were written one substep late, R5 on time (%u)", (unsigned)row.late);
    ut_checkf(row.longest == 3u, "the longest run one record behind was three flushes (%u)",
              (unsigned)row.longest);
    ut_check(!row.dirty && !row.queue.held, "and the delay is caught up by a substep with none");
}

static void test_a_new_life(void)
{
    static const float WANT[] = { 1.0f, 20.0f };
    row_t              row;

    ut_section("a new life keeps nothing of the old one");
    begin(&row);
    (void)take(&row, 1.0f, 1u, true);
    flush(&row);
    (void)take(&row, 2.0f, 1u, true);
    (void)take(&row, 3.0f, 1u, true);
    ut_check(take(&row, 20.0f, 2u, true) == MP_ENEMY_QUEUE_NEW_LIFE,
             "life 2 after two unwritten records of life 1");
    ut_check(row.fell[MP_ENEMY_QUEUE_NEW_LIFE] == 2u && !row.queue.held,
             "both of the old life's fall, the kept one and the unwritten mirror");
    flush(&row);
    flush(&row);
    ut_check(wrote(&row, WANT, 2u) && row.late == 0u,
             "and the new life is written at the next flush, on time, and nothing after it");

    begin(&row);
    (void)take(&row, 1.0f, 1u, true);
    flush(&row);
    ut_check(take(&row, 20.0f, 2u, true) == MP_ENEMY_QUEUE_ALONE,
             "a new life after a written record drops nothing");
}

static void test_no_body(void)
{
    row_t row;

    ut_section("a record that reaches no body");
    begin(&row);
    (void)take(&row, 1.0f, 1u, true);
    (void)take(&row, 2.0f, 1u, true);
    ut_check(row.queue.held, "a record is kept");
    ut_check(take(&row, 3.0f, 1u, false) == MP_ENEMY_QUEUE_NO_BODY &&
                 row.fell[MP_ENEMY_QUEUE_NO_BODY] == 1u && !row.queue.held,
             "a record for no body drops what was kept for one");
    ut_check(take(&row, 4.0f, 1u, false) == MP_ENEMY_QUEUE_ALONE,
             "and with nothing kept drops nothing");
}

/* Every way out of holding a key is a clear, and a clear leaves the queue as a fresh one. */
static void test_the_clear(void)
{
    static const float WANT[] = { 3.0f };
    row_t              row;

    ut_section("every way out empties the queue");
    begin(&row);
    (void)take(&row, 1.0f, 1u, true);
    (void)take(&row, 2.0f, 1u, true);
    flush(&row);
    (void)take(&row, 3.0f, 1u, true);
    ut_check(row.queue.held && row.queue.behind == 1u, "a record kept, one flush behind");
    ut_check(mp_enemy_queue_clear(&row.queue) == 1u, "a clear says one record fell");
    ut_check(!row.queue.held && !row.queue.older_waited && !row.queue.mirror_waited &&
                 row.queue.behind == 0u,
             "and leaves nothing kept, nothing waited and no run");
    ut_check(mp_enemy_queue_clear(&row.queue) == 0u, "a second clear drops nothing");
    row.writes = 0u;
    flush(&row);
    ut_check(wrote(&row, WANT, 1u), "the next flush writes the mirror, the newest record");

    ut_check(mp_enemy_queue_handled(NULL) == false && mp_enemy_queue_flushed(NULL, true) == 0u &&
                 mp_enemy_queue_clear(NULL) == 0u,
             "no queue is no fault");
}

int main(void)
{
    test_one_and_two();
    test_the_two_newest();
    test_a_new_life();
    test_no_body();
    test_the_clear();

    return ut_summary("mp_enemy_queue_rule");
}
