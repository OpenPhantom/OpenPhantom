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
#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CRASH_SECTION      "crash_report"
#define MAX_REPORTS        4       /* full reports; more than four say nothing new */
/* Distinct first-chance access violations named in the log. One compact line each rather than a
 * report, drawn from a budget of their own so they cannot starve the four above. */
#define MAX_AV_SITES       8
#define STACK_SCAN_BYTES   3072    /* about 768 pointers; deeper only gets blurrier */
/* On a stack overflow the sweep is kept but shortened: the repeating pattern IS the bug, so a
 * few frames already show it, and every line logged costs a kilobyte of the single page that
 * is left to work with. */
#define STACK_SCAN_BYTES_OVERFLOW 512
#define MAX_FRAMES_SHOWN   24
/* Frames from OUTSIDE the engine, which is to say from this project or from something it
 * called. Fewer than the engine budget because they are the interesting ones and a report
 * that lists forty of them buries the two that matter. */
#define MAX_MODULE_FRAMES  12
#define BYTES_BEFORE_EIP   16
#define BYTES_AROUND_EIP   32

typedef struct crash_report_state {
    bool                            installed;
    LONG                            reports;
    LONG                            busy;    /* held across write_report, see its own note */
    LONG                            av_total;      /* every first-chance AV, new or repeated */
    unsigned                        av_site_count;
    struct {
        uint32_t eip;
        uint32_t fault;
        unsigned hits;
    }                               av_sites[MAX_AV_SITES];
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
/* The allocation base of whatever an address belongs to, or 0 when it is not in committed
 * executable memory.
 *
 * VirtualQuery AND NOTHING ELSE, which is the whole design of this. Naming the module would
 * mean GetModuleFileName, and that takes the loader lock: the one call in this file that is
 * deliberately held back until the registers are already written, because a crash inside the
 * loader would deadlock on it. A base needs no lock, and it costs nothing to resolve later,
 * because the loader writes the mapping into this same log at startup:
 *
 *     [loader] mod framerate_fix.dll     loaded at 6E1D0000, calling engine_fix_install
 *
 * So a base in the sweep becomes a name by looking up the file, and the report takes no risk
 * to earn it.
 *
 * WHY THIS EXISTS AT ALL. The sweep used to recognise addresses in WMAIN and in nothing else,
 * so the moment a fault came from one of this project DLLs the report went quiet exactly where
 * it mattered. A real one printed the engine frame loop and then stopped, with no return
 * address beneath it, and finding the DLL responsible took five rounds of disabling things by
 * hand. The information was on the stack the whole time and the reporter would not print it.
 *
 * The one-entry cache is not a micro-optimisation: this runs up to 768 times per report and a
 * stack carries long runs from the same region. Both outcomes are cached, because the common
 * case is ordinary stack data that is not executable at all. The cache is three plain words
 * and a second thread faulting at the same instant can read a mismatched trio; the cost of
 * that is one wrong base on one line, not a fault, since nothing here dereferences it. */
static uintptr_t executable_allocation_base(uintptr_t address)
{
    static uintptr_t cached_low, cached_high, cached_base;
    MEMORY_BASIC_INFORMATION info;
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    if (address < 0x10000u) {
        return 0;                    /* the null page and small integers, most of the noise */
    }
    if (cached_high != 0 && address >= cached_low && address < cached_high) {
        return cached_base;
    }
    if (VirtualQuery((LPCVOID)address, &info, sizeof(info)) != sizeof(info)) {
        return 0;                    /* no region to remember, so nothing is cached */
    }
    cached_low  = (uintptr_t)info.BaseAddress;
    cached_high = cached_low + info.RegionSize;
    cached_base = (info.State == MEM_COMMIT && (info.Protect & executable) != 0)
                ? (uintptr_t)info.AllocationBase : 0;
    return cached_base;
}

static void report_engine_frames(uintptr_t stack_pointer, unsigned scan_bytes)
{
    const uint32_t *slot = (const uint32_t *)stack_pointer;
    uintptr_t       text_start = host_image_text();
    uintptr_t       text_end   = text_start + host_image_text_size();
    uintptr_t       own_base = executable_allocation_base((uintptr_t)&report_engine_frames);
    /* THE SWEEP MUST STOP AT THE TOP OF THE STACK. Above NtTib.StackBase is not this thread's
     * stack and never was, so anything found there is not a frame, however much it looks like one.
     * The first report that carried the stack extent showed six entries past the top being printed
     * as frames, and two of them had already been used to build a wrong theory about which feature
     * was at fault. A sweep that reads past its own bounds does not merely add noise, it invents
     * evidence. */
    uintptr_t       stack_top = (uintptr_t)__readfsdword(0x04);
    uintptr_t       bases[MAX_MODULE_FRAMES];
    unsigned        base_count = 0;
    unsigned        index;
    unsigned        shown = 0;
    unsigned        elsewhere = 0;

    log_info("--- stack sweep, .text %08X..%08X (lowest offset = youngest frame) ---",
             (unsigned)text_start, (unsigned)text_end);
    log_info("  a frame outside the engine is named by the base of the module holding it; the "
             "loader lines at the top of this file say which DLL each base is");

    for (index = 0; index < scan_bytes / 4 &&
                    (shown < MAX_FRAMES_SHOWN || elsewhere < MAX_MODULE_FRAMES); ++index) {
        uint32_t value;

        if (stack_top != 0 && (uintptr_t)&slot[index] >= stack_top) {
            log_info("  (top of the stack at %08X, %u bytes swept)",
                     (unsigned)stack_top, index * 4);
            break;
        }
        if (!memory_read_u32((uintptr_t)&slot[index], &value)) {
            break;
        }
        if (value >= text_start && value < text_end) {
            /* A return address always sits behind a call. We do not verify that (E8 and FF are
             * different lengths) and therefore say so: these are candidates, not certainties. */
            if (shown < MAX_FRAMES_SHOWN) {
                log_info("  esp+%04X   %08X   engine", index * 4, (unsigned)value);
                ++shown;
            }
        } else if (elsewhere < MAX_MODULE_FRAMES) {
            uintptr_t base = executable_allocation_base((uintptr_t)value);

            if (base != 0) {
                unsigned seen;

                /* The offset is what a map file or a disassembler wants, so it is printed
                 * rather than left to be worked out from two numbers on one line. */
                log_info("  esp+%04X   %08X   module %08X + %X%s", index * 4, (unsigned)value,
                         (unsigned)base, (unsigned)((uintptr_t)value - base),
                         (base == own_base) ? "   <- this reporter" : "");
                ++elsewhere;

                for (seen = 0; seen < base_count; ++seen) {
                    if (bases[seen] == base) {
                        break;
                    }
                }
                if (seen == base_count && base_count < MAX_MODULE_FRAMES) {
                    bases[base_count++] = base;
                }
            }
        }
    }

    if (shown == 0 && elsewhere == 0) {
        log_info("  (nothing executable on the stack at all)");
    } else if (shown == 0) {
        log_info("  (nothing from the ENGINE on the stack: the crash did not come through it, "
                 "which is itself the finding)");
    }

    /* THE LEGEND IS LAST, AND THAT IS THE POINT. Naming a module means GetModuleFileName and
     * the loader lock, so it goes after every address is already in the file: if this deadlocks
     * the report is still complete and only the names are missing. Resolving distinct bases
     * rather than every frame keeps it to a handful of calls whatever the sweep found.
     *
     * Without it a base is only resolvable for the DLLs the loader announced at startup, and a
     * crash whose frames are all in system modules reads as a dead end. The first real one was
     * exactly that. */
    if (base_count > 0) {
        unsigned seen;

        log_info("--- the modules those bases belong to ---");
        for (seen = 0; seen < base_count; ++seen) {
            static char where[MAX_PATH + 32];

            describe_module(bases[seen], where, sizeof(where));
            log_info("  %08X   %s", (unsigned)bases[seen], where);
        }
    }
}

/* ==============================================================================================
 * WHY A FIRST-CHANCE ACCESS VIOLATION IS NOT A CRASH REPORT.
 *
 * This project reads engine memory through the guarded readers in common/memory.c, and those are
 * SEH: memory_try_read wraps a memcpy in __try and __except and answers false when it faults.
 * Forty seven call sites use them, on per object and per frame paths, because that is what
 * CONTRIBUTING asks for. Every one of those probes that touches an unmapped page raises a real
 * access violation.
 *
 * A vectored handler installed first sees all of them, ahead of the __except that is about to
 * swallow them. Filtering by exception code does not help, because the code is exactly the code a
 * real crash carries. So this was writing a full CRASH block for routine probe faults, and at four
 * reports the budget was gone: four recovered probes and the reporter was finished, silent for the
 * crash it exists to catch. It did not merely add noise to the log, it spent itself on noise.
 *
 * Identifying the probe is not available from in here. common/ is a static library, so each of the
 * twenty one DLLs carries its own copy of memory_try_read, and a handler living in this one would
 * need every one of their address ranges; a thread local depth counter has the same problem from
 * the other end. What IS available is that a probe can raise only an access violation. A memcpy
 * cannot raise an illegal instruction, a divide by zero or a privileged instruction, so every
 * other fatal code still gets its report at first chance, exactly as it did before.
 *
 * Access violations therefore get one compact line per distinct site and a count for the rest,
 * and the full report for one comes from the unhandled filter, which runs only when nothing else
 * took it. The line is deliberately cheap: no module lookup, because that takes the loader lock
 * and this runs often.
 *
 * WHAT THIS COSTS. An access violation that something further out swallows, while the process then
 * hangs rather than dying, is now one line instead of a report. That line still names the faulting
 * address, the address it touched and what it was doing, which is the part worth having; the
 * registers and the stack sweep are what is given up. That is the right way round: a report never
 * written because four probes spent the budget is worth less than a line that always is.
 * ============================================================================================ */
/* THE TEST THAT SEPARATES A PROBE FROM A CRASH, and it was missing from the first version of
 * this: a guarded reader faults INSIDE its own memcpy, so the instruction pointer is in code
 * that is mapped and executable and only the address it touched is bad. Execution that has left
 * the rails has an instruction pointer that is itself nowhere: 00000001, FFFFFFFF, a freed page.
 *
 * Without this, a real crash announced itself as three routine probe faults and then a report,
 * and the three lines said in plain words that the guarded readers had provoked them on purpose.
 * That was a claim the code had no way to support, and it was worse than saying nothing, because
 * the reader who most needs those three addresses is the one being told to ignore them. */
static bool eip_is_in_executable_memory(uintptr_t address)
{
    return executable_allocation_base(address) != 0;
}

static void note_first_chance_av(const EXCEPTION_RECORD *record, const CONTEXT *context)
{
    uint32_t eip   = (uint32_t)context->Eip;
    uint32_t fault = (record->NumberParameters >= 2)
                   ? (uint32_t)record->ExceptionInformation[1] : 0u;
    unsigned index;

    /* The same latch write_report holds, so the table cannot be raced and a note cannot land in
     * the middle of a report. Dropping the note while a report is in progress is correct: the
     * report is the more important of the two and it says what it is reporting. */
    if (InterlockedCompareExchange(&crash_state.busy, 1, 0) != 0) {
        return;
    }

    crash_state.av_total++;

    for (index = 0; index < crash_state.av_site_count; ++index) {
        if (crash_state.av_sites[index].eip == eip &&
            crash_state.av_sites[index].fault == fault) {
            crash_state.av_sites[index].hits++;   /* seen before: counted, not printed again */
            InterlockedExchange(&crash_state.busy, 0);
            return;
        }
    }

    if (crash_state.av_site_count < MAX_AV_SITES) {
        const char *operation = (record->NumberParameters < 2) ? "?"
                              : (record->ExceptionInformation[0] == 0) ? "READ"
                              : (record->ExceptionInformation[0] == 1) ? "WRITE" : "EXECUTE";

        crash_state.av_sites[crash_state.av_site_count].eip   = eip;
        crash_state.av_sites[crash_state.av_site_count].fault = fault;
        crash_state.av_sites[crash_state.av_site_count].hits  = 1;
        crash_state.av_site_count++;

        /* What was seen, and nothing about why. The guarded readers are the usual source and
         * they recover, but this cannot tell one of theirs from an access violation something
         * else swallowed, so it does not pretend to. */
        log_info("first-chance AV at %08X, %s %08X, in executable memory and not fatal by "
                 "itself. Named once, then counted.",
                 (unsigned)eip, operation, (unsigned)fault);
    }

    InterlockedExchange(&crash_state.busy, 0);
}

/* Written into a real report, so the recovered faults leading up to a crash sit beside it rather
 * than being the reason there is no report at all. */
static void report_first_chance_summary(void)
{
    unsigned index;

    if (crash_state.av_total == 0) {
        return;
    }
    log_info("--- %ld first-chance access violations before this, %u distinct ---",
             (long)crash_state.av_total, crash_state.av_site_count);
    for (index = 0; index < crash_state.av_site_count; ++index) {
        log_info("  %08X touched %08X, %u time(s)",
                 (unsigned)crash_state.av_sites[index].eip,
                 (unsigned)crash_state.av_sites[index].fault,
                 crash_state.av_sites[index].hits);
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

    /* HOW MUCH STACK WAS LEFT, because "we ran out of stack" is a common explanation for a
     * wild instruction pointer and it should be answerable from the report rather than argued
     * about. The two words come from this thread's own TEB, which is what fs addresses on x86:
     * NtTib.StackBase at +4 is the high end, NtTib.StackLimit at +8 is the lowest page that is
     * currently committed. The handler runs on the faulting thread, so these are its numbers.
     *
     * USED is how deep the call chain had gone. FREE is what remained before the guard page,
     * and a figure in the tens or hundreds of bytes is the signature of an overflow; a figure in
     * the tens of kilobytes rules it out and points the investigation elsewhere. Both are worth
     * having in every report: the first crash this was written for had an entire call chain in
     * the graphics driver, where a deep stack is exactly what one would suspect. */
    {
        uintptr_t base  = (uintptr_t)__readfsdword(0x04);
        uintptr_t limit = (uintptr_t)__readfsdword(0x08);
        uintptr_t esp   = (uintptr_t)context->Esp;

        log_info("stack %08X..%08X, %u bytes used, %u free below esp",
                 (unsigned)limit, (unsigned)base,
                 (unsigned)((base > esp) ? (base - esp) : 0u),
                 (unsigned)((esp > limit) ? (esp - limit) : 0u));
    }
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

    report_first_chance_summary();

    log_info("WMAIN .text %08X + %08X",
             (unsigned)host_image_text(), (unsigned)host_image_text_size());
    log_info("################ END ################");

    InterlockedExchange(&crash_state.busy, 0);
}

static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *pointers)
{
    if (pointers == NULL || pointers->ExceptionRecord == NULL ||
        pointers->ContextRecord == NULL ||
        !is_fatal(pointers->ExceptionRecord->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;          /* bend nothing */
    }

    if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        eip_is_in_executable_memory((uintptr_t)pointers->ContextRecord->Eip)) {
        /* The one code the guarded readers can raise, AND faulting from somewhere a guarded
         * reader could actually be running. An access violation whose instruction pointer is
         * not in executable memory is control flow that has already left the rails, and it gets
         * the full report at first chance like any other fatal code. See the essay above. */
        note_first_chance_av(pointers->ExceptionRecord, pointers->ContextRecord);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    write_report("first hand", pointers->ExceptionRecord, pointers->ContextRecord);
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
