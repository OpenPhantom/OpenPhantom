/* mp_memory_watch.c: the report of the guarded reads and writes, against the log it writes.
 *
 * Four things are held here. A multiplayer switched off arms nothing and tests nothing. Arming runs
 * the guarded read's self-test once, and on this machine it says one fault for one call. A level's
 * report names what happened in it and nothing of the level before. And no line outgrows the log's
 * 1021 characters even with every table full: forty places that ask and twenty that refuse, more
 * than either table keeps.
 *
 * The log is kept in memory: the test is the logger.
 */
#include "unittest.h"

#include "mp_memory_watch.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LINES      96u
#define LINE_BYTES 1200u
#define LOG_ROOM   1021u

/* The first page is never mapped on Windows. */
#define UNMAPPED ((uintptr_t)0x10)

static char   kept[LINES][LINE_BYTES];
static size_t kept_count;
static size_t longest;

static void keep(const char *format, va_list arguments)
{
    char   line[4096];
    size_t length;

    (void)text_vformat(line, sizeof line, format, arguments);
    length = strlen(line);
    if (length > longest) {
        longest = length;
    }
    if (kept_count < LINES) {
        memcpy(kept[kept_count], line, length < LINE_BYTES ? length + 1u : LINE_BYTES);
        kept[kept_count][LINE_BYTES - 1u] = '\0';
    }
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

static void forget_the_log(void)
{
    kept_count = 0u;
    longest    = 0u;
}

static const char *line_starting(const char *start)
{
    size_t i;

    for (i = 0; i < kept_count && i < LINES; ++i) {
        if (strncmp(kept[i], start, strlen(start)) == 0) {
            return kept[i];
        }
    }
    return NULL;
}

static bool line_has(const char *start, const char *part)
{
    const char *line = line_starting(start);

    return line != NULL && strstr(line, part) != NULL;
}

/* Forty places that ask, each its own function, and twenty that refuse before the touch. Each
 * reads its own address, or the linker would fold forty identical functions into one. */
#define ASKER(n) static void ask_##n(void) \
    { \
        uint32_t word = 0; \
        (void)memory_read_u32(UNMAPPED + 4u * (n), &word); \
    }
#define REFUSER(n) static void refuse_##n(void) \
    { \
        uint32_t word = 0; \
        (void)memory_try_read_u32(UNMAPPED + 4u * (n), &word); \
    }

ASKER(0) ASKER(1) ASKER(2) ASKER(3) ASKER(4) ASKER(5) ASKER(6) ASKER(7) ASKER(8) ASKER(9)
ASKER(10) ASKER(11) ASKER(12) ASKER(13) ASKER(14) ASKER(15) ASKER(16) ASKER(17) ASKER(18)
ASKER(19) ASKER(20) ASKER(21) ASKER(22) ASKER(23) ASKER(24) ASKER(25) ASKER(26) ASKER(27)
ASKER(28) ASKER(29) ASKER(30) ASKER(31) ASKER(32) ASKER(33) ASKER(34) ASKER(35) ASKER(36)
ASKER(37) ASKER(38) ASKER(39)
REFUSER(0) REFUSER(1) REFUSER(2) REFUSER(3) REFUSER(4) REFUSER(5) REFUSER(6) REFUSER(7)
REFUSER(8) REFUSER(9) REFUSER(10) REFUSER(11) REFUSER(12) REFUSER(13) REFUSER(14) REFUSER(15)
REFUSER(16) REFUSER(17) REFUSER(18) REFUSER(19)

static void (*const askers[])(void) = {
    ask_0, ask_1, ask_2, ask_3, ask_4, ask_5, ask_6, ask_7, ask_8, ask_9, ask_10, ask_11, ask_12,
    ask_13, ask_14, ask_15, ask_16, ask_17, ask_18, ask_19, ask_20, ask_21, ask_22, ask_23,
    ask_24, ask_25, ask_26, ask_27, ask_28, ask_29, ask_30, ask_31, ask_32, ask_33, ask_34,
    ask_35, ask_36, ask_37, ask_38, ask_39
};

static void (*const refusers[])(void) = {
    refuse_0, refuse_1, refuse_2, refuse_3, refuse_4, refuse_5, refuse_6, refuse_7, refuse_8,
    refuse_9, refuse_10, refuse_11, refuse_12, refuse_13, refuse_14, refuse_15, refuse_16,
    refuse_17, refuse_18, refuse_19
};

static const unsigned char source[4] = { 1, 2, 3, 4 };

static void check_arming(void)
{
    ut_section("arming, and the self-test");
    forget_the_log();
    mp_memory_watch_arm(false, true);
    ut_check(kept_count == 0u, "a multiplayer switched off arms nothing and tests nothing");

    mp_memory_watch_arm(true, true);
    ut_check(line_has("the guarded read's self-test: a read across a page nobody holds was "
                      "refused, 1 fault(s)", "(must be 1), 1 turned back by the guard (must be 1)"),
             "arming runs the self-test, and it is one fault for one call, "
             "turned back by the guard");
    ut_check(kept_count == 1u, "and says so in one line");

    forget_the_log();
    mp_memory_watch_arm(true, true);
    ut_check(kept_count == 0u, "a second arming does not run it again");
}

static void check_one_level(void)
{
    SYSTEM_INFO info;
    uint8_t    *page;
    uint32_t    word = 0;
    int         i;

    ut_section("one level's report");
    GetSystemInfo(&info);
    page = (uint8_t *)VirtualAlloc(NULL, info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    for (i = 0; i < 2; ++i) {
        (void)memory_read_u32((uintptr_t)source, &word);
    }
    (void)memory_read_u32(UNMAPPED, &word);
    ut_check(page != NULL && !memory_try_read_u32((uintptr_t)page, &word),
             "a no access page is refused (prerequisite)");

    forget_the_log();
    mp_memory_watch_report();
    ut_check(line_has("the reads that asked the system first, in this level: 3 call(s)",
                      "1 refused"),
             "the asking side: three calls, one refused");
    ut_check(line_has("the reads that asked the system first, the busiest callers: ",
                      "x2 (0 refused)") &&
                 line_has("the reads that asked the system first, the busiest callers: ",
                          "x1 (1 refused)"),
             "named by caller, the two of the loop on one row");
    ut_check(line_has("the reads that asked the system first, minute by minute, calls/ns each/KB "
                      "behind the address: 1 3/", ""),
             "and all three in the first minute");
    ut_check(line_has("the guarded reads and writes that faulted, in this level: 1 read(s), "
                      "0 probe(s), 0 write(s)", "read fault (last at"),
             "the one fault, by kind and caller, with the address it was handed");
    ut_check(line_starting("the census read both ways once a second: 0 actor(s) compared, 0 "
                           "that differed (must be 0)") != NULL,
             "the double probe prints at nought, with no census to read");
    ut_check(line_starting("the far bodies' windows: 0 cell write(s) and 0 block write(s)") != NULL,
             "the windows' line prints at nought");
    ut_check(line_starting("the chains walked by hand that did not finish: 0 walk(s)") != NULL,
             "and the chains' line");

    forget_the_log();
    mp_memory_watch_report();
    ut_check(line_has("the reads that asked the system first, in this level: 0 call(s)", "") &&
                 line_has("the guarded reads and writes that faulted, in this level: 0 read(s)",
                          "none"),
             "the next level's report starts from nought");
    if (page != NULL) {
        (void)VirtualFree(page, 0, MEM_RELEASE);
    }
}

static void check_full_tables(void)
{
    size_t i;

    ut_section("every table full, and no line past the log's room");
    for (i = 0; i < sizeof askers / sizeof askers[0]; ++i) {
        askers[i]();
    }
    for (i = 0; i < sizeof refusers / sizeof refusers[0]; ++i) {
        refusers[i]();
    }
    forget_the_log();
    mp_memory_watch_report();
    ut_check(line_has("the reads that asked the system first, the busiest callers: ",
                      "call(s) from callers past the table"),
             "the callers past the table are summed, not dropped");
    ut_check(line_has("the guarded reads and writes that faulted, in this level: 0 read(s), "
                      "0 probe(s), 0 write(s), 20 refused outside",
                      "x1 read outside (last at 000000"),
             "twenty refusals before the touch, filed by caller");
    ut_checkf(longest <= LOG_ROOM, "the longest line is %u characters, inside the log's %u",
              (unsigned)longest, (unsigned)LOG_ROOM);
}

/* In a field run the table was full of the menu's and the load's callers before the first level
 * began, and every caller of the level itself was summed past it. */
static void check_a_level_begins(void)
{
    size_t   i;
    uint32_t word = 0;

    ut_section("a level's beginning starts the asking side over and keeps the faults");
    for (i = 0; i < sizeof askers / sizeof askers[0]; ++i) {
        askers[i]();
    }
    refusers[0]();
    mp_memory_watch_level_begins();
    (void)memory_read_u32((uintptr_t)source, &word);
    forget_the_log();
    mp_memory_watch_report();
    ut_check(line_has("the reads that asked the system first, in this level: 1 call(s)", ""),
             "only the level's own read is counted");
    ut_check(line_has("the reads that asked the system first, the busiest callers: ",
                      "x1 (0 refused)") &&
                 !line_has("the reads that asked the system first, the busiest callers: ",
                           "past the table"),
             "and its caller has a row: the forty of the load are gone");
    ut_check(line_has("the guarded reads and writes that faulted, in this level: 0 read(s), "
                      "0 probe(s), 0 write(s), 1 refused outside", ""),
             "the refusal from before the level is still in the level's report");
}

static void check_switched_off_measuring(void)
{
    ut_section("the stopwatch off");
    mp_memory_watch_arm(true, false);
    forget_the_log();
    mp_memory_watch_report();
    ut_check(line_starting("the reads that asked the system first and the census read both "
                           "ways: not measured in this run") != NULL,
             "a run without the stopwatch says it did not measure, rather than printing nought");
    ut_check(line_starting("the reads that asked the system first, in this level") == NULL,
             "and prints no count it did not take");
    ut_check(line_starting("the guarded reads and writes that faulted") != NULL,
             "the faults are counted all the same");
}

int main(void)
{
    check_arming();
    check_one_level();
    check_full_tables();
    check_a_level_begins();
    check_switched_off_measuring();
    return ut_summary("the guarded reads and writes in the report");
}
