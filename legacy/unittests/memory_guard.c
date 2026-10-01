/* memory_guard.c: the guarded copy holds where a __try does not.
 *
 * The game runs with the IgnoreException compatibility fix, ACCESS_VIOLATION_READ:1: a vectored
 * handler at the end of the chain that resumes every read fault behind the instruction that
 * faulted. A __try around a memcpy then never runs its handler, and a read of a bad pointer reports
 * success: in the game the self-test's read across a page nobody holds was delivered with nought
 * faults.
 *
 * The stand-in here is weaker than the real fix, which knows the length of every instruction: it
 * skips only the loads the copies in this test compile to (8A and 8B with a register operand and
 * no displacement) and the stub's rep movsb, and passes anything else on. That is enough to fool
 * the old form exactly as the game did, which the read across a seam shows, and it is removed
 * again after it.
 *
 * The log is kept in memory: the test is the logger.
 */
#include "unittest.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/memory_guard.h"
#include "common/text.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static SYSTEM_INFO s_info;
static char        s_last_line[512];
static unsigned    s_lines;

static void keep(const char *format, va_list arguments)
{
    text_vformat(s_last_line, sizeof s_last_line, format, arguments);
    ++s_lines;
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

/* A page this process holds, and behind it one it only reserved. */
static uint8_t *two_pages(void)
{
    uint8_t *pages = (uint8_t *)VirtualAlloc(NULL, 2u * s_info.dwPageSize, MEM_RESERVE,
                                             PAGE_NOACCESS);

    if (pages == NULL ||
        VirtualAlloc(pages, s_info.dwPageSize, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        return NULL;
    }
    memset(pages, 0x5A, s_info.dwPageSize);
    return pages;
}

/* Only a read fault, only at one of the instructions named in the head of this file. */
static LONG CALLBACK resume_like_the_fix(EXCEPTION_POINTERS *pointers)
{
    const uint8_t *code;
    uint8_t        rm;

    if (pointers->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        pointers->ExceptionRecord->NumberParameters < 2u ||
        pointers->ExceptionRecord->ExceptionInformation[0] != 0u) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    code = (const uint8_t *)(uintptr_t)pointers->ContextRecord->Eip;
    rm   = (uint8_t)(code[1] & 7u);
    if ((code[0] == 0x8Bu || code[0] == 0x8Au) && (code[1] & 0xC0u) == 0u && rm != 4u && rm != 5u) {
        pointers->ContextRecord->Eip += 2u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code[0] == 0xF3u && code[1] == 0xA4u) {
        pointers->ContextRecord->Eip += 2u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* The form memory.c used before: a __try around a memcpy. The size is read through a volatile so
 * the compiler calls the copy rather than inlining one it might compile differently. */
static bool old_form_read(const void *source, void *destination)
{
    volatile size_t twelve = 12u;

    __try {
        memcpy(destination, source, twelve);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void check_the_stub(void)
{
    uint8_t        wrong[24];
    uint8_t        jump[5] = { 0xE9u, 0x10u, 0x00u, 0x00u, 0x00u };
    const uint8_t *followed;

    ut_section("the stub is the one this file was built to recognise");
    ut_check(memory_guard_ready() && memory_guard_state() == MEMORY_GUARD_READY,
             "the guard starts, which means its bytes matched and its handler is installed");
    memset(wrong, 0x90, sizeof wrong);
    ut_check(!memory_guard_stub_matches(wrong), "other bytes are not the stub");
    ut_check(!memory_guard_stub_matches(NULL), "and no bytes are not either");
    followed = memory_guard_follow_jump(jump);
    ut_check(followed == jump + 5 + 0x10, "a relative jump a linker put in front is followed");
    ut_check(memory_guard_follow_jump(wrong) == wrong, "anything else is taken as it is");
}

/* A module that does not count its refusals is every module but the multiplayer's. crash_report
 * no longer sees a read the guard turned back, so the first one is said here, once. */
static void check_the_first_turned_back_is_said_once(void)
{
    uint8_t *pages = two_pages();
    uint8_t  out[12];

    ut_section("a module that does not count its refusals says the first turned back, once");
    if (pages == NULL) {
        ut_check(false, "two pages to read across (prerequisite)");
        return;
    }
    memory_watch_arm(false, false);
    s_lines = 0u;
    ut_check(!memory_try_read((uintptr_t)(pages + s_info.dwPageSize - 4u), out, sizeof out),
             "the read across is refused");
    ut_check(s_lines == 1u &&
                 strncmp(s_last_line, "a guarded read was refused at a fault: called from ",
                         51u) == 0,
             "and says so in one line, with the caller and the address");
    ut_check(!memory_try_read((uintptr_t)(pages + s_info.dwPageSize - 4u), out, sizeof out) &&
                 s_lines == 1u,
             "a second one is refused without a line");
    (void)VirtualFree(pages, 0, MEM_RELEASE);
}

static void check_a_read_across_a_seam(void)
{
    const memory_watch_stats_t *stats = memory_watch_stats();
    uint8_t                    *pages = two_pages();
    uint8_t                     out[12];
    PVOID                       fix;
    uint32_t                    before;

    ut_section("a read across into a page nobody holds, with a fix that "
               "resumes read faults behind");
    if (pages == NULL) {
        ut_check(false, "two pages to read across (prerequisite)");
        return;
    }
    memory_watch_arm(false, true);
    memory_watch_reset();
    ut_check(!memory_try_read((uintptr_t)(pages + s_info.dwPageSize - 4u), out, sizeof out) &&
                 stats->faults[MEMORY_WATCH_READ] == 1u,
             "without the fix: refused, one fault");

    fix = AddVectoredExceptionHandler(0, resume_like_the_fix);
    ut_check(fix != NULL, "the stand-in for the fix is installed at the end of the chain");
    ut_check(old_form_read(pages + s_info.dwPageSize - 4u, out),
             "and the old form is fooled by it: its __try never sees the fault, the copy goes on "
             "and reports success, as it did in the game");
    before = memory_guard_redirected();
    ut_check(!memory_try_read((uintptr_t)(pages + s_info.dwPageSize - 4u), out, sizeof out),
             "the guarded read is refused all the same");
    ut_check(stats->faults[MEMORY_WATCH_READ] == 2u && memory_guard_redirected() == before + 1u,
             "with exactly one fault, turned back by the guard's own handler "
             "before the fix saw it");
    ut_check(!memory_try_readable((uintptr_t)(pages + s_info.dwPageSize), 4u),
             "and a probe of the page nobody holds is refused");
    if (fix != NULL) {
        (void)RemoveVectoredExceptionHandler(fix);
    }
    ut_check(memory_try_read((uintptr_t)pages, out, sizeof out) && out[0] == 0x5Au &&
                 out[11] == 0x5Au,
             "a read of memory that is there is copied whole");
    ut_check(memory_try_readable((uintptr_t)pages, s_info.dwPageSize), "and a probe of it says so");
    (void)VirtualFree(pages, 0, MEM_RELEASE);
}

static void check_a_guard_page(void)
{
    MEMORY_BASIC_INFORMATION information;
    uint8_t                 *page;

    ut_section("a guard page is refused and armed again");
    page = (uint8_t *)VirtualAlloc(NULL, s_info.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE | PAGE_GUARD);
    if (page == NULL) {
        ut_check(false, "a guard page (prerequisite)");
        return;
    }
    ut_check(!memory_try_readable((uintptr_t)page, 4u), "a probe of it is refused");
    ut_check(VirtualQuery(page, &information, sizeof information) == sizeof information &&
                 (information.Protect & PAGE_GUARD) != 0u,
             "and the page is a guard page again, so a stack guard would still work");
    (void)VirtualFree(page, 0, MEM_RELEASE);
}

static void check_a_write(void)
{
    uint8_t *page;
    uint32_t value = 7u;
    DWORD    previous;

    ut_section("a write into a page it may only read is refused, as before");
    page = (uint8_t *)VirtualAlloc(NULL, s_info.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (page == NULL || !VirtualProtect(page, s_info.dwPageSize, PAGE_READONLY, &previous)) {
        ut_check(false, "a read-only page (prerequisite)");
        return;
    }
    ut_check(!memory_try_write((uintptr_t)page, &value, sizeof value), "refused");
    (void)VirtualFree(page, 0, MEM_RELEASE);
}

int main(void)
{
    GetSystemInfo(&s_info);
    check_the_stub();
    check_the_first_turned_back_is_said_once();
    check_a_read_across_a_seam();
    check_a_guard_page();
    check_a_write();
    return ut_summary("memory_guard");
}
