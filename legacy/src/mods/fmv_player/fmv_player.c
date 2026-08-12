/* fmv_player.c: play the movies through a modern decoder instead of Bink, when a converted copy
 * exists, falling back to the original retail path unchanged when it does not.
 *
 * ==============================================================================================
 * WHY THIS EXISTS, AND WHAT IT REPLACES
 *
 * The DLL this one replaces tried three designs to make movies fill a chosen display resolution
 * without the retail engine's own per-frame surface access costing whole seconds of it at 4K: a
 * CPU resample into the engine's own locked surface, the same resample through
 * IDirectDrawSurface::GetDC and StretchDIBits instead of the engine's lock, and the host
 * DirectDraw-to-Direct3D translation layer's own shadow-surface setting. All three measured the
 * SAME frame rate on the reporting machine. That result is the reason this file exists: every one
 * of those designs still asked the RETAIL ENGINE to hand a decoded Bink frame to a DirectDraw
 * surface the translation layer manages, every decoded frame, and whatever makes that expensive on
 * that layer did not care which API was used to ask.
 *
 * This file does not ask at all. It replaces the movie player wholesale, for any movie a converted
 * file exists for, with a borderless overlay window (video_overlay.c) playing through a 32-bit
 * libVLC (vlc_playback.c, found by vlc_locate.c) - a code path that never touches the game's
 * DirectDraw surface, never locks anything the translation layer manages, and gets hardware video
 * decode that Bink 1 from 1999 never had. A movie with no converted file falls straight through to
 * the original Bink playback, unchanged, so this is safe to install with nothing converted: the
 * game plays exactly as it always did until you convert something.
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
 * exactly why the two-stage detour rule exists, and why this signature reaches two branches into
 * the function rather than stopping at the prologue. Measured against the real retail WMAIN.EXE
 * (829,952 bytes): exactly one match, all 72 bytes, at 0x0046C35A.
 *
 * The detour uses the chained-hook mechanism rather than a one-off call-site redirect: this is a
 * FUNCTION being replaced wholesale, not one call inside a larger function, and other DLLs in this
 * tree already share this chaining convention for functions more than one feature might reasonably
 * want to sit in front of.
 * ============================================================================================== */
#include "fmv_player.h"

#include "movie_path.h"
#include "video_overlay.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FMV_PLAYER_SECTION "fmv_player"

/* --- 0x0046C35A, the movie player. The pattern is the function's own prologue plus its first two
 * early-exit branches, because the prologue alone is not unique in this image. Byte for byte,
 * against the retail executable:
 *
 *   0046C35A  55 8B EC 81 EC 90 00 00 00     push ebp; mov ebp,esp; sub esp,0x90
 *   0046C363  A1 F0 77 4B 00                 mov eax,[004b77f0]
 *   0046C368  89 85 74 FF FF FF              mov [ebp-0x8c],eax
 *   0046C36E  C7 85 78 FF FF FF 01 00 00 00  mov [ebp-0x88],1
 *   0046C378  83 3D 60 63 6D 00 00           cmp [006d6360],0     <- the gate this file honours
 *   0046C37F  75 07                          jnz +7
 *   0046C381  33 C0                          xor eax,eax
 *   0046C383  E9 5C 01 00 00                 jmp 0046c4e4         <- returns 0
 *   0046C388  83 3D 3C A4 86 00 00           cmp [0086a43c],0
 *   0046C38F  74 07                          jz +7
 *   0046C391  33 C0                          xor eax,eax
 *   0046C393  E9 4C 01 00 00                 jmp 0046c4e4         <- returns 0
 *   0046C398  C7 05 3C A4 86 00 01 00 00 00  mov [0086a43c],1
 */
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

/* Where the two cells above keep their address operands, counted from the start of the pattern.
 * Six instructions precede the first cmp, in four groups: the 9-byte prologue (1 + 2 + 6), then 5
 * for the global load, 6 for the first store and 10 for the second, which is 30; then 2 more for
 * the `83 3D` opcode, so its four operand bytes are at 32..35. The second cmp begins at 46 and puts
 * its operand at 48; the `mov` that follows begins at 62, and `C7 05` puts its operand at 64.
 *
 * The second cell therefore appears TWICE, and both are read and required to agree. That costs
 * nothing and it is the cheapest possible check that the pattern really did land where it was
 * meant to: two independent encodings of the same address in one matched run.
 *
 * The cells are read out of the matched bytes rather than written down as constants, which is what
 * keeps them right under forced ASLR and after another patch has edited a nearby immediate. All
 * three offsets are past the nine bytes a detour overwrites, so they survive this file's own hook
 * and one another DLL placed there first. */
#define GFX_UP_OPERAND_OFFSET      32u
#define IN_MOVIE_CMP_OPERAND_OFFSET 48u
#define IN_MOVIE_MOV_OPERAND_OFFSET 64u

enum {
    SITE_MOVIE_PLAY,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("movie_play", SIG_MOVIE_PLAY, MOVIE_PLAY_PROLOGUE_SIZE)
};

typedef int (__cdecl *movie_play_fn_t)(const char *name, int param2, int param3);

typedef struct fmv_player_state {
    bool            installed;
    bool            enabled;
    bool            logged_no_libvlc;   /* said once: it cannot change within a session */
    char            movie_directory[MAX_PATH];
    char            extension[16];
    detour_t        detour;

    /* The engine's own two movie cells, both read out of the matched signature.
     *
     * `graphics_up` is set once the graphics device has been chosen. The retail function's first
     * act is to refuse and return 0 while it is clear.
     *
     * `in_movie` is what the engine means by "a cutscene is on screen". Retail sets it for the
     * whole of a movie, and other parts of the engine read it: the display hot keys are ignored
     * while it is set, and the game's own key hook steps aside entirely. Holding it for the length
     * of OUR playback is what makes the rest of the engine behave the way it does during a retail
     * movie rather than the way it does during gameplay. */
    const int32_t  *graphics_up;
    int32_t        *in_movie;
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

/* True when the configured folder exists at all. Nothing else in this DLL is worth doing when it
 * does not: libVLC would be loaded, its plugin bank built and its threads started during the
 * game's own startup, for a feature that could not act on a single movie. A folder created after
 * the game has started is therefore not noticed until the next launch, which is the same as every
 * other setting here. */
static bool movie_directory_exists(void)
{
    char  path[MAX_PATH];
    DWORD attributes;

    if (!movie_path_directory(host_directory(), fmv_player_state.movie_directory, path,
                              sizeof path)) {
        log_warning("\"%s\" does not give a usable path under \"%s\", so no movie can be looked "
                    "up", fmv_player_state.movie_directory, host_directory());
        return false;
    }

    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        log_info("no converted movies: \"%s\" is not a folder. Every movie uses the retail Bink "
                 "path, which is what an installation with nothing converted is supposed to do.",
                 path);
        return false;
    }
    return true;
}

/* Reads one address operand out of the matched pattern and keeps it only if it points somewhere
 * that can actually be read.
 *
 * Readability is the only test. An image-bounds test was considered and left out: it would assert
 * something about this executable's SizeOfImage that has not been measured, and being wrong about
 * that would switch off a working check rather than catch a broken one. The address is not a guess
 * to begin with, it is the operand of an instruction a 72-byte signature just matched. */
static bool read_cell_operand(uintptr_t site, unsigned offset, uint32_t *out_cell)
{
    if (!memory_read_u32(site + offset, out_cell)) {
        return false;
    }
    return memory_is_readable_range(*out_cell, sizeof(int32_t));
}

/* Resolves both engine cells. A cell that cannot be resolved is left NULL, which costs that one
 * check rather than the feature, and every outcome is logged with its address: "the hook is
 * behaving like the engine" and "the hook is not" are different behaviours and neither should have
 * to be guessed at from outside. */
static void resolve_engine_cells(uintptr_t site)
{
    uint32_t graphics_up = 0;
    uint32_t in_movie_cmp = 0;
    uint32_t in_movie_mov = 0;

    if (read_cell_operand(site, GFX_UP_OPERAND_OFFSET, &graphics_up)) {
        fmv_player_state.graphics_up = (const int32_t *)(uintptr_t)graphics_up;
        log_info("the engine's graphics-ready cell is at %08X and is honoured before every movie",
                 (unsigned)graphics_up);
    } else {
        log_warning("the graphics-ready operand at %08X did not read back as a usable address, so "
                    "the hook cannot honour the engine's own refusal to play a movie",
                    (unsigned)(site + GFX_UP_OPERAND_OFFSET));
    }

    /* Both encodings must agree. They are the same cell written two different ways twenty bytes
     * apart, so a disagreement means the pattern is not sitting where it was believed to be, and
     * the right answer to that is to touch nothing. */
    if (!read_cell_operand(site, IN_MOVIE_CMP_OPERAND_OFFSET, &in_movie_cmp) ||
        !read_cell_operand(site, IN_MOVIE_MOV_OPERAND_OFFSET, &in_movie_mov)) {
        log_warning("the in-movie operands did not read back as usable addresses, so the engine is "
                    "not told that a cutscene is on screen");
        return;
    }
    if (in_movie_cmp != in_movie_mov) {
        log_warning("the two in-movie operands disagree (%08X and %08X), so the pattern is not "
                    "where it was believed to be and neither is used",
                    (unsigned)in_movie_cmp, (unsigned)in_movie_mov);
        return;
    }

    fmv_player_state.in_movie = (int32_t *)(uintptr_t)in_movie_cmp;
    log_info("the engine's in-movie cell is at %08X and is held for the length of every movie "
             "this DLL plays", (unsigned)in_movie_cmp);
}

/* ============================================================================================ */
static int __cdecl hook_play_movie(const char *name, int param2, int param3)
{
    movie_play_fn_t original = (movie_play_fn_t)fmv_player_state.detour.original;
    char            ansi_path[MAX_PATH];
    wchar_t         modern_path[MAX_PATH];
    bool            played;

    /* The retail function's own first two acts are to refuse and return 0: once when the graphics
     * device is not up, and once when a movie is already on screen. Both are reproduced here by
     * handing the call to the original rather than by answering for it, so the refusal is the
     * engine's own rather than an imitation of it.
     *
     * The second one is not theoretical. This function has four call sites and nothing stops one
     * of them running while another movie is up; retail answers 0 and plays nothing, and a hook
     * that skipped the check would start a second overlay on top of the first. */
    if (fmv_player_state.graphics_up != NULL && *fmv_player_state.graphics_up == 0) {
        return original(name, param2, param3);
    }
    if (fmv_player_state.in_movie != NULL && *fmv_player_state.in_movie != 0) {
        return original(name, param2, param3);
    }

    /* Every branch below reaches a log line, and that is a property worth keeping deliberately.
     * A movie that quietly used Bink is indistinguishable, from outside, from a feature that was
     * never armed, and a log that says "intercepted" at startup and then nothing at all is the
     * failure this whole DLL is most likely to present as. So the name is resolved into a path in
     * two steps rather than one long condition, and the step that fails says which one it was. */
    if (name == NULL) {
        log_warning("the engine asked for a movie with no name, using the retail Bink path");
        return original(name, param2, param3);
    }

    if (!movie_path_build(host_directory(), fmv_player_state.movie_directory, name,
                          fmv_player_state.extension, ansi_path, sizeof ansi_path)) {
        log_warning("no usable path for \"%s\" under \"%s%s\" with extension \"%s\" - too long, or "
                    "the configuration is not usable - so the retail Bink path is used", name,
                    host_directory(), fmv_player_state.movie_directory,
                    fmv_player_state.extension);
        return original(name, param2, param3);
    }

    if (MultiByteToWideChar(CP_ACP, 0, ansi_path, -1, modern_path, ARRAYSIZE(modern_path)) == 0) {
        log_warning("%s could not be converted to wide characters (error %u), using the retail "
                    "Bink path", ansi_path, (unsigned)GetLastError());
        return original(name, param2, param3);
    }

    if (GetFileAttributesW(modern_path) == INVALID_FILE_ATTRIBUTES) {
        log_info("no converted file for \"%s\" at %s, using the retail Bink movie", name,
                 ansi_path);
        return original(name, param2, param3);
    }

    /* There is a file to play, so now it matters whether libVLC can play it. This is a live,
     * non-blocking poll rather than a wait, and it is asked HERE rather than earlier so that a
     * machine with nothing converted never mentions libVLC at all.
     *
     * The two answers are different situations and are told apart, because saying "not finished
     * loading yet" about a load that finished and failed would be a false cause repeated once per
     * movie for the whole session. Still loading is temporary and the next movie asks again; not
     * available does not change within a session, so it is said once. */
    if (!video_overlay_is_ready()) {
        if (video_overlay_is_still_loading()) {
            log_info("\"%s\" has a converted file, but libVLC is still loading in the background, "
                     "so this one plays through the retail Bink path rather than making the game "
                     "wait for it. The next movie asks again.", name);
        } else if (!fmv_player_state.logged_no_libvlc) {
            fmv_player_state.logged_no_libvlc = true;
            log_warning("\"%s\" has a converted file, but no usable 32-bit libVLC was found, so "
                        "every movie plays through the retail Bink path this session. See the "
                        "libVLC line earlier in this log for where it looked. Reported once.",
                        name);
        }
        return original(name, param2, param3);
    }

    log_info("playing \"%s\" from %s", name, ansi_path);

    /* Held for exactly as long as the picture is on screen, which is what the retail function does
     * with the same cell. It is not bookkeeping: the engine's display hot keys check it before
     * changing resolution or gamma, and the game's own key hook steps aside entirely while it is
     * set. Without it a cutscene played by this DLL is, to the rest of the engine, ordinary
     * gameplay with a window over it - which is what "it does not feel like the game" is made of.
     *
     * Set immediately before and cleared immediately after, with nothing in between that can
     * return early, so it cannot be left standing. A stuck value would make the engine refuse
     * every later movie, including its own. */
    if (fmv_player_state.in_movie != NULL) {
        *fmv_player_state.in_movie = 1;
    }
    played = video_overlay_play_blocking(modern_path);
    if (fmv_player_state.in_movie != NULL) {
        *fmv_player_state.in_movie = 0;
    }

    if (played) {
        /* Non-zero because all three refusal paths in the retail function return 0, which is byte
         * evidence that zero means "did not play". The exact value it returns when it DID play has
         * not been read out of the image, and no caller has been shown to distinguish one non-zero
         * value from another. */
        return 1;
    }

    log_warning("playback of \"%s\" did not start, falling back to the retail Bink movie", name);
    return original(name, param2, param3);
}

static bool install_movie_hook(void)
{
    uintptr_t site = sites[SITE_MOVIE_PLAY].address;

    if (!detour_install(&fmv_player_state.detour, site, (const void *)hook_play_movie,
                        MOVIE_PLAY_PROLOGUE_SIZE)) {
        log_error("the detour at %08X failed, every movie keeps using the retail Bink path",
                  (unsigned)site);
        return false;
    }

    log_info("movie playback intercepted at %08X. Any movie with a matching file in \"%s%s\" "
             "plays through libVLC instead of Bink; everything else falls through to the original "
             "path unchanged.", (unsigned)site, host_directory(),
             fmv_player_state.movie_directory);
    return true;
}

/* ============================================================================================ */
void fmv_player_install(void)
{
    uintptr_t site;

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

    /* Resolving the site and checking the folder both come BEFORE libVLC is brought up, and the
     * order is the point: each of them can rule the whole feature out, and neither costs anything.
     * Loading libVLC first meant its plugin bank was built and its threads started during the
     * game's own startup even when the very next step was going to switch the feature off. */
    signature_resolve_table(sites, SITE_COUNT);
    site = sites[SITE_MOVIE_PLAY].address;
    if (site == 0) {
        log_warning("the movie player did not resolve, every movie keeps using the retail Bink "
                    "path");
        return;
    }
    if (!movie_directory_exists()) {
        return;
    }

    resolve_engine_cells(site);

    /* Started here, as early as this DLL's own install runs, so the background load has the most
     * possible time to finish before the first movie needs it. It is not waited on: the hook polls
     * video_overlay_is_ready() per movie and falls through to the retail path while the answer is
     * no. What IS answered here is the window class, because that part is immediate, and without
     * it there is nothing to play into at all. */
    if (!video_overlay_start_async_init()) {
        log_error("the video overlay's window class could not be registered, every movie keeps "
                  "using the retail Bink path");
        return;
    }

    install_movie_hook();
}
