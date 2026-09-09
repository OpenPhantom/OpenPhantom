/* slider_rounding.c: the audio screen's two volume sliders lose 7 of 127 every time it opens.
 *
 * The round trip, and both halves TRUNCATE.
 *
 * The screen has nineteen slider steps ([0x004A8634] is 19.0f). Opening it seeds each widget from
 * the live volume, and moving one reads the widget back:
 *
 *     seed      widget = ftol(volume / 127.0f * 19.0f)      0x004420B1 music, 0x004420E1 sfx
 *     readback  volume = ftol(widget / 19.0f * 127.0f)
 *
 * `__ftol` truncates toward zero, so neither step ever rounds up and the value can only walk down:
 *
 *     SVOL  60 -> widget  8 -> SVOL  53      SVOL 106 -> widget 15 -> SVOL 100
 *     SVOL 100 -> widget 14 -> SVOL  93      SVOL  33 -> widget  4 -> SVOL  26
 *     SVOL 127 -> widget 19 -> SVOL 127
 *
 * Seven every visit, and only full volume is stable. Those exact numbers were read out of a field
 * log: 106, 100, 93, 60, 53 and 33 in that order, across two sessions of a player opening the
 * screen and touching nothing but the provider list.
 *
 * Music takes the same path through a 100 scale, so 0.47 seeds 8.93, truncates to 8, comes back as
 * 0.421 and writes MVOL=42. That is a 47 becoming 42 with nothing touched.
 *
 * Rounding the seed is enough. 8.93 becomes 9, which reads back as 60.16 and truncates to 60, and
 * the value is then stable. Rounding the readback as well would be a second write for no gain,
 * because the readback only ever sees a value the seed has already made representable. Checked
 * across the whole range: every value is stable except 1, which has nowhere to sit among nineteen
 * steps and becomes 0.
 *
 * Why two call redirects and not a detour. `__ftol` is the compiler's own helper and the image
 * calls it from everywhere; detouring it to round would change every float-to-int conversion in the
 * game. The two calls that matter are the seeds, so only those two displacements are rewritten, to
 * a thunk that adds a half and falls into the real helper. Every other caller is untouched, and the
 * helper itself is not modified at all. */
#include "slider_rounding.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The seed, for either slider:
 *
 *   D8 0D <19.0f>   fmul dword ptr [slider_steps]
 *   D9 55 <disp8>   fst  dword ptr [ebp-x]
 *   E8 <rel32>      call __ftol
 *   50              push eax
 *   6A 02 | 6A 04   push the widget id, 2 for music and 4 for sfx
 *
 * The widget id separates the two, so each pattern is unique on its own. The 19.0f operand
 * and the call displacement are masked: both move on a rebuild, and neither identifies the site. */
#define SEED_CALL_OFFSET 9u     /* where the E8 sits inside the pattern */

static const uint8_t SIG_SEED_MUSIC[] = {
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x55, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x6A, 0x02
};
static const uint8_t SIG_SEED_SFX[] = {
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x55, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x6A, 0x04
};
static const uint8_t MSK_SEED[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SEED_MUSIC == sizeof MSK_SEED,
               "the music seed pattern and its mask are different lengths");
_Static_assert(sizeof SIG_SEED_SFX == sizeof MSK_SEED,
               "the sfx seed pattern and its mask are different lengths");

enum {
    SITE_SEED_MUSIC,
    SITE_SEED_SFX,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("audio_seed_music", SIG_SEED_MUSIC, MSK_SEED),
    SIGNATURE_ENTRY_MASKED("audio_seed_sfx",   SIG_SEED_SFX,   MSK_SEED)
};

static const float HALF = 0.5f;

/* The real helper, read out of the displacement being replaced rather than resolved separately.
 * Whatever those two calls already pointed at is what this falls into, so a wrapper somebody else
 * had already installed keeps working. */
static uintptr_t real_ftol;

/* Naked because there is nothing to do in C: the value is in ST(0), not on the stack, and the
 * answer belongs in EAX. Add a half and fall into the helper, which pops and converts exactly as
 * it did before. The volumes here are never negative, so a single bias rounds them all. */
static __declspec(naked) void ftol_rounded(void)
{
    __asm {
        fadd dword ptr [HALF]
        jmp  dword ptr [real_ftol]
    }
}

static bool slider_active;

bool slider_rounding_is_active(void)
{
    return slider_active;
}

bool slider_rounding_install(void)
{
    uintptr_t call_sites[SITE_COUNT];
    uintptr_t target = 0;
    size_t    index;

    signature_resolve_table(sites, SITE_COUNT);
    for (index = 0; index < SITE_COUNT; ++index) {
        if (sites[index].address == 0) {
            log_warning("%s did not resolve, so the audio sliders keep losing 7 of 127 every time "
                        "that screen is opened", sites[index].name);
            return false;
        }
        call_sites[index] = sites[index].address + SEED_CALL_OFFSET;
    }

    /* Read both before writing either, and refuse unless they agree. Two seeds that call different
     * helpers would mean this pattern has found something it does not describe. */
    for (index = 0; index < SITE_COUNT; ++index) {
        uintptr_t found = 0;

        if (!patch_read_call_target(call_sites[index], &found)) {
            log_warning("the call at %08X is not a readable E8, the sliders are left alone",
                        (unsigned)call_sites[index]);
            return false;
        }
        if (index == 0) {
            target = found;
        } else if (found != target) {
            log_warning("the two seeds call different helpers (%08X and %08X), which is not the "
                        "shape this patch describes, so neither is touched",
                        (unsigned)target, (unsigned)found);
            return false;
        }
    }
    real_ftol = target;

    for (index = 0; index < SITE_COUNT; ++index) {
        if (patch_redirect_call(call_sites[index], (const void *)ftol_rounded) !=
            PATCH_RESULT_OK) {
            log_warning("%s could not be redirected at %08X. %s", sites[index].name,
                        (unsigned)call_sites[index],
                        (index == 0) ? "Nothing was written, so both sliders are as they were."
                                     : "The music seed is already rounding and the sfx seed is "
                                       "not, so that one still loses 7 a visit.");
            return false;
        }
    }

    slider_active = true;
    log_info("the audio sliders now round when the screen seeds them. Nineteen steps against a "
             "0..127 volume, and both the seed and the read back truncated, so every visit to that "
             "screen cost 7 and only full volume survived. Seeds at %08X and %08X, both falling "
             "into the conversion helper at %08X",
             (unsigned)call_sites[SITE_SEED_MUSIC], (unsigned)call_sites[SITE_SEED_SFX],
             (unsigned)real_ftol);
    return true;
}
