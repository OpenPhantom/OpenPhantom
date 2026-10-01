/* memory.h: range checks and page protection, so no feature has to call VirtualProtect itself.
 *
 * The rule these functions exist to enforce: validate the COMPLETE range you are about to touch.
 * Checking only the first address is not enough when reading a structure or an array, an engine
 * table can start in committed memory and end past the last committed page, and the read that
 * finds out is the one that kills the process.
 */
#ifndef COMMON_MEMORY_H
#define COMMON_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True when [address, address+size) is committed and not PAGE_NOACCESS / PAGE_GUARD.
 * Spans several regions correctly; it walks them. */
bool memory_is_readable_range(uintptr_t address, size_t size);

/* True when [address, address+size) lies inside the host executable's image. */
bool memory_is_inside_image(uintptr_t address, size_t size);

/* True when the range is readable AND every page of it is executable. This is the check a branch
 * target needs, and it is deliberately not `memory_is_inside_image`: a chained detour's previous
 * hook is a function in another mod's DLL, which is outside the host image by construction, so an
 * image test would refuse exactly the case chaining exists for. */
bool memory_is_executable_range(uintptr_t address, size_t size);

/* Makes a range writable and executable and leaves it that way. Only for the handful of sites
 * that are rewritten repeatedly at run time (the per-frame camera immediates). Everything else
 * must go through patch_write_*, which restores the original protection. */
bool memory_make_writable(uintptr_t address, size_t size);

/* Reads that refuse rather than fault. */
bool memory_read(uintptr_t address, void *destination, size_t size);
bool memory_read_u8 (uintptr_t address, uint8_t  *out);
bool memory_read_u32(uintptr_t address, uint32_t *out);

/* The read every site table makes: the 32-bit operand of a matched instruction names an engine
 * cell, and the cell has to be `cell_size` bytes that lie inside the host image and are readable.
 * Anything else means the pattern matched something that is not the function it was cut from, and
 * believing it would read or write into a stranger. Install time only, like every read above. */
bool memory_read_image_cell(uintptr_t operand_address, size_t cell_size, uint32_t *out_cell);

/* The same guarantee for code that runs once per drawn object rather than once at install.
 *
 * Everything above asks the operating system whether the range is readable, one system call per
 * call. At install time nobody can measure it. On the drawing path it is the wrong shape entirely:
 * a hook that runs for every emitter, every decal fan and every model in view pays a syscall per
 * read, and the cost is fixed per frame, so it stays invisible while frames are long and becomes a
 * material share of the frame as soon as they are short.
 *
 * These do the read directly and catch the fault instead of asking permission first. A bad pointer
 * costs one exception, which is expensive but only happens when the alternative would have been a
 * refusal anyway; a good pointer costs the read alone. Use them where the pointer comes
 * from the engine at run time and the code runs per object. Keep the asking form for install-time
 * validation, where being told WHY a range is unusable is worth more than the nanoseconds.
 *
 * They give the answer the asking form gives, with one difference that is on purpose. A range
 * outside the addresses this process can own at all, the null page and everything above the
 * application's top, is refused before anything is touched, which is what the asking form says
 * about it too and costs three comparisons instead of an exception. What is caught is an access
 * violation, an in-page error and a guard page; a guard page is armed again at once, so a stray
 * pointer into another thread's stack guard does not leave that stack unable to grow.
 *
 * The reads catch it in the guarded copy of memory_guard.h and not in a __try, because the game
 * runs with a compatibility fix that resumes every read fault before a __try is asked. Should that
 * copy be unusable, a read asks first, as the functions above do. */
bool memory_try_read(uintptr_t address, void *destination, size_t size);
bool memory_try_readable(uintptr_t address, size_t size);

/* The same two conveniences the asking form has, so a call site on a path the engine drives
 * reads the same as one at install time and only the guard changes. */
bool memory_try_read_u8 (uintptr_t address, uint8_t  *out);
bool memory_try_read_u32(uintptr_t address, uint32_t *out);
/* The same bargain for a write into a run time structure: a record the engine allocated, never
 * code. patch.c is the way into code and does what code needs, which is to lift the page
 * protection and flush the instruction cache; on a heap page both are wrong. The flush is
 * wasted, and the lift makes a data page executable for the duration of a four byte store,
 * which is a worse trade every time it is made. Faults are caught here by a __try, which in the
 * game does see a write fault, so a pointer into a page the engine never handed out, or may
 * only read, costs one exception rather than a crash. A freed heap block stays committed and
 * is not one of those pages. */
bool memory_try_write(uintptr_t address, const void *source, size_t size);

/* ==============================================================================================
 * What the two forms cost, counted for a feature that wants to print it.
 *
 * Off in every DLL until that DLL arms it, and each DLL links its own copy of this file, so arming
 * it in one counts that DLL's calls and nobody else's. Unarmed, a call pays one comparison.
 *
 * The asking side is counted per call of a public asking function: its time, whether it refused,
 * how long the run of like pages behind the address was (VirtualQuery walks that run, so its
 * length is what the call costs), and the address it was called from. The trying side is counted
 * only when it refuses: a fault by kind, and a range refused before the touch for lying outside
 * the application's addresses, each with its caller; a refused range with the address it was
 * handed, a fault with the address that faulted.
 *
 * The counts are plain increments with no lock. The reads this is armed for run on one thread;
 * a second thread would at worst miscount, never corrupt a read.
 * ============================================================================================ */

#define MEMORY_WATCH_CALLERS       32u   /* asking callers told apart, the rest summed */
#define MEMORY_WATCH_REFUSERS      16u   /* trying callers that refused, told apart */
#define MEMORY_WATCH_MINUTES       32u   /* the last row holds every later minute */
#define MEMORY_WATCH_REGION_BANDS   4u   /* under 64 KB, up to 1 MB, up to 4 MB, larger */

typedef enum memory_watch_kind {
    MEMORY_WATCH_READ,     /* memory_try_read and its two conveniences */
    MEMORY_WATCH_PROBE,    /* memory_try_readable */
    MEMORY_WATCH_WRITE,    /* memory_try_write */
    MEMORY_WATCH_KINDS
} memory_watch_kind_t;

typedef struct memory_watch_caller {
    uintptr_t caller;      /* the return address into the function that called */
    uint32_t  calls;
    uint32_t  refused;
} memory_watch_caller_t;

typedef struct memory_watch_minute {
    uint32_t calls;
    uint64_t ticks;        /* QueryPerformanceCounter ticks spent asking */
    uint64_t region_kb;    /* the runs behind the addresses, summed, in KB */
} memory_watch_minute_t;

typedef struct memory_watch_refuser {
    uintptr_t caller;
    uintptr_t last_address;
    uint32_t  count;
    uint8_t   kind;        /* memory_watch_kind_t */
    bool      outside;     /* refused before the touch rather than caught */
} memory_watch_refuser_t;

typedef struct memory_watch_stats {
    uint32_t               calls;
    uint32_t               refused;
    uint64_t               ticks;
    uint64_t               dearest_ticks;
    uint32_t               region[MEMORY_WATCH_REGION_BANDS];
    memory_watch_caller_t  caller[MEMORY_WATCH_CALLERS];
    uint32_t               callers;
    uint32_t               calls_unlisted;
    memory_watch_minute_t  minute[MEMORY_WATCH_MINUTES];
    uint64_t               since_ms;      /* GetTickCount64 at the last reset */

    uint32_t               faults[MEMORY_WATCH_KINDS];
    uint32_t               outside;
    memory_watch_refuser_t refuser[MEMORY_WATCH_REFUSERS];
    uint32_t               refusers;
    uint32_t               refusals_unlisted;
} memory_watch_stats_t;

/* `asking` counts the asking side, which reads the performance counter twice per call; `refusals`
 * counts the trying side's refusals, which costs nothing until one happens. */
void memory_watch_arm(bool asking, bool refusals);

/* While on, memory_try_read_u8 and memory_try_read_u32 ask VirtualQuery first and refuse what it
 * refuses, exactly as memory_read_u8 and memory_read_u32 do, and those questions are not counted.
 * The two conveniences are the ones a run time path took over from the asking form, so this is how
 * a caller times the same reads both ways without keeping a second copy of the reading code. The
 * generic memory_try_read, memory_try_readable and memory_try_write are left alone: they never
 * asked. A measurement switch; nothing may be left running with it on. */
void memory_watch_asking_mode(bool on);

const memory_watch_stats_t *memory_watch_stats(void);

/* Every count to zero and the minute rows from now. */
void memory_watch_reset(void);

/* The asking side alone to zero, its callers and minute rows included, and the minutes from now;
 * the refusals of the trying side are kept. For a level's beginning, where the reads of the menu
 * and the load would otherwise fill the caller table before the level's own callers arrive. */
void memory_watch_reset_asking(void);

/* Pure, for a test: which band a run of `bytes` behind an address falls into. */
uint32_t memory_watch_region_band(size_t bytes);

#endif /* COMMON_MEMORY_H */
