/* music_volume.c: see music_volume.h for the fault and the call sequence that causes it. */
#include "music_volume.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMUSE_FIX_SECTION "imuse_fix"

/* ==============================================================================================
 * The three sites, from retail WMAIN.EXE, 829,952 bytes, MD5 7c5af8428c19b17cca09ae3a49bd10ef.
 *
 * Every absolute operand is masked out. The cells they name (g_musicVolume 0x004AA418,
 * g_musicAttached 0x005BAB8C, g_musicModuleOn 0x005BAB88) move on the Edit Tool's recompile, and a
 * pattern that insisted on them would resolve to nothing there and switch this off with a warning
 * rather than a fault.
 * ============================================================================================ */

/* bapMusicAttach, 0x00410331.
 *   55              push ebp
 *   8B EC           mov  ebp, esp
 *   83 EC 18        sub  esp, 18h
 *   83 3D <abs> 00  cmp  dword ptr [g_musicModuleOn], 0
 *   75 0A           jne  +0A
 *   B8 01 00 00 00  mov  eax, 1                          the module is off, answer 1 */
static const uint8_t SIG_MUSIC_ATTACH[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MUSIC_ATTACH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MUSIC_ATTACH == sizeof MSK_MUSIC_ATTACH,
               "the attach mask must be the same length as its pattern");
#define MUSIC_ATTACH_PROLOGUE 6u        /* push ebp; mov ebp,esp; sub esp,18h */

/* bapMusicDetach, 0x0041046C.
 *   55              push ebp
 *   8B EC           mov  ebp, esp
 *   83 3D <abs> 00  cmp  dword ptr [g_musicAttached], 0
 *   75 02           jne  +02
 *   EB 70           jmp  the tail                        not attached, nothing to do
 *   83 3D <abs> 00  cmp  dword ptr [g_musicPaused], 0 */
static const uint8_t SIG_MUSIC_DETACH[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02, 0xEB, 0x70,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MUSIC_DETACH[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
_Static_assert(sizeof SIG_MUSIC_DETACH == sizeof MSK_MUSIC_DETACH,
               "the detach mask must be the same length as its pattern");
#define MUSIC_DETACH_PROLOGUE 10u       /* push ebp; mov ebp,esp; cmp [abs],0 */

/* bapMusicSetVolume, 0x004106CC.
 *   55              push ebp
 *   8B EC           mov  ebp, esp
 *   51              push ecx
 *   C7 45 FC 0..0   mov  dword ptr [ebp-4], 0
 *   D9 05 <abs>     fld  dword ptr [g_musicVolume]
 *   D8 5D 08        fcomp dword ptr [ebp+8]              the newVol argument
 *   DF E0           fnstsw ax
 *   F6 C4 ..        test ah, ..                          the == that returns early */
static const uint8_t SIG_MUSIC_SET_VOLUME[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x5D, 0x08, 0xDF, 0xE0, 0xF6, 0xC4
};
static const uint8_t MSK_MUSIC_SET_VOLUME[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MUSIC_SET_VOLUME == sizeof MSK_MUSIC_SET_VOLUME,
               "the set-volume mask must be the same length as its pattern");
/* push ebp; mov ebp,esp; push ecx; mov [ebp-4],0. An exact instruction boundary and no operand
 * that a trampoline would have to relocate. */
#define MUSIC_SET_VOLUME_PROLOGUE 11u

enum {
    SITE_ATTACH,
    SITE_DETACH,
    SITE_SET_VOLUME,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("bap_music_attach", SIG_MUSIC_ATTACH, MSK_MUSIC_ATTACH,
                                  MUSIC_ATTACH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bap_music_detach", SIG_MUSIC_DETACH, MSK_MUSIC_DETACH,
                                  MUSIC_DETACH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bap_music_set_volume", SIG_MUSIC_SET_VOLUME,
                                  MSK_MUSIC_SET_VOLUME, MUSIC_SET_VOLUME_PROLOGUE)
};

typedef uint32_t (__cdecl *music_attach_fn_t)(void);
typedef void     (__cdecl *music_detach_fn_t)(void);
typedef void     (__cdecl *music_set_volume_fn_t)(float volume);

static struct {
    bool     active;
    detour_t attach;
    detour_t detach;
    detour_t set_volume;

    /* Two deep, and the depth is the whole mechanism. The screen's own silencing call arrives
     * between the player's last drag and the detach, so the value the player chose is the one
     * BEFORE the current one whenever the current one is a zero standing in front of a detach. */
    float    current;
    float    previous;
    bool     seen_any;

    bool     restore_valid;
    float    restore_value;
    bool     logged_restore;
} music;

static void __cdecl hook_set_volume(float volume)
{
    music_set_volume_fn_t original = (music_set_volume_fn_t)music.set_volume.original;

    music.previous = music.seen_any ? music.current : volume;
    music.current  = volume;
    music.seen_any = true;

    if (original != NULL) {
        original(volume);
    }
}

static void __cdecl hook_detach(void)
{
    music_detach_fn_t original = (music_detach_fn_t)music.detach.original;

    /* A zero standing immediately in front of a detach is the screen silencing the music for a
     * provider change, never the player, because a player who drags to silence has already put
     * their own zero into `previous` on the call before this one. Either way `previous` is what
     * they asked for, so this needs no rule about what zero means. */
    if (music.seen_any && music.current == 0.0f) {
        music.restore_value = music.previous;
        music.restore_valid = true;
    }

    if (original != NULL) {
        original();
    }
}

static uint32_t __cdecl hook_attach(void)
{
    music_attach_fn_t     original = (music_attach_fn_t)music.attach.original;
    music_set_volume_fn_t set      = (music_set_volume_fn_t)music.set_volume.original;
    uint32_t              result   = 1u;

    if (original != NULL) {
        result = original();
    }
    /* AFTER the original, deliberately. It ends by reading MVOL out of obi.ini and applying it,
     * and that is the write being corrected; putting the value back first would simply be
     * overwritten a few instructions later. */
    if (music.restore_valid) {
        music.restore_valid = false;
        if (set != NULL) {
            set(music.restore_value);
            music.current  = music.restore_value;
            music.previous = music.restore_value;
            if (!music.logged_restore) {
                music.logged_restore = true;
                log_info("the music volume (%.2f) was put back after a provider change; the "
                         "re-attach had reloaded MVOL from obi.ini, which still held the value "
                         "from before the slider was moved. Reported once.",
                         (double)music.restore_value);
            }
        }
    }
    return result;
}

bool music_volume_is_active(void)
{
    return music.active;
}

bool music_volume_install(void)
{
    size_t index;

    if (!ini_read_bool(IMUSE_FIX_SECTION, "MusicVolumeAcrossProvider", true)) {
        log_info("MusicVolumeAcrossProvider=0, the music slider keeps reloading MVOL from the "
                 "file when the 3-D provider changes");
        return false;
    }

    signature_resolve_table(sites, SITE_COUNT);
    for (index = 0; index < SITE_COUNT; ++index) {
        if (sites[index].address == 0) {
            log_warning("%s did not resolve, so the music volume is left to the engine and a "
                        "provider change still discards a slider move", sites[index].name);
            return false;
        }
    }

    /* All three or none. A detour cannot be taken back out, so a half-installed feature would
     * leave hooks in the host belonging to something that reports itself absent. */
    if (!detour_install(&music.set_volume, sites[SITE_SET_VOLUME].address,
                        (const void *)hook_set_volume, MUSIC_SET_VOLUME_PROLOGUE)) {
        log_warning("bapMusicSetVolume at %08X could not be detoured, the music volume is left "
                    "to the engine", (unsigned)sites[SITE_SET_VOLUME].address);
        return false;
    }
    if (!detour_install(&music.detach, sites[SITE_DETACH].address,
                        (const void *)hook_detach, MUSIC_DETACH_PROLOGUE)) {
        log_warning("bapMusicDetach at %08X could not be detoured. The value latch above is live "
                    "and harmless, but nothing reads it, so a provider change still discards a "
                    "slider move", (unsigned)sites[SITE_DETACH].address);
        return false;
    }
    if (!detour_install(&music.attach, sites[SITE_ATTACH].address,
                        (const void *)hook_attach, MUSIC_ATTACH_PROLOGUE)) {
        log_warning("bapMusicAttach at %08X could not be detoured, so the value is remembered "
                    "across a provider change and never put back",
                    (unsigned)sites[SITE_ATTACH].address);
        return false;
    }

    music.active = true;
    log_info("the music volume now survives a 3-D provider change. The audio screen silences the "
             "music, detaches and re-attaches to switch provider, and the re-attach reloads MVOL "
             "from obi.ini, which the screen does not write until it closes. A slider moved before "
             "the switch was therefore thrown away, and a switch made while music was already "
             "detached wrote MVOL=0. attach %08X, detach %08X, setVolume %08X",
             (unsigned)sites[SITE_ATTACH].address, (unsigned)sites[SITE_DETACH].address,
             (unsigned)sites[SITE_SET_VOLUME].address);
    return true;
}
