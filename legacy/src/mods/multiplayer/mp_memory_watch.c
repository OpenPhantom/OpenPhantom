/* mp_memory_watch.c: the guarded reads and writes of this side, in the report. See the header. */
#include "mp_memory_watch.h"

#include "mp_bank.h"
#include "mp_capacity.h"
#include "mp_census_probe.h"
#include "mp_enemy_bind.h"
#include "mp_stopwatch.h"
#include "mp_task.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/memory_guard.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* What a list may take of one log line. The longest text around a list, the faults' line with its
 * four counts at ten digits each, leaves this much of the log's 1021 characters. */
#define LIST_BYTES 760u

/* The most callers one line names. Twelve of the widest fit the list whole. */
#define CALLERS_NAMED 12u

typedef struct watch_state {
    bool     tested;          /* the self-test has run in this process */
    bool     measuring;       /* the asking side and the census probe are counted */
    uint32_t substep_base;    /* the task's count of substeps at the last reset */
} watch_state_t;

static watch_state_t watch;

static const char *const KIND_NAMES[MEMORY_WATCH_KINDS] = { "read", "probe", "write" };

/* ==============================================================================================
 * Lists.
 * ============================================================================================ */

/* Appends one item to a comma separated list, or nothing at all when it would not fit whole. */
static bool append(char *list, size_t *used, const char *item)
{
    size_t length    = strlen(item);
    size_t separator = *used == 0u ? 0u : 2u;

    if (*used + separator + length + 1u > LIST_BYTES) {
        return false;
    }
    if (separator != 0u) {
        memcpy(list + *used, ", ", separator);
    }
    memcpy(list + *used + separator, item, length + 1u);
    *used += separator + length;
    return true;
}

/* A caller as the module that holds it and the offset into that module, which is what the
 * linker's map of that build names. The absolute address when no module claims it. */
static void name_caller(uintptr_t caller, char *out, size_t size)
{
    HMODULE     module = NULL;
    char        path[MAX_PATH];
    const char *name = "?";
    DWORD       length;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)caller, &module) || module == NULL) {
        text_format(out, size, "%08X", (unsigned)caller);
        return;
    }
    length = GetModuleFileNameA(module, path, (DWORD)sizeof path);
    if (length > 0u && length < sizeof path) {
        const char *slash = strrchr(path, '\\');

        path[length] = '\0';
        name = slash != NULL ? slash + 1 : path;
    }
    text_format(out, size, "%s+0x%05X", name, (unsigned)(caller - (uintptr_t)module));
}

/* The index of the row with the most, among those not yet taken. */
static uint32_t most_of(const uint32_t *counts, const bool *taken, uint32_t rows)
{
    uint32_t best = rows;
    uint32_t row;

    for (row = 0; row < rows; ++row) {
        if (!taken[row] && (best == rows || counts[row] > counts[best])) {
            best = row;
        }
    }
    return best;
}

/* ==============================================================================================
 * The asking side.
 * ============================================================================================ */

static void list_asking_callers(char *list)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    uint32_t                    counts[MEMORY_WATCH_CALLERS];
    bool                        taken[MEMORY_WATCH_CALLERS];
    size_t                      used = 0;
    uint32_t                    named;
    uint32_t                    row;

    list[0] = '\0';
    memset(taken, 0, sizeof taken);
    for (row = 0; row < s->callers; ++row) {
        counts[row] = s->caller[row].calls;
    }
    for (named = 0; named < s->callers && named < CALLERS_NAMED; ++named) {
        char item[96];
        char where[64];

        row = most_of(counts, taken, s->callers);
        taken[row] = true;
        name_caller(s->caller[row].caller, where, sizeof where);
        text_format(item, sizeof item, "%s x%u (%u refused)", where,
                    (unsigned)s->caller[row].calls, (unsigned)s->caller[row].refused);
        if (!append(list, &used, item)) {
            break;
        }
    }
    if (s->calls_unlisted != 0u) {
        char item[64];

        text_format(item, sizeof item, "%u call(s) from callers past the table",
                    (unsigned)s->calls_unlisted);
        (void)append(list, &used, item);
    }
    if (used == 0u) {
        (void)append(list, &used, "none");
    }
}

/* The mean price of one question in nanoseconds, which is what tells a minute of dearer questions
 * from a minute of more of them. */
static uint32_t nanos_each(uint64_t ticks, uint32_t calls)
{
    LARGE_INTEGER frequency;
    uint64_t      each;

    if (calls == 0u || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        return 0u;
    }
    each = ticks * 1000000000u / (uint64_t)frequency.QuadPart / calls;
    return each > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)each;
}

/* One row a minute from the first to the last that asked: its number, the calls, the mean price of
 * one in nanoseconds and the mean run behind the addresses in KB. The last row holds every minute
 * after it. Thirty two rows of the widest fit the list whole. */
static void list_minutes(char *list)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    size_t                      used = 0;
    uint32_t                    last = 0;
    uint32_t                    m;

    list[0] = '\0';
    for (m = 0; m < MEMORY_WATCH_MINUTES; ++m) {
        if (s->minute[m].calls != 0u) {
            last = m + 1u;
        }
    }
    for (m = 0; m < last; ++m) {
        const memory_watch_minute_t *row = &s->minute[m];
        char                         item[48];

        text_format(item, sizeof item, "%u %u/%u/%u", (unsigned)(m + 1u), (unsigned)row->calls,
                    (unsigned)nanos_each(row->ticks, row->calls),
                    (unsigned)(row->calls != 0u ? row->region_kb / row->calls : 0u));
        if (!append(list, &used, item)) {
            break;
        }
    }
    if (used == 0u) {
        (void)append(list, &used, "none");
    }
}

static void report_asking(void)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    uint32_t                    substeps = mp_task_ticks() - watch.substep_base;
    uint64_t                    hundredths =
        substeps != 0u ? (uint64_t)s->calls * 100u / substeps : 0u;
    char                        list[LIST_BYTES];

    log_info("the reads that asked the system first, in this level: %u call(s) over %u "
             "substep(s), %u.%02u a substep, %u refused; %u us in all, the dearest %u us; the "
             "region behind the address %u under 64 KB, %u up to 1 MB, %u up to 4 MB, %u larger",
             (unsigned)s->calls, (unsigned)substeps, (unsigned)(hundredths / 100u),
             (unsigned)(hundredths % 100u), (unsigned)s->refused,
             (unsigned)mp_stopwatch_duration_micros(s->ticks),
             (unsigned)mp_stopwatch_duration_micros(s->dearest_ticks), (unsigned)s->region[0],
             (unsigned)s->region[1], (unsigned)s->region[2], (unsigned)s->region[3]);
    list_asking_callers(list);
    log_info("the reads that asked the system first, the busiest callers: %s", list);
    list_minutes(list);
    log_info("the reads that asked the system first, minute by minute, calls/ns each/KB behind "
             "the address: %s", list);
}

static void report_probe(void)
{
    mp_census_probe_totals_t totals;

    mp_census_probe_totals(&totals);
    log_info("the census read both ways once a second: %u actor(s) compared, %u that differed "
             "(must be 0); the asking form %u us, the faulting form %u us, over all compared",
             (unsigned)totals.compared, (unsigned)totals.differed,
             (unsigned)mp_stopwatch_duration_micros(totals.asking_ticks),
             (unsigned)mp_stopwatch_duration_micros(totals.trying_ticks));
}

/* ==============================================================================================
 * The trying side.
 * ============================================================================================ */

static void list_refusers(char *list)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    uint32_t                    counts[MEMORY_WATCH_REFUSERS];
    bool                        taken[MEMORY_WATCH_REFUSERS];
    size_t                      used = 0;
    uint32_t                    named;
    uint32_t                    row;

    list[0] = '\0';
    memset(taken, 0, sizeof taken);
    for (row = 0; row < s->refusers; ++row) {
        counts[row] = s->refuser[row].count;
    }
    for (named = 0; named < s->refusers; ++named) {
        const memory_watch_refuser_t *r;
        char                          item[112];
        char                          where[64];

        row = most_of(counts, taken, s->refusers);
        taken[row] = true;
        r = &s->refuser[row];
        name_caller(r->caller, where, sizeof where);
        text_format(item, sizeof item, "%s x%u %s %s (last at %08X)", where, (unsigned)r->count,
                    r->kind < MEMORY_WATCH_KINDS ? KIND_NAMES[r->kind] : "?",
                    r->outside ? "outside" : "fault", (unsigned)r->last_address);
        if (!append(list, &used, item)) {
            break;
        }
    }
    if (s->refusals_unlisted != 0u) {
        char item[64];

        text_format(item, sizeof item, "%u more from callers past the table",
                    (unsigned)s->refusals_unlisted);
        (void)append(list, &used, item);
    }
    if (used == 0u) {
        (void)append(list, &used, "none");
    }
}

static void report_refusals(void)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    char                        list[LIST_BYTES];

    list_refusers(list);
    log_info("the guarded reads and writes that faulted, in this level: %u read(s), %u probe(s), "
             "%u write(s), %u refused outside the application's address range without a fault; "
             "the callers, most first: %s",
             (unsigned)s->faults[MEMORY_WATCH_READ], (unsigned)s->faults[MEMORY_WATCH_PROBE],
             (unsigned)s->faults[MEMORY_WATCH_WRITE], (unsigned)s->outside, list);
}

static void report_chains(void)
{
    uint32_t unreadable = 0;
    uint32_t overlong = 0;

    mp_capacity_broken_walks(&unreadable, &overlong);
    log_info("the chains walked by hand that did not finish: %u walk(s) of the enemies stopped at "
             "a link that would not read; %u walk(s) of the objects stopped at one and %u ran "
             "past their capacity; the first of each kind is logged, the rest only counted",
             (unsigned)mp_enemy_bind_broken_links(), (unsigned)unreadable, (unsigned)overlong);
}

/* ==============================================================================================
 * The self-test, and arming.
 * ============================================================================================ */

/* Twelve bytes from four before the end of a page this process holds, into a page it only
 * reserved: the copy stops at the first byte it may not read, and the guard's handler takes over.
 * One call, one fault, and that fault turned back by the guard. A read delivered, or more than one
 * fault, means something in this process resumes the copy after a fault; a refusal the guard did
 * not turn back means the reads fell back to asking first, which holds but costs a system call
 * each. Either way the census does not stand where it was built to. */
static void self_test(void)
{
    SYSTEM_INFO info;
    uint8_t    *pages;
    uint8_t     out[12];
    uint32_t    before;
    uint32_t    faults;
    uint32_t    turned;
    bool        read;

    GetSystemInfo(&info);
    pages = (uint8_t *)VirtualAlloc(NULL, 2u * info.dwPageSize, MEM_RESERVE, PAGE_NOACCESS);
    if (pages == NULL ||
        VirtualAlloc(pages, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        log_warning("the guarded read's self-test could not reserve its two pages, so it did not "
                    "run");
        if (pages != NULL) {
            (void)VirtualFree(pages, 0, MEM_RELEASE);
        }
        return;
    }
    before = memory_watch_stats()->faults[MEMORY_WATCH_READ];
    turned = memory_guard_redirected();
    read   = memory_try_read((uintptr_t)(pages + info.dwPageSize - 4u), out, sizeof out);
    faults = memory_watch_stats()->faults[MEMORY_WATCH_READ] - before;
    turned = memory_guard_redirected() - turned;
    (void)VirtualFree(pages, 0, MEM_RELEASE);

    if (!read && faults == 1u && turned == 1u) {
        log_info("the guarded read's self-test: a read across a page nobody holds %s, %u fault(s) "
                 "for one call (must be 1), %u turned back by the guard (must be 1)",
                 "was refused", (unsigned)faults, (unsigned)turned);
        return;
    }
    log_warning("the guarded read's self-test: a read across a page nobody holds %s, %u fault(s) "
                "for one call (must be 1), %u turned back by the guard (must be 1); the guarded "
                "reads of this build do not behave as they were built to, and no count of this run "
                "can be trusted",
                read ? "was delivered" : "was refused", (unsigned)faults, (unsigned)turned);
}

void mp_memory_watch_arm(bool enabled, bool measuring)
{
    if (!enabled) {
        return;   /* a multiplayer switched off counts nothing and tests nothing */
    }
    watch.measuring = measuring;
    memory_watch_arm(measuring, true);
    mp_census_probe_arm(measuring);
    /* Only a run that measures pays the one real fault the test costs. A player's process, the
     * single player included, would otherwise start every time with a first-chance access
     * violation in crash_report's table, and multiplayer code would run with no session. */
    if (measuring && !watch.tested) {
        watch.tested = true;
        self_test();
    }
    memory_watch_reset();
    watch.substep_base = mp_task_ticks();
}

/* Whether this module's guarded copy stands, and how many faults it turned back. The count is the
 * process's, not the level's: the guard keeps it and this file does not reset it. */
static void report_guard(void)
{
    static const char *const STATES[] = { "not started", "starting on another thread", "ready",
                                          "refused, so its reads ask the system first" };
    memory_guard_state_t     state    = memory_guard_state();

    log_info("the guarded copy of this module: %s; %u fault(s) turned back into a refusal before "
             "crash_report and the compatibility fix saw them",
             (size_t)state < sizeof STATES / sizeof STATES[0] ? STATES[state] : "unknown",
             (unsigned)memory_guard_redirected());
}

void mp_memory_watch_level_begins(void)
{
    memory_watch_reset_asking();
    watch.substep_base = mp_task_ticks();
}

void mp_memory_watch_report(void)
{
    if (watch.measuring) {
        report_asking();
        report_probe();
    } else {
        log_info("the reads that asked the system first and the census read both ways: not "
                 "measured in this run, because the substep stopwatch is off");
    }
    report_refusals();
    report_guard();
    mp_bank_window_report();
    report_chains();
    memory_watch_reset();
    watch.substep_base = mp_task_ticks();
}
