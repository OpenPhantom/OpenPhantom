/* diag_write_watch.c: which instruction wrote these four bytes.
 *
 * WHY THIS EXISTS. Reading the disassembly outward from a field tells you which functions COULD
 * write it. It does not tell you which one does, and this project has twice spent a long time on a
 * mechanism that turned out not to be the one running. A hardware data breakpoint answers the
 * question directly: the processor stops on the instruction that performed the write, and the
 * address in the exception's own context is that instruction. There is no inference left in it.
 *
 * HOW IT IS DONE, and why not the obvious way. The debug registers are per thread. The obvious
 * approach, calling SetThreadContext on GetCurrentThread from inside the frame callback, is not
 * reliable: a thread setting its own context has no defined behaviour for the register state it is
 * currently running on, and in practice the write is silently dropped, which looks exactly like
 * "nothing ever writes this field". A short lived helper thread suspends the simulation thread,
 * writes the registers into a stopped context, and resumes it. That is what a debugger does.
 *
 * THE HANDLER DOES NO FILE WORK. It runs inside an exception on the simulation thread, so it
 * records into a small fixed buffer and returns. The frame callback drains that buffer afterwards.
 * Logging from inside the handler would put file IO between the faulting instruction and the
 * instruction after it, which changes the timing of the very thing being measured, and would
 * deadlock outright if the exception ever landed while the log's own lock was held.
 *
 * THIS OBSERVES AND DOES NOT CHANGE THE GAME. The handler reads the context, records, clears its
 * own status bit and continues execution. It never alters a register, a flag or a game field, and
 * the write that triggered it has already happened by the time it runs.
 *
 * LIMITS worth knowing before trusting a report:
 *
 *   A debugger attached to the game owns these registers. If one is attached the arm will appear
 *   to succeed and then be overwritten, so do not run this under a debugger and believe it.
 *
 *   Four bytes at an aligned address is the only shape used here. An unaligned address does not
 *   fail, it simply never fires, which is the most misleading outcome available, so it is refused
 *   up front instead.
 *
 *   The reported address is the instruction AFTER the one that wrote. A data breakpoint is a trap,
 *   raised once the write has retired, so the context's own instruction pointer has already moved
 *   on. Every report says "just before" for that reason.
 *
 *   The address is watched, not the object. If the object is freed and the memory reused, writes
 *   from something entirely unrelated will be reported. The caller disarms when the object it
 *   cared about may be gone.
 */
#include "diag_write_watch.h"

#include "debug_register.h"
#include "diag_log.h"

#include "common/logging.h"

#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WATCH_SLOT 0u

/* Enough to hold a burst without the handler ever needing to allocate or block. A report that
 * overflows says so rather than silently dropping the tail. */
#define WATCH_RECORD_MAX 64u

typedef struct watch_record {
    uint32_t instruction;   /* the address of the instruction that performed the write */
    uint32_t value;         /* the four bytes as they read immediately afterwards */
} watch_record_t;

typedef struct write_watch_state {
    bool            prepared;
    bool            armed;
    HANDLE          thread;              /* the simulation thread, duplicated to a real handle */
    PVOID           handler;
    uintptr_t       address;
    char            what[64];
    volatile LONG   count;               /* records written by the handler */
    uint32_t        last_value;          /* to record only writes that CHANGE the value */
    bool            have_last;
    volatile LONG   overflow;            /* changing writes the buffer could not hold */
    volatile LONG   unchanged;           /* writes that put the value back unchanged */
    watch_record_t  records[WATCH_RECORD_MAX];
    LONG            reported;            /* records already drained by the frame callback */
} write_watch_state_t;

static write_watch_state_t watch_state;

/* The handler. Runs on the simulation thread, inside the debug exception the write raised. */
static LONG CALLBACK on_debug_exception(EXCEPTION_POINTERS *info)
{
    LONG     index;
    uint32_t value;

    if (info == NULL || info->ExceptionRecord == NULL || info->ContextRecord == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->ExceptionCode != (DWORD)EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!watch_state.armed ||
        !debug_register_fired((uint32_t)info->ContextRecord->Dr6, WATCH_SLOT)) {
        /* Somebody else's debug exception, or a stale one. Leaving it alone matters: claiming it
           would swallow a single step belonging to a debugger or another observer. */
        return EXCEPTION_CONTINUE_SEARCH;
    }

    /* Only writes that CHANGE the value are recorded. Measured in the game, the field this was
       built for is rewritten about once per simulation step with the value it already held, and
       the handful of writes that actually move it are the whole point. Recording every write
       filled the buffer in two seconds and buried them. The comparison is on the raw bits rather
       than as a float so an unchanged NaN does not read as a change. */
    value = *(const volatile uint32_t *)watch_state.address;
    if (watch_state.have_last && value == watch_state.last_value) {
        InterlockedIncrement(&watch_state.unchanged);
    } else {
        watch_state.have_last = true;
        watch_state.last_value = value;
        index = InterlockedIncrement(&watch_state.count) - 1;
        if (index < (LONG)WATCH_RECORD_MAX) {
            watch_state.records[index].instruction = (uint32_t)info->ContextRecord->Eip;
            watch_state.records[index].value = value;
        } else {
            InterlockedIncrement(&watch_state.overflow);
        }
    }

    /* The processor never clears these. Leaving the bit set makes every later debug exception look
       like this same breakpoint. */
    info->ContextRecord->Dr6 = debug_register_acknowledge((uint32_t)info->ContextRecord->Dr6,
                                                          WATCH_SLOT);
    return EXCEPTION_CONTINUE_EXECUTION;
}

typedef struct apply_request {
    uintptr_t address;    /* 0 disarms */
    bool      ok;
} apply_request_t;

/* Runs on a helper thread so the simulation thread is stopped while its registers are written. */
static DWORD WINAPI apply_debug_registers(LPVOID parameter)
{
    apply_request_t *request = (apply_request_t *)parameter;
    CONTEXT          context;

    request->ok = false;
    if (watch_state.thread == NULL) {
        return 0;
    }
    if (SuspendThread(watch_state.thread) == (DWORD)-1) {
        return 0;
    }

    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(watch_state.thread, &context)) {
        if (request->address != 0u) {
            context.Dr0 = (DWORD)request->address;
            context.Dr7 = debug_register_arm((uint32_t)context.Dr7, WATCH_SLOT, DEBUG_WATCH_WRITE,
                                             DEBUG_LENGTH_4);
        } else {
            context.Dr0 = 0;
            context.Dr7 = debug_register_disarm((uint32_t)context.Dr7, WATCH_SLOT);
        }
        context.Dr6 = 0;
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        request->ok = SetThreadContext(watch_state.thread, &context) ? true : false;
    }

    ResumeThread(watch_state.thread);
    return 0;
}

static bool apply(uintptr_t address)
{
    apply_request_t request;
    HANDLE          helper;

    request.address = address;
    request.ok = false;

    helper = CreateThread(NULL, 0, apply_debug_registers, &request, 0, NULL);
    if (helper == NULL) {
        log_error("write watch: could not start the helper thread that writes the debug registers");
        return false;
    }
    /* The helper suspends this thread, so waiting for it here is not a deadlock: the suspension is
       lifted by the helper itself before it returns. A bounded wait rather than an infinite one so
       a failure cannot hang the game. */
    if (WaitForSingleObject(helper, 5000) != WAIT_OBJECT_0) {
        log_error("write watch: the debug register helper did not finish");
        CloseHandle(helper);
        return false;
    }
    CloseHandle(helper);
    return request.ok;
}

bool diag_write_watch_prepare(void)
{
    if (watch_state.prepared) {
        return true;
    }
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &watch_state.thread,
                         THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                         0)) {
        log_error("write watch: could not take a handle to the simulation thread");
        return false;
    }
    /* First in the chain, so a write is reported before any other handler can claim the
       exception. */
    watch_state.handler = AddVectoredExceptionHandler(1, on_debug_exception);
    if (watch_state.handler == NULL) {
        log_error("write watch: could not install the exception handler");
        CloseHandle(watch_state.thread);
        watch_state.thread = NULL;
        return false;
    }
    watch_state.prepared = true;
    return true;
}

bool diag_write_watch_arm(uintptr_t address, const char *what)
{
    if (!watch_state.prepared) {
        return false;
    }
    if (!debug_register_address_is_aligned(address, DEBUG_LENGTH_4)) {
        log_warning("write watch: %08X is not four byte aligned, so a watch on it would never "
                    "fire. Refused rather than armed", (unsigned)address);
        return false;
    }

    watch_state.armed = false;
    watch_state.address = address;
    watch_state.count = 0;
    watch_state.overflow = 0;
    watch_state.reported = 0;
    watch_state.what[0] = '\0';
    if (what != NULL) {
        size_t length = strlen(what);

        if (length >= sizeof(watch_state.what)) {
            length = sizeof(watch_state.what) - 1u;
        }
        memcpy(watch_state.what, what, length);
        watch_state.what[length] = '\0';
    }

    if (!apply(address)) {
        log_error("write watch: the debug registers refused the watch on %08X", (unsigned)address);
        return false;
    }
    watch_state.armed = true;
    diag_log_write("watch  armed on %08X (%s), reporting every instruction that writes it",
                   (unsigned)address, watch_state.what);
    log_info("write watch armed on %08X (%s)", (unsigned)address, watch_state.what);
    return true;
}

void diag_write_watch_disarm(void)
{
    if (!watch_state.prepared || !watch_state.armed) {
        return;
    }
    watch_state.armed = false;
    (void)apply(0u);
    diag_log_write("watch  disarmed");
}

bool diag_write_watch_is_armed(void)
{
    return watch_state.armed;
}

void diag_write_watch_report(void)
{
    LONG written;
    LONG index;

    if (!watch_state.prepared) {
        return;
    }
    /* The handler runs as a synchronous exception on this same thread, so there is no reader and
       writer racing here and the buffer can simply be emptied at the end. */
    written = watch_state.count;
    if (written > (LONG)WATCH_RECORD_MAX) {
        written = (LONG)WATCH_RECORD_MAX;
    }
    for (index = 0; index < written; ++index) {
        float value;

        memcpy(&value, &watch_state.records[index].value, sizeof(value));
        /* A data breakpoint is a TRAP: the processor reports it once the write has completed, so
           the address in the context is the NEXT instruction, not the storing one. Saying "just
           before" rather than "at" is the difference between naming the right instruction and the
           one after it, and this observer exists precisely so nobody has to guess which. */
        diag_log_write("watch  %s written by the instruction just before %08X, value now %.4f",
                       watch_state.what, (unsigned)watch_state.records[index].instruction,
                       (double)value);
    }
    watch_state.count = 0;
    watch_state.reported = 0;

    if (watch_state.overflow != 0) {
        diag_log_write("watch  %ld further CHANGING writes were not recorded, the buffer holds %u",
                       (long)watch_state.overflow, (unsigned)WATCH_RECORD_MAX);
        watch_state.overflow = 0;
    }
    if (watch_state.unchanged != 0) {
        diag_log_write("watch  and %ld writes put the same value back, not listed",
                       (long)watch_state.unchanged);
        watch_state.unchanged = 0;
    }
}
