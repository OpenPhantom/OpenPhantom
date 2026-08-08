/* music_sites.c: find the music latch pair and the three cells that say what it means.
 *
 * ==============================================================================================
 * BYTE BASIS
 *
 * The music module is small enough to quote whole. Two functions carry the pause, and they are
 * byte-for-byte identical except for the import they call and the value they latch:
 *
 *   bapMusicPause                                bapMusicResume
 *   55 8B EC              push ebp/mov ebp,esp   55 8B EC
 *   FF 15 <AIL_lock>      call                   FF 15 <AIL_lock>
 *   A1 <reentry>          mov eax,[..]           A1 <reentry>
 *   83 C0 01              add eax,1              83 C0 01
 *   A3 <reentry>          mov [..],eax           A3 <reentry>
 *   E8 <ImPause>          call                   E8 <ImResume>      <-- the only real difference
 *   FF 15 <AIL_unlock>    call                   FF 15 <AIL_unlock>
 *   8B 0D <reentry>       mov ecx,[..]           8B 0D <reentry>
 *   83 E9 01              sub ecx,1              83 E9 01
 *   89 0D <reentry>       mov [..],ecx           89 0D <reentry>
 *   C7 05 <paused> 01000000   mov [..],1         C7 05 <paused> 00000000   <-- and this immediate
 *   5D C3                                        5D C3
 *
 * ImPause has exactly one caller in the whole image and ImResume exactly one: these two. So the
 * pair below is the ONLY way the music stream can be stopped and started again, and calling them
 * is calling the engine's own pause rather than inventing one.
 *
 * Neither of them checks whether the music system is up. They call into IMUSE.DLL unconditionally.
 * That is why g_musicAttached is resolved as well and why nothing here is called while it is not
 * exactly 1, the engine's own per-frame service tests `!= 1`, not `!= 0`, and this follows it.
 *
 *   bapMusicPeriodic   55 8B EC 83 3D <attached> 01 75 ..   the `== 1` test, then the same lock
 *
 * The two cue getters sit next to each other and are near-identical, which makes one pattern
 * cover both. They are wanted for the log rather than for the repair: when the music hangs, the
 * cue that was in force at that moment is the first thing worth knowing.
 *
 *   bapMusicGetState   ... 75 07 B8 E8 03 00 00 EB 05 A1 <state latch> 5D C3    (1000 = NULL cue)
 *   bapMusicGetSequence... 75 07 B8 D0 07 00 00 EB 05 A1 <seq latch>   5D C3    (2000 = NULL cue)
 *
 * The fifth site is the one that says a pause is LEGITIMATE. sys_pause is the pause menu:
 *
 *   0043FAB5  55 8B EC
 *             83 3D <sys_pause_on> 01 / 75 02 / EB ..     already paused? then do nothing
 *             C7 05 <sys_pause_on> 01000000               claim it
 *             A1 <frameDt> / 50 / 6A 08 / 6A 00           push dt, command 8, id 0 = ALL MODULES
 *             E8 <module_broadcastDt>
 *             83 C4 0C
 *             C7 05 <in_cutscene> 01000000
 *             ... pausemenu_run ...
 *             6A 09 / 6A 00 / E8 <module_broadcastDt>     command 9, id 0
 *             C7 05 <sys_pause_on> 00000000
 *
 * That broadcast is what actually pauses the music in the shipped game, and it is BALANCED: the
 * dispatcher it goes through walks the module list, calls each handler and keeps the return value
 * in a local nobody reads. There is a second dispatcher pair in the image that DOES gate on the
 * return value and on a flag bit, and whose music arm would indeed stick, but neither of those
 * two functions has a caller, a jump or a stored pointer anywhere in any shipped image. The live
 * path is this one, and on it the handler's return value is not read at all.
 *
 * ============================================================================================ */
#include "music_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4, "imuse_fix reads 32-bit engine pointers out of operands");

/* --- the pause / resume pair ------------------------------------------------------------------
 * Everything absolute is wildcarded, so the pattern survives the recompiled build where all of
 * these cells sit 0x50 lower. The two bodies differ in exactly one byte, the latched immediate at
 * +0x36: 01 for pause, 00 for resume.
 *
 * All four arrays are spelled out. A macro would be shorter and would cost the offline proof
 * twice over: the verifier reads these arrays out of the SOURCE TEXT, so a macro body is one
 * token to it rather than sixty bytes, and it pairs a pattern with its mask BY NAME, so a mask
 * shared under a third name leaves both patterns looking unmasked, and an unmasked read of this
 * body matches tens of thousands of places. Both mistakes compile, link and run correctly. */
static const uint8_t SIG_MUSIC_PAUSE[] = {
    0x55, 0x8B, 0xEC,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xE9, 0x01,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_MUSIC_PAUSE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
static const uint8_t SIG_MUSIC_RESUME[] = {
    0x55, 0x8B, 0xEC,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xE9, 0x01,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_MUSIC_RESUME[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};

_Static_assert(sizeof(SIG_MUSIC_PAUSE) == sizeof(MSK_MUSIC_PAUSE),
               "the pause pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_MUSIC_RESUME) == sizeof(MSK_MUSIC_RESUME),
               "the resume pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_MUSIC_PAUSE) == sizeof(SIG_MUSIC_RESUME),
               "the two halves of the latch pair must be the same shape");

/* The `mov eax,[g_musicReentry]` operand, and the `mov dword [g_musicPaused], imm` operand. */
#define OFFSET_LATCH_REENTRY 0x0Au
#define OFFSET_LATCH_PAUSED  0x32u

/* --- bapMusicPeriodic, for g_musicAttached ---------------------------------------------------
 * The short-jump displacement at +0x0B is wildcarded because it is a function-length artefact. */
static const uint8_t SIG_MUSIC_PERIODIC[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x75, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xE8
};
static const uint8_t MSK_MUSIC_PERIODIC[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF
};
_Static_assert(sizeof(SIG_MUSIC_PERIODIC) == sizeof(MSK_MUSIC_PERIODIC),
               "the periodic pattern and its mask are different lengths");

#define OFFSET_PERIODIC_ATTACHED 0x05u
#define OFFSET_PERIODIC_REENTRY  0x13u

/* --- the two cue getters, one pattern ---------------------------------------------------------
 * The NULL cues 1000 and 2000 are literal on purpose: they are what tells the two apart, and a
 * build that spelled them differently is a build this feature should not read latches from. */
static const uint8_t SIG_MUSIC_GETTERS[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x07,
    0xB8, 0xE8, 0x03, 0x00, 0x00,
    0xEB, 0x05,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3,
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x07,
    0xB8, 0xD0, 0x07, 0x00, 0x00,
    0xEB, 0x05,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_MUSIC_GETTERS[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MUSIC_GETTERS) == sizeof(MSK_MUSIC_GETTERS),
               "the getter pattern and its mask are different lengths");

#define OFFSET_GETTERS_ATTACHED_A 0x05u
#define OFFSET_GETTERS_STATE      0x14u
#define OFFSET_GETTERS_ATTACHED_B 0x1Fu
#define OFFSET_GETTERS_SEQUENCE   0x2Eu

/* --- sys_pause, for the "somebody legitimately owns this pause" cell -------------------------- */
static const uint8_t SIG_SYS_PAUSE[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x75, 0x02,
    0xEB, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x6A, 0x08,
    0x6A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SYS_PAUSE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SYS_PAUSE) == sizeof(MSK_SYS_PAUSE),
               "the sys_pause pattern and its mask are different lengths");

/* The `cmp dword [..], 1` operand at the top; the claim below it must name the same cell. */
#define OFFSET_SYS_PAUSE_TEST  0x05u
#define OFFSET_SYS_PAUSE_CLAIM 0x10u

enum {
    SITE_MUSIC_PAUSE,
    SITE_MUSIC_RESUME,
    SITE_MUSIC_PERIODIC,
    SITE_MUSIC_GETTERS,
    SITE_SYS_PAUSE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("bap_music_pause",    SIG_MUSIC_PAUSE,    MSK_MUSIC_PAUSE),
    SIGNATURE_ENTRY_MASKED("bap_music_resume",   SIG_MUSIC_RESUME,   MSK_MUSIC_RESUME),
    SIGNATURE_ENTRY_MASKED("bap_music_periodic", SIG_MUSIC_PERIODIC, MSK_MUSIC_PERIODIC),
    SIGNATURE_ENTRY_MASKED("bap_music_getters",  SIG_MUSIC_GETTERS,  MSK_MUSIC_GETTERS),
    SIGNATURE_ENTRY_MASKED("sys_pause",          SIG_SYS_PAUSE,      MSK_SYS_PAUSE)
};

/* ============================================================================================ */
static bool read_cell(uintptr_t site, uint32_t offset, const char *what, uint32_t *out)
{
    uint32_t cell = 0;

    if (!memory_read_u32(site + offset, &cell)) {
        log_warning("could not read the %s operand at %08X", what, (unsigned)(site + offset));
        return false;
    }
    /* An engine global lives in the image. Anything else means the pattern matched something that
     * is not the function it was cut from, and believing it would write into a stranger. */
    if (!memory_is_inside_image(cell, sizeof(uint32_t)) ||
        !memory_is_readable_range(cell, sizeof(uint32_t))) {
        log_warning("the %s would be at %08X, which is not a readable address inside the image "
                    "- refused", what, (unsigned)cell);
        return false;
    }
    *out = cell;
    return true;
}

/* Two patterns that name the same cell are a free cross-check, and a disagreement means one of
 * them matched the wrong place. Refuse rather than pick a winner. */
static bool agree(uint32_t a, uint32_t b, const char *what)
{
    if (a == b) {
        return true;
    }
    log_warning("two independent sites disagree about the %s (%08X against %08X), refused, "
                "because one of the two patterns has matched something it was not cut from",
                what, (unsigned)a, (unsigned)b);
    return false;
}

bool music_sites_resolve(music_sites_t *out)
{
    uint32_t paused_a = 0, paused_b = 0;
    uint32_t reentry_a = 0, reentry_b = 0, reentry_c = 0;
    uint32_t attached_a = 0, attached_b = 0, attached_c = 0;
    uint32_t sys_test = 0, sys_claim = 0;
    uint32_t state_latch = 0, sequence_latch = 0;

    out->pause_function = 0;
    out->resume_function = 0;
    out->attached = NULL;
    out->paused = NULL;
    out->sys_pause_on = NULL;
    out->state_latch = NULL;
    out->sequence_latch = NULL;

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_MUSIC_PAUSE].address == 0 || sites[SITE_MUSIC_RESUME].address == 0) {
        log_warning("the music pause/resume pair did not resolve, imuse_fix does nothing this "
                    "session, and the game keeps exactly the behaviour it shipped with");
        return false;
    }
    if (sites[SITE_MUSIC_PERIODIC].address == 0) {
        log_warning("bapMusicPeriodic did not resolve, so there is no way to tell whether the "
                    "music system is up, refused, because both functions this feature calls go "
                    "into IMUSE.DLL without checking that themselves");
        return false;
    }

    /* g_musicPaused, named by both halves of the pair. */
    if (!read_cell(sites[SITE_MUSIC_PAUSE].address, OFFSET_LATCH_PAUSED, "music pause latch",
                   &paused_a) ||
        !read_cell(sites[SITE_MUSIC_RESUME].address, OFFSET_LATCH_PAUSED, "music pause latch",
                   &paused_b) ||
        !agree(paused_a, paused_b, "music pause latch")) {
        return false;
    }

    /* The reentry counter is not used by this feature. It is read from all three sites purely as
     * a third and fourth check that the three patterns matched the same module. */
    if (!read_cell(sites[SITE_MUSIC_PAUSE].address, OFFSET_LATCH_REENTRY, "music reentry counter",
                   &reentry_a) ||
        !read_cell(sites[SITE_MUSIC_RESUME].address, OFFSET_LATCH_REENTRY, "music reentry counter",
                   &reentry_b) ||
        !read_cell(sites[SITE_MUSIC_PERIODIC].address, OFFSET_PERIODIC_REENTRY,
                   "music reentry counter", &reentry_c) ||
        !agree(reentry_a, reentry_b, "music reentry counter") ||
        !agree(reentry_a, reentry_c, "music reentry counter")) {
        return false;
    }

    if (!read_cell(sites[SITE_MUSIC_PERIODIC].address, OFFSET_PERIODIC_ATTACHED,
                   "music attached flag", &attached_a)) {
        return false;
    }

    out->pause_function = sites[SITE_MUSIC_PAUSE].address;
    out->resume_function = sites[SITE_MUSIC_RESUME].address;
    out->paused = (volatile int32_t *)(uintptr_t)paused_a;
    out->attached = (volatile int32_t *)(uintptr_t)attached_a;

    log_info("music pause %08X, resume %08X; attached flag %08X, pause latch %08X "
             "(both named by two patterns each, and the reentry counter %08X by three)",
             (unsigned)out->pause_function, (unsigned)out->resume_function,
             (unsigned)attached_a, (unsigned)paused_a, (unsigned)reentry_a);

    /* ---- optional from here on: the feature works without these, the log is poorer ---------- */
    if (sites[SITE_MUSIC_GETTERS].address != 0 &&
        read_cell(sites[SITE_MUSIC_GETTERS].address, OFFSET_GETTERS_ATTACHED_A,
                  "music attached flag", &attached_b) &&
        read_cell(sites[SITE_MUSIC_GETTERS].address, OFFSET_GETTERS_ATTACHED_B,
                  "music attached flag", &attached_c) &&
        agree(attached_a, attached_b, "music attached flag") &&
        agree(attached_a, attached_c, "music attached flag") &&
        read_cell(sites[SITE_MUSIC_GETTERS].address, OFFSET_GETTERS_STATE,
                  "music state latch", &state_latch) &&
        read_cell(sites[SITE_MUSIC_GETTERS].address, OFFSET_GETTERS_SEQUENCE,
                  "music sequence latch", &sequence_latch)) {
        out->state_latch = (volatile int32_t *)(uintptr_t)state_latch;
        out->sequence_latch = (volatile int32_t *)(uintptr_t)sequence_latch;
        log_info("the two cue latches are at %08X (state) and %08X (sequence), they are read for "
                 "the log only, never written", (unsigned)state_latch, (unsigned)sequence_latch);
    } else {
        log_warning("the two cue getters did not resolve, the log can still name every state "
                    "change, but not which music cue was in force when it happened");
    }

    if (sites[SITE_SYS_PAUSE].address != 0 &&
        read_cell(sites[SITE_SYS_PAUSE].address, OFFSET_SYS_PAUSE_TEST, "pause-menu latch",
                  &sys_test) &&
        read_cell(sites[SITE_SYS_PAUSE].address, OFFSET_SYS_PAUSE_CLAIM, "pause-menu latch",
                  &sys_claim) &&
        agree(sys_test, sys_claim, "pause-menu latch")) {
        out->sys_pause_on = (volatile int32_t *)(uintptr_t)sys_test;
        log_info("the pause menu's own latch is at %08X - while it is set, the music is paused by "
                 "the game and this feature keeps its hands off", (unsigned)sys_test);
    } else {
        log_warning("sys_pause did not resolve, the orphan guard stays OFF this session, because "
                    "without that latch a legitimate pause-menu pause is indistinguishable from a "
                    "pause nobody owns, and resuming the first one would play music over the "
                    "pause menu");
    }

    return true;
}
