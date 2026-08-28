/* imuse_guard.c: close the two ways iMUSE's heartbeat lock gets stuck.
 *
 * ==============================================================================================
 * WHAT IS BROKEN
 *
 * iMUSE's script engine and its music mixer both run from ImHeartbeat, and ImHeartbeat runs only
 * while a counter reads zero. That counter is maintained by two functions:
 *
 *   ImLock     FF 05 <gate>  C3                          inc  [gate]
 *   ImUnlock   A1 <gate> 85 C0 74 06 48 A3 <gate> C3      load / test / dec / store
 *
 * Neither is interlocked, both threads use them, and the release side tests for zero BEFORE it
 * decrements, so the counter can only ever get stuck too HIGH, never too low. Stuck high, the
 * heartbeat never runs again; nothing refills the music buffer; and because that buffer plays
 * LOOPING, the result is the last half second of music circling forever rather than silence.
 * Only ImInitialize writes the counter back to zero, which is why nothing short of tearing the
 * music system down and rebuilding it brings the music back.
 *
 * Two ways in, and they need different repairs.
 *
 *   1. THE RACE. Both threads increment and decrement concurrently. Two releases that overlap
 *      can both read the same value and both store the same decrement, and one decrement is
 *      lost. On the single-core machines this shipped for, `inc [mem]` could not be split and the
 *      load/store pair almost never lost; on more than one core both are genuinely racy.
 *
 *   2. THE LEAK. ImSetParam takes the lock in its fourth instruction, before it has decided
 *      anything, and FIVE of its refusals return without giving it back. It is an oversight and
 *      not a convention: the neighbouring refusals for parameter 0x900 and for an unknown
 *      parameter both call ImUnlock first, and all seven return the same -5. ONE out-of-range
 *      call ends the music for the process.
 *
 * ---- How each is closed --------------------------------------------------------------------
 *
 *   1. ImLock becomes `lock inc`, a one-byte prefix, written into the seven-byte body that has
 *      nine bytes of padding after it. ImUnlock is replaced outright by a compare-and-exchange
 *      loop that keeps the engine's own floor exactly.
 *
 *   2. ImSetParam is refused BEFORE it is entered. The five leaking cases are pure range checks
 *      on (parameter, value), so they can be evaluated in front of the call and answered with
 *      the DLL's own -5. The leaking code is then never reached, which is a stronger repair than
 *      noticing the leak afterwards, and noticing afterwards is not even possible: all seven
 *      refusals return -5, so the return value cannot tell a leak from a correct release.
 *
 * No file is modified. Every write here is into the mapped image at run time, the way everything
 * else in this project patches the game, and iMUSE.DLL on disk is untouched.
 *
 * ---- The one behavioural difference, stated rather than hidden -------------------------------
 * ImSetParam looks its handle up BEFORE it range-checks, so an out-of-range value on a handle
 * that does not exist returns -4 ("no such sound") in the original and -5 ("bad value") here.
 * Both are failures, both are ignored by every caller in this game, and no caller distinguishes
 * them. Refusing first is what makes the repair race-free.
 * ============================================================================================ */
#include "imuse_guard.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>
#include <tlhelp32.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The DLL's own answer for "that value is out of range", returned by all seven refusals. */
#define IMUSE_PARAM_BAD_VALUE ((int32_t)0xFFFFFFFB)

/* The parameters that have a range check, and the bound each one applies. Taken from the body of
 * ImSetParam; the first four compare UNSIGNED and the fifth SIGNED, which is why they are not one
 * expression. */
#define IMUSE_PARAM_TRANSPOSE   0x400   /* value < 0x10   unsigned */
#define IMUSE_PARAM_PAN         0x500   /* value < 0x80   unsigned */
#define IMUSE_PARAM_VOLUME      0x600   /* value < 0x80   unsigned */
#define IMUSE_PARAM_GROUP_VOL   0x700   /* value < 0x80   unsigned */
#define IMUSE_PARAM_DETUNE      0x800   /* -0x2400 <= value <= 0x2400, SIGNED */

typedef int32_t (__cdecl *set_param_fn_t)(int32_t handle, int32_t parameter, int32_t value);

typedef struct guard_state {
    bool installed;

    volatile LONG *gate;

    detour_t       set_param_detour;
    set_param_fn_t original_set_param;
    bool           set_param_guarded;
    bool           lock_made_atomic;

    LONG           refusals;
    bool           refusal_reported;
} guard_state_t;

static guard_state_t guard;

/* ============================================================================================ */
bool imuse_guard_set_param_leaks(int32_t parameter, int32_t value)
{
    switch (parameter) {
    case IMUSE_PARAM_TRANSPOSE:
        return (uint32_t)value >= 0x10u;
    case IMUSE_PARAM_PAN:
    case IMUSE_PARAM_VOLUME:
    case IMUSE_PARAM_GROUP_VOL:
        return (uint32_t)value >= 0x80u;
    case IMUSE_PARAM_DETUNE:
        /* The body tests `-0x2401 < value && value < 0x2401`, i.e. the inclusive range below. */
        return value < -0x2400 || value > 0x2400;
    default:
        /* Every other parameter either has no range check or releases the lock on its refusal. */
        return false;
    }
}

/* ============================================================================================ */
static int32_t __cdecl hook_set_param(int32_t handle, int32_t parameter, int32_t value)
{
    if (imuse_guard_set_param_leaks(parameter, value)) {
        LONG count = InterlockedIncrement(&guard.refusals);

        /* Loud exactly once. This is the deterministic route into the stuck lock, so the FIRST
         * occurrence is a finding worth having in the log; a flood of them would be a second
         * defect and the first line already carries what the next one would. */
        if (!guard.refusal_reported) {
            guard.refusal_reported = true;
            log_warning("refused an ImSetParam(parameter %04X, value %d) that the music DLL would "
                        "have answered by keeping its heartbeat lock forever. That single call is "
                        "the deterministic way the music gets stuck looping, and this is the "
                        "first time it has happened this session. Further ones are counted, not "
                        "printed.",
                        (unsigned)parameter, (int)value);
        }
        (void)count;
        return IMUSE_PARAM_BAD_VALUE;
    }

    return guard.original_set_param(handle, parameter, value);
}

/* The replacement for ImUnlock: the engine's own semantics, made atomic. It keeps the floor,
 * dropping it would let the counter go negative, and a negative counter blocks the heartbeat just
 * as effectively as a positive one, which would turn one bug into another. */
static void __cdecl hook_im_unlock(void)
{
    for (;;) {
        LONG current = *guard.gate;

        if (current == 0) {
            return;                      /* the engine's `je`, release below zero is a no-op */
        }
        if (InterlockedCompareExchange(guard.gate, current - 1, current) == current) {
            return;
        }
        /* Somebody moved it between the read and the exchange, which is precisely the window the
         * original loses a decrement in. Read it again and retry. */
    }
}

/* ==============================================================================================
 * QUIESCING THE OTHER THREADS, AND WHY ONLY THIS ONE WRITE NEEDS IT.
 *
 * iMUSE runs its heartbeat from a WINMM timer thread, and that thread calls ImLock. This patch is
 * therefore written over code another thread may be executing at that instant. Installing before
 * audio starts was the assumption that made it safe, and it was only ever an assumption: nothing
 * checked it and nothing wrote it down.
 *
 * IT MATTERS HERE BECAUSE THE INSTRUCTION BOUNDARIES MOVE. Before the patch the body is
 *
 *     +0  FF 05 <abs32>   inc dword ptr [gate]     six bytes
 *     +6  C3              ret
 *
 * and after it, one byte longer for the prefix,
 *
 *     +0  F0 FF 05 <abs32>  lock inc dword ptr [gate]   seven bytes
 *     +7  C3                ret
 *
 * A thread parked at +0 is fine either way: it resumes into the new instruction and the increment
 * happens once, atomically. A thread parked at +6, which is to say about to execute the ret,
 * resumes into the last byte of the address operand and runs whatever that decodes as. That is not
 * a torn read that a careful write order avoids, it is an instruction boundary that no longer
 * exists, and the only way to be sure of it is to look.
 *
 * So every other thread in the process is suspended, each one asked where its instruction pointer
 * is, and the write happens only when none of them is inside the function. If one is, they are all
 * resumed and it is tried again a moment later; after eight attempts the patch declines, and
 * declining is handled by the caller exactly as any other failure to make the lock atomic is.
 *
 * NOTHING IS ALLOCATED WHILE THEY ARE SUSPENDED. patch_write_bytes reaches VirtualProtect, memcpy
 * and FlushInstructionCache and no further; if a suspended thread held a lock this one then
 * wanted, the process would stop there and never start again. That is also why the ImUnlock detour
 * is installed outside this window rather than inside it: detour_install builds a trampoline with
 * VirtualAlloc, and taking the address space lock while holding threads still is a worse trade
 * than the thing it would buy.
 *
 * WHAT ImUnlock DOES NOT NEED. Its patch puts a five byte jmp where a five byte mov was, so no
 * boundary moves: an instruction pointer in that function is either at +0, which is valid before
 * and after, or already past +5. What is left there is the ordinary cross modifying code window,
 * which is narrow and which every detour in this project already lives with.
 * ============================================================================================ */
#define QUIESCE_MAX_THREADS 128u
#define QUIESCE_ATTEMPTS    8u

typedef struct quiesce {
    HANDLE   threads[QUIESCE_MAX_THREADS];
    unsigned count;
} quiesce_t;

static void quiesce_release(quiesce_t *held)
{
    unsigned index;

    for (index = 0; index < held->count; ++index) {
        ResumeThread(held->threads[index]);
        CloseHandle(held->threads[index]);
    }
    held->count = 0;
}

/* Suspends every other thread in this process and answers whether the range is clear of all of
 * them. The caller releases either way: a false answer still leaves threads suspended, because
 * stopping the walk early is the point of it. Anything that cannot be established, a thread that
 * will not open, a suspend that fails, more threads than there is room for, counts as not clear;
 * a thread this cannot vouch for is exactly the thread that might be standing on the bytes. */
static bool quiesce_take(quiesce_t *held, uintptr_t low, uintptr_t high)
{
    HANDLE        snapshot;
    THREADENTRY32 entry;
    DWORD         self = GetCurrentThreadId();
    DWORD         own_process = GetCurrentProcessId();
    bool          clear = true;

    held->count = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        HANDLE  thread;
        CONTEXT context;

        if (entry.th32OwnerProcessID != own_process || entry.th32ThreadID == self) {
            continue;
        }
        if (held->count >= QUIESCE_MAX_THREADS) {
            clear = false;
            break;
        }

        thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE,
                            entry.th32ThreadID);
        if (thread == NULL) {
            clear = false;
            break;
        }
        if (SuspendThread(thread) == (DWORD)-1) {
            CloseHandle(thread);
            clear = false;
            break;
        }
        held->threads[held->count++] = thread;

        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread, &context)) {
            clear = false;
            break;
        }
        if ((uintptr_t)context.Eip >= low && (uintptr_t)context.Eip < high) {
            clear = false;                 /* parked on the bytes about to be rewritten */
            break;
        }
    } while (Thread32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return clear;
}

/* ============================================================================================ */
/* ImLock is `inc [gate]` followed by `ret`, and the compiler left nine bytes of padding after it.
 * A `lock` prefix makes the increment atomic and costs one byte, so the whole repair fits inside
 * the function's own paragraph. See the essay above for why the write waits for the other
 * threads to be somewhere else. */
static bool make_lock_atomic(uintptr_t im_lock)
{
    uint8_t  atomic_increment[8];
    uint8_t  existing[7];
    uint32_t gate_operand;

    if (!memory_read(im_lock, existing, sizeof(existing))) {
        return false;
    }
    /* Already atomic, so this has run once before in this process and there is nothing to do.
     * Tested BEFORE the shape check below, and that order is the fix rather than a preference:
     * an already-patched body begins with the F0 prefix, the shape check only accepts one
     * beginning FF, so while this sat after it the arm could never be reached at all. A second
     * install did not report "nothing to do", it fell into the warning below and announced that
     * ImLock was not `inc [mem] / ret` when in truth it was, and was already repaired. */
    if (existing[0] == 0xF0) {
        return true;
    }
    /* Refuse unless it is exactly the body this was measured against: `FF 05 <abs32>` then `C3`.
     * Resolving the site is not the same as knowing what is at it. */
    if (existing[0] != 0xFF || existing[1] != 0x05 || existing[6] != 0xC3) {
        log_warning("ImLock at %08X is not `inc [mem] / ret` - the atomic increment is NOT written",
                    (unsigned)im_lock);
        return false;
    }

    memcpy(&gate_operand, existing + 2, sizeof(gate_operand));

    atomic_increment[0] = 0xF0;          /* lock                         */
    atomic_increment[1] = 0xFF;          /* inc dword ptr [gate]         */
    atomic_increment[2] = 0x05;
    memcpy(atomic_increment + 3, &gate_operand, sizeof(gate_operand));
    atomic_increment[7] = 0xC3;          /* ret                          */

    {
        quiesce_t held;
        unsigned  attempt;

        for (attempt = 0; attempt < QUIESCE_ATTEMPTS; ++attempt) {
            bool clear = quiesce_take(&held, im_lock, im_lock + sizeof(atomic_increment));
            bool written = false;

            if (clear) {
                written = (patch_write_bytes(im_lock, atomic_increment,
                                             sizeof(atomic_increment)) == PATCH_RESULT_OK);
            }
            quiesce_release(&held);

            if (clear) {
                return written;            /* logged by the caller either way */
            }
            Sleep(2);                      /* outside the suspension, on purpose */
        }

        log_warning("ImLock at %08X had another thread standing on it every time this looked, so "
                    "the atomic increment is NOT written. The music lock stays as the game "
                    "shipped it, which is the state this fix improves on rather than depends on.",
                    (unsigned)im_lock);
        return false;
    }
}

bool imuse_guard_install(const imuse_sites_t *sites)
{
    static detour_t unlock_detour;

    if (guard.installed) {
        return guard.lock_made_atomic || guard.set_param_guarded;
    }
    guard.installed = true;

    if (sites == NULL || sites->gate == NULL || sites->im_lock == 0 || sites->im_unlock == 0) {
        log_warning("the music lock was not resolved, it stays as the game shipped it");
        return false;
    }
    guard.gate = (volatile LONG *)sites->gate;

    /* ---- the race ---------------------------------------------------------------------- */
    guard.lock_made_atomic = make_lock_atomic(sites->im_lock);
    if (guard.lock_made_atomic) {
        /* The prologue of ImUnlock is `mov eax,[abs32]`, five bytes, which is exactly what a
         * jmp rel32 needs and a clean instruction boundary. The original is deliberately never
         * called: this replaces it rather than wrapping it. */
        if (detour_install(&unlock_detour, sites->im_unlock, (const void *)hook_im_unlock, 5u)) {
            log_info("the music heartbeat lock is now atomic: ImLock at %08X increments with a "
                     "lock prefix, and ImUnlock at %08X is replaced by a compare-and-exchange "
                     "loop that keeps the engine's own floor. A lost decrement was one of the two "
                     "ways the music got stuck looping.",
                     (unsigned)sites->im_lock, (unsigned)sites->im_unlock);
        } else {
            /* Rolled back on purpose. An atomic raise against a racy lower is not half a repair,
             * it is a different imbalance: it would make the increment reliable while the
             * decrement can still be lost, which is the same failure with better odds of
             * arriving. Section 17 of the rules, a partially installed feature stays inactive. */
            uint8_t  plain[8];
            uint32_t operand = (uint32_t)(uintptr_t)sites->gate;

            plain[0] = 0xFF;                                   /* inc dword ptr [gate] */
            plain[1] = 0x05;
            memcpy(plain + 2, &operand, sizeof(operand));
            plain[6] = 0xC3;                                   /* ret                  */
            plain[7] = 0x90;                                   /* the byte the prefix took */
            (void)patch_write_bytes(sites->im_lock, plain, sizeof(plain));

            guard.lock_made_atomic = false;
            log_warning("ImUnlock at %08X could not be replaced, so the atomic increment at %08X "
                        "was rolled back. An atomic raise against a racy lower is a different "
                        "imbalance, not half a repair.",
                        (unsigned)sites->im_unlock, (unsigned)sites->im_lock);
        }
    }

    /* ---- the leak ---------------------------------------------------------------------- */
    if (sites->set_param_body != 0) {
        if (detour_install(&guard.set_param_detour, sites->set_param_body,
                           (const void *)hook_set_param, 7u)) {
            guard.original_set_param = (set_param_fn_t)guard.set_param_detour.original;
            guard.set_param_guarded = true;
            log_info("ImSetParam at %08X is guarded: the five value ranges whose refusal keeps the "
                     "heartbeat lock are now refused BEFORE the call, with the DLL's own answer. "
                     "That was the deterministic way one bad call could end the music for the "
                     "rest of the session.", (unsigned)sites->set_param_body);
        } else {
            log_warning("ImSetParam at %08X could not be detoured, the deterministic half of the "
                        "leak stays open", (unsigned)sites->set_param_body);
        }
    }

    if (!guard.lock_made_atomic && !guard.set_param_guarded) {
        log_warning("neither half of the music lock repair could be installed, the watchdog is "
                    "the only thing standing between a stuck lock and looping music");
        return false;
    }
    return true;
}
