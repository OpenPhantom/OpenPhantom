/* mp_stopwatch.c: where the host's substep spends its time. */
#include "unittest.h"

#include "mp_stopwatch.h"

#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

/* ==============================================================================================
 * The log, kept. The stopwatch speaks only through its report, so the report is what is read.
 * ============================================================================================ */

#define LINES_KEPT 128u
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

static void forget_lines(void)
{
    kept_count = 0;
}

/* The newest kept line that contains every one of the pieces, or NULL. */
static const char *line_with(const char *first, const char *second)
{
    size_t count = kept_count < LINES_KEPT ? kept_count : LINES_KEPT;
    size_t i;

    for (i = count; i-- > 0;) {
        const char *line = kept[i];

        if (strstr(line, first) != NULL && (second == NULL || strstr(line, second) != NULL)) {
            return line;
        }
    }
    return NULL;
}

/* ============================================================================================ */

/* The bucketing is the only part of this module that is a decision rather than a clock read, and
 * it is the part a wrong answer would quietly spoil: a report whose buckets do not mean what the
 * header says is worse than no report, because it looks like a measurement. */
static void test_the_floor_is_one_bucket(void)
{
    ut_check(mp_stopwatch_bucket(0u) == 0u, "nothing at all is bucket 0");
    ut_check(mp_stopwatch_bucket(15u) == 0u, "just under the floor is still bucket 0");
    ut_check(mp_stopwatch_bucket(16u) == 1u, "exactly the floor moves up, so the floor is a bound "
                                             "and not a hole");
}

static void test_each_bucket_is_twice_the_one_before(void)
{
    ut_check(mp_stopwatch_bucket(31u) == 1u, "16 to 31 is bucket 1");
    ut_check(mp_stopwatch_bucket(32u) == 2u, "32 opens bucket 2");
    ut_check(mp_stopwatch_bucket(63u) == 2u, "32 to 63 is bucket 2");
    ut_check(mp_stopwatch_bucket(64u) == 3u, "64 opens bucket 3");
}

/* The numbers this was built for. A substep has 31250 us, the host's dear frames were 4 to 9 ms,
 * and those two have to land in different buckets or the report cannot tell them apart. */
static void test_the_numbers_this_was_built_for(void)
{
    ut_check(mp_stopwatch_bucket(100u) < mp_stopwatch_bucket(4000u),
             "a tenth of a millisecond and four milliseconds are not the same bucket");
    ut_check(mp_stopwatch_bucket(4000u) < mp_stopwatch_bucket(9000u),
             "four milliseconds and nine milliseconds are not the same bucket, which is the whole "
             "question this instrument exists to answer");
}

/* The last bucket is everything above, so an outlier cannot walk off the end of the array. */
static void test_the_last_bucket_holds_the_tail(void)
{
    ut_check(mp_stopwatch_bucket(1000000u) == MP_WATCH_BUCKETS - 1u,
             "a whole second lands in the last bucket");
    ut_check(mp_stopwatch_bucket(0xFFFFFFFFFFFFFFFFull) == MP_WATCH_BUCKETS - 1u,
             "and so does the largest number there is, rather than running past the array");
}

/* ==============================================================================================
 * The report, which is where the three defects of the first build sat.
 * ============================================================================================ */

/* A stage left without being entered is the client's first half under the first build: its leave
 * stood inside the host's branch. The row with no finished run was skipped, and its imbalance with
 * it, so a client's report simply had no first half and nobody read the absence. */
static void test_a_row_with_no_run_is_printed_with_its_imbalance(void)
{
    const char *row;

    ut_section("a row with no finished run");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();   /* whatever an earlier test left, gone */
    forget_lines();

    mp_stopwatch_leave(MP_WATCH_TICK_PRE);
    mp_stopwatch_report();
    row = line_with("its first half", "0 run(s) finished");
    ut_check(row != NULL, "the first half is printed although no run of it finished");
    ut_check(row != NULL && strstr(row, "1 start(s) left open or ended twice") != NULL &&
                 strstr(row, "UNBALANCED") != NULL,
             "and it says the one leave that had no enter, as UNBALANCED");
    row = line_with("the whole substep", "0 run(s) finished");
    ut_check(row != NULL && strstr(row, "UNBALANCED") == NULL,
             "a stage that was simply never entered prints its row as well, without the warning");
}

/* The first build never cleared anything, and the report runs at every level end: the second
 * level's report held the first level's substeps as well, and said nothing about it. */
static void test_the_rows_start_over_after_every_report(void)
{
    ut_section("the rows start over");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    forget_lines();

    mp_stopwatch_enter(MP_WATCH_TICK_POST);
    mp_stopwatch_leave(MP_WATCH_TICK_POST);
    mp_stopwatch_report();
    ut_check(line_with("its second half", "1 run(s)") != NULL,
             "the first report has the one run of the second half");

    forget_lines();
    mp_stopwatch_report();
    ut_check(line_with("its second half", "0 run(s) finished") != NULL,
             "and the next report starts from nothing, as the level it describes did");
    ut_check(line_with("where the substep went in this level", NULL) != NULL,
             "and its head says that it is about this level");
}

/* The names are the report's contract with whatever reads it, which finds a row by them, and each
 * stage has one: a stage added to the enum without its name is refused by the build, and this
 * holds the names and their order to what the report was written to say. */
static void test_every_stage_prints_its_named_row(void)
{
    static const char *const NAMES[] = {
        "  the engine's tasks ahead of this side's, from the enemies' activation scan: ",
        "  the whole substep: ",
        "    its first half, receive and apply: ",
        "    its second half, the far bodies and the relays: ",
        "      the far body positions for the range gate: ",
        "      one far body's window: ",
        "    its last half, where this side sends: ",
        "      the census of the enemies: ",
        "      the world to one peer, chosen and encoded: "
    };
    size_t i;

    _Static_assert(sizeof NAMES / sizeof NAMES[0] == MP_WATCH_STAGE_COUNT,
                   "the test names every stage");
    ut_section("every stage has its row");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    forget_lines();
    mp_stopwatch_report();
    for (i = 0; i < MP_WATCH_STAGE_COUNT; ++i) {
        ut_checkf(line_with(NAMES[i], NULL) != NULL && strncmp(kept[i + 1u], NAMES[i],
                                                               strlen(NAMES[i])) == 0,
                  "stage %u prints its row as `%s`, in the order a substep runs it",
                  (unsigned)i, NAMES[i]);
    }
}

/* The engine's tasks ahead of this side's begin where the activation scan is entered and end where
 * this side's task enters the substep. The scan can be entered again in one substep, and a substep
 * whose enemy task never reached it has no such run; neither is an imbalance. */
static void test_the_engine_tasks_run_from_the_scan_to_this_side(void)
{
    ut_section("the engine's tasks ahead of this side's");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    forget_lines();

    mp_stopwatch_mark(MP_WATCH_ENGINE_TASKS);
    mp_stopwatch_mark(MP_WATCH_ENGINE_TASKS);
    mp_stopwatch_enter_substep();
    mp_stopwatch_leave(MP_WATCH_SUBSTEP);
    mp_stopwatch_enter_substep();   /* no scan in this one */
    mp_stopwatch_leave(MP_WATCH_SUBSTEP);
    mp_stopwatch_report();
    ut_check(line_with("the engine's tasks ahead of this side's", "1 run(s),") != NULL,
             "two marks and two substeps are one run, from the first mark");
    ut_check(line_with("the engine's tasks ahead of this side's", "UNBALANCED") == NULL,
             "and neither the second mark nor the substep without one is an imbalance");
    ut_check(line_with("  the whole substep: 2 run(s)", NULL) != NULL,
             "while the substep's own entry counts every substep");
}

/* Unarmed, the stages count nothing and the report says nothing, which is what lets the calls stay
 * in the substep of every session. */
static void test_unarmed_it_counts_and_says_nothing(void)
{
    ut_section("unarmed");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    mp_stopwatch_set_armed(false);
    forget_lines();

    mp_stopwatch_mark(MP_WATCH_ENGINE_TASKS);
    mp_stopwatch_enter_substep();
    mp_stopwatch_enter(MP_WATCH_CENSUS);
    mp_stopwatch_leave(MP_WATCH_CENSUS);
    mp_stopwatch_leave(MP_WATCH_TICK_PRE);
    mp_stopwatch_note_census(true, 22u);
    mp_stopwatch_report();
    ut_check(kept_count == 0u, "an unarmed report prints nothing");

    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    ut_check(line_with("the census of the enemies", "0 run(s) finished, 0 start(s)") != NULL &&
                 line_with("its first half", "0 run(s) finished, 0 start(s)") != NULL,
             "and nothing of what was called unarmed is in the next armed report");
    ut_check(line_with("the census, minute by minute", ": none") != NULL,
             "not even the census's actors");
}

/* ==============================================================================================
 * The minutes, the census's size and the engine between the halves.
 * ============================================================================================ */

static void test_the_minute_of_a_moment(void)
{
    ut_section("the minute of a moment");
    ut_check(mp_stopwatch_minute_of(0u, 600u) == 0u, "the level's first moment is minute 0");
    ut_check(mp_stopwatch_minute_of(599u, 600u) == 0u, "the last tick of it as well");
    ut_check(mp_stopwatch_minute_of(600u, 600u) == 1u, "a whole minute later is minute 1");
    ut_check(mp_stopwatch_minute_of(600u * 40u, 600u) == 40u,
             "the minute is not clamped here; the row it goes into is");
    ut_check(mp_stopwatch_minute_of(12345u, 0u) == 0u, "a counter without a rate says minute 0");
}

static void test_the_minute_rows_as_printed(void)
{
    mp_stopwatch_minute_t minutes[MP_WATCH_MINUTES];
    char                  text[720];
    char                  tiny[24];
    size_t                i;

    ut_section("the minute rows");
    memset(minutes, 0, sizeof minutes);
    ut_check(mp_stopwatch_minute_text(minutes, MP_WATCH_MINUTES, false, text, sizeof text) == 4u &&
                 strcmp(text, "none") == 0,
             "no run in any minute reads none, so the line still prints");

    mp_stopwatch_minute_add(&minutes[0], 800u);
    mp_stopwatch_minute_add(&minutes[0], 900u);
    mp_stopwatch_minute_add(&minutes[0], 2100u);
    mp_stopwatch_minute_add(&minutes[2], 870u);
    (void)mp_stopwatch_minute_text(minutes, MP_WATCH_MINUTES, false, text, sizeof text);
    ut_check(strcmp(text, "1 1266/2100, 3 870/870") == 0,
             "each minute with a run is its number from 1, its mean and its worst; an empty "
             "minute is left out and the next one keeps its own number");

    mp_stopwatch_minute_add(&minutes[MP_WATCH_MINUTES - 1u], 5u);
    (void)mp_stopwatch_minute_text(minutes, MP_WATCH_MINUTES, true, text, sizeof text);
    ut_check(strstr(text, ", 32+ 5/5") != NULL,
             "the last minute says it holds the later ones when they went into it");

    for (i = 0; i < MP_WATCH_MINUTES; ++i) {
        memset(&minutes[i], 0, sizeof minutes[i]);
        minutes[i].runs  = 1u;
        minutes[i].total = 0xFFFFFFFFu;
        minutes[i].worst = 0xFFFFFFFFu;
    }
    (void)mp_stopwatch_minute_text(minutes, MP_WATCH_MINUTES, true, text, sizeof text);
    ut_check(strlen(text) < sizeof text && strcmp(text + strlen(text) - 5u, ", ...") == 0,
             "a row of thirty two minutes at their widest ends in ... inside the room it has, "
             "rather than in half a number");
    (void)mp_stopwatch_minute_text(minutes, MP_WATCH_MINUTES, false, tiny, sizeof tiny);
    ut_check(strlen(tiny) < sizeof tiny, "and a buffer too small for one entry stays terminated");
}

static void test_the_engine_between_the_halves(void)
{
    ut_section("the whole less its three halves");
    ut_check(mp_stopwatch_between(1000u, 300u, 200u, 100u) == 400u,
             "the whole less the three halves is what ran between them");
    ut_check(mp_stopwatch_between(500u, 300u, 200u, 100u) == 0u,
             "halves that add up to more than the whole give nought rather than wrapping");
}

/* A stage's minute row and the census's, through the stopwatch itself. The runs of a test take
 * microseconds, so they all land in the level's first minute. */
static void test_a_level_prints_its_minutes(void)
{
    ut_section("a level's minutes");
    mp_stopwatch_set_armed(true);
    mp_stopwatch_report();
    forget_lines();

    mp_stopwatch_enter(MP_WATCH_CENSUS);
    mp_stopwatch_leave(MP_WATCH_CENSUS);
    mp_stopwatch_note_census(true, 20u);
    mp_stopwatch_enter(MP_WATCH_CENSUS);
    mp_stopwatch_leave(MP_WATCH_CENSUS);
    mp_stopwatch_note_census(true, 24u);
    mp_stopwatch_note_census(false, 90u);   /* a substep whose census did not run */
    mp_stopwatch_report();
    ut_check(line_with("    the census of the enemies, minute by minute, mean/WORST in us: 1 ",
                       NULL) != NULL,
             "the census stage has its runs in the level's first minute");
    ut_check(line_with("the census, minute by minute, actors mean/most: 1 22/24", NULL) != NULL,
             "and the census's size in that minute is the mean and the most of the actors it read");
    ut_check(line_with("  the whole substep, minute by minute, mean/WORST in us: none", NULL) !=
                 NULL,
             "a stage with no run in any minute says none");
    ut_check(line_with("the engine between this side's halves", "0 us in all, mean 0") != NULL,
             "and the engine's share between the halves prints with no substep behind it");
}

/* A duration kept in counter ticks is not a span between two readings. The span function answers 0
 * for a start of 0, because 0 is what a machine without the counter reads, so a duration handed
 * to it as (0, ticks) printed 0 us for every total the memory watch and the census probe kept. */
static void test_a_duration_in_ticks_is_microseconds(void)
{
    LARGE_INTEGER frequency;
    uint64_t      second;

    ut_section("a duration in ticks");
    ut_check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0,
             "this machine has the performance counter the test needs");
    second = (uint64_t)frequency.QuadPart;
    ut_check(mp_stopwatch_micros(0u, second) == 0u,
             "the span function still refuses a start of 0, so a duration must not go through it");
    ut_check(mp_stopwatch_duration_micros(second) == 1000000u,
             "one second's worth of ticks is a million microseconds");
    ut_check(mp_stopwatch_duration_micros(second / 1000u) >= 999u &&
                 mp_stopwatch_duration_micros(second / 1000u) <= 1000u,
             "a millisecond's worth is a thousand, give or take the rounding of the division");
    ut_check(mp_stopwatch_duration_micros(0u) == 0u, "no ticks is no time");
    ut_check(mp_stopwatch_duration_micros(second * 5000u) == 0xFFFFFFFFu,
             "more than a 32-bit count of microseconds saturates rather than wraps");
}

int main(void)
{
    test_a_duration_in_ticks_is_microseconds();

    test_the_floor_is_one_bucket();
    test_each_bucket_is_twice_the_one_before();
    test_the_numbers_this_was_built_for();
    test_the_last_bucket_holds_the_tail();

    test_a_row_with_no_run_is_printed_with_its_imbalance();
    test_the_rows_start_over_after_every_report();
    test_every_stage_prints_its_named_row();
    test_the_engine_tasks_run_from_the_scan_to_this_side();
    test_unarmed_it_counts_and_says_nothing();

    test_the_minute_of_a_moment();
    test_the_minute_rows_as_printed();
    test_the_engine_between_the_halves();
    test_a_level_prints_its_minutes();

    return ut_summary("mp_stopwatch");
}
