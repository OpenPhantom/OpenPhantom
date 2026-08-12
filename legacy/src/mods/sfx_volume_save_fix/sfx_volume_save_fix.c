/* sfx_volume_save_fix.c: the SFX slider always comes back to (about) full, no matter what was
 * saved.
 *
 * ==============================================================================================
 * BUG 1 - the value handed to the ini write is wrong (fixed, but it turned out not to be the whole
 *          story)
 *
 * The audio options screen (options_audio, retail 0x00441FA4) has exactly one function it calls to
 * find out "what is the SFX volume right now": bapsound_getMasterVolume, retail 0x00417459. It
 * asks MILES for the digital device's current master volume instead of reading back whatever the
 * engine itself last set, and that AIL round-trip does not reliably reflect the value just pushed
 * with _AIL_set_digital_master_volume.
 *
 * That getter has exactly two callers in the whole image, and both are inside options_audio: an
 * E8 sweep of the entire .text finds call sites at 0x004420C1 and 0x004428C2 and nothing else,
 * and the next function entry after 0x00441FA4 is 0x00442A98, so both lie inside that one screen.
 * They are the two things the screen does with the number: seed the slider widget when it opens,
 * and build the value written to obi.ini's SVOL key when it closes.
 *
 * bapsound_setMasterVolume (retail 0x00417379), the function that DRIVES the slider live, keeps a
 * reliable engine-side copy of the true value two instructions in:
 *
 *     0041738D  DB 45 08               fild dword ptr [ebp+0x08]      ; the int 0..127 argument
 *     00417390  D8 35 50 81 4A 00      fdiv float ptr [g_sfxVolumeScale]  ; 0x004A8150 = 127.0
 *     00417396  D9 1D 70 A9 4A 00      fstp float ptr [g_sfxMasterVolume]    ; -> 0x004AA970
 *
 * [0x004AA970] is not a write-only shadow: the per-channel attenuation at 0x004169BD reads it back
 * on every sample start, `base * scale * g_sfxMasterVolume`, so this cell has to stay correct for
 * gameplay volume to be right at all. bapsound_getMasterVolume is entirely replaced (not wrapped -
 * calling through to the AIL query first would just reintroduce the bug) with a hook that computes
 * the same 0..127 integer the engine itself derived the mirror from.
 *
 * THE REPLACEMENT REPRODUCES THE ENGINE'S OWN GUARD BRANCH, and that is not a detail. The original
 * body answers 0, not a volume, while g_soundReady is still 0:
 *
 *     0041745C  83 3D B8 B4 5B 00 00   cmp dword ptr [g_soundReady], 0
 *     00417463  75 04                  jnz  <ask AIL>
 *     00417465  33 C0                  xor eax,eax                    ; <- 0, and that is an answer
 *     00417467  EB 0C                  jmp  <return>
 *
 * A replacement that skipped that branch would answer with the mirror instead, and the mirror is
 * 1.0 at that point for exactly the reason bug 2 below describes - so on a machine whose sound
 * never initialises, the options screen would seed its slider at full and write SVOL=127 over the
 * player's saved value. That is the very symptom this DLL exists to remove, reintroduced for the
 * no-sound case, so the branch is kept.
 *
 * ==============================================================================================
 * BUG 2 - the value never gets APPLIED on load, which is the actual cause of the reported symptom
 *
 * bapsound_moduleInit (retail 0x004159F0), the function that runs once at startup, does this, in
 * exactly this order:
 *
 *     ini_read_int_alt(&"SVOL", 0x7F, &loaded);   // reads obi.ini correctly
 *     bapsound_setMasterVolume(loaded);           // called from 0x00415A78
 *     ...
 *     [0x005BB4B8] = 1;                           // "sound is ready" - set AFTER the call above
 *
 * bapsound_setMasterVolume's entire body is gated on that same cell:
 *
 *     0041737F  83 3D B8 B4 5B 00 00   cmp dword ptr [g_soundReady], 0
 *     00417386  75 05                  jnz +5            ; only THEN does the real work run
 *     0041738B  E9 C8 00 00 00         jmp <exit, does nothing>
 *
 * At the moment bapsound_moduleInit calls it with the value it just loaded from obi.ini,
 * g_soundReady is STILL 0 - it is not set to 1 until several instructions later, in the SAME
 * function. So the very first, load-time apply is a GUARANTEED silent no-op, every single launch,
 * regardless of what SVOL says in the file. The mirror simply keeps its compiled-in startup value
 * (measured as 1.0, i.e. full) until the player manually touches the slider. This is a genuine
 * ordering mistake in the original 1999 code - one statement in the wrong place - and it is the
 * actual reason "the SFX slider always resets to full on reload": the save was never the whole
 * problem, the load silently threw the saved value away.
 *
 * The setter's own caller census says the same thing: two E8 call sites in the whole image,
 * 0x00415A78 (inside bapsound_moduleInit) and 0x0044249C (inside options_audio). One start-up
 * apply that cannot work, and one live slider that can.
 *
 * Confirmed with a temporary diagnostic build across two separate sessions: the very first call to
 * bapsound_setMasterVolume in each run showed the mirror ending up at 1.0 regardless of the
 * argument passed in (120 in one run, 0 in the other), which is exactly what "the guard blocked
 * the write and the mirror kept its old value" looks like from outside the function.
 *
 * The fix taps bapsound_setMasterVolume: if it is called while g_soundReady is still 0, the
 * intended value is remembered rather than lost. A per-frame check (common/frame_hook.h, the same
 * "call me once per rendered frame" site every other feature in this tree uses for a live slider
 * preview) re-applies that value the moment g_soundReady actually becomes 1 - which happens a
 * handful of instructions later in the very same function, so in practice this resolves within the
 * same frame sys_frame is next pumped. Nothing about live control changes; this only affects the
 * one call that the original code was never going to honour anyway.
 */
#include "sfx_volume_save_fix.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SFX_VOLUME_SAVE_FIX_SECTION "sfx_volume_save_fix"

/* --- 0x00417459  bapsound_getMasterVolume: the whole function is the detour target ------------ *
 * The 10-byte prologue alone (push ebp / mov ebp,esp / cmp [g_soundReady],0) is NOT unique -
 * measured against the retail image it matches twice, at 0x00417459 and 0x0041778C, because
 * another tiny getter shares the same guard idiom. The full 30-byte body is used instead: it ends
 * on a clean instruction boundary and matches exactly once in every retail build measured,
 * including the German one. detour_prologue stays 10, since that is all a `jmp rel32` overwrites,
 * and those 10 bytes carry no relative operand for the trampoline to break.
 *
 * The two embedded absolute addresses (the guard cell and the _AIL_digital_master_volume IAT slot)
 * are the site's own operands rather than something this patch derives, so leaving them literal
 * follows the same precedent as diag_audio.c's SIG_SOUND_MASTER_VOLUME, one function over. The
 * consequence is worth naming: a build that relinked its data section fails this pattern and the
 * bug-1 half declines, which is the correct answer rather than a guess. */
static const uint8_t SIG_MASTER_GET[] = {
    0x55, 0x8B, 0xEC,                         /* push ebp / mov ebp,esp                       */
    0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00, 0x00, /* cmp dword ptr [0x005BB4B8], 0                */
    0x75, 0x04,                               /* jnz +4                                       */
    0x33, 0xC0,                               /* xor eax,eax                                  */
    0xEB, 0x0C,                               /* jmp +0xC                                     */
    0xA1, 0x98, 0xAE, 0x5B, 0x00,             /* mov eax,[0x005BAE98]                         */
    0x50,                                     /* push eax                                     */
    0xFF, 0x15, 0x58, 0x17, 0x8C, 0x00,       /* call dword ptr [_AIL_digital_master_volume]  */
    0x5D,                                     /* pop ebp                                      */
    0xC3                                      /* ret                                          */
};
#define MASTER_GET_PROLOGUE     10u
#define MASTER_GET_GUARD_OFFSET  5u   /* the `cmp dword ptr [addr],0` operand */

/* --- 0x0041738D  inside bapsound_setMasterVolume: where the scale and the mirror live --------- *
 * `fild [ebp+8]; fdiv [scale]; fstp [mirror]` - both operands masked out and read back, because
 * they are plain .data addresses with no reason to survive a relink at the same offset. Being
 * address-free, this is the one site of the three that also resolves on the Edit Tool's recompile
 * of the engine. */
static const uint8_t SIG_MIRROR_SITE[] = {
    0xDB, 0x45, 0x08,                   /* fild dword ptr [ebp+0x08]             */
    0xD8, 0x35, 0x00, 0x00, 0x00, 0x00, /* fdiv float ptr [scale, wildcarded]    */
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00  /* fstp float ptr [mirror, wildcarded]   */
};
static const uint8_t MASK_MIRROR_SITE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_MIRROR_SITE) == sizeof(MASK_MIRROR_SITE),
               "the mirror pattern and its mask are different lengths");
#define MIRROR_SITE_SCALE_OFFSET  5u
#define MIRROR_SITE_MIRROR_OFFSET 11u

/* --- 0x00417379  bapsound_setMasterVolume: the detour target for bug 2 ------------------------ *
 * Identical to diag_audio.c's proven SIG_SOUND_MASTER_VOLUME (same function, same guard idiom).
 * The guard operand at offset 8 is read back rather than assumed, even though it is also embedded
 * literally for uniqueness: an address that has to appear in a pattern is still not something the
 * calling code should hardcode a second time. It is cross-checked against the getter's own guard
 * operand below, so a build where the two sites disagree is refused rather than half-patched. */
static const uint8_t SIG_MASTER_SET[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00,
    0x00, 0x75, 0x05, 0xE9, 0xC8, 0x00, 0x00, 0x00
};
#define MASTER_SET_PROLOGUE      6u
#define MASTER_SET_GUARD_OFFSET  8u

enum {
    SITE_MASTER_GET,
    SITE_MIRROR_SITE,
    SITE_MASTER_SET,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("master_get",  SIG_MASTER_GET, MASTER_GET_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("mirror_site", SIG_MIRROR_SITE, MASK_MIRROR_SITE),
    SIGNATURE_ENTRY_DETOUR("master_set",  SIG_MASTER_SET, MASTER_SET_PROLOGUE)
};

typedef int32_t (__cdecl *master_get_fn_t)(void);
typedef void    (__cdecl *master_set_fn_t)(int32_t volume);

typedef struct sfx_volume_save_fix_state {
    bool             installed;
    bool             enabled;
    detour_t         master_get;
    detour_t         master_set;
    const float     *mirror;      /* g_sfxMasterVolume, normalised 0..1, kept live by the setter */
    const float     *scale;       /* g_sfxVolumeScale, the 0..127 conversion factor (127.0)      */
    const int32_t   *sound_ready; /* g_soundReady - the flag that arrives one statement too late */
    bool             pending;
    int32_t          pending_volume;
} sfx_volume_save_fix_state_t;

static sfx_volume_save_fix_state_t fix_state;

static bool sound_is_ready(void)
{
    return fix_state.sound_ready != NULL && *fix_state.sound_ready != 0;
}

/* Replaces bapsound_getMasterVolume outright. Same answers as the original in both of its
 * branches, but the volume branch reads the engine's own mirror rather than asking the driver. */
static int32_t __cdecl hook_master_get(void)
{
    float value;
    float scale;

    /* The original's own first branch: no sound, no volume, answer 0. See the header for why
     * dropping this would put SVOL=127 into obi.ini on a machine with no working sound device. */
    if (!sound_is_ready()) {
        return 0;
    }

    scale = *fix_state.scale;
    value = *fix_state.mirror * scale;
    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > scale) {
        value = scale;
    }
    return (int32_t)(value + 0.5f);
}

static void __cdecl hook_master_set(int32_t volume)
{
    master_set_fn_t original = (master_set_fn_t)fix_state.master_set.original;
    bool            was_not_ready;

    if (original == NULL) {
        return;                         /* the un-armed instant between the branch and the state */
    }

    was_not_ready = !sound_is_ready();
    original(volume);

    if (was_not_ready) {
        /* The call above was a guaranteed no-op (bug 2). Remember what it was trying to do so the
         * frame hook can finish the job the instant g_soundReady actually flips to 1. */
        fix_state.pending_volume = volume;
        fix_state.pending = true;
    }
}

static void on_frame(void)
{
    master_set_fn_t original;

    if (!fix_state.pending || !sound_is_ready()) {
        return;
    }
    fix_state.pending = false;

    original = (master_set_fn_t)fix_state.master_set.original;
    if (original == NULL) {
        return;
    }
    original(fix_state.pending_volume);

    log_info("startup SFX volume (%d) applied - bapsound_moduleInit calls bapsound_setMasterVolume "
             "BEFORE marking sound ready, so its own load-time apply is always dropped and "
             "the engine would otherwise start every session at full SFX volume regardless of "
             "obi.ini. Re-applied the moment the sound subsystem actually finished initialising.",
             (int)fix_state.pending_volume);
}

/* Reads an absolute operand out of a resolved site and range-checks it before it becomes a
 * pointer. An operand that reads back as a cell outside the host image is a build this pattern
 * does not actually describe, and following it would be worse than declining. */
static bool resolve_operand(size_t site_index, size_t operand_offset, const char *what,
                            uintptr_t *out_address)
{
    uintptr_t site = sites[site_index].address;
    uint32_t  address = 0;

    *out_address = 0;
    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + operand_offset, &address) || address == 0) {
        log_warning("the %s operand at %08X could not be read", what,
                    (unsigned)(site + operand_offset));
        return false;
    }
    if (!memory_is_inside_image((uintptr_t)address, sizeof(uint32_t))) {
        log_warning("the %s operand reads back as %08X, which is outside the host image, refused",
                    what, (unsigned)address);
        return false;
    }
    *out_address = (uintptr_t)address;
    return true;
}

void sfx_volume_save_fix_install(void)
{
    uintptr_t scale_address = 0;
    uintptr_t mirror_address = 0;
    uintptr_t set_guard = 0;
    uintptr_t get_guard = 0;

    if (fix_state.installed) {
        return;
    }
    fix_state.installed = true;

    log_init("sfx_volume_save_fix", false);

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the SFX slider keeps resetting to full");
        return;
    }

    fix_state.enabled = ini_read_bool(SFX_VOLUME_SAVE_FIX_SECTION, "Enabled", true);
    if (!fix_state.enabled) {
        log_info("Enabled=0, the SFX slider keeps resetting to full");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    if (!resolve_operand(SITE_MIRROR_SITE, MIRROR_SITE_SCALE_OFFSET, "volume scale",
                         &scale_address) ||
        !resolve_operand(SITE_MIRROR_SITE, MIRROR_SITE_MIRROR_OFFSET, "volume mirror",
                         &mirror_address)) {
        log_warning("the scale/mirror operands did not resolve, nothing changed");
        return;
    }
    fix_state.scale  = (const float *)scale_address;
    fix_state.mirror = (const float *)mirror_address;

    /* The guard cell is resolved BEFORE either detour goes in, because both hooks need it: the
     * getter to reproduce the engine's own "not ready answers 0" branch, and the setter to know
     * whether the call it just forwarded was a no-op. Without it there is no faithful replacement
     * to install, so the feature declines rather than installing a hook that answers differently
     * from the function it replaced. Both sites carry the operand and they must name the same
     * cell; if they do not, this is not the pair of functions this file was written against. */
    if (sites[SITE_MASTER_SET].address != 0 &&
        !resolve_operand(SITE_MASTER_SET, MASTER_SET_GUARD_OFFSET, "sound-ready flag",
                         &set_guard)) {
        return;
    }
    if (sites[SITE_MASTER_GET].address != 0 &&
        !resolve_operand(SITE_MASTER_GET, MASTER_GET_GUARD_OFFSET, "sound-ready flag",
                         &get_guard)) {
        return;
    }
    if (set_guard != 0 && get_guard != 0 && set_guard != get_guard) {
        log_warning("the getter and the setter guard different cells (%08X vs %08X), this is not "
                    "the pair of functions this fix was written against, nothing changed",
                    (unsigned)get_guard, (unsigned)set_guard);
        return;
    }
    fix_state.sound_ready = (const int32_t *)(set_guard != 0 ? set_guard : get_guard);

    if (fix_state.sound_ready == NULL) {
        log_warning("neither audio site resolved, obi.ini's SVOL will keep being written from an "
                    "unreliable AIL query and the SFX slider will keep starting at full");
        return;
    }

    if (sites[SITE_MASTER_GET].address != 0) {
        if (detour_install(&fix_state.master_get, sites[SITE_MASTER_GET].address,
                           (const void *)hook_master_get, MASTER_GET_PROLOGUE)) {
            log_info("bapsound_getMasterVolume redirected at %08X: the SFX slider now saves and "
                     "re-seeds from the engine's own volume mirror at %08X (scaled by %08X) "
                     "instead of an AIL query that does not reliably reflect what was last set. "
                     "The original's \"sound not ready answers 0\" branch is reproduced, gated on "
                     "%08X.",
                     (unsigned)sites[SITE_MASTER_GET].address, (unsigned)mirror_address,
                     (unsigned)scale_address, (unsigned)(uintptr_t)fix_state.sound_ready);
        } else {
            log_error("the master_get detour at %08X failed",
                      (unsigned)sites[SITE_MASTER_GET].address);
        }
    } else {
        log_warning("bapsound_getMasterVolume did not resolve - obi.ini's SVOL will keep being "
                    "written from an unreliable AIL query");
    }

    if (sites[SITE_MASTER_SET].address == 0) {
        log_warning("bapsound_setMasterVolume did not resolve - the SFX slider will keep starting "
                    "at full every launch regardless of obi.ini's SVOL");
        return;
    }
    if (!detour_install(&fix_state.master_set, sites[SITE_MASTER_SET].address,
                        (const void *)hook_master_set, MASTER_SET_PROLOGUE)) {
        log_error("the master_set detour at %08X failed",
                  (unsigned)sites[SITE_MASTER_SET].address);
        return;
    }
    if (!frame_hook_add(on_frame)) {
        log_warning("frame hook unavailable, the startup re-apply is skipped - the SFX slider will "
                    "keep starting at full every launch regardless of obi.ini's SVOL");
        return;
    }

    log_info("bapsound_setMasterVolume tapped at %08X, g_soundReady at %08X: bapsound_moduleInit's "
             "own load-time volume apply happens before it marks sound ready and was always a "
             "silent no-op, which is why the SFX slider reset to full on every reload regardless "
             "of what obi.ini said. The dropped value is now re-applied the moment sound actually "
             "finishes initialising.",
             (unsigned)sites[SITE_MASTER_SET].address, (unsigned)(uintptr_t)fix_state.sound_ready);
}
