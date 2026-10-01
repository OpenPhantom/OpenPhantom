#include "memory.h"

#include "host_image.h"
#include "logging.h"
#include "memory_guard.h"

#include <windows.h>
#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UNREADABLE_PROTECTION (PAGE_NOACCESS | PAGE_GUARD)

/* The four protection constants that carry execute rights. They are distinct values, not flags
 * (PAGE_EXECUTE_READ is 0x20, not a combination), but they occupy 0x10 through 0x80 and no
 * non-executable value touches those bits, so once PAGE_GUARD and PAGE_NOACCESS are masked off a
 * bit test against their union is exact. That is the test the executable walk below makes. */
#define EXECUTABLE_PROTECTION_MASK (PAGE_EXECUTE | PAGE_EXECUTE_READ \
                                    | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

/* The caller of a public function, taken in that function itself and handed down. Taken any lower,
 * it would name whichever function of this file the compiler kept as a frame. The public functions
 * that take it are never inlined, so the address is always the caller's own call site. */
#define CALLER() ((uintptr_t)_ReturnAddress())

typedef struct memory_watch_state {
    bool                 asking;        /* armed: the asking side is counted */
    bool                 refusals;      /* armed: the trying side's refusals are counted */
    bool                 asking_mode;   /* the two small trying reads ask first */
    memory_watch_stats_t stats;
} memory_watch_state_t;

static memory_watch_state_t watch;

/* The addresses this process can own at all, from the system once. */
typedef struct application_range {
    volatile bool known;
    uintptr_t     lowest;
    uintptr_t     highest;
} application_range_t;

static application_range_t application;

/* A fault the filter let through to the handler: where, and whether it was a guard page. */
typedef struct caught {
    uintptr_t address;
    bool      guard;
} caught_t;

/* ==============================================================================================
 * The counting.
 * ============================================================================================ */

uint32_t memory_watch_region_band(size_t bytes)
{
    if (bytes < 64u * 1024u) {
        return 0u;
    }
    if (bytes <= 1024u * 1024u) {
        return 1u;
    }
    if (bytes <= 4u * 1024u * 1024u) {
        return 2u;
    }
    return 3u;
}

static uint32_t minute_now(void)
{
    uint64_t minutes = (GetTickCount64() - watch.stats.since_ms) / 60000u;

    return minutes >= MEMORY_WATCH_MINUTES ? MEMORY_WATCH_MINUTES - 1u : (uint32_t)minutes;
}

static void note_asking(uintptr_t caller, bool readable, uint64_t ticks, size_t run)
{
    memory_watch_stats_t  *s = &watch.stats;
    memory_watch_minute_t *m = &s->minute[minute_now()];
    uint32_t               row;

    ++s->calls;
    s->refused += readable ? 0u : 1u;
    s->ticks += ticks;
    if (ticks > s->dearest_ticks) {
        s->dearest_ticks = ticks;
    }
    if (run != 0u) {
        ++s->region[memory_watch_region_band(run)];
        m->region_kb += run / 1024u;
    }
    ++m->calls;
    m->ticks += ticks;

    for (row = 0; row < s->callers; ++row) {
        if (s->caller[row].caller == caller) {
            break;
        }
    }
    if (row == s->callers) {
        if (row >= MEMORY_WATCH_CALLERS) {
            ++s->calls_unlisted;
            return;
        }
        s->caller[row].caller = caller;
        ++s->callers;
    }
    ++s->caller[row].calls;
    s->caller[row].refused += readable ? 0u : 1u;
}

static void note_refusal(memory_watch_kind_t kind, bool outside, uintptr_t caller,
                         uintptr_t address)
{
    memory_watch_stats_t *s = &watch.stats;
    uint32_t              row;

    if (!watch.refusals) {
        return;
    }
    if (outside) {
        ++s->outside;
    } else {
        ++s->faults[kind];
    }
    for (row = 0; row < s->refusers; ++row) {
        const memory_watch_refuser_t *r = &s->refuser[row];

        if (r->caller == caller && r->kind == (uint8_t)kind && r->outside == outside) {
            break;
        }
    }
    if (row == s->refusers) {
        if (row >= MEMORY_WATCH_REFUSERS) {
            ++s->refusals_unlisted;
            return;
        }
        s->refuser[row].caller  = caller;
        s->refuser[row].kind    = (uint8_t)kind;
        s->refuser[row].outside = outside;
        ++s->refusers;
    }
    ++s->refuser[row].count;
    s->refuser[row].last_address = address;
}

void memory_watch_arm(bool asking, bool refusals)
{
    watch.asking   = asking;
    watch.refusals = refusals;
    if (watch.stats.since_ms == 0u) {
        watch.stats.since_ms = GetTickCount64();
    }
}

void memory_watch_asking_mode(bool on)
{
    watch.asking_mode = on;
}

const memory_watch_stats_t *memory_watch_stats(void)
{
    return &watch.stats;
}

void memory_watch_reset(void)
{
    memset(&watch.stats, 0, sizeof watch.stats);
    watch.stats.since_ms = GetTickCount64();
}

void memory_watch_reset_asking(void)
{
    memory_watch_stats_t *s = &watch.stats;

    s->calls          = 0u;
    s->refused        = 0u;
    s->ticks          = 0u;
    s->dearest_ticks  = 0u;
    s->callers        = 0u;
    s->calls_unlisted = 0u;
    memset(s->region, 0, sizeof s->region);
    memset(s->caller, 0, sizeof s->caller);
    memset(s->minute, 0, sizeof s->minute);
    s->since_ms = GetTickCount64();
}

/* ==============================================================================================
 * The asking form.
 * ============================================================================================ */

/* One region at a time. A single VirtualQuery only describes the region the START address falls
 * into, which is exactly the check that misses a table running off its last page. `first_run`, when
 * given, gets the length of the run of like pages the address opens, which is what the first query
 * had to walk. */
static bool walk_readable(uintptr_t address, size_t size, size_t *first_run)
{
    MEMORY_BASIC_INFORMATION information;
    uintptr_t                 cursor;
    uintptr_t                 end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }

    for (cursor = address; cursor < end; cursor = (uintptr_t)information.BaseAddress
                                                 + information.RegionSize) {
        if (VirtualQuery((LPCVOID)cursor, &information, sizeof(information))
            != sizeof(information)) {
            return false;
        }
        if (first_run != NULL && cursor == address) {
            *first_run = information.RegionSize;
        }
        if (information.State != MEM_COMMIT) {
            return false;
        }
        if ((information.Protect & UNREADABLE_PROTECTION) != 0) {
            return false;
        }
    }

    return true;
}

static bool readable_range(uintptr_t address, size_t size, uintptr_t caller)
{
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    size_t        run = 0;
    bool          readable;

    if (!watch.asking) {
        return walk_readable(address, size, NULL);
    }
    QueryPerformanceCounter(&before);
    readable = walk_readable(address, size, &run);
    QueryPerformanceCounter(&after);
    note_asking(caller, readable, (uint64_t)(after.QuadPart - before.QuadPart), run);
    return readable;
}

static bool read_asking(uintptr_t address, void *destination, size_t size, uintptr_t caller)
{
    if (destination == NULL || !readable_range(address, size, caller)) {
        return false;
    }
    memcpy(destination, (const void *)address, size);
    return true;
}

__declspec(noinline) bool memory_is_readable_range(uintptr_t address, size_t size)
{
    return readable_range(address, size, CALLER());
}

bool memory_is_executable_range(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION information;
    uintptr_t                 cursor;
    uintptr_t                 end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;
    }

    /* Region by region, for the same reason the readable check walks them: a branch target may sit
     * one byte before the end of an executable region whose neighbour is data. */
    for (cursor = address; cursor < end; cursor = (uintptr_t)information.BaseAddress
                                                 + information.RegionSize) {
        DWORD protection;

        if (VirtualQuery((LPCVOID)cursor, &information, sizeof(information))
            != sizeof(information)) {
            return false;
        }
        if (information.State != MEM_COMMIT) {
            return false;
        }
        if ((information.Protect & UNREADABLE_PROTECTION) != 0) {
            return false;
        }

        protection = information.Protect & ~(DWORD)UNREADABLE_PROTECTION;
        if ((protection & EXECUTABLE_PROTECTION_MASK) == 0) {
            return false;
        }
    }

    return true;
}

bool memory_is_inside_image(uintptr_t address, size_t size)
{
    uintptr_t end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;
    }

    return address >= host_image_base() && end <= host_image_end();
}

bool memory_make_writable(uintptr_t address, size_t size)
{
    DWORD previous_protection;

    if (size == 0) {
        return false;
    }

    return VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous_protection)
           != 0;
}

__declspec(noinline) bool memory_read(uintptr_t address, void *destination, size_t size)
{
    return read_asking(address, destination, size, CALLER());
}

__declspec(noinline) bool memory_read_u8(uintptr_t address, uint8_t *out)
{
    return read_asking(address, out, sizeof(*out), CALLER());
}

__declspec(noinline) bool memory_read_u32(uintptr_t address, uint32_t *out)
{
    return read_asking(address, out, sizeof(*out), CALLER());
}

bool memory_read_image_cell(uintptr_t operand_address, size_t cell_size, uint32_t *out_cell)
{
    uint32_t cell = 0;

    if (out_cell == NULL || !memory_read_u32(operand_address, &cell)) {
        return false;
    }
    if (!memory_is_inside_image(cell, cell_size) || !memory_is_readable_range(cell, cell_size)) {
        return false;
    }
    *out_cell = cell;
    return true;
}

/* ==============================================================================================
 * The trying form.
 *
 * Catching the fault instead of asking first is the whole point of these, so they must not be
 * folded back into the functions above: a guarded copy is a few instructions, while VirtualQuery is
 * a system call, and the difference only matters where these are used. The reads catch it in the
 * guarded copy of memory_guard.c, because in the game a __try never sees a read fault; the write
 * keeps its __try, because the fix that resumes read faults lets write faults through.
 * ============================================================================================ */

/* Whether [address, address + size) lies where this process could own memory at all. Below the
 * lowest application address is the null page and the guard band above it, above the highest is
 * the system's; the asking form refuses both, one as free and one because VirtualQuery fails, and
 * so does this, without the exception a touch would cost. `size` is at least one. */
static bool inside_application(uintptr_t address, size_t size)
{
    if (!application.known) {
        SYSTEM_INFO info;

        GetSystemInfo(&info);
        application.lowest  = (uintptr_t)info.lpMinimumApplicationAddress;
        application.highest = (uintptr_t)info.lpMaximumApplicationAddress;
        MemoryBarrier();
        application.known = true;
    }
    return address >= application.lowest && address <= application.highest &&
           size - 1u <= application.highest - address;
}

/* The three ways a touch of memory this process does not hold can end: an access violation, an
 * in-page error on a mapped file whose backing is gone, and a guard page. Everything else goes on
 * to the next handler. Nothing else can come out of a memcpy of one caller's range, and if
 * something did it would be somebody else's to see. */
static int catch_fault(const EXCEPTION_POINTERS *pointers, caught_t *caught)
{
    const EXCEPTION_RECORD *record = pointers->ExceptionRecord;

    switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_GUARD_PAGE:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    caught->guard   = record->ExceptionCode == EXCEPTION_GUARD_PAGE;
    caught->address = record->NumberParameters >= 2u
                          ? (uintptr_t)record->ExceptionInformation[1]
                          : 0u;
    return EXCEPTION_EXECUTE_HANDLER;
}

/* The system takes the guard off a page before it reports the touch. Left like that, a stray read
 * into another thread's stack guard would leave that stack unable to grow, which is a crash
 * somewhere else later; so the bit goes back on the one page that was touched, and the read is
 * refused as the asking form would have refused it without touching. */
static void rearm_guard(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION information;
    DWORD                    previous;

    if (VirtualQuery((LPCVOID)address, &information, sizeof information) != sizeof information ||
        information.State != MEM_COMMIT || (information.Protect & PAGE_GUARD) != 0u) {
        return;
    }
    (void)VirtualProtect((LPVOID)address, 1u, information.Protect | PAGE_GUARD, &previous);
}

static void settle(const caught_t *caught, memory_watch_kind_t kind, uintptr_t caller)
{
    if (caught->guard) {
        rearm_guard(caught->address);
    }
    note_refusal(kind, false, caller, caught->address);
}

/* The first read this module's guard turned back, said once, in a module that does not count its
 * refusals. crash_report does not see such a fault, so without this line a feature whose probe
 * refuses a stale pointer would leave no trace of it at all. A module that counts reports them
 * with the rest of its counts instead. */
static void say_the_first_turned_back(uintptr_t caller, uintptr_t address)
{
    static volatile LONG said;

    if (watch.refusals || InterlockedCompareExchange(&said, 1, 0) != 0) {
        return;
    }
    log_info("a guarded read was refused at a fault: called from %08X, reading %08X; the guard "
             "turns every such fault into a refusal, and later ones are not said here",
             (unsigned)caller, (unsigned)address);
}

/* A read through the guarded copy (memory_guard.h), which holds in the game where a __try does not:
 * the game runs with a compatibility fix that resumes every read fault before a __try is asked. */
static bool guarded_read(uintptr_t address, void *destination, size_t size,
                         memory_watch_kind_t kind, uintptr_t caller)
{
    memory_guard_fault_t fault = { 0u, false };
    caught_t             caught;

    if (memory_guard_copy(destination, (const void *)address, size, &fault)) {
        return true;
    }
    caught.address = fault.address;
    caught.guard   = fault.guard;
    settle(&caught, kind, caller);
    say_the_first_turned_back(caller, fault.address);
    return false;
}

/* The guard could not start, or is starting on another thread: the range is asked about first,
 * which is what the asking form does and costs a system call, and then copied. The __try stays for
 * a process without the compatibility fix, where it is what catches a range freed in between. */
static bool fallback_read(uintptr_t address, void *destination, size_t size,
                          memory_watch_kind_t kind, uintptr_t caller)
{
    caught_t caught = { 0u, false };

    if (!walk_readable(address, size, NULL)) {
        note_refusal(kind, false, caller, address);
        return false;
    }
    __try {
        memcpy(destination, (const void *)address, size);
        return true;
    } __except (catch_fault(GetExceptionInformation(), &caught)) {
        settle(&caught, kind, caller);
        return false;
    }
}

static bool try_read(uintptr_t address, void *destination, size_t size, uintptr_t caller)
{
    if (destination == NULL || size == 0) {
        return false;
    }
    if (address + size < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }
    if (!inside_application(address, size)) {
        note_refusal(MEMORY_WATCH_READ, true, caller, address);
        return false;
    }
    if (memory_guard_ready()) {
        return guarded_read(address, destination, size, MEMORY_WATCH_READ, caller);
    }
    return fallback_read(address, destination, size, MEMORY_WATCH_READ, caller);
}

/* The two conveniences, which in the asking mode ask first exactly as their asking twins do. */
static bool try_read_small(uintptr_t address, void *out, size_t size, uintptr_t caller)
{
    if (watch.asking_mode && !walk_readable(address, size, NULL)) {
        return false;
    }
    return try_read(address, out, size, caller);
}

__declspec(noinline) bool memory_try_read(uintptr_t address, void *destination, size_t size)
{
    return try_read(address, destination, size, CALLER());
}

__declspec(noinline) bool memory_try_read_u8(uintptr_t address, uint8_t *out)
{
    return try_read_small(address, out, sizeof(*out), CALLER());
}

__declspec(noinline) bool memory_try_read_u32(uintptr_t address, uint32_t *out)
{
    return try_read_small(address, out, sizeof(*out), CALLER());
}

__declspec(noinline) bool memory_try_write(uintptr_t address, const void *source, size_t size)
{
    uintptr_t caller = CALLER();
    caught_t  caught = { 0u, false };

    if (source == NULL || size == 0) {
        return false;
    }
    if (address + size < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }
    if (!inside_application(address, size)) {
        note_refusal(MEMORY_WATCH_WRITE, true, caller, address);
        return false;
    }

    __try {
        memcpy((void *)address, source, size);
        return true;
    } __except (catch_fault(GetExceptionInformation(), &caught)) {
        settle(&caught, MEMORY_WATCH_WRITE, caller);
        return false;
    }
}

/* Touching the first and the last byte is the whole range for this purpose. A range that spans a
 * hole would have to be committed at both ends and not in the middle, which no allocator this
 * engine uses produces. A caller that then reads the range directly is on its own for such a hole:
 * in the game the compatibility fix resumes that read with whatever the registers held, and only a
 * write would fault for real. That is a deliberate difference from the asking form above, which
 * walks every region because it is used to validate patch sites where a hole is a real possibility
 * and a fault is not acceptable. */
__declspec(noinline) bool memory_try_readable(uintptr_t address, size_t size)
{
    uintptr_t caller = CALLER();
    uint8_t   first  = 0u;
    uint8_t   last   = 0u;

    if (size == 0 || address + size < address) {
        return false;
    }
    if (!inside_application(address, size)) {
        note_refusal(MEMORY_WATCH_PROBE, true, caller, address);
        return false;
    }
    if (!memory_guard_ready()) {
        if (walk_readable(address, size, NULL)) {
            return true;
        }
        note_refusal(MEMORY_WATCH_PROBE, false, caller, address);
        return false;
    }
    return guarded_read(address, &first, 1u, MEMORY_WATCH_PROBE, caller) &&
           guarded_read(address + size - 1u, &last, 1u, MEMORY_WATCH_PROBE, caller);
}
