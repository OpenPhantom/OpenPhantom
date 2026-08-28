/* crash_report.c: exception code, address, module, registers and the engine frames on the stack.
 *
 * ==============================================================================================
 * WHY THIS EXISTS
 *
 * Three sessions in a row the log ended at the same place after a level load, with not one line
 * about what went wrong. Once the process hung inside a graphics wrapper's cleanup; once it died
 * hard, no DLL_PROCESS_DETACH, no wrapper goodbye, nothing. Without a debugger and without a
 * dump, a hard crash is silent.
 *
 * Two hands, because one is not enough
 *   1. A vectored handler sees the exception FIRST, before any SEH frame, including the wrapper's.
 *      It therefore fires even when something further out swallows the exception afterwards.
 *   2. SetUnhandledExceptionFilter catches the case where the vectored handler did not run.
 *      The last installer wins: a wrapper that loads after us replaces our filter. That is why
 *      the vectored handler is the important one of the two, not the other way round.
 *
 * WE CHANGE NOTHING. Both paths return CONTINUE_SEARCH / hand on to the previous filter, so the
 * crash unfolds exactly as it would without us. A reporter that bends the control flow reports on
 * a different program than the one that crashed.
 *
 * First hand also means false alarms. Some libraries use SEH for control flow, and a C++ throw
 * (0xE06D7363) or a breakpoint is not a crash. So we filter by code and cap the number of
 * reports, a log that fills up with harmless exceptions hides the one entry that matters.
 */
#include "crash_report.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CRASH_SECTION      "crash_report"
#define MAX_REPORTS        4       /* more than four reports say nothing new */
#define STACK_SCAN_BYTES   3072    /* about 768 pointers; deeper only gets blurrier */
/* On a stack overflow the sweep is kept but shortened: the repeating pattern IS the bug, so a
 * few frames already show it, and every line logged costs a kilobyte of the single page that
 * is left to work with. */
#define STACK_SCAN_BYTES_OVERFLOW 512
#define MAX_FRAMES_SHOWN   24
#define BYTES_BEFORE_EIP   16
#define BYTES_AROUND_EIP   32

typedef struct crash_report_state {
    bool                            installed;
    LONG                            reports;
    LONG                            busy;    /* held across write_report, see its own note */
    PVOID                           vectored_handler;
    LPTOP_LEVEL_EXCEPTION_FILTER    previous_filter;
} crash_report_state_t;

static crash_report_state_t crash_state;

/* This table defines BOTH the printed name and what counts as fatal: is_fatal() below asks it.
 * A code that belongs in the log but not in a crash report must therefore not be added here. */
static const char *fatal_exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE";
    default:                                 return "?";
    }
}

/* Breakpoints, C++ throws (0xE06D7363), the thread-naming exception (0x406D1388) and everything
 * unknown stay out. */
static bool is_fatal(DWORD code)
{
    return fatal_exception_name(code)[0] != '?';
}

static void describe_module(uintptr_t address, char *out, size_t size)
{
    HMODULE  module = NULL;
    /* Static, not automatic. write_report holds a guard that makes this single threaded for
     * its whole body, so there is nothing here to race, and 260 bytes kept off the stack are
     * 260 bytes that still exist on the one path where the stack is what ran out. */
    static char path[MAX_PATH];
    char    *file_name;

    out[0] = '\0';

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)address, &module) || module == NULL) {
        _snprintf(out, size, "(no module)");
        out[size - 1] = '\0';
        return;
    }

    if (GetModuleFileNameA(module, path, MAX_PATH) == 0) {
        _snprintf(out, size, "(unnamed) +%X", (unsigned)(address - (uintptr_t)module));
        out[size - 1] = '\0';
        return;
    }
    path[MAX_PATH - 1] = '\0';

    file_name = strrchr(path, '\\');
    _snprintf(out, size, "%s+%X", (file_name != NULL) ? file_name + 1 : path,
              (unsigned)(address - (uintptr_t)module));
    out[size - 1] = '\0';
}

static void report_faulting_bytes(uintptr_t instruction_pointer)
{
    char           line[160];
    const uint8_t *bytes;
    int            written = 0;
    unsigned       index;

    if (instruction_pointer < host_image_text() ||
        instruction_pointer >= host_image_text() + host_image_text_size()) {
        return;
    }
    if (!memory_is_readable_range(instruction_pointer - BYTES_BEFORE_EIP, BYTES_AROUND_EIP)) {
        return;
    }

    bytes = (const uint8_t *)(instruction_pointer - BYTES_BEFORE_EIP);
    for (index = 0; index < BYTES_AROUND_EIP && written < (int)sizeof(line) - 4; ++index) {
        /* _snprintf returns NEGATIVE on truncation rather than the length it wanted. Adding
         * that to `written` walks the next write off the FRONT of the buffer, and the loop
         * guard does not catch it, because a negative is still below the limit. It cannot
         * happen at the constants above, where 32 bytes need 98 of 160, and that is exactly
         * why it would go unnoticed by whoever widens BYTES_AROUND_EIP later, inside the one
         * function here that only ever runs when something has already gone wrong. */
        int chunk = _snprintf(line + written, sizeof(line) - (size_t)written, "%02X%s",
                              bytes[index], (index == BYTES_BEFORE_EIP - 1) ? " | " : " ");
        if (chunk < 0) {
            break;
        }
        written += chunk;
    }
    line[sizeof(line) - 1] = '\0';

    log_info("bytes eip-%u..+%u: %s", BYTES_BEFORE_EIP, BYTES_AROUND_EIP - BYTES_BEFORE_EIP - 1,
             line);
}

/* Deliberately NOT a real stack walk: that would need the unwind data of a 1999 MSVC build, which
 * does not exist. We sweep the raw stack for values that land in WMAIN's .text. That yields
 * candidates for the call chain, stale ones included, which is exactly why the stack offset is
 * printed with each: the LOWEST offsets are the youngest frames and the most believable. */
static void report_engine_frames(uintptr_t stack_pointer, unsigned scan_bytes)
{
    const uint32_t *slot = (const uint32_t *)stack_pointer;
    uintptr_t       text_start = host_image_text();
    uintptr_t       text_end   = text_start + host_image_text_size();
    unsigned        index;
    unsigned        shown = 0;

    log_info("--- stack sweep, .text %08X..%08X (lowest offset = youngest frame) ---",
             (unsigned)text_start, (unsigned)text_end);

    for (index = 0; index < scan_bytes / 4 && shown < MAX_FRAMES_SHOWN; ++index) {
        uint32_t value;

        if (!memory_read_u32((uintptr_t)&slot[index], &value)) {
            break;
        }
        if (value >= text_start && value < text_end) {
            /* A return address always sits behind a call. We do not verify that (E8 and FF are
             * different lengths) and therefore say so: these are candidates, not certainties. */
            log_info("  esp+%04X   %08X", index * 4, (unsigned)value);
            ++shown;
        }
    }

    if (shown == 0) {
        log_info("  (nothing from the engine on the stack, the crash did not come through it)");
    }
}

static void write_report(const char *how, EXCEPTION_RECORD *record, CONTEXT *context)
{
    /* Static, for the same reason describe_module's own buffer is: the guard below makes this
     * function single threaded for the whole of its body. */
    static char where[MAX_PATH + 32];
    bool        overflow;

    /* A CRASH HANDLER THAT FAULTS CALLS ITSELF. The vectored handler sees the second exception
     * exactly as it saw the first, so without this the reporter recurses, each time on a shorter
     * stack, until something else kills the process and the log ends mid line. The report counter
     * below does not prevent that on its own, it only bounds how often it happens.
     *
     * InterlockedCompareExchange rather than a plain flag, because two threads can fault in the
     * same instant. The loser is dropped rather than made to wait, which is the right way round:
     * the first crash is the one worth reading and the second is usually a consequence of it. */
    if (InterlockedCompareExchange(&crash_state.busy, 1, 0) != 0) {
        return;
    }

    if (InterlockedIncrement(&crash_state.reports) > MAX_REPORTS) {
        InterlockedExchange(&crash_state.busy, 0);
        return;
    }

    overflow = (record->ExceptionCode == EXCEPTION_STACK_OVERFLOW);

    /* THE CODE, THE ADDRESS AND THE REGISTERS GO OUT BEFORE THE MODULE IS NAMED, and that order
     * is the whole point of this paragraph. GetModuleHandleEx and GetModuleFileName both take the
     * loader lock, and one of the three crashes this reporter was written for hung inside a
     * graphics wrapper cleanup, which is to say inside the loader, holding it. Naming the module
     * first, as this used to, means that deadlock costs the entire report rather than one line of
     * it. Everything that can be had from the record and the context alone is therefore already
     * in the file by the time anything reaches for the lock. */
    log_info("################ CRASH (%s) ################", how);
    log_error("%s (%08lX) at %08X", fatal_exception_name(record->ExceptionCode),
              (unsigned long)record->ExceptionCode,
              (unsigned)(uintptr_t)record->ExceptionAddress);

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char *operation = (record->ExceptionInformation[0] == 0) ? "READ"
                              : (record->ExceptionInformation[0] == 1) ? "WRITE" : "EXECUTE";
        log_error("%s at %08X", operation, (unsigned)record->ExceptionInformation[1]);
    }

    log_info("eip=%08X esp=%08X ebp=%08X",
             (unsigned)context->Eip, (unsigned)context->Esp, (unsigned)context->Ebp);
    log_info("eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X",
             (unsigned)context->Eax, (unsigned)context->Ebx, (unsigned)context->Ecx,
             (unsigned)context->Edx, (unsigned)context->Esi, (unsigned)context->Edi);

    describe_module((uintptr_t)record->ExceptionAddress, where, sizeof(where));
    log_info("faulting module: %s", where);

    if (overflow) {
        /* The byte dump is skipped here, and not because it would fault: it is simply the least
         * useful part of this particular report, since on a stack overflow the faulting
         * instruction is whichever one happened to touch the guard page rather than the bug. What
         * it costs is another 160 byte buffer and another kilobyte log line on the single page
         * Windows leaves once it clears the guard, and the sweep is worth more than it is: in a
         * runaway recursion the repeating pattern of return addresses IS the answer. */
        report_engine_frames((uintptr_t)context->Esp, STACK_SCAN_BYTES_OVERFLOW);
    } else {
        report_faulting_bytes((uintptr_t)context->Eip);
        report_engine_frames((uintptr_t)context->Esp, STACK_SCAN_BYTES);
    }

    log_info("WMAIN .text %08X + %08X",
             (unsigned)host_image_text(), (unsigned)host_image_text_size());
    log_info("################ END ################");

    InterlockedExchange(&crash_state.busy, 0);
}

static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *pointers)
{
    if (pointers != NULL && pointers->ExceptionRecord != NULL && pointers->ContextRecord != NULL &&
        is_fatal(pointers->ExceptionRecord->ExceptionCode)) {
        write_report("first hand", pointers->ExceptionRecord, pointers->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;              /* bend nothing */
}

static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *pointers)
{
    if (pointers != NULL && pointers->ExceptionRecord != NULL && pointers->ContextRecord != NULL) {
        write_report("unhandled", pointers->ExceptionRecord, pointers->ContextRecord);
    }
    if (crash_state.previous_filter != NULL) {
        return crash_state.previous_filter(pointers);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void crash_report_install(void)
{
    host_image_resolve();
    log_init("crash_report", false);

    if (crash_state.installed) {
        return;
    }
    if (!ini_read_bool(CRASH_SECTION, "Enabled", true)) {
        log_info("Enabled=0, no crash reporter");
        return;
    }

    crash_state.vectored_handler = AddVectoredExceptionHandler(1 /* first */, vectored_handler);
    crash_state.previous_filter  = SetUnhandledExceptionFilter(unhandled_filter);
    crash_state.installed        = true;

    log_info("active (vectored=%s, previous filter %08X). On a hard crash the code, address, "
             "module, registers and the engine frames from the stack land in this file, up to "
             "%d reports.",
             (crash_state.vectored_handler != NULL) ? "yes" : "NO",
             (unsigned)(uintptr_t)crash_state.previous_filter, MAX_REPORTS);
}
