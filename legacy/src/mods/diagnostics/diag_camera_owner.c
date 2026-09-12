/* diag_camera_owner.c: see diag_camera_owner.h. */
#include "diag_camera_owner.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/signature.h"

#include <intrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* --- bapview_overrideOn 0x0041840A ------------------------------------------------------------ *
 *   55 8B EC                     push ebp / mov ebp,esp
 *   C7 05 E8 B4 5B 00 01000000   mov dword [gOver],1
 *   8B 45 08                     mov eax,[ebp+8]          the camera group it was handed
 *   A3 F0 B4 5B 00               mov [forcedRegion],eax
 *   5D C3                        pop ebp / ret
 *
 * The whole function is twenty three bytes and all of it is taken. Thirteen are already unique, so
 * the rest is margin against a build with one instruction different. The prologue ends after the
 * store, on an instruction boundary, and holds no relative operand: on 32-bit x86 the address in
 * that store is absolute, so copying those bytes to a trampoline moves them safely.
 *
 * The absolute cell address IS in the pattern and cannot be kept out of it, the function being
 * nothing but two stores. That is the one thing here tied to this exact build, and it fails the
 * safe way: a different build does not match, and the observer reports that it did not install. */
static const uint8_t SIG_OVERRIDE_ON[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0xE8, 0xB4, 0x5B, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0xA3, 0xF0, 0xB4, 0x5B, 0x00, 0x5D, 0xC3
};
#define OVERRIDE_ON_PROLOGUE 13u

/* --- bapview_overrideOff 0x00418421 ----------------------------------------------------------- *
 *   55 8B EC                     push ebp / mov ebp,esp
 *   C7 05 E8 B4 5B 00 00000000   mov dword [gOver],0
 *   5D C3                        pop ebp / ret
 *
 * Fifteen bytes, all taken, unique at thirteen. It takes no argument: it clears the flag and
 * leaves the forced region cell alone, so the flag rather than the cell is watched. */
static const uint8_t SIG_OVERRIDE_OFF[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0xE8, 0xB4, 0x5B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x5D, 0xC3
};
#define OVERRIDE_OFF_PROLOGUE 13u

/* --- Dialog_Close 0x00430E82 ------------------------------------------------------------------ *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 7D 08 00           cmp dword [ebp+8],0        its one argument
 *   74 0A                 je   +10
 *
 * Twelve bytes are unique and sixteen are taken. The prologue stops at SEVEN, before the jump: a
 * `je` carries a relative operand, and a relative operand copied to a trampoline points somewhere
 * else. Seven is an instruction boundary and the three instructions in it hold nothing relative.
 *
 * Why this function is watched at all. The release is written as
 *
 *     if (Dialog_LeaveInputLock(1) != 0 && choiceCount != 0) bapview_overrideOff();
 *
 * so a dialogue that took the camera keeps it unless BOTH hold, and the census above cannot tell
 * which of the two failed, or whether this function ran at all. Reading the flag on the way in and
 * again on the way out answers that without reading the lock: the count is visible directly, and
 * if the count was fine and the flag still did not clear, the lock is what refused. */
static const uint8_t SIG_DIALOG_CLOSE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x0A, 0x6A, 0x00, 0xE8,
    0x2F, 0x4A, 0x03, 0x00
};
#define DIALOG_CLOSE_PROLOGUE 7u

/* The choice count this function tests, as the absolute operand of its own `cmp` at +0x23, and the
 * scripted-camera flag as the operand of the setter's store at +5. Both are read out of a resolved
 * site rather than written down, so neither is a second copy of an address to keep in step. */
#define DIALOG_CLOSE_CHOICE_COUNT_OPERAND 0x23u
#define OVERRIDE_ON_FLAG_OPERAND          0x05u

enum { SITE_OVERRIDE_ON, SITE_OVERRIDE_OFF, SITE_DIALOG_CLOSE, SITE_COUNT };

static signature_t sites[SITE_COUNT] = {
    /* Declared as detour targets, all three, because all three are detoured here and
     * camera_handback_fix detours the same three functions. With plain patterns whichever DLL
     * loaded second scanned for a prologue the first had already replaced with a jump, found
     * nothing, and reported an unsupported executable instead of a collision. */
    SIGNATURE_ENTRY_DETOUR("bapview_overrideOn",  SIG_OVERRIDE_ON,  OVERRIDE_ON_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_AFTER("bapview_overrideOff", SIG_OVERRIDE_OFF,
                                OVERRIDE_OFF_PROLOGUE, 1u, sizeof SIG_OVERRIDE_ON),
    SIGNATURE_ENTRY_DETOUR("Dialog_Close",        SIG_DIALOG_CLOSE, DIALOG_CLOSE_PROLOGUE)
};

/* Every call site of both functions in the retail image, by the address control returns to, which
 * is the call site plus its five bytes, with the name of the enclosing function. Where a caller
 * belongs to a pair that sets in one branch and clears in the other, the note says so, because
 * such a pair cannot leak.
 *
 * These are notes about one build, not the list of callers. The list is made at install by
 * scanning the code section for direct calls to the two resolved targets, and a note is attached
 * to a caller only when the scan found a call at the address the note was written for. On a
 * build that laid the code out differently the scan still names every caller as found or not
 * found, and the notes do not apply. */
typedef struct caller {
    uintptr_t   ret;
    const char *what;
} caller_t;

static const caller_t TOOK[] = {
    { 0x00417EAAu, "the module restore, on coming back from a save" },
    { 0x00430D76u, "a spoken line naming a camera group (Dialog_SpeakSingle)" },
    { 0x00434F4Bu, "the cutscene camera opcode" },
    { 0x00434F90u, "the cutscene camera opcode, which also takes the input lock to level 5" },
    { 0x0043FD50u, "a menu" },
    { 0x0044F22Cu, "the fall-death camera, group 13 (Plr_UpdateFall)" },
    { 0x00450505u, "the tripod gun" }
};

static const caller_t GAVE_BACK[] = {
    { 0x00417A98u, "the module proc" },
    { 0x00430EB1u, "a dialogue closing (Dialog_Close)" },
    { 0x00434F55u, "the cutscene camera opcode, the other branch of the take above" },
    { 0x00434FA4u, "the cutscene camera opcode, the other branch of the take above" },
    { 0x0044013Fu, "a menu closing" },
    { 0x00450938u, "the tripod gun releasing" }
};

/* One line per call, not per change: the sequence is the finding here, and the same caller taking
 * the camera twice running is a different story from taking it once. Bounded all the same, since a
 * dialogue-heavy level speaks hundreds of lines. */
#define OWNER_LINES 200

/* The callers the scan can hold. Retail has seven and six; a build with more than twice that is
 * reported as such and the overflow goes unnamed rather than unseen. */
#define CALLERS_MAX 16u

typedef struct callers {
    uintptr_t ret[CALLERS_MAX];
    size_t    count;                /* how many were kept */
    size_t    found;                /* how many the scan saw, which may be more */
} callers_t;

static struct {
    detour_t        on;
    detour_t        off;
    detour_t        close;
    const int32_t  *flag;           /* the scripted-camera flag itself, read only */
    const int32_t  *choice_count;   /* what Dialog_Close tests before releasing   */
    callers_t       takers;         /* every direct call to overrideOn in this build */
    callers_t       givers;         /* and to overrideOff */
    int             lines;
    int             depth;          /* calls, NOT the flag: see the note on the census below */
    bool            armed;          /* both halves of the pair are in, so reporting is honest */
} owner;

typedef void(__cdecl *override_on_fn_t)(int32_t group);
typedef void(__cdecl *override_off_fn_t)(void);
typedef void(__cdecl *dialog_close_fn_t)(int32_t from_op);

/* Every `call rel32` in the code section whose target is `target`, by return address. The scan is
 * byte by byte, since the section is not disassembled: an E8 whose displacement happens to land
 * on the target from inside another instruction's bytes would be counted, and none did in retail.
 * Run once at install, before the detour goes in, so the target is the function itself. */
static void scan_callers(uintptr_t target, callers_t *out)
{
    uintptr_t text = host_image_text();
    size_t    size = host_image_text_size();
    size_t    offset;

    out->count = 0;
    out->found = 0;
    for (offset = 0; offset + 5u <= size; ++offset) {
        uintptr_t at = text + offset;
        int32_t   displacement;

        /* Read straight out of the section rather than through the asking form: the section is
         * the image's own and this is one pass over all of it. */
        if (*(const uint8_t *)at != 0xE8u) {
            continue;
        }
        memcpy(&displacement, (const void *)(at + 1u), sizeof displacement);
        if (at + 5u + (uintptr_t)displacement != target) {
            continue;
        }
        ++out->found;
        if (out->count < CALLERS_MAX) {
            out->ret[out->count++] = at + 5u;
        }
    }
}

static bool scan_found(const callers_t *callers, uintptr_t ret)
{
    size_t index;

    for (index = 0; index < callers->count; ++index) {
        if (callers->ret[index] == ret) {
            return true;
        }
    }
    return false;
}

/* The retail note for a caller, attached only when the scan found a call at that address. */
static const char *name_of(const caller_t *table, unsigned count, const callers_t *callers,
                           uintptr_t ret)
{
    unsigned i;

    if (!scan_found(callers, ret)) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (table[i].ret == ret) {
            return table[i].what;
        }
    }
    return "a caller the scan found, with no note for it";
}

/* How the callers of one function are described in the log at install. */
static void report_callers(const callers_t *callers, const caller_t *table, unsigned count,
                           const char *what)
{
    size_t index;
    size_t noted = 0;

    for (index = 0; index < callers->count; ++index) {
        unsigned i;

        for (i = 0; i < count; i++) {
            if (table[i].ret == callers->ret[index]) {
                ++noted;
                break;
            }
        }
    }
    log_info("%u direct calls to %s found in this build, %u of them with a retail note%s",
             (unsigned)callers->found, what, (unsigned)noted,
             (callers->found > callers->count) ? "; more than the census can hold, so the rest "
                                                "are reported by address only" : "");
}

/* False once the budget is spent, having said so exactly once. */
static bool may_report(void)
{
    if (owner.lines < OWNER_LINES) {
        owner.lines++;
        return true;
    }
    if (owner.lines == OWNER_LINES) {
        owner.lines++;
        diag_log_write("cam  %d camera hand-overs reported and no more this session. The running "
                       "depth is %+d, and anything other than zero while the player has control "
                       "is a camera nobody gave back", OWNER_LINES, owner.depth);
    }
    return false;
}

/* An unknown caller is worth more than a known one, so it is named as unknown rather than printed
 * as a bare address that reads like noise. Every direct call in this build was found by the scan
 * at install, so reaching that case means the call came from somewhere a scan for direct calls
 * cannot see: a call through a register, or a caller outside the code section. */
static void __cdecl hook_override_on(int32_t group)
{
    uintptr_t   ret = (uintptr_t)_ReturnAddress();
    const char *what;

    ((override_on_fn_t)owner.on.original)(group);
    /* Abandoned, so this hook does nothing but pass the call on. A detour cannot be lifted once
     * it is placed, so a feature that gave up during its install has to say so here: the pair
     * below is worth nothing as a single half, and half of it went on writing to the log with
     * the census itself already reported as off. */
    if (!owner.armed) {
        return;
    }
    owner.depth++;
    if (!may_report()) {
        return;
    }

    what = name_of(TOOK, (unsigned)(sizeof TOOK / sizeof TOOK[0]), &owner.takers, ret);
    if (what != NULL) {
        diag_log_write("cam  camera TAKEN, group %d from %08X: %s. Depth now %+d",
                       (int)group, (unsigned)ret, what, owner.depth);
        return;
    }
    diag_log_write("cam  camera TAKEN, group %d from %08X, which is NOT one of the direct call "
                   "sites the scan found in this build. Depth now %+d",
                   (int)group, (unsigned)ret, owner.depth);
}

static void __cdecl hook_override_off(void)
{
    uintptr_t   ret = (uintptr_t)_ReturnAddress();
    const char *what;

    ((override_off_fn_t)owner.off.original)();
    if (!owner.armed) {      /* same reason as the take above */
        return;
    }
    owner.depth--;
    if (!may_report()) {
        return;
    }

    what = name_of(GAVE_BACK, (unsigned)(sizeof GAVE_BACK / sizeof GAVE_BACK[0]), &owner.givers,
                   ret);
    if (what != NULL) {
        diag_log_write("cam  camera GIVEN BACK from %08X: %s. Depth now %+d", (unsigned)ret, what,
                       owner.depth);
        return;
    }
    diag_log_write("cam  camera GIVEN BACK from %08X, which is NOT one of the direct call sites "
                   "the scan found in this build. Depth now %+d", (unsigned)ret, owner.depth);
}

/* The flag is a BOOLEAN, not a count, so taking it twice running leaves it exactly as set as taking
 * it once, and the running depth above is a tally of CALLS rather than the flag's value. What
 * settles a leak is the ordering: whether the last write before the camera went wrong was a
 * take. This reads the flag itself on both sides of the close, so it needs no arithmetic. */
static void __cdecl hook_dialog_close(int32_t from_op)
{
    int32_t before = (owner.flag != NULL) ? *owner.flag : -1;
    int32_t count  = (owner.choice_count != NULL) ? *owner.choice_count : -1;
    int32_t after;

    ((dialog_close_fn_t)owner.close.original)(from_op);
    after = (owner.flag != NULL) ? *owner.flag : -1;

    if (before == 0 || !may_report()) {
        return;         /* it held no camera going in, so it owed nothing coming out */
    }

    if (after == 0) {
        diag_log_write("cam  a dialogue closed holding the camera and gave it back. Choice count "
                       "was %d", count);
        return;
    }
    diag_log_write("cam  a dialogue closed STILL HOLDING the camera. Choice count was %d, and the "
                   "release needs that to be non-zero AND the input lock to unwind at level 1, so "
                   "%s", count,
                   (count == 0) ? "the count is what refused"
                                : "the count was fine and the input lock is what refused");
}

int diag_camera_owner_install(int level)
{
    bool on_live;
    bool off_live;

    if (level <= 0) {
        return 0;
    }
    signature_resolve_table(sites, SITE_COUNT);


    owner.flag = (const int32_t *)diag_derive_address(sites, SITE_OVERRIDE_ON,
                                                      OVERRIDE_ON_FLAG_OPERAND,
                                                      "the scripted-camera flag");
    owner.choice_count = (const int32_t *)diag_derive_address(sites, SITE_DIALOG_CLOSE,
                                                              DIALOG_CLOSE_CHOICE_COUNT_OPERAND,
                                                              "the dialogue choice count");

    /* Before the detours, so the calls still go to the functions themselves. */
    if (sites[SITE_OVERRIDE_ON].address != 0 && sites[SITE_OVERRIDE_OFF].address != 0) {
        scan_callers(sites[SITE_OVERRIDE_ON].address, &owner.takers);
        scan_callers(sites[SITE_OVERRIDE_OFF].address, &owner.givers);
        report_callers(&owner.takers, TOOK, (unsigned)(sizeof TOOK / sizeof TOOK[0]),
                       "bapview_overrideOn");
        report_callers(&owner.givers, GAVE_BACK,
                       (unsigned)(sizeof GAVE_BACK / sizeof GAVE_BACK[0]), "bapview_overrideOff");
    }

    on_live  = diag_install_observer(sites, SITE_OVERRIDE_ON, &owner.on,
                                     (const void *)hook_override_on, OVERRIDE_ON_PROLOGUE,
                                     "a script taking the camera (bapview_overrideOn)");
    off_live = diag_install_observer(sites, SITE_OVERRIDE_OFF, &owner.off,
                                     (const void *)hook_override_off, OVERRIDE_OFF_PROLOGUE,
                                     "a script giving the camera back (bapview_overrideOff)");

    /* Both or neither is the only useful state. A take with nobody watching the release says the
     * camera was taken and nothing about whether that was wrong; a release with nobody watching
     * the take is worse, since the interesting event is the one with no partner. */
    if (!on_live || !off_live) {
        log_warning("the camera owner census needs both halves and got %s, so it reports nothing. "
                    "A hand-over is only readable as a pair. Whichever half was placed stays in "
                    "the image, because a detour cannot be lifted, and passes its call straight "
                    "through",
                    on_live ? "only the take" : (off_live ? "only the release" : "neither"));
        return 0;
    }

    /* Last, so that neither hook can report anything until the pair is known to be whole. */
    owner.armed = true;

    /* Not part of the both-or-neither pair: it explains a leak the pair has already found, so
     * losing it costs detail rather than the finding. */
    (void)diag_install_observer(sites, SITE_DIALOG_CLOSE, &owner.close,
                                (const void *)hook_dialog_close, DIALOG_CLOSE_PROLOGUE,
                                "a dialogue closing, and whether it gave the camera back");

    log_info("the camera owner census is armed on both writers of the scripted-camera flag. Every "
             "take and release is reported with the caller that asked for it and a running depth; "
             "a depth that stays above zero while the player has control is a camera nobody gave "
             "back, and it lasts until the level is reloaded");
    return 2;
}
