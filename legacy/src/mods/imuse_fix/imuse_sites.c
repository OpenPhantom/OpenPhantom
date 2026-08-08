/* imuse_sites.c: find the heartbeat gate and the heartbeat tick counter inside iMUSE.DLL.
 *
 * ==============================================================================================
 * Why these two cells
 *
 * The music does not stream on the game's thread. iMUSE.DLL imports timeSetEvent/timeKillEvent
 * from WINMM and installs a PERIODIC multimedia timer whose callback is the only caller of the
 * script engine's heartbeat. The callback, in full:
 *
 *   A1 <busy>            mov  eax,[busy]          ; a teardown flag
 *   85 C0 / 75 33        test / jne exit
 *   A1 <enabled>         mov  eax,[enabled]
 *   85 C0 / 74 2A        test / je  exit
 *   68 <cs>              push <critical section>
 *   FF 15 <EnterCS>      call
 *   A1 <busy>            mov  eax,[busy]          ; re-tested inside the lock
 *   85 C0 / 75 05        test / jne skip
 *   E8 <rel32>           call ImHeartbeat
 *   68 <cs> / FF 15 <LeaveCS>
 *   FF 05 <TICKS>        inc  dword ptr [ticks]   ; <-- ONE per executed heartbeat
 *   C2 14 00             ret  0x14                ; 5 args = LPTIMECALLBACK
 *
 * The heartbeat is also the only thing that refills the music buffer, and that buffer is played
 * with DSBPLAY_LOOPING. So if the heartbeat stops, nothing goes silent: the buffer keeps circling
 * whatever PCM it last held. That is the whole shape of the defect being chased.
 *
 * What can stop it is not the critical section above. It is a second, hand-rolled gate:
 *
 *   ImLock     FF 05 <GATE> C3                          inc  dword ptr [gate]
 *              90 x9                                    padding to the next paragraph
 *   ImUnlock   A1 <GATE> 85 C0 74 06 48 A3 <GATE> C3     load / test / dec / store
 *
 * and the heartbeat runs only while that gate reads 0. NEITHER of those is interlocked. The game
 * thread raises and lowers the gate on every frame and on every cue; the timer thread raises and
 * lowers it too. Two concurrent releases can both read the same value and both store the same
 * decrement, which leaves the gate standing one too high with nobody holding it, and the `je`
 * floor means it can only ever get stuck HIGH. From that moment the heartbeat never runs again.
 *
 * On the single-core machines this shipped for, `inc [mem]` could not be interrupted mid-
 * instruction and the load/store pair almost never lost. Neither is true on more than one core.
 *
 * These two cells therefore state the failure in two numbers, without interpretation:
 * the tick counter stops advancing, and the gate is non-zero.
 * ============================================================================================ */
#include "imuse_sites.h"

#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4, "imuse_fix reads 32-bit operands out of iMUSE.DLL");

/* WMAIN's import table spells it this way; GetModuleHandle is case-insensitive, so the spelling
 * is documentation rather than a requirement. */
#define IMUSE_MODULE_NAME "iMUSE.DLL"

/* --- ImLock and ImUnlock, as one pattern ------------------------------------------------------
 * The two sit in consecutive 16-byte slots with the compiler's 0x90 padding between them, which
 * is what makes a 32-byte pattern out of two functions that are 7 and 16 bytes long. Neither is
 * exported, so a pattern is the only way to them; both operands are wildcarded and read out. */
static const uint8_t SIG_IMUSE_GATE[] = {
    0xFF, 0x05, 0x00, 0x00, 0x00, 0x00,     /* inc  [gate]            <- operand +0x02 */
    0xC3,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0xA1, 0x00, 0x00, 0x00, 0x00,           /* mov  eax,[gate]        <- operand +0x11 */
    0x85, 0xC0,
    0x74, 0x06,
    0x48,
    0xA3, 0x00, 0x00, 0x00, 0x00,           /* mov  [gate],eax        <- operand +0x1B */
    0xC3
};
static const uint8_t MSK_IMUSE_GATE[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF
};
_Static_assert(sizeof(SIG_IMUSE_GATE) == sizeof(MSK_IMUSE_GATE),
               "the gate pattern and its mask are different lengths");

#define OFFSET_GATE_INC   0x02u
#define OFFSET_GATE_LOAD  0x11u
#define OFFSET_GATE_STORE 0x1Bu

/* --- the multimedia-timer callback, for the tick counter --------------------------------------
 * Anchored on its two guard tests and its tail. The `ret 0x14` is what proves it is the timer
 * callback rather than an ordinary routine: five stack arguments is LPTIMECALLBACK's shape. */
static const uint8_t SIG_IMUSE_HEARTBEAT_TICK[] = {
    0x68, 0x00, 0x00, 0x00, 0x00,           /* push <critical section> */
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,     /* call [EnterCriticalSection] */
    0xA1, 0x00, 0x00, 0x00, 0x00,           /* mov  eax,[busy] */
    0x85, 0xC0,
    0x75, 0x05,
    0xE8, 0x00, 0x00, 0x00, 0x00,           /* call ImHeartbeat */
    0x68, 0x00, 0x00, 0x00, 0x00,           /* push <critical section> */
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,     /* call [LeaveCriticalSection] */
    0xFF, 0x05, 0x00, 0x00, 0x00, 0x00,     /* inc  [ticks]           <- operand +0x26 */
    0xC2, 0x14, 0x00                        /* ret  0x14 */
};
static const uint8_t MSK_IMUSE_HEARTBEAT_TICK[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_IMUSE_HEARTBEAT_TICK) == sizeof(MSK_IMUSE_HEARTBEAT_TICK),
               "the heartbeat pattern and its mask are different lengths");

#define OFFSET_TICK_INC 0x26u

/* --- ImHeartbeat's prologue: all four gate cells in one place ---------------------------------
 * The body is refused at FOUR tests, not one, and this pattern covers three of them plus the
 * 20 ms rate limit. It is also a fourth, independent witness for the gate cell that ImLock and
 * ImUnlock already named, if the two disagree, one of the patterns matched the wrong place.
 *
 *   55 56 57                push ebp/esi/edi
 *   E8 <rel32>              call the clock (result in eax, milliseconds)
 *   8B 0D <REENTRY>         mov ecx,[re-entrancy flag]      +0x0A
 *   85 C9 / 0F 85 <rel32>   set  -> refuse
 *   8B 0D <GATE>            mov ecx,[the gate]              +0x18
 *   85 C9 / 0F 85 <rel32>   non-zero -> refuse
 *   8B 0D <LAST_MS>         mov ecx,[last body ran]         +0x26
 *   85 C9 / 7C 0D           negative -> skip the rate limit
 *   8B D0 / 2B D1           edx = now, last
 *   83 FA 14                cmp edx,0x14                    the 20 ms rate limit
 *   0F 82 <rel32>           too soon -> refuse
 *   A3 <LAST_MS>            mov [last body ran],eax         +0x3C
 *   C7 05 <REENTRY> 1       mov [re-entrancy flag],1        +0x42
 *   E8 <rel32>              call the body
 */
static const uint8_t SIG_IMUSE_HEARTBEAT[] = {
    0x55, 0x56, 0x57,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC9,
    0x0F, 0x85, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC9,
    0x0F, 0x85, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC9,
    0x7C, 0x0D,
    0x8B, 0xD0,
    0x2B, 0xD1,
    0x83, 0xFA, 0x14,
    0x0F, 0x82, 0x00, 0x00, 0x00, 0x00,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xE8
};
static const uint8_t MSK_IMUSE_HEARTBEAT[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
_Static_assert(sizeof(SIG_IMUSE_HEARTBEAT) == sizeof(MSK_IMUSE_HEARTBEAT),
               "the heartbeat prologue pattern and its mask are different lengths");

#define OFFSET_HB_REENTRY_READ  0x0Au
#define OFFSET_HB_GATE_READ     0x18u
#define OFFSET_HB_LAST_READ     0x26u
#define OFFSET_HB_LAST_WRITE    0x3Cu
#define OFFSET_HB_REENTRY_WRITE 0x42u

/* --- the body of ImSetParam ------------------------------------------------------------------
 * The export at ImSetParam is three loads, three pushes and a call into this; every internal
 * caller goes straight here. It takes the lock in its fourth instruction, before it has decided
 * anything, and five of its refusals then return without giving it back.
 *
 *   56                 push esi
 *   8B 35 <soundlist>  mov  esi,[the sound list head]
 *   57                 push edi
 *   E8 <ImLock>        call                       <-- unconditional
 *   85 F6 / 0F 84      test esi,esi / je  (empty list -> unlock, return -4)
 *   8B 44 24 0C        mov  eax,[esp+0xC]         the handle
 *   39 46 0C           cmp  [esi+0xC],eax
 *   74 14 / 8B 76 04 / 85 F6 / 75 F4               the list walk
 *   E8 <ImUnlock> / B8 FC FFFFFF / 5F              not found -> unlock, return -4
 */
static const uint8_t SIG_IMUSE_SET_PARAM[] = {
    0x56,
    0x8B, 0x35, 0x00, 0x00, 0x00, 0x00,
    0x57,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xF6,
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x44, 0x24, 0x0C,
    0x39, 0x46, 0x0C,
    0x74, 0x14,
    0x8B, 0x76, 0x04,
    0x85, 0xF6,
    0x75, 0xF4,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xB8, 0xFC, 0xFF, 0xFF, 0xFF,
    0x5F
};
static const uint8_t MSK_IMUSE_SET_PARAM[] = {
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
_Static_assert(sizeof(SIG_IMUSE_SET_PARAM) == sizeof(MSK_IMUSE_SET_PARAM),
               "the ImSetParam pattern and its mask are different lengths");

/* `push esi` + `mov esi,[abs32]` = 1 + 6, the first instruction boundary at or past the five
 * bytes a jmp rel32 needs. */
#define SET_PARAM_PROLOGUE 7u

/* ImUnlock sits in the paragraph after ImLock, which is what lets one pattern name both. */
#define OFFSET_IM_UNLOCK_FROM_LOCK 0x10u

/* --- ImPrintf, and the one pointer it calls ---------------------------------------------------
 * ImPrintf is EXPORTED, so it needs no pattern at all, only a check that the body is the shape
 * the operand offset was measured from. It formats into a static buffer and then calls a single
 * host-supplied function with the finished string:
 *
 *   8B 4C 24 04        mov  ecx,[esp+4]        the format
 *   8D 44 24 08        lea  eax,[esp+8]        the varargs
 *   50 51              push eax / push ecx
 *   68 <buffer>        push <static buffer>
 *   E8 <rel32>         call the formatter
 *   83 C4 0C           add  esp,0x0C
 *   68 <buffer>        push <static buffer>
 *   FF 15 <SLOT>       call [slot]             <-- the operand at +0x1E
 *   83 C4 04 33 C0 C3
 *
 * Only the two absolute buffer pushes and the two relocatable operands are wildcarded. If the
 * body does not match, the offset below is not trustworthy and nothing is taken. */
static const uint8_t IMPRINTF_BODY[] = {
    0x8B, 0x4C, 0x24, 0x04,
    0x8D, 0x44, 0x24, 0x08,
    0x50,
    0x51,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04,
    0x33, 0xC0,
    0xC3
};
static const uint8_t IMPRINTF_MASK[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF
};
_Static_assert(sizeof(IMPRINTF_BODY) == sizeof(IMPRINTF_MASK),
               "the ImPrintf body check and its mask are different lengths");

#define OFFSET_IMPRINTF_SLOT 0x1Eu

/* ============================================================================================ */
typedef struct module_text {
    const uint8_t *start;
    size_t         size;
} module_text_t;

/* The DLL's own code range, out of its own headers. Nothing here trusts a fixed image base. */
static bool resolve_module_text(uintptr_t base, module_text_t *out)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_SECTION_HEADER *section;
    unsigned i;

    if (!memory_is_readable_range(base, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    nt = (const IMAGE_NT_HEADERS *)(base + (uintptr_t)dos->e_lfanew);
    if (!memory_is_readable_range((uintptr_t)nt, sizeof(*nt)) ||
        nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    section = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if ((section[i].Characteristics & IMAGE_SCN_CNT_CODE) != 0) {
            out->start = (const uint8_t *)(base + section[i].VirtualAddress);
            out->size = section[i].Misc.VirtualSize;
            return memory_is_readable_range((uintptr_t)out->start, out->size);
        }
    }
    return false;
}

/* A masked search over one module's code. Deliberately local: common/signature.c searches the
 * HOST image, which is the right thing for every other feature and the wrong thing here. */
static const uint8_t *find_unique(const module_text_t *text, const uint8_t *pattern,
                                  const uint8_t *mask, size_t size, const char *what)
{
    const uint8_t *hit = NULL;
    size_t i, j;
    size_t matches = 0;

    if (text->size < size) {
        return NULL;
    }
    for (i = 0; i + size <= text->size; ++i) {
        for (j = 0; j < size; ++j) {
            if (mask[j] != 0 && text->start[i + j] != pattern[j]) {
                break;
            }
        }
        if (j == size) {
            matches++;
            if (matches > 1) {
                log_warning("the %s pattern matches more than once in iMUSE.DLL - refused, "
                            "because a pattern that is not unique cannot identify anything",
                            what);
                return NULL;
            }
            hit = text->start + i;
        }
    }
    if (hit == NULL) {
        log_warning("the %s pattern does not occur in iMUSE.DLL - this is a different build of "
                    "the music DLL than the one this was written against", what);
    }
    return hit;
}

static bool read_operand(const uint8_t *site, uint32_t offset, const char *what, uint32_t *out)
{
    uint32_t cell;

    memcpy(&cell, site + offset, sizeof(cell));
    if (!memory_is_readable_range(cell, sizeof(uint32_t))) {
        log_warning("the %s would be at %08X, which is not readable, refused",
                    what, (unsigned)cell);
        return false;
    }
    *out = cell;
    return true;
}

/* The heartbeat's own prologue. It carries the two cells that separate "the gate is stuck" from
 * "the body never returned", and it names the gate a fourth time. */
static void resolve_heartbeat_cells(const module_text_t *text, uint32_t known_gate,
                                    imuse_sites_t *out)
{
    const uint8_t *site;
    uint32_t reentry_read = 0, reentry_write = 0;
    uint32_t last_read = 0, last_write = 0;
    uint32_t gate_here = 0;

    site = find_unique(text, SIG_IMUSE_HEARTBEAT, MSK_IMUSE_HEARTBEAT,
                       sizeof(SIG_IMUSE_HEARTBEAT), "the heartbeat prologue");
    if (site == NULL) {
        log_warning("without the heartbeat prologue the log can say that the timer is firing and "
                    "that the gate is held, but not whether the body is being refused or has "
                    "simply never returned");
        return;
    }

    if (!read_operand(site, OFFSET_HB_GATE_READ, "heartbeat gate", &gate_here)) {
        return;
    }
    if (gate_here != known_gate) {
        log_warning("the heartbeat tests a different cell (%08X) than ImLock maintains (%08X) - "
                    "one of the two patterns matched the wrong place, so neither is believed",
                    (unsigned)gate_here, (unsigned)known_gate);
        return;
    }

    if (!read_operand(site, OFFSET_HB_LAST_READ, "heartbeat timestamp", &last_read) ||
        !read_operand(site, OFFSET_HB_LAST_WRITE, "heartbeat timestamp", &last_write) ||
        !read_operand(site, OFFSET_HB_REENTRY_READ, "heartbeat re-entrancy flag", &reentry_read) ||
        !read_operand(site, OFFSET_HB_REENTRY_WRITE, "heartbeat re-entrancy flag",
                      &reentry_write)) {
        return;
    }
    /* Each of the two is read in one instruction and written in another; both must name the same
     * cell, or the pattern is sitting on something that only looks like the prologue. */
    if (last_read != last_write || reentry_read != reentry_write) {
        log_warning("the heartbeat prologue reads and writes different cells (%08X/%08X, "
                    "%08X/%08X), refused", (unsigned)last_read, (unsigned)last_write,
                    (unsigned)reentry_read, (unsigned)reentry_write);
        return;
    }

    out->heartbeat_last_ms = (volatile int32_t *)(uintptr_t)last_read;
    out->heartbeat_reentry = (volatile int32_t *)(uintptr_t)reentry_read;
    log_info("the heartbeat body is refused at four tests, and two of them are readable: the "
             "re-entrancy flag %08X and the timestamp of the last body that ran %08X. It also "
             "tests the same gate %08X ImLock maintains, which is a fourth witness for that cell.",
             (unsigned)reentry_read, (unsigned)last_read, (unsigned)gate_here);
}

/* The commentary pointer, from ImPrintf's exported body. Failure here is not fatal: it costs the
 * running commentary, not the two counters the failure is actually stated in. */
static void resolve_trace_slot(HMODULE module, imuse_sites_t *out)
{
    const uint8_t *body = (const uint8_t *)GetProcAddress(module, "ImPrintf");
    uint32_t slot = 0;
    size_t i;

    if (body == NULL) {
        log_warning("iMUSE.DLL does not export ImPrintf, its own running commentary cannot be "
                    "captured this session");
        return;
    }
    if (!memory_is_readable_range((uintptr_t)body, sizeof(IMPRINTF_BODY))) {
        return;
    }
    for (i = 0; i < sizeof(IMPRINTF_BODY); ++i) {
        if (IMPRINTF_MASK[i] != 0 && body[i] != IMPRINTF_BODY[i]) {
            log_warning("ImPrintf is not the body this was measured against (byte %u differs) - "
                        "the commentary pointer is NOT taken, because the offset to it would be "
                        "a guess", (unsigned)i);
            return;
        }
    }
    if (!read_operand(body, OFFSET_IMPRINTF_SLOT, "iMUSE commentary pointer", &slot)) {
        return;
    }
    out->trace_slot = (void (__cdecl **)(const char *))(uintptr_t)slot;
    log_info("iMUSE's own commentary is routed through the pointer at %08X, which the game "
             "installs at startup - 59 places in the DLL feed it", (unsigned)slot);
}

bool imuse_sites_resolve(imuse_sites_t *out)
{
    HMODULE       module;
    module_text_t text;
    const uint8_t *gate_site;
    const uint8_t *tick_site;
    uint32_t gate_inc = 0, gate_load = 0, gate_store = 0, ticks = 0;

    memset(out, 0, sizeof(*out));

    module = GetModuleHandleA(IMUSE_MODULE_NAME);
    if (module == NULL) {
        log_warning("iMUSE.DLL is not loaded in this process, the music probe stays off. That is "
                    "normal only if music was disabled before the audio system came up");
        return false;
    }
    out->module_base = (uintptr_t)module;

    if (!resolve_module_text(out->module_base, &text)) {
        log_warning("iMUSE.DLL at %08X has no readable code section, refused",
                    (unsigned)out->module_base);
        return false;
    }

    /* The exported entry points come from the export table, which is exact and needs no pattern. */
    out->set_state = (int32_t (__cdecl *)(int32_t))GetProcAddress(module, "ImSetState");
    out->set_sequence = (int32_t (__cdecl *)(int32_t))GetProcAddress(module, "ImSetSequence");

    gate_site = find_unique(&text, SIG_IMUSE_GATE, MSK_IMUSE_GATE, sizeof(SIG_IMUSE_GATE),
                            "ImLock/ImUnlock");
    if (gate_site == NULL) {
        return false;
    }
    /* Three operands, one cell. All three must agree, because the whole point of the pair is that
     * the increment and the decrement address the same counter. */
    if (!read_operand(gate_site, OFFSET_GATE_INC, "heartbeat gate", &gate_inc) ||
        !read_operand(gate_site, OFFSET_GATE_LOAD, "heartbeat gate", &gate_load) ||
        !read_operand(gate_site, OFFSET_GATE_STORE, "heartbeat gate", &gate_store)) {
        return false;
    }
    if (gate_inc != gate_load || gate_inc != gate_store) {
        log_warning("ImLock and ImUnlock name different cells (%08X, %08X, %08X), refused",
                    (unsigned)gate_inc, (unsigned)gate_load, (unsigned)gate_store);
        return false;
    }

    tick_site = find_unique(&text, SIG_IMUSE_HEARTBEAT_TICK, MSK_IMUSE_HEARTBEAT_TICK,
                            sizeof(SIG_IMUSE_HEARTBEAT_TICK), "the timer callback");
    if (tick_site == NULL || !read_operand(tick_site, OFFSET_TICK_INC, "heartbeat tick counter",
                                           &ticks)) {
        return false;
    }

    out->gate = (volatile int32_t *)(uintptr_t)gate_inc;
    out->heartbeat_ticks = (volatile int32_t *)(uintptr_t)ticks;
    out->im_lock = (uintptr_t)gate_site;
    out->im_unlock = (uintptr_t)gate_site + OFFSET_IM_UNLOCK_FROM_LOCK;

    {
        const uint8_t *body = find_unique(&text, SIG_IMUSE_SET_PARAM, MSK_IMUSE_SET_PARAM,
                                          sizeof(SIG_IMUSE_SET_PARAM), "the ImSetParam body");
        if (body != NULL) {
            out->set_param_body = (uintptr_t)body;
        } else {
            log_warning("the ImSetParam body did not resolve, the deterministic half of the lock "
                        "leak cannot be closed this session");
        }
    }

    /* Optional from here on. Everything above already states the failure; these two sharpen it. */
    resolve_heartbeat_cells(&text, gate_inc, out);
    resolve_trace_slot(module, out);

    log_info("iMUSE.DLL at %08X: heartbeat gate %08X (named by all three ImLock/ImUnlock "
             "operands), heartbeat tick counter %08X, ImSetState %08X. The script engine runs on "
             "a WINMM timer thread and executes only while that gate reads 0; neither the "
             "increment nor the decrement is interlocked.",
             (unsigned)out->module_base, (unsigned)gate_inc, (unsigned)ticks,
             (unsigned)(uintptr_t)out->set_state);
    return true;
}
