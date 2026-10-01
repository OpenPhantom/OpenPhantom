/* mp_stopwatch.c: where the host's substep spends its time. See the header. */
#include "mp_stopwatch.h"

#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

/* The bucket floor in microseconds, and the step between buckets. Bucket 0 holds everything under
 * the floor, so a stage that never costs anything prints one number and is done with. */
#define BUCKET_FLOOR_US 16u

/* The room a minute row gets. Thirty two minutes at their widest, `32+ 4294967295/4294967295, `,
 * would not fit beside the name inside the 1021 characters the log keeps of a line, so a row that
 * reaches this ends in `...`; a real one, with means and worsts of seven digits at most, stays
 * well inside it. */
#define MINUTE_TEXT_BYTES 720u

typedef struct stage_row {
    LARGE_INTEGER started;
    bool          running;

    uint64_t entries;
    uint64_t total_us;
    uint64_t worst_us;
    uint64_t buckets[MP_WATCH_BUCKETS];
    uint32_t unbalanced;   /* a leave with no enter, or an enter over a running one */
    mp_stopwatch_minute_t minutes[MP_WATCH_MINUTES];
} stage_row_t;

typedef struct stopwatch_state {
    bool          armed;
    bool          have_frequency;
    LARGE_INTEGER frequency;
    stage_row_t   rows[MP_WATCH_STAGE_COUNT];

    /* The level's clock for the minute rows: the counter at the first stage that began after the
     * last report, 0 until one has, and the minute the newest finished run fell in, not clamped,
     * which is also the minute a census is noted in. */
    uint64_t              level_started;
    uint32_t              minute_now;
    uint32_t              minute_latest;
    mp_stopwatch_minute_t census[MP_WATCH_MINUTES];

    uint64_t      setup_started;   /* 0 while no setup is being timed */
} stopwatch_state_t;

static stopwatch_state_t watch;

/* Sized by its initialiser, so a stage added to the enum without a name fails the assertion
 * below instead of printing whatever a null pointer formats as. */
static const char *const STAGE_NAMES[] = {
    "the engine's tasks ahead of this side's, from the enemies' activation scan",
    "the whole substep",
    "  its first half, receive and apply",
    "  its second half, the far bodies and the relays",
    "    the far body positions for the range gate",
    "    one far body's window",
    "  its last half, where this side sends",
    "    the census of the enemies",
    "    the world to one peer, chosen and encoded"
};

_Static_assert(sizeof STAGE_NAMES / sizeof STAGE_NAMES[0] == MP_WATCH_STAGE_COUNT,
               "every stopwatch stage needs its name, in the order of the enum");

size_t mp_stopwatch_bucket(uint64_t microseconds)
{
    size_t   bucket = 0;
    uint64_t edge   = BUCKET_FLOOR_US;

    while (bucket + 1u < MP_WATCH_BUCKETS && microseconds >= edge) {
        ++bucket;
        edge *= 2u;
    }
    return bucket;
}

uint32_t mp_stopwatch_minute_of(uint64_t elapsed_ticks, uint64_t ticks_per_minute)
{
    uint64_t minute;

    if (ticks_per_minute == 0u) {
        return 0u;
    }
    minute = elapsed_ticks / ticks_per_minute;
    return minute > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)minute;
}

void mp_stopwatch_minute_add(mp_stopwatch_minute_t *minute, uint32_t value)
{
    if (minute == NULL) {
        return;
    }
    ++minute->runs;
    minute->total += value;
    if (value > minute->worst) {
        minute->worst = value;
    }
}

size_t mp_stopwatch_minute_text(const mp_stopwatch_minute_t *minutes, size_t count,
                                bool last_holds_later, char *out, size_t size)
{
    static const char MORE[] = ", ...";
    size_t            at = 0;
    size_t            i;

    if (out == NULL || size == 0u) {
        return 0u;
    }
    out[0] = '\0';
    for (i = 0; minutes != NULL && i < count; ++i) {
        char   entry[48];
        size_t wrote;

        if (minutes[i].runs == 0u) {
            continue;
        }
        wrote = text_format(entry, sizeof entry, "%s%u%s %u/%u", at == 0u ? "" : ", ",
                            (unsigned)(i + 1u), (last_holds_later && i + 1u == count) ? "+" : "",
                            (unsigned)(minutes[i].total / minutes[i].runs),
                            (unsigned)minutes[i].worst);
        if (wrote == 0u || wrote + 1u >= sizeof entry) {
            continue;   /* nothing, or a full buffer that may hold a cut entry */
        }
        /* The entry goes in only with room left for the mark that says one was left out, which
         * loses its comma when nothing stands in front of it. */
        if (at + wrote + sizeof MORE > size) {
            const char *more = at == 0u ? MORE + 2 : MORE;
            size_t      length = strlen(more);

            if (at + length < size) {
                memcpy(out + at, more, length + 1u);
                at += length;
            }
            return at;
        }
        memcpy(out + at, entry, wrote + 1u);
        at += wrote;
    }
    if (at == 0u && size > 4u) {
        memcpy(out, "none", 5u);
        at = 4u;
    }
    return at;
}

uint64_t mp_stopwatch_between(uint64_t whole_us, uint64_t pre_us, uint64_t post_us,
                              uint64_t end_us)
{
    uint64_t halves = pre_us + post_us + end_us;

    return whole_us > halves ? whole_us - halves : 0u;
}

/* The counter's rate is fixed at boot, so it is asked once and kept. A machine that has none is
 * asked again next time, which costs one call. */
static bool frequency_known(void)
{
    if (!watch.have_frequency) {
        watch.have_frequency = QueryPerformanceFrequency(&watch.frequency) &&
                               watch.frequency.QuadPart > 0;
    }
    return watch.have_frequency;
}

void mp_stopwatch_set_armed(bool armed)
{
    if (armed && !frequency_known()) {
        log_warning("the substep stopwatch has no performance counter on this machine and "
                    "stays off; nothing else changes");
        return;
    }
    watch.armed = armed;
}

bool mp_stopwatch_armed(void)
{
    return watch.armed;
}

/* A stage's start, and with the first of a level the level's own. */
static void start(stage_row_t *row)
{
    row->running = QueryPerformanceCounter(&row->started) ? true : false;
    if (row->running && watch.level_started == 0u) {
        watch.level_started = (uint64_t)row->started.QuadPart;
    }
}

void mp_stopwatch_enter(mp_stopwatch_stage_t stage)
{
    stage_row_t *row;

    if (!watch.armed || (unsigned)stage >= (unsigned)MP_WATCH_STAGE_COUNT) {
        return;
    }
    row = &watch.rows[stage];
    if (row->running) {
        ++row->unbalanced;   /* the outer one is abandoned rather than nested into itself */
    }
    start(row);
}

void mp_stopwatch_mark(mp_stopwatch_stage_t stage)
{
    if (!watch.armed || (unsigned)stage >= (unsigned)MP_WATCH_STAGE_COUNT ||
        watch.rows[stage].running) {
        return;
    }
    start(&watch.rows[stage]);
}

/* The minute a run that finished at `now` belongs to, kept as the minute of the level's newest
 * run. A run begun before the level's clock, across a report, counts in its first minute. */
static uint32_t minute_slot(LONGLONG now)
{
    uint64_t elapsed = 0u;

    if (watch.level_started != 0u && (uint64_t)now > watch.level_started) {
        elapsed = (uint64_t)now - watch.level_started;
    }
    watch.minute_now = mp_stopwatch_minute_of(elapsed, (uint64_t)watch.frequency.QuadPart * 60u);
    if (watch.minute_now > watch.minute_latest) {
        watch.minute_latest = watch.minute_now;
    }
    return watch.minute_now < MP_WATCH_MINUTES ? watch.minute_now : MP_WATCH_MINUTES - 1u;
}

void mp_stopwatch_leave(mp_stopwatch_stage_t stage)
{
    stage_row_t  *row;
    LARGE_INTEGER now;
    uint64_t      ticks;
    uint64_t      microseconds;

    if (!watch.armed || (unsigned)stage >= (unsigned)MP_WATCH_STAGE_COUNT) {
        return;
    }
    row = &watch.rows[stage];
    if (!row->running) {
        ++row->unbalanced;
        return;
    }
    row->running = false;
    if (!QueryPerformanceCounter(&now) || now.QuadPart < row->started.QuadPart) {
        ++row->unbalanced;   /* the counter went backwards, which is a fault of its own */
        return;
    }
    /* Multiplied before divided, and in 64 bits: a substep is microseconds, so the product cannot
     * come near the range, and dividing first would round every short stage to nothing. */
    ticks        = (uint64_t)(now.QuadPart - row->started.QuadPart);
    microseconds = (ticks * 1000000u) / (uint64_t)watch.frequency.QuadPart;

    ++row->entries;
    row->total_us += microseconds;
    if (microseconds > row->worst_us) {
        row->worst_us = microseconds;
    }
    ++row->buckets[mp_stopwatch_bucket(microseconds)];
    mp_stopwatch_minute_add(&row->minutes[minute_slot(now.QuadPart)],
                            microseconds > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)microseconds);
}

void mp_stopwatch_enter_substep(void)
{
    if (!watch.armed) {
        return;
    }
    if (watch.rows[MP_WATCH_ENGINE_TASKS].running) {
        mp_stopwatch_leave(MP_WATCH_ENGINE_TASKS);
    }
    mp_stopwatch_enter(MP_WATCH_SUBSTEP);
}

void mp_stopwatch_note_census(bool ran, uint32_t actors)
{
    uint32_t slot;

    if (!watch.armed || !ran) {
        return;
    }
    slot = watch.minute_now < MP_WATCH_MINUTES ? watch.minute_now : MP_WATCH_MINUTES - 1u;
    mp_stopwatch_minute_add(&watch.census[slot], actors);
}

/* One stage's row. A stage that finished no run prints what it has, which is its imbalance: a
 * row that is simply absent reads the same as a stage that never existed. */
static void report_row(size_t stage)
{
    const stage_row_t *row = &watch.rows[stage];
    char               line[256];
    size_t             at = 0;
    size_t             bucket;

    if (row->entries == 0u) {
        log_info("  %s: 0 run(s) finished, %u start(s) left open or ended twice%s",
                 STAGE_NAMES[stage], (unsigned)row->unbalanced,
                 row->unbalanced != 0u ? " (UNBALANCED, this row is not to be trusted)" : "");
        return;
    }
    for (bucket = 0; bucket < MP_WATCH_BUCKETS && at + 1u < sizeof(line); ++bucket) {
        at += text_format(line + at, sizeof(line) - at, "%s%u", bucket == 0 ? "" : " ",
                          (unsigned)row->buckets[bucket]);
    }
    log_info("  %s: %u run(s), %u us in all, mean %u, WORST %u%s | %s",
             STAGE_NAMES[stage], (unsigned)row->entries, (unsigned)row->total_us,
             (unsigned)(row->total_us / row->entries), (unsigned)row->worst_us,
             row->unbalanced != 0u ? " (UNBALANCED, this row is not to be trusted)" : "",
             line);
}

/* The engine's work that falls between this side's halves: the collision and the rest of the
 * substep message after the task, and the far bodies' spawn and tick between the first two
 * halves. Worked out of the totals, so it costs the substep nothing. */
static void report_between(void)
{
    const stage_row_t *whole = &watch.rows[MP_WATCH_SUBSTEP];
    uint64_t           rest  = mp_stopwatch_between(whole->total_us,
                                                    watch.rows[MP_WATCH_TICK_PRE].total_us,
                                                    watch.rows[MP_WATCH_TICK_POST].total_us,
                                                    watch.rows[MP_WATCH_SUBSTEP_END].total_us);

    log_info("  the engine between this side's halves, the whole less its three halves: %u us in "
             "all, mean %u",
             (unsigned)rest, (unsigned)(whole->entries != 0u ? rest / whole->entries : 0u));
}

static void report_minutes(void)
{
    bool   folded = watch.minute_latest >= MP_WATCH_MINUTES;
    char   rows[MINUTE_TEXT_BYTES];
    size_t stage;

    for (stage = 0; stage < (size_t)MP_WATCH_STAGE_COUNT; ++stage) {
        (void)mp_stopwatch_minute_text(watch.rows[stage].minutes, MP_WATCH_MINUTES, folded, rows,
                                       sizeof rows);
        log_info("  %s, minute by minute, mean/WORST in us: %s", STAGE_NAMES[stage], rows);
    }
    (void)mp_stopwatch_minute_text(watch.census, MP_WATCH_MINUTES, folded, rows, sizeof rows);
    log_info("the census, minute by minute, actors mean/most: %s", rows);
}

/* Everything counted since the last report goes, and nothing that is running: a stage begun
 * before the report ends after it and belongs to the level it ends in. */
static void start_over(void)
{
    size_t stage;

    for (stage = 0; stage < (size_t)MP_WATCH_STAGE_COUNT; ++stage) {
        stage_row_t *row = &watch.rows[stage];

        row->entries    = 0u;
        row->total_us   = 0u;
        row->worst_us   = 0u;
        row->unbalanced = 0u;
        memset(row->buckets, 0, sizeof row->buckets);
        memset(row->minutes, 0, sizeof row->minutes);
    }
    memset(watch.census, 0, sizeof watch.census);
    watch.level_started = 0u;
    watch.minute_now    = 0u;
    watch.minute_latest = 0u;
}

void mp_stopwatch_report(void)
{
    size_t stage;

    if (!watch.armed) {
        return;
    }
    log_info("where the substep went in this level, in microseconds. The buckets double from %u "
             "us; the LAST bucket is everything above, and it is the one to read, because a mean "
             "over a second of substeps hides the one that cost nine milliseconds",
             (unsigned)BUCKET_FLOOR_US);
    for (stage = 0; stage < (size_t)MP_WATCH_STAGE_COUNT; ++stage) {
        report_row(stage);
    }
    report_between();
    report_minutes();
    start_over();
}

uint64_t mp_stopwatch_ticks(void)
{
    LARGE_INTEGER now;

    if (!frequency_known() || !QueryPerformanceCounter(&now) || now.QuadPart <= 0) {
        return 0u;
    }
    return (uint64_t)now.QuadPart;
}

uint32_t mp_stopwatch_micros(uint64_t from, uint64_t to)
{
    if (!frequency_known() || from == 0u || to < from) {
        return 0u;
    }
    return mp_stopwatch_duration_micros(to - from);
}

uint32_t mp_stopwatch_duration_micros(uint64_t ticks)
{
    uint64_t microseconds;

    if (!frequency_known()) {
        return 0u;
    }
    /* Multiplied before divided, as in the stages: the product stays in range for weeks. */
    microseconds = (ticks * 1000000u) / (uint64_t)watch.frequency.QuadPart;
    return microseconds > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)microseconds;
}

void mp_stopwatch_setup_begin(void)
{
    watch.setup_started = mp_stopwatch_ticks();
}

void mp_stopwatch_setup_end(void)
{
    uint32_t us;

    if (watch.setup_started == 0u) {
        return;   /* no counter on this machine, or an end with no begin */
    }
    us = mp_stopwatch_micros(watch.setup_started, mp_stopwatch_ticks());
    watch.setup_started = 0u;
    if (us / 1000u >= MP_WATCH_SETUP_WARN_MS) {
        log_warning("setting the session up from the menu took %u.%03u ms, and the menu stood "
                    "still for all of it: a step of the setup is doing work the player waits on. "
                    "The installation lines above this one carry their own times where they "
                    "have one",
                    (unsigned)(us / 1000u), (unsigned)(us % 1000u));
        return;
    }
    log_info("the session was set up from the menu in %u.%03u ms",
             (unsigned)(us / 1000u), (unsigned)(us % 1000u));
}
