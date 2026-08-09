/* fmv_player.c: play the movies through a modern decoder instead of Bink, when a converted copy
 * exists, falling back to the original retail path unchanged when it doesn't.
 *
 * ==============================================================================================
 * WHY THIS EXISTS, AND WHAT IT REPLACES
 *
 * fmv_scaling.dll, the DLL this one replaces, tried three designs to make movies fill a chosen
 * display resolution without the retail engine's own per-frame surface access costing whole
 * seconds of it at 4K: a CPU resample into the engine's own locked surface, the same resample
 * through IDirectDrawSurface::GetDC + StretchDIBits instead of the engine's lock, and (as a
 * config-only experiment, no code of its own) the host DirectDraw-to-Direct3D translation layer's
 * own shadow-surface setting. All three measured the SAME frame rate on the reporting machine.
 * That result is the reason this file exists: every one of those designs still asked the RETAIL
 * ENGINE to hand a decoded Bink frame to a DirectDraw surface the translation layer manages, every
 * decoded frame, and whatever makes that expensive on that layer did not care which API was used
 * to ask.
 *
 * This file does not ask at all. It replaces the movie player wholesale, for any movie a converted
 * file exists for, with a borderless overlay window (video_overlay.c) playing through a 32-bit
 * libVLC install (vlc_playback.c) - a code path that never touches the game's DirectDraw surface,
 * never locks anything the translation layer manages, and gets real hardware-accelerated video
 * decode Bink 1 from 1999 never had. See video_overlay.c's header comment for the full history of
 * designs this settled on, including why Media Foundation was tried first and replaced. A movie
 * with no converted file falls straight through to the original Bink playback, unchanged, so this
 * is safe to install with nothing converted yet: the game plays exactly as it always did until you
 * convert something.
 *
 * ==============================================================================================
 * BYTE BASIS
 *
 * All four movie call sites (intro/logo, in-level cutscenes, the arena replay, credits) funnel
 * through one function, confirmed by an xref sweep of its four UNCONDITIONAL_CALL callers inside
 * 0x0043EB2A:
 *
 *   0043EB93  LEA EDX,[EBP-0x84]      ; a local buffer already filled with e.g. "movie\arena"
 *   0043EB99  PUSH EDX
 *   0043EB9A  CALL 0x0046C35A         ; the movie player - THIS is what this file detours
 *   0043EB9F  ADD ESP,0xC             ; caller cleans 12 bytes: __cdecl, 3 arguments
 *
 * `ADD ESP,0xC` after every one of the four call sites confirms the calling convention directly:
 * int __cdecl(const char *name, int param2, int param3). `name` is a plain ANSI, null-terminated,
 * backslash-relative movie name with no extension ("movie\arena", "movie\scene1", ...), matching
 * the game's own real layout on disk (confirmed against a legitimate install:
 * GAMEDATA\MOVIE\ARENA.BIK). param2 and param3 govern details of the RETAIL Bink path only (a
 * clear-and-present timing flag and an opaque per-call context cell); this file's hook never reads
 * either, because when it takes over it never runs any of the retail code that would have.
 *
 * The signature is the function's own prologue plus enough of its body to be unique: the bare
 * `push ebp / mov ebp,esp / sub esp,0x90` shape recurs elsewhere in an 830 KB image, which is
 * exactly why `common/signature.h`'s two-stage detour rule exists, and why this signature reaches
 * two branches into the function rather than stopping at the prologue. Measured against the real
 * retail WMAIN.EXE (829,952 bytes): exactly one match, all 72 bytes, at 0x0046C35A.
 *
 * The detour uses common/detour.h's chained-hook mechanism rather than a one-off call-site
 * redirect (unlike fmv_player's predecessor): this is a FUNCTION being replaced wholesale, not one
 * call inside a larger function, and other DLLs in this tree already share this exact chaining
 * convention for functions more than one feature might reasonably want to sit in front of.
 * ============================================================================================== */
#include "fmv_player.h"

#include "video_overlay.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FMV_PLAYER_SECTION "fmv_player"

/* --- 0x0046C35A, the movie player, entry through the last mode-check to the forced-resolution
 * push, so the anchor cannot be confused with the 640x480 push/push/call shape that (not
 * coincidentally) recurs at several OTHER sites in this image; see the byte-basis comment above. */
static const uint8_t SIG_MOVIE_PLAY[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,         /* push ebp; mov ebp,esp; sub esp,0x90 */
    0xA1, 0xF0, 0x77, 0x4B, 0x00,                                 /* mov eax,[004b77f0]      */
    0x89, 0x85, 0x74, 0xFF, 0xFF, 0xFF,                           /* mov [ebp-0x8c],eax      */
    0xC7, 0x85, 0x78, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,   /* mov [ebp-0x88],1        */
    0x83, 0x3D, 0x60, 0x63, 0x6D, 0x00, 0x00,                     /* cmp [006d6360],0        */
    0x75, 0x07,                                                    /* jnz +7                  */
    0x33, 0xC0,                                                    /* xor eax,eax             */
    0xE9, 0x5C, 0x01, 0x00, 0x00,                                 /* jmp 0046c4e4            */
    0x83, 0x3D, 0x3C, 0xA4, 0x86, 0x00, 0x00,                     /* cmp [0086a43c],0        */
    0x74, 0x07,                                                    /* jz +7                   */
    0x33, 0xC0,                                                    /* xor eax,eax             */
    0xE9, 0x4C, 0x01, 0x00, 0x00,                                 /* jmp 0046c4e4            */
    0xC7, 0x05, 0x3C, 0xA4, 0x86, 0x00, 0x01, 0x00, 0x00, 0x00    /* mov [0086a43c],1        */
};
#define MOVIE_PLAY_PROLOGUE_SIZE 9u

enum {
    SITE_MOVIE_PLAY,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("movie_play", SIG_MOVIE_PLAY, MOVIE_PLAY_PROLOGUE_SIZE)
};

typedef int (__cdecl *movie_play_fn_t)(const char *name, int param2, int param3);

typedef struct fmv_player_state {
    bool      installed;
    bool      enabled;
    char      movie_directory[MAX_PATH];
    char      extension[16];
    detour_t  detour;
    bool      overlay_ready;
} fmv_player_state_t;

static fmv_player_state_t fmv_player_state;

/* ============================================================================================ */
static void load_config(void)
{
    fmv_player_state.enabled = ini_read_bool(FMV_PLAYER_SECTION, "Enabled", true);
    ini_read_string(FMV_PLAYER_SECTION, "MovieDirectory", "movies_hd",
                    fmv_player_state.movie_directory, sizeof fmv_player_state.movie_directory);
    ini_read_string(FMV_PLAYER_SECTION, "Extension", "mp4",
                    fmv_player_state.extension, sizeof fmv_player_state.extension);
}

/* `name` is "movie\arena"-shaped; the converted file this looks for is
 * <host_directory>\<MovieDirectory>\<basename>.<Extension>, flat, no subfolder, so a user
 * converting their own files does not have to reproduce the retail "movie\" prefix at all. */
static bool build_modern_movie_path(const char *name, wchar_t *out_path, size_t out_capacity)
{
    char        ansi_path[MAX_PATH];
    const char *base_name;
    const char *cursor;
    int         written;

    base_name = name;
    for (cursor = name; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            base_name = cursor + 1;
        }
    }
    if (*base_name == '\0') {
        return false;
    }

    written = _snprintf(ansi_path, sizeof ansi_path, "%s%s\\%s.%s", host_directory(),
                        fmv_player_state.movie_directory, base_name, fmv_player_state.extension);
    if (written <= 0 || (size_t)written >= sizeof ansi_path) {
        return false;
    }

    return MultiByteToWideChar(CP_ACP, 0, ansi_path, -1, out_path, (int)out_capacity) != 0;
}

/* ============================================================================================ */
static int __cdecl hook_play_movie(const char *name, int param2, int param3)
{
    movie_play_fn_t original = (movie_play_fn_t)fmv_player_state.detour.original;
    wchar_t         modern_path[MAX_PATH];

    if (fmv_player_state.overlay_ready && name != NULL &&
        build_modern_movie_path(name, modern_path, ARRAYSIZE(modern_path)) &&
        GetFileAttributesW(modern_path) != INVALID_FILE_ATTRIBUTES) {

        log_info("playing \"%s\" through the video overlay", name);
        if (video_overlay_play_blocking(modern_path)) {
            return 1;
        }
        log_warning("modern playback of \"%s\" failed or was interrupted, falling back to the "
                    "original Bink movie", name);
    }

    return original(name, param2, param3);
}

static bool install_movie_hook(void)
{
    uintptr_t site = sites[SITE_MOVIE_PLAY].address;

    if (site == 0) {
        log_warning("the movie player did not resolve, every movie keeps using the retail Bink "
                    "path");
        return false;
    }
    if (!detour_install(&fmv_player_state.detour, site, (const void *)hook_play_movie,
                        MOVIE_PLAY_PROLOGUE_SIZE)) {
        log_error("the detour at %08X failed, every movie keeps using the retail Bink path",
                  (unsigned)site);
        return false;
    }

    log_info("movie playback intercepted at %08X. Any movie with a matching file in "
             "\"%s%s\" plays through Media Foundation instead of Bink; everything else falls "
             "through to the original path unchanged.", (unsigned)site, host_directory(),
             fmv_player_state.movie_directory);
    return true;
}

/* ============================================================================================ */
void fmv_player_install(void)
{
    log_init("fmv_player", false);

    if (fmv_player_state.installed) {
        return;
    }
    fmv_player_state.installed = true;

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, movies are not intercepted");
        return;
    }

    load_config();
    if (!fmv_player_state.enabled) {
        log_info("Enabled=0, every movie keeps using the retail Bink path");
        return;
    }

    fmv_player_state.overlay_ready = video_overlay_init();
    if (!fmv_player_state.overlay_ready) {
        log_error("the video overlay could not start (no 32-bit libVLC found, or the window class "
                  "could not be registered), every movie keeps using the retail Bink path");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    install_movie_hook();
}
