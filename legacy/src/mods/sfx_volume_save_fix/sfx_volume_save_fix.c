/* sfx_volume_save_fix.c: the SFX slider always comes back to (about) full, no matter what was
 * saved.
 *
 * ==============================================================================================
 * BUG 1 - the value handed to the ini write is wrong (fixed, but turned out not to be the whole
 *          story)
 *
 * The audio options screen (menu_options_audio_run, retail 0x00441FA4) has exactly one function
 * it calls to find out "what is the SFX volume right now": sfx_master_volume_get_from_ail,
 * retail 0x00417459. It asks MILES for the digital device's current master volume instead of
 * reading back whatever the engine itself last set, and that AIL round-trip does not reliably
 * reflect the value just pushed with _AIL_set_digital_master_volume. Measured in game: with only
 * this bug fixed, a live session correctly saves the value the slider was left at (confirmed via
 * a temporary diagnostic build - dragging to 33 produced `SVOL=33` in obi.ini). So this getter
 * really was wrong, and is still worth fixing. It is not, however, why the volume resets on
 * RELOAD: that bug survived this fix completely unchanged, which is what pointed at bug 2.
 *
 * sfx_master_volume_set (retail 0x00417379), the function that DRIVES the slider live, keeps a
 * reliable engine-side copy of the true value two instructions in:
 *
 *     0041738D  DB 45 08               fild dword ptr [ebp+0x08]      ; the int 0..127 argument
 *     00417390  D8 35 50 81 4A 00      fdiv float ptr [g_sfxVolumeScale]     ; -> 0x004A8150 (127.0)
 *     00417396  D9 1D 70 A9 4A 00      fstp float ptr [g_sfxMasterVolume]    ; -> 0x004AA970
 *
 * [0x004AA970] is not a write-only shadow: FUN_004169BD (the per-channel volume calculation)
 * reads it back on every sample start, `base * scale * g_sfxMasterVolume`, so this cell has to
 * stay correct for gameplay volume to be right at all. sfx_master_volume_get_from_ail is entirely
 * replaced (not wrapped - calling through to the AIL query first would just reintroduce the bug)
 * with a hook that computes the same 0..127 integer the engine itself derived the mirror from.
 *
 * ==============================================================================================
 * BUG 2 - the value never gets APPLIED on load, which is the actual cause of the reported symptom
 *
 * sfx_sound_init (retail 0x004159F0), the function that runs once at startup, does this, in
 * exactly this order:
 *
 *     FUN_004649A8(&DAT_004AB04C, 0x7F, &local_8);   // ini_read_int_alt("SVOL", default 127)
 *     FUN_00417379(local_8);                          // sfx_master_volume_set(loaded value)
 *     ...
 *     DAT_005BB4B8 = 1;                               // "sound is ready" - set AFTER the call above
 *
 * sfx_master_volume_set's entire body is gated on `DAT_005BB4B8 != 0`:
 *
 *     0041737F  83 3D B8 B4 5B 00 00   cmp dword ptr [g_soundReady], 0
 *     00417386  75 05                  jnz +5            ; only THEN does the real work run
 *     0041738B  E9 C8 00 00 00         jmp <exit, does nothing>
 *
 * At the moment sfx_sound_init calls it with the value it just loaded from obi.ini, g_soundReady
 * is STILL 0 - it is not set to 1 until several instructions later, in the SAME function. So the
 * very first, load-time apply is a GUARANTEED silent no-op, every single launch, regardless of
 * what SVOL says in the file. The mirror simply keeps its compiled-in startup value (measured as
 * 1.0, i.e. full) until the player manually touches the slider. This is a genuine ordering mistake
 * in the original 1999 code - one statement in the wrong place - and it is the actual reason "the
 * SFX slider always resets to full on reload": the save was never the whole problem, the load
 * silently threw the saved value away.
 *
 * Confirmed with a temporary diagnostic build across two separate sessions: the very first call to
 * sfx_master_volume_set in each run showed the mirror ending up at 1.0 regardless of the argument
 * passed in (120 in one run, 0 in the other), which is exactly what "the guard blocked the write
 * and the mirror kept its old value" looks like from outside the function.
 *
 * The fix taps sfx_master_volume_set: if it is called while g_soundReady is still 0, the intended
 * value is remembered rather than lost. A per-frame check (common/frame_hook.h, the same "call me
 * once per rendered frame" site every other feature in this tree uses for a live slider preview)
 * re-applies that value the moment g_soundReady actually becomes 1 - which happens a handful of
 * instructions later in the very same function, so in practice this resolves within the same
 * frame sys_frame is next pumped. Nothing about live control changes; this only affects the one
 * call that the original code was never going to honour anyway.
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

/* --- 0x00417459  sfx_master_volume_get_from_ail: the whole function is the detour target ----- *
 * The 10-byte prologue alone (push ebp/mov ebp,esp/cmp [g_soundReady],0) is NOT unique - measured
 * against the retail image, it matches twice, another tiny getter shares the same guard idiom.
 * The full 30-byte body is used instead: a clean instruction boundary (the next byte begins the
 * `jnz` at 00417463), short enough to use whole, detour_prologue stays 10 since that is all a
 * `jmp rel32` overwrites. The two embedded absolute addresses (the guard flag and the
 * _AIL_digital_master_volume IAT slot) are the site's own operands, not something this patch
 * derives, so leaving them literal follows the same precedent as diag_audio.c's
 * SIG_SOUND_MASTER_VOLUME (same guard cell, same idiom, one function over). */
static const uint8_t SIG_MASTER_GET[] = {
    0x55, 0x8B, 0xEC,                         /* push ebp / mov ebp,esp                      */
    0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00, 0x00, /* cmp dword ptr [0x005BB4B8], 0                */
    0x75, 0x04,                               /* jnz +4                                      */
    0x33, 0xC0,                               /* xor eax,eax                                 */
    0xEB, 0x0C,                               /* jmp +0xC                                    */
    0xA1, 0x98, 0xAE, 0x5B, 0x00,             /* mov eax,[0x005BAE98]                         */
    0x50,                                     /* push eax                                    */
    0xFF, 0x15, 0x58, 0x17, 0x8C, 0x00,       /* call dword ptr [_AIL_digital_master_volume]  */
    0x5D,                                     /* pop ebp                                     */
    0xC3                                      /* ret                                         */
};
#define MASTER_GET_PROLOGUE 10u

/* --- 0x0041738D  inside sfx_master_volume_set: where the scale and the mirror live ----------- *
 * `fild [ebp+8]; fdiv [scale]; fstp [mirror]` - both operands masked out and read back, they are
 * plain .data addresses with no reason to survive a relink at the same offset. */
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
#define MIRROR_SITE_SCALE_OFFSET  5u
#define MIRROR_SITE_MIRROR_OFFSET 11u

/* --- 0x00417379  sfx_master_volume_set: the detour target for bug 2 --------------------------- *
 * Identical to diag_audio.c's proven SIG_SOUND_MASTER_VOLUME (same function, same guard idiom).
 * The guard-flag operand at offset 8 (the `cmp dword ptr [addr],0` target) is read back rather
 * than assumed, even though it is also embedded literally for uniqueness - the same rule as
 * every other site in this file: an address that has to appear in the pattern is still not
 * something calling code should hardcode separately. */
static const uint8_t SIG_MASTER_SET[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00,
    0x00, 0x75, 0x05, 0xE9, 0xC8, 0x00, 0x00, 0x00
};
#define MASTER_SET_PROLOGUE   6u
#define MASTER_SET_GUARD_OFFSET 8u

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

static int32_t __cdecl hook_master_get(void)
{
    float   value;
    float   scale = *fix_state.scale;
    int32_t result;

    value = *fix_state.mirror * scale;
    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > scale) {
        value = scale;
    }
    result = (int32_t)(value + 0.5f);
    return result;
}

static void __cdecl hook_master_set(int32_t volume)
{
    master_set_fn_t original = (master_set_fn_t)fix_state.master_set.original;
    bool             was_not_ready = (fix_state.sound_ready != NULL && *fix_state.sound_ready == 0);

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

    if (!fix_state.pending || fix_state.sound_ready == NULL || *fix_state.sound_ready == 0) {
        return;
    }
    fix_state.pending = false;

    original = (master_set_fn_t)fix_state.master_set.original;
    original(fix_state.pending_volume);

    log_info("startup SFX volume (%d) applied - sfx_sound_init calls sfx_master_volume_set BEFORE "
             "marking sound ready, so its own load-time apply is always silently dropped and the "
             "engine would otherwise start every session at full SFX volume regardless of obi.ini. "
             "Re-applied the moment the sound subsystem actually finished initialising.",
             (int)fix_state.pending_volume);
}

static bool resolve_float_operand(size_t site_index, size_t operand_offset, const float **out)
{
    uintptr_t site = sites[site_index].address;
    uint32_t  address = 0;

    *out = NULL;
    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + operand_offset, &address) || address == 0) {
        return false;
    }
    *out = (const float *)(uintptr_t)address;
    return true;
}

static bool resolve_int_operand(size_t site_index, size_t operand_offset, const int32_t **out)
{
    uintptr_t site = sites[site_index].address;
    uint32_t  address = 0;

    *out = NULL;
    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + operand_offset, &address) || address == 0) {
        return false;
    }
    *out = (const int32_t *)(uintptr_t)address;
    return true;
}

void sfx_volume_save_fix_install(void)
{
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

    if (!resolve_float_operand(SITE_MIRROR_SITE, MIRROR_SITE_SCALE_OFFSET, &fix_state.scale) ||
        !resolve_float_operand(SITE_MIRROR_SITE, MIRROR_SITE_MIRROR_OFFSET, &fix_state.mirror)) {
        log_warning("could not read the scale/mirror operands, nothing changed");
        return;
    }

    if (sites[SITE_MASTER_GET].address != 0) {
        if (detour_install(&fix_state.master_get, sites[SITE_MASTER_GET].address,
                           (const void *)hook_master_get, MASTER_GET_PROLOGUE)) {
            log_info("sfx_master_volume_get_from_ail redirected at %08X: the SFX slider now saves "
                     "and re-seeds from the engine's own volume mirror at %08X (scaled by %08X) "
                     "instead of an AIL query that does not reliably reflect what was last set.",
                     (unsigned)sites[SITE_MASTER_GET].address, (unsigned)(uintptr_t)fix_state.mirror,
                     (unsigned)(uintptr_t)fix_state.scale);
        } else {
            log_error("the master_get detour at %08X failed", (unsigned)sites[SITE_MASTER_GET].address);
        }
    } else {
        log_warning("sfx_master_volume_get_from_ail did not resolve - obi.ini's SVOL will keep "
                    "being written from an unreliable AIL query");
    }

    if (sites[SITE_MASTER_SET].address == 0) {
        log_warning("sfx_master_volume_set did not resolve - the SFX slider will keep starting at "
                    "full every launch regardless of obi.ini's SVOL");
        return;
    }
    if (!resolve_int_operand(SITE_MASTER_SET, MASTER_SET_GUARD_OFFSET, &fix_state.sound_ready)) {
        log_warning("could not read the g_soundReady operand, the startup re-apply is skipped");
        return;
    }
    if (!detour_install(&fix_state.master_set, sites[SITE_MASTER_SET].address,
                        (const void *)hook_master_set, MASTER_SET_PROLOGUE)) {
        log_error("the master_set detour at %08X failed", (unsigned)sites[SITE_MASTER_SET].address);
        return;
    }
    if (!frame_hook_add(on_frame)) {
        log_warning("frame hook unavailable, the startup re-apply is skipped - the SFX slider will "
                    "keep starting at full every launch regardless of obi.ini's SVOL");
        return;
    }

    log_info("sfx_master_volume_set tapped at %08X, g_soundReady at %08X: sfx_sound_init's own "
             "load-time volume apply happens before it marks sound ready and was always a silent "
             "no-op, which is why the SFX slider reset to full on every reload regardless of what "
             "obi.ini said. The dropped value is now re-applied the moment sound actually finishes "
             "initialising.",
             (unsigned)sites[SITE_MASTER_SET].address, (unsigned)(uintptr_t)fix_state.sound_ready);
}
