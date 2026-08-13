/* diag_present.c: which of the engine's two presentation paths is live, and what the present costs.
 *
 * Why this exists. The engine finishes a frame and hands it to Windows in one of two completely
 * different ways, chosen per display device, and everything one might do about presentation
 * depends on which. On the DirectDraw path the finished surface is flipped, so a DirectDraw
 * wrapper owns the presentation and a wrapper setting such as a flip model can reach it. On the
 * GDI path the surface is copied into the window with BitBlt, which goes past any DirectDraw
 * wrapper entirely, and then nothing but a patch on the engine itself can change how the frame
 * reaches the screen.
 *
 * There is a third outcome, and it is not a rounding of the second. On the DirectDraw path the
 * flip is behind a second gate, a global that records whether a back buffer was ever requested.
 * With that global clear the present function returns success without presenting anything at all.
 * The listing beside SIG_DEVICE_BACKEND shows all three branches in the shipped bytes.
 *
 * When it reads, which is the part that is easy to get wrong. The selector lives behind a pointer
 * that is null until the display has been opened, and the display is opened long after this DLL is
 * loaded. Reading it at install time would therefore report nothing useful while looking like a
 * clean answer. The read happens on the frame hook instead, from the first frame at which the
 * pointer is populated, and it repeats only when the value changes, because a mode change can
 * reopen the display.
 *
 * The second question, and it is the one that decides where the frame time goes. The flip routine
 * does not block. It issues the flip and, when the answer is "still drawing", issues the identical
 * call again immediately, with no wait of any kind in between. Whether that loop turns once or ten
 * thousand times depends on a single bit of the device capabilities, which selects whether the flip
 * is asked to wait for the display or to return at once. If it waits, the thread sleeps in the
 * driver and the frame costs no CPU. If it does not, the engine burns a core in the retry loop and
 * every measurement of "CPU per frame" reads that spin as if it were work.
 *
 * So this observer times the flip and, at the same time, counts the thread cycles spent inside it.
 * That distinguishes the two cases without knowing anything about the machine: a wait that consumes
 * cycles at the same rate as the engine's own drawing is a spin, and a wait that consumes almost
 * none is a block. Both rates are measured in the same second against each other, so no clock speed
 * and no calibration is needed.
 *
 * This observer changes no game state. It reads three cells, redirects one call to a wrapper that
 * calls the original and returns its result unchanged, and prints lines.
 *
 * =============================================== SIZE NOTE ====================================
 *
 * This file is over 600 lines. The code is short and the byte level evidence under it is not:
 * three anchors, the disassembly each was derived from, the addresses each resolves to on all
 * three builds, and two approaches that were tried and refused. Deleting any of that would leave a
 * reader unable to check the anchors against a binary, which is the only way they can be checked
 * at all.
 *
 * The obvious seam is the one between the two questions, the path report and the flip timing, and
 * it was measured and rejected. SIG_FLIP_GATE resolves both halves at once: the same match yields
 * the back buffer cell that the path report reads and the call site and original target that the
 * timing redirects. Cutting there would put one pattern and its evidence behind a translation unit
 * boundary from half its users, and would leave the timing half unable to say why its call site is
 * safe to rewrite. The next real growth belongs in a third file rather than in a cut through this
 * one.
 */
#include "diag_present.h"

#include "diag_log.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The field inside a display device record that selects the path, and its two authored values. */
#define DEVICE_BACKEND_OFFSET     0x100u
#define BACKEND_GDI               0u
#define BACKEND_DIRECTDRAW        1u

/* --- the present, stdDisplay_present 0x0048F052 on retail WMAIN.EXE, 0x7A bytes ---------------- *
 * Read out of the image bytes. This is where the three outcomes come from, and the third one is a
 * real branch in the shipped code rather than a reading of the other two:
 *
 *   0048F052  55                    push ebp
 *   0048F053  8B EC                 mov  ebp, esp
 *   0048F055  51                    push ecx
 *   0048F056  A1 10 20 86 00        mov  eax, [0x00862010]     the selected device record
 *   0048F05B  8B 88 00 01 00 00     mov  ecx, [eax + 0x100]    its backend field
 *   0048F061  89 4D FC              mov  [ebp-4], ecx
 *   0048F064  83 7D FC 00           cmp  [ebp-4], 0
 *   0048F068  74 08                 je   0048F072              the GDI path
 *   0048F06A  83 7D FC 01           cmp  [ebp-4], 1
 *   0048F06E  74 43                 je   0048F0B3              the DirectDraw path
 *   0048F070  EB 56                 jmp  0048F0C8              neither, and eax is undefined
 *   0048F072  FF 15 D4 14 8C 00     call [0x008C14D4]          GdiFlush
 *   0048F078  68 20 00 CC 00        push 0x00CC0020            SRCCOPY
 *   ...                                                        hdc, width, height, source hdc
 *   0048F0A6  FF 15 9C 14 8C 00     call [0x008C149C]          BitBlt
 *   0048F0AC  B8 01 00 00 00        mov  eax, 1
 *   0048F0B1  EB 15                 jmp  0048F0C8
 *   0048F0B3  83 3D 1C 20 86 00 00  cmp  [0x0086201C], 0       was a back buffer requested
 *   0048F0BA  74 07                 je   0048F0C3              no, and nothing is presented
 *   0048F0BC  E8 87 43 00 00        call 0x00493448            the flip
 *   0048F0C1  EB 05                 jmp  0048F0C8
 *   0048F0C3  B8 01 00 00 00        mov  eax, 1
 *   0048F0C8  8B E5 5D C3           mov esp,ebp / pop ebp / ret
 *
 * The width and height pushed to BitBlt come from [0x00862018] + 8 and + 0xC, which is the chosen
 * display mode record and not the window. The source device context is [0x00861FFC] and the
 * destination is [0x008619E0]. None of that is needed here; it is written down because it is what
 * proves the GDI branch really does copy the mode-sized surface into the window.
 *
 * --- the backend anchor, deliberately not unique ----------------------------------------------- *
 * Three functions in the display layer open with the same "load the selected device, branch on its
 * backend field" sequence, so this pattern matches three times by design. The key is the triple of
 * an address free pattern, the expected match count, and the address read out of the operand, which
 * is what signature_count_matches() serves. All three sites must name the same cell; a build where
 * they disagree is not a build this understands, and the observer declines rather than picking one.
 *
 * Measured across the three builds that ship in one installation:
 *
 *   retail WMAIN.EXE   0x0048EBAC  0x0048EFBF  0x0048F056   all name 0x00862010
 *   wmain.exe          0x0048EBAC  0x0048EFBF  0x0048F056   all name 0x00862010
 *   obi.exe            0x0048EB4C  0x0048EF5F  0x0048EFF6   all name 0x00861FC0
 *
 * obi.exe is the recompile. It puts the cell 0x50 lower and the three sites at different addresses,
 * which is the whole argument for reading the operand rather than writing the address down: a table
 * of addresses would have read a different global on that build and reported a confident wrong
 * answer. The German retail executable is byte identical to the English one, so it is the same
 * build and not a fourth measurement.
 *
 * Requiring all three operands to agree is not decoration. It is the cheapest available test that
 * the three matches really are the three sites this understands and not two of them plus a
 * coincidence. */
static const uint8_t SIG_DEVICE_BACKEND[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax, [stdDisplay_pDevice]  */
    0x8B, 0x88, 0x00, 0x01, 0x00, 0x00,              /* mov ecx, [eax + 0x100]         */
    0x89, 0x4D, 0xFC,                                /* mov [ebp-4], ecx               */
    0x83, 0x7D, 0xFC, 0x00, 0x74, 0x08,              /* cmp [ebp-4], 0  / je  gdi      */
    0x83, 0x7D, 0xFC, 0x01, 0x74                     /* cmp [ebp-4], 1  / je  directdraw */
};
static const uint8_t MSK_DEVICE_BACKEND[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define EXPECTED_BACKEND_SITES    3u
#define BACKEND_OPERAND_OFFSET    0x01u

/* --- the second gate, inside the present itself, unique image wide --------------------------- *
 *   83 3D ?? ?? ?? ?? 00   cmp [stdDisplay_bWantBackBuffer], 0
 *   74 07                  je  past the flip
 *   E8 ?? ?? ?? ??         call the flip
 *   EB 05                  jmp the exit
 *   B8 01 00 00 00         mov eax, 1
 *
 * Unique on all three builds. It resolves to flag 0x0086201C and flip 0x00493448 on retail
 * WMAIN.EXE and on wmain.exe, and to flag 0x00861FCC and flip 0x004933E8 on obi.exe, which is the
 * same cross build behaviour as the backend anchor and the reason the relative call operand is
 * decoded rather than ignored: a candidate whose call target falls outside the image is rejected.
 *
 * The call displacement is a wildcard, and that is what makes this anchor survive the redirect
 * installed further down. The observer rewrites exactly those four bytes and the pattern does not
 * depend on them, so a second generation of this DLL in the same process still resolves the site
 * instead of switching itself off with a "did not resolve" warning. */
static const uint8_t SIG_FLIP_GATE[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [stdDisplay_bWantBackBuffer], 0 */
    0x74, 0x07,                                      /* je  past the flip                   */
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the flip                       */
    0xEB, 0x05,                                      /* jmp the exit                        */
    0xB8, 0x01, 0x00, 0x00, 0x00                     /* mov eax, 1                          */
};
static const uint8_t MSK_FLIP_GATE[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define FLIP_GATE_OPERAND_OFFSET  0x02u
#define FLIP_CALL_OPCODE_OFFSET   0x09u

/* --- the one site that derives the flip flags, at 0x00487640 on retail --------------------------*
 *   00487640  8B 90 44 01 00 00     mov edx, [eax + 0x144]   the device capabilities dword
 *   00487646  C1 EA 0B              shr edx, 11
 *   00487649  23 D7                 and edx, edi
 *   0048764B  3B CE                 cmp ecx, esi
 *   0048764D  89 15 84 59 85 00     mov [flipFlags], edx
 *
 * [0x00855984] is the dwFlags argument of every Flip, and this is the only instruction in the image
 * that writes it. Bit 11 of the capabilities is the "wait for the display rather than return at
 * once" capability, so the cell holds 0 or 1 and it is exactly the bit that decides whether the
 * retry loop spins. Unique on all three builds, and the operand is read rather than written down:
 *
 *   retail WMAIN.EXE   0x00487640 -> 0x00855984
 *   wmain.exe          0x00487640 -> 0x00855984
 *   obi.exe            0x004875E0 -> 0x00855934
 *
 * One caveat the cell alone does not cover, and the observer deliberately does not paper over it.
 * The function that writes this cell has two early returns before the store, and the cell lives in
 * BSS, so a run on which that function bails out keeps a zero here without ever executing the
 * instruction above. A reported 0 therefore means either "the capability is absent" or "the store
 * never ran", and this observer reports the value rather than the reason. */
static const uint8_t SIG_FLIP_FLAGS[] = {
    0x8B, 0x90, 0x44, 0x01, 0x00, 0x00,              /* mov edx, [eax + 0x144]  the caps dword */
    0xC1, 0xEA, 0x0B,                                /* shr edx, 11                            */
    0x23, 0xD7,                                      /* and edx, edi                           */
    0x3B, 0xCE,                                      /* cmp ecx, esi                           */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00               /* mov [flipFlags], edx                   */
};
static const uint8_t MSK_FLIP_FLAGS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define FLIP_FLAGS_OPERAND_OFFSET 0x0Fu

/* A changing answer is worth printing; a flapping one is worth stopping. */
#define MAX_REPORTS               4

typedef int32_t (__cdecl *flip_fn_t)(void);
typedef BOOL (WINAPI *query_thread_cycle_time_fn_t)(HANDLE, PULONG64);

typedef struct present_state {
    bool      armed;
    uintptr_t device_pointer_cell;   /* holds the address OF the pointer, not the pointer */
    uintptr_t want_back_buffer_cell;
    uintptr_t flip_flags_cell;
    uintptr_t flip_call_site;        /* the E8 of the one call to the flip */
    flip_fn_t flip_original;
    int       reports;
    bool      have_last;
    uint32_t  last_backend;
    uint32_t  last_want_back_buffer;

    /* Timing. `timing` is false when the call could not be redirected, and then only the path is
     * reported; nothing here silently degrades into a wrong number. */
    bool      timing;
    query_thread_cycle_time_fn_t query_cycles;
    LARGE_INTEGER frequency;

    uint64_t  flip_ticks;            /* accumulated inside the flip, this second */
    uint64_t  flip_cycles;
    uint32_t  flip_calls;
    uint64_t  flip_worst_ticks;

    LARGE_INTEGER second_start;
    uint64_t  second_start_cycles;
} present_state_t;

static present_state_t present_state;

/* All three sites must agree on the cell, so the answer does not depend on which one was found
 * first. Returns 0 when the count is wrong, when an operand is outside the image, or when the sites
 * disagree; every one of those means this is not a build whose display layer is understood. */
static uintptr_t resolve_device_pointer_cell(void)
{
    uintptr_t sites[SIGNATURE_MAX_REPORTED];
    uintptr_t cell = 0;
    size_t    count;
    size_t    i;

    count = signature_count_matches(SIG_DEVICE_BACKEND, MSK_DEVICE_BACKEND,
                                    sizeof SIG_DEVICE_BACKEND, sites, EXPECTED_BACKEND_SITES);
    if (count != EXPECTED_BACKEND_SITES) {
        log_warning("the display backend anchor matched %u times instead of %u, so the "
                    "presentation path cannot be read on this build",
                    (unsigned)count, (unsigned)EXPECTED_BACKEND_SITES);
        return 0;
    }

    for (i = 0; i < count; ++i) {
        uint32_t operand = 0;

        if (!memory_read_u32(sites[i] + BACKEND_OPERAND_OFFSET, &operand) ||
            !memory_is_inside_image((uintptr_t)operand, sizeof(uint32_t))) {
            log_warning("the display backend anchor at %08X names %08X, which is not inside the "
                        "image", (unsigned)sites[i], (unsigned)operand);
            return 0;
        }
        if (i == 0) {
            cell = (uintptr_t)operand;
        } else if ((uintptr_t)operand != cell) {
            log_warning("the three display backend anchors name different cells, %08X and %08X, "
                        "so none of them is trusted", (unsigned)cell, (unsigned)operand);
            return 0;
        }
    }

    return cell;
}

/* Fills in the back buffer flag, the flip routine and the address of the call displacement. The
 * flip address is not needed to answer the path question; it is read because a call target inside
 * the image is what distinguishes the present from a coincidence, and because the redirect needs
 * both the site and the original target. */
static bool resolve_flip_gate(void)
{
    uintptr_t site;
    uintptr_t call_site;
    uint32_t  cell = 0;
    uintptr_t target = 0;

    site = signature_find_unique(SIG_FLIP_GATE, MSK_FLIP_GATE, sizeof SIG_FLIP_GATE);
    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + FLIP_GATE_OPERAND_OFFSET, &cell) ||
        !memory_is_inside_image((uintptr_t)cell, sizeof(uint32_t))) {
        return false;
    }

    /* This also proves the E8 is where the pattern says it is and that its target is a function of
     * the host rather than something that has already been redirected out of the image. */
    call_site = site + FLIP_CALL_OPCODE_OFFSET;
    if (!patch_read_call_target(call_site, &target)) {
        return false;
    }

    present_state.want_back_buffer_cell = (uintptr_t)cell;
    present_state.flip_original         = (flip_fn_t)target;
    present_state.flip_call_site        = call_site;
    return true;
}

static uintptr_t resolve_flip_flags_cell(void)
{
    uintptr_t site;
    uint32_t  cell = 0;

    site = signature_find_unique(SIG_FLIP_FLAGS, MSK_FLIP_FLAGS, sizeof SIG_FLIP_FLAGS);
    if (site == 0) {
        return 0;
    }
    if (!memory_read_u32(site + FLIP_FLAGS_OPERAND_OFFSET, &cell) ||
        !memory_is_inside_image((uintptr_t)cell, sizeof(uint32_t))) {
        return 0;
    }
    return (uintptr_t)cell;
}

/* The wrapper the call is pointed at. It must leave everything except the return value exactly as
 * the real flip did, so it does nothing but read two counters on either side of it. */
static int32_t __cdecl hook_flip(void)
{
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    ULONG64  cycles_before = 0;
    ULONG64  cycles_after  = 0;
    uint64_t elapsed;
    int32_t  result;

    QueryPerformanceCounter(&before);
    if (present_state.query_cycles != NULL) {
        present_state.query_cycles(GetCurrentThread(), &cycles_before);
    }

    result = present_state.flip_original();

    QueryPerformanceCounter(&after);
    if (present_state.query_cycles != NULL) {
        present_state.query_cycles(GetCurrentThread(), &cycles_after);
        present_state.flip_cycles += (cycles_after - cycles_before);
    }

    elapsed = (uint64_t)(after.QuadPart - before.QuadPart);
    present_state.flip_ticks += elapsed;
    ++present_state.flip_calls;
    if (elapsed > present_state.flip_worst_ticks) {
        present_state.flip_worst_ticks = elapsed;
    }

    return result;
}

/* Rewrites the four displacement bytes of one call. This is not a detour, and the reason it is not
 * is worth having in front of you before anyone tidies it into one.
 *
 * stdDisplay_flip 0x00493448 opens like this:
 *
 *   00493448  55                    push ebp
 *   00493449  8B EC                 mov  ebp, esp
 *   0049344B  51                    push ecx
 *   0049344C  B8 01 00 00 00        mov  eax, 1        the loop head
 *   00493451  85 C0                 test eax, eax
 *   00493453  ..                    je   the exit
 *   00493455  8B 0D 84 59 85 00     mov  ecx, [flipFlags]
 *   ...
 *   004934B1  EB 99                 jmp  0049344C      the retry jumps back to the loop head
 *
 * The prologue is four bytes and a detour needs at least five, so the smallest legal boundary is
 * nine bytes and reaches 0x00493451. That range swallows the loop head at 0x0049344C, and the
 * retry at 0x004934B1 jumps straight to it, so a prologue detour would leave the loop jumping into
 * the middle of the `jmp rel32` this project would have written there. This function cannot be
 * detoured at its head at all.
 *
 * The call site is rewritten instead. There is exactly one, at 0x0048F0BC inside the present, and
 * SIG_FLIP_GATE has already located it at pattern offset 0x09. Two properties make this safe where
 * a detour was not: the loop is untouched, and the pattern that finds the site wildcards exactly
 * the four bytes written here. It chains by construction too, because resolve_flip_gate reads what
 * the displacement currently names and calls that as the original, so an earlier generation of this
 * DLL still in the process ends up as the middle link. */
static bool redirect_flip_call(void)
{
    /* patch_redirect_call reports a patch_result_t, and PATCH_RESULT_OK is zero. Reading that
     * return value as a boolean inverts it, which is how the first build of this file reported
     * "the flip call could not be redirected" about a redirect that had in fact succeeded. A field
     * run came back with the path and the flip flags and no timing at all, nothing was wrong with
     * any address, and the log line was the whole of the defect. When a helper in this tree returns
     * a `*_result_t`, compare it against its OK enumerator rather than testing it for truth. */
    return patch_redirect_call(present_state.flip_call_site, (const void *)&hook_flip) ==
           PATCH_RESULT_OK;
}

static void report_path(uint32_t backend, uint32_t want_back_buffer)
{
    const char *verdict;
    uint32_t    flip_flags = 0;
    bool        have_flags;

    if (backend == BACKEND_GDI) {
        verdict = "GDI. The finished surface is copied into the window with BitBlt, which goes "
                  "past a DirectDraw wrapper entirely, so no wrapper setting can change how this "
                  "frame reaches the screen and only a patch on the engine can";
    } else if (backend == BACKEND_DIRECTDRAW && want_back_buffer != 0u) {
        verdict = "DirectDraw, with a back buffer. The frame is presented by a flip, so the "
                  "presentation belongs to the DirectDraw layer and a wrapper's present model "
                  "reaches it";
    } else if (backend == BACKEND_DIRECTDRAW) {
        verdict = "DirectDraw, but no back buffer was requested, and on that combination the "
                  "present function returns success WITHOUT presenting anything";
    } else {
        verdict = "neither of the two authored values, which this observer does not understand and "
                  "will not guess at";
    }

    have_flags = present_state.flip_flags_cell != 0 &&
                 memory_read_u32(present_state.flip_flags_cell, &flip_flags);

    diag_log_write("present: backend=%u backbuffer=%u -> %s",
                   (unsigned)backend, (unsigned)want_back_buffer, verdict);
    log_info("presentation path: backend=%u backbuffer=%u -> %s",
             (unsigned)backend, (unsigned)want_back_buffer, verdict);

    if (!have_flags) {
        return;
    }
    diag_log_write("present: flip flags=%u -> the flip %s", (unsigned)flip_flags,
                   (flip_flags != 0)
                       ? "is asked to WAIT for the display, so the thread should block in the "
                         "driver and cost no CPU"
                       : "is asked to return AT ONCE, so the engine's retry loop is free to spin "
                         "and burn a core while it waits");
    log_info("flip flags=%u (%s)", (unsigned)flip_flags,
             (flip_flags != 0) ? "wait for the display" : "return at once");
}

/* One line a second, and the two shares are what the line exists for.
 *
 * The question is whether the time inside the flip is spent waiting in the driver or burning the
 * thread in the retry loop. Wall time alone cannot tell those apart, and turning a cycle count into
 * seconds would need the machine's clock rate, which is not available here and would need
 * calibrating if it were. It is not needed. Both the wall time inside the flip and the thread
 * cycles inside it are accumulated over the same second and each is reported as a percentage of it,
 * so the engine's own drawing supplies the reference: it is known to be busy work.
 *
 *   cycle share close to wall share  the flip consumed CPU for the whole time it took, so it spun
 *   cycle share far below wall share the thread was descheduled, so it blocked
 *
 * QueryThreadCycleTime is resolved with GetProcAddress rather than linked, so a machine without it
 * loses the verdict and keeps the timing. */
static void report_second(void)
{
    LARGE_INTEGER now;
    ULONG64  cycles_now = 0;
    uint64_t wall_ticks;
    uint64_t wall_cycles = 0;
    double   seconds;
    double   flip_ms;
    double   worst_ms;
    double   wall_share;
    double   cycle_share = -1.0;

    QueryPerformanceCounter(&now);
    wall_ticks = (uint64_t)(now.QuadPart - present_state.second_start.QuadPart);
    seconds = (double)wall_ticks / (double)present_state.frequency.QuadPart;
    if (seconds < 1.0) {
        return;
    }

    if (present_state.query_cycles != NULL &&
        present_state.query_cycles(GetCurrentThread(), &cycles_now)) {
        wall_cycles = cycles_now - present_state.second_start_cycles;
    }

    flip_ms  = 1000.0 * (double)present_state.flip_ticks /
               (double)present_state.frequency.QuadPart;
    worst_ms = 1000.0 * (double)present_state.flip_worst_ticks /
               (double)present_state.frequency.QuadPart;
    wall_share = 100.0 * (double)present_state.flip_ticks / (double)wall_ticks;
    if (wall_cycles > 0u) {
        cycle_share = 100.0 * (double)present_state.flip_cycles / (double)wall_cycles;
    }

    if (cycle_share < 0.0) {
        diag_log_write("flip: %u calls, %.2f ms each, worst %.2f, %.1f%% of wall time "
                       "(thread cycle counter unavailable, so spin and block cannot be separated)",
                       (unsigned)present_state.flip_calls,
                       (present_state.flip_calls != 0)
                           ? flip_ms / (double)present_state.flip_calls : 0.0,
                       worst_ms, wall_share);
    } else {
        const char *verdict = "inconclusive";

        if (wall_share >= 5.0) {
            /* The flip's share of the thread's cycles against its share of the wall clock. Near
             * one means it consumed CPU for the whole time it took, which is a spin. */
            double ratio = cycle_share / wall_share;

            verdict = (ratio >= 0.5) ? "SPINNING, the wait costs a core"
                                     : ((ratio <= 0.1) ? "blocking, the wait is free"
                                                       : "partly spinning");
        } else {
            verdict = "not where the frame time goes";
        }
        diag_log_write("flip: %u calls, %.2f ms each, worst %.2f, %.1f%% of wall time and "
                       "%.1f%% of thread cycles -> %s",
                       (unsigned)present_state.flip_calls,
                       (present_state.flip_calls != 0)
                           ? flip_ms / (double)present_state.flip_calls : 0.0,
                       worst_ms, wall_share, cycle_share, verdict);
    }

    present_state.flip_ticks   = 0;
    present_state.flip_cycles  = 0;
    present_state.flip_calls   = 0;
    present_state.flip_worst_ticks = 0;
    present_state.second_start = now;
    present_state.second_start_cycles = cycles_now;
}

/* The pointer is null until the display has been opened, which is why this runs per frame rather
 * than once at install: the graphics startup runs well after the loader has put the mods in, so a
 * read at install time would see null on every launch and would have to report either nothing or a
 * guess. A frame on which it is still null is not an error, says nothing, and costs one image read.
 *
 * Reporting again on change rather than latching the first answer is deliberate. A mode change
 * closes and reopens the display, and a backend that changed under a running game is exactly the
 * kind of thing one wants in the log rather than absent from it. The report counter is what keeps a
 * flapping value from becoming a stream.
 *
 * This shares the frame hook with the other observers and with the feature DLLs, which is expected
 * rather than a conflict: the shared layer's hook is a chained detour. */
static void on_frame_end(void)
{
    uint32_t device;
    uint32_t backend = 0;
    uint32_t want_back_buffer = 0;

    if (present_state.timing) {
        report_second();
    }

    if (present_state.reports >= MAX_REPORTS) {
        return;
    }
    if (!memory_read_u32(present_state.device_pointer_cell, &device) || device == 0u) {
        return;
    }
    if (!memory_read_u32((uintptr_t)device + DEVICE_BACKEND_OFFSET, &backend)) {
        return;
    }
    if (!memory_read_u32(present_state.want_back_buffer_cell, &want_back_buffer)) {
        return;
    }

    if (present_state.have_last &&
        backend == present_state.last_backend &&
        want_back_buffer == present_state.last_want_back_buffer) {
        return;
    }

    present_state.have_last             = true;
    present_state.last_backend          = backend;
    present_state.last_want_back_buffer = want_back_buffer;
    ++present_state.reports;

    report_path(backend, want_back_buffer);

    if (present_state.reports >= MAX_REPORTS) {
        diag_log_write("present: the path changed %d times, no further changes will be reported",
                       MAX_REPORTS);
    }
}

/* Timing is optional and every one of its failures leaves the path report working. */
static void arm_timing(void)
{
    HMODULE kernel32;

    if (!QueryPerformanceFrequency(&present_state.frequency) ||
        present_state.frequency.QuadPart == 0) {
        log_warning("the performance counter is not available, so the present cannot be timed");
        return;
    }

    kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 != NULL) {
        present_state.query_cycles = (query_thread_cycle_time_fn_t)
            GetProcAddress(kernel32, "QueryThreadCycleTime");
    }

    if (!redirect_flip_call()) {
        log_warning("the flip call could not be redirected, so the present is not timed. The "
                    "presentation path is still reported.");
        return;
    }

    QueryPerformanceCounter(&present_state.second_start);
    if (present_state.query_cycles != NULL) {
        present_state.query_cycles(GetCurrentThread(), &present_state.second_start_cycles);
    }
    present_state.timing = true;
}

/* What this observer does not establish, so that nobody reads more out of a log line than is in it.
 * It reports what this installation does. It does not prove which branch the engine can reach in
 * general. Whether a device record can carry backend 0 at all, given that the enumeration callback
 * writes 1 into every record it fills, is a separate piece of reverse engineering and is not
 * answered here. */
int diag_present_install(int level)
{
    if (level <= 0 || present_state.armed) {
        return 0;
    }

    present_state.device_pointer_cell = resolve_device_pointer_cell();
    if (present_state.device_pointer_cell == 0) {
        return 0;
    }
    if (!resolve_flip_gate()) {
        log_warning("the flip gate inside the present did not resolve uniquely, so the "
                    "presentation path stays unreported");
        return 0;
    }
    present_state.flip_flags_cell = resolve_flip_flags_cell();

    if (!frame_hook_add(on_frame_end)) {
        log_warning("the per-frame hook could not be installed, and the presentation path can only "
                    "be read after the display has been opened, so it stays unreported");
        return 0;
    }

    arm_timing();
    present_state.armed = true;

    log_info("presentation path armed: the engine chooses per display device between a GDI BitBlt "
             "into the window and a DirectDraw flip, and which one is live decides whether a "
             "DirectDraw wrapper owns the presentation at all. The answer is read from the first "
             "frame at which the display has been opened, and again only if it changes.%s",
             present_state.timing
                 ? " The present is also timed, and the thread cycles spent inside it are counted "
                   "beside the wall clock, which is what separates a flip that blocks in the "
                   "driver from one whose retry loop spins."
                 : "");
    if (present_state.flip_flags_cell == 0) {
        log_warning("the flip flags cell did not resolve, so whether the flip is asked to wait for "
                    "the display stays unreported");
    }
    if (level >= 2) {
        log_info("  device pointer %08X, back buffer flag %08X, flip flags %08X, flip routine "
                 "%08X, call site %08X, timing %s",
                 (unsigned)present_state.device_pointer_cell,
                 (unsigned)present_state.want_back_buffer_cell,
                 (unsigned)present_state.flip_flags_cell,
                 (unsigned)(uintptr_t)present_state.flip_original,
                 (unsigned)present_state.flip_call_site,
                 present_state.timing ? "on" : "OFF");
    }
    return 1;
}
