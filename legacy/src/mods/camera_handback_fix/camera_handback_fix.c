/* camera_handback_fix.c: see camera_handback_fix.h. */
#include "camera_handback_fix.h"

#include "handback_rule.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stdint.h>

#define HANDBACK_SECTION "camera_handback_fix"

/* --- bapview_overrideOn 0x0041840A ------------------------------------------------------------ *
 *   55 8B EC                     push ebp / mov ebp,esp
 *   C7 05 E8 B4 5B 00 01000000   mov dword [flag],1
 *   8B 45 08                     mov eax,[ebp+8]         the camera group it was handed
 *   A3 F0 B4 5B 00               mov [forcedRegion],eax
 *   5D C3                        pop ebp / ret
 *
 * The whole function is twenty three bytes and all of it is taken; thirteen are already unique.
 * The prologue ends after the store, on an instruction boundary. On 32-bit x86 the address in that
 * store is absolute, not relative to the instruction, so those bytes move to a trampoline safely.
 *
 * The flag's own address is IN the pattern and cannot be kept out of it, the function being
 * nothing but two stores. That is read back out as an operand below rather than written down a
 * second time, so there is only ever one copy of it here. */
static const uint8_t SIG_OVERRIDE_ON[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0xE8, 0xB4, 0x5B, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0xA3, 0xF0, 0xB4, 0x5B, 0x00, 0x5D, 0xC3
};
#define OVERRIDE_ON_PROLOGUE 13u
#define OVERRIDE_ON_FLAG_OPERAND 0x05u

/* --- bapview_overrideOff 0x00418421 ----------------------------------------------------------- *
 *   55 8B EC / C7 05 E8 B4 5B 00 00000000 / 5D C3    the same shape, storing zero.
 *
 * Fifteen bytes, all taken, unique at thirteen. It takes no argument and leaves the forced region
 * cell alone, so everything here reads the flag and not that cell. */
static const uint8_t SIG_OVERRIDE_OFF[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0xE8, 0xB4, 0x5B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x5D, 0xC3
};
#define OVERRIDE_OFF_PROLOGUE 13u

/* --- Dialog_SpeakSingle 0x00430D12 ------------------------------------------------------------ *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 EC 0C              sub esp,0x0C
 *   C7 45 F8 00000000     mov dword [ebp-8],0
 *   8B 45 10              mov eax,[ebp+0x10]
 *
 * Sixteen bytes are unique, twenty are taken. Detoured, and that detour does the attribution on
 * its own: the setter is called from inside this function, so a take that happens while this is on
 * the stack is the dialogue's and one that happens outside it is somebody else's.
 *
 * It used to be attributed by the address control returned to, derived from this site. That only
 * works while this module's hook is the OUTERMOST link of the detour chain. The loader installs
 * in name order, so diagnostics installs after this module and chains in front of it, and with
 * [diagnostics] CameraOwner=1 the return address is inside diagnostics.dll: the take was credited
 * to nobody, and the fix installed, logged success and never fired, in exactly the session
 * somebody was instrumenting the fault in. Being on the stack does not care who chained where.
 *
 * The signature is `int Dialog_SpeakSingle(void *speaker, int cameraGroup, i32 lineId,
 * const void *pos)`, cdecl, and the call it makes is `if (cameraGroup >= 0)
 * bapview_overrideOn(cameraGroup)`. */
static const uint8_t SIG_DIALOG_SPEAK_SINGLE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x10, 0x50, 0xE8, 0x27, 0x04
};
/* Six bytes: push ebp; mov ebp,esp; sub esp,0x0C. diagnostics detours this one too. */
#define DIALOG_SPEAK_SINGLE_PROLOGUE 6u

/* --- Dialog_Close 0x00430E82 ------------------------------------------------------------------ *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 7D 08 00           cmp dword [ebp+8],0     its one argument
 *   74 0A                 je  +10
 *
 * Twelve bytes are unique, sixteen taken. The prologue stops at SEVEN, before the jump: a `je`
 * carries an operand relative to itself, and copying that to a trampoline aims it somewhere else.
 * Seven is an instruction boundary and holds nothing relative. */
static const uint8_t SIG_DIALOG_CLOSE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x0A, 0x6A, 0x00, 0xE8,
    0x2F, 0x4A, 0x03, 0x00
};
#define DIALOG_CLOSE_PROLOGUE 7u

/* --- Dialog_LeaveInputLock 0x00430F18 ---------------------------------------------------------- *
 *   55 8B EC 51           push ebp / mov ebp,esp / push ecx
 *   C7 45 FC 00000000     mov dword [ebp-4],0
 *   83 3D 8C 4D 6C 00 00  cmp dword [lock],0
 *
 * Twenty bytes for uniqueness, twelve and sixteen both being ambiguous. NOT detoured either: it is
 * resolved so the cinematic lock cell can be read out of its own compare as an operand. */
static const uint8_t SIG_DIALOG_LEAVE_LOCK[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x8C, 0x4D, 0x6C, 0x00, 0x00, 0x7E, 0x25
};
#define DIALOG_LEAVE_LOCK_OPERAND 0x0Du
/* Eleven bytes, ending before the compare whose operand is read at 13. Same reason as above. */
#define DIALOG_LEAVE_LOCK_PROLOGUE 11u

enum {
    SITE_OVERRIDE_ON,
    SITE_OVERRIDE_OFF,
    SITE_DIALOG_SPEAK_SINGLE,
    SITE_DIALOG_CLOSE,
    SITE_DIALOG_LEAVE_LOCK,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    /* The four that are detoured are declared as detour targets, so a pattern still matches
     * after another DLL has replaced the prologue with its jump. diagnostics detours the same
     * functions, so whichever of the two loaded second used to find nothing at all. The last
     * entry stays plain because nothing detours it; it is read for an operand. */
    SIGNATURE_ENTRY_DETOUR("bapview_overrideOn",  SIG_OVERRIDE_ON,  OVERRIDE_ON_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_AFTER("bapview_overrideOff", SIG_OVERRIDE_OFF,
                                OVERRIDE_OFF_PROLOGUE, 1u, sizeof SIG_OVERRIDE_ON),
    SIGNATURE_ENTRY_DETOUR("Dialog_SpeakSingle", SIG_DIALOG_SPEAK_SINGLE,
                           DIALOG_SPEAK_SINGLE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("Dialog_Close",        SIG_DIALOG_CLOSE, DIALOG_CLOSE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("Dialog_LeaveInputLock", SIG_DIALOG_LEAVE_LOCK,
                           DIALOG_LEAVE_LOCK_PROLOGUE)
};

typedef void(__cdecl *override_on_fn_t)(int32_t group);
typedef void(__cdecl *override_off_fn_t)(void);
typedef void(__cdecl *dialog_close_fn_t)(int32_t from_op);
typedef int(__cdecl *dialog_speak_fn_t)(void *speaker, int32_t camera_group, int32_t line_id,
                                        const void *pos);

static struct {
    detour_t       on;
    detour_t       off;
    detour_t       close;
    const int32_t *flag;        /* the scripted-camera flag                       */
    const int32_t *lock;        /* the cinematic input lock                       */
    detour_t       speak;
    int            speaking;    /* how deep inside a spoken line this thread is   */
    bool           took_it;     /* the dialogue set the flag, not somebody else   */
    bool           reported;    /* the repair says what it did once, not per line */
    bool           declined;    /* and the rarer case, why it did nothing, once   */
} fix;

/* Reads a 32-bit absolute operand out of a resolved site, refusing anything outside the image. */
static const int32_t *cell_at(int index, uint32_t offset, const char *what)
{
    uint32_t address = 0;

    if (sites[index].address == 0 ||
        !memory_read_u32(sites[index].address + offset, &address) || address == 0 ||
        !memory_is_readable_range((uintptr_t)address, sizeof(int32_t))) {
        log_warning("%s could not be read out of %s, so the camera hand-back stays off", what,
                    sites[index].name);
        return NULL;
    }
    return (const int32_t *)(uintptr_t)address;
}

/* Counted rather than set, so a line that somehow starts another one leaves this above zero for
 * as long as any of them is still running. */
static int __cdecl hook_dialog_speak_single(void *speaker, int32_t camera_group, int32_t line_id,
                                            const void *pos)
{
    int result;

    ++fix.speaking;
    result = ((dialog_speak_fn_t)fix.speak.original)(speaker, camera_group, line_id, pos);
    --fix.speaking;
    return result;
}

/* Attribution, and the only place it happens. The engine's flag says a script owns the camera; it
 * does not say WHICH, and everything this file refuses to do rests on knowing that. */
static void __cdecl hook_override_on(int32_t group)
{
    bool from_a_spoken_line = fix.speaking > 0;

    ((override_on_fn_t)fix.on.original)(group);
    fix.took_it = from_a_spoken_line;
}

static void __cdecl hook_override_off(void)
{
    ((override_off_fn_t)fix.off.original)();
    fix.took_it = false;
}

static void __cdecl hook_dialog_close(int32_t from_op)
{
    ((dialog_close_fn_t)fix.close.original)(from_op);

    if (!handback_rule_owes_camera(fix.took_it, *fix.flag, *fix.lock)) {
        /* Said once, and only for the case that is not obviously fine: the dialogue closed still
         * holding a camera it took and this declined anyway, which can only be the lock. That is
         * a cutscene above the dialogue, and its own opcode owes the camera back. Without this
         * line the two reasons for doing nothing are indistinguishable in the log. */
        if (fix.took_it && *fix.flag != 0 && !fix.declined) {
            fix.declined = true;
            log_info("a dialogue closed still holding the camera and it was LEFT alone, because "
                     "the cinematic lock is %d rather than zero. Something above the dialogue is "
                     "still running and the camera is its business, not this module's. Reported "
                     "once", (int)*fix.lock);
        }
        /* A dialogue whose camera the engine released on its own owes nothing. That is the common
         * case; it happens every time a choice menu was open. */
        fix.took_it = (*fix.flag != 0) && fix.took_it;
        return;
    }

    ((override_off_fn_t)fix.off.original)();
    fix.took_it = false;

    if (!fix.reported) {
        fix.reported = true;
        log_info("a dialogue closed still holding the camera and it has been handed back. The "
                 "engine only releases it when the choice MENU had rows in it, and an ordinary "
                 "spoken line zeroes that count on its way in, so a line that names a camera "
                 "group keeps the camera until the level is reloaded. Reported once; it is "
                 "repaired every time from here on");
    }
}

void camera_handback_fix_install(void)
{
    size_t resolved;

    /* BEFORE anything that logs. Without it every line from this module is dropped on the floor,
     * install record and refusals alike, and the module reads as though it never ran. */
    log_init("camera_handback_fix", false);

    /* And BEFORE anything that searches. Every signature below is looked for in the host's code
     * section, and without this there is no code section to look in: the search runs over
     * nothing and every site reports zero matches, which reads exactly like a stale pattern. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, so the camera hand-back stays off");
        return;
    }

    if (!ini_read_bool(HANDBACK_SECTION, "Enabled", true)) {
        log_info("Enabled=0, so a dialogue keeps the camera exactly as the engine shipped it, "
                 "including for the rest of the level when it never gives it back");
        return;
    }

    resolved = signature_resolve_table(sites, SITE_COUNT);


    if (resolved != SITE_COUNT) {
        log_warning("%u of %u sites resolved, so the camera hand-back stays off. Every one of "
                    "them is needed: two to see the camera change hands, one to know the dialogue "
                    "was the one that took it, one to act when it closes, and one to read the lock "
                    "that says whether anybody above it is still running. Another DLL reaching "
                    "these functions first is no longer a reason for this: every pattern here is "
                    "declared as a detour target and the one function too short to anchor on its "
                    "own is found behind its neighbour",
                    (unsigned)resolved, (unsigned)SITE_COUNT);
        return;
    }

    fix.flag = cell_at(SITE_OVERRIDE_ON, OVERRIDE_ON_FLAG_OPERAND, "the scripted-camera flag");
    fix.lock = cell_at(SITE_DIALOG_LEAVE_LOCK, DIALOG_LEAVE_LOCK_OPERAND, "the cinematic lock");
    if (fix.flag == NULL || fix.lock == NULL) {
        return;
    }
    /* All four or none. Without the spoken line nothing knows whose camera it is and the repair
     * would reach for anybody's; without the setter and the clearer it would go on thinking the
     * dialogue holds a camera the engine has already given back; and the close is the only moment
     * the repair acts at. Any three of them is not a smaller version of this fix; it is a wrong
     * one. Ordered so that the attribution is live before anything that reads it. */
    if (!detour_install(&fix.speak, sites[SITE_DIALOG_SPEAK_SINGLE].address,
                        (const void *)hook_dialog_speak_single, DIALOG_SPEAK_SINGLE_PROLOGUE) ||
        !detour_install(&fix.on, sites[SITE_OVERRIDE_ON].address,
                        (const void *)hook_override_on, OVERRIDE_ON_PROLOGUE) ||
        !detour_install(&fix.off, sites[SITE_OVERRIDE_OFF].address,
                        (const void *)hook_override_off, OVERRIDE_OFF_PROLOGUE) ||
        !detour_install(&fix.close, sites[SITE_DIALOG_CLOSE].address,
                        (const void *)hook_dialog_close, DIALOG_CLOSE_PROLOGUE)) {
        log_warning("the camera hand-back could not place all four of its detours, so it is off. "
                    "A partial install would attribute the camera wrongly rather than do less");
        return;
    }

    log_info("a dialogue now gives the camera back when it closes, unless a cutscene above it is "
             "still holding the input lock. Flag at %08X, lock at %08X, and a take is credited to "
             "the dialogue by the spoken line being on the stack at the time, so no other camera "
             "is ever touched and no other DLL reaching these functions first can change that",
             (unsigned)(uintptr_t)fix.flag, (unsigned)(uintptr_t)fix.lock);
}
