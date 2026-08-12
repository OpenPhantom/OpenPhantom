/* large_textures.c: raise the ceiling on how big a texture page may be.
 *
 * ============================== What this is, and what it is not ==============================
 *
 * A capability, not a fix. It repairs no defect, and on the artwork the game ships, both patches
 * are the identity. The reason to have it is replacement artwork, which the engine otherwise
 * crops without a word.
 *
 * That the default is inert is a measurement rather than an assumption. 4000 of the 6482 exported
 * textures were read out of their own headers. The most common sizes are 32 by 32 (1063), 16 by 32
 * (471), 64 by 32 (329), 64 by 64 (320) and 16 by 16 (317), and none of the 4000 exceeds 256 on
 * either axis. So on the original artwork the clamp never fires and any ceiling at or above 256 is
 * the identity. The remaining 2482 were not measured, which is why this says 4000 and not all.
 *
 * The engine enforces a size limit twice, in different modules, for different reasons. A player
 * who raises one and not the other gets either cropped artwork or a corrupted heap, and neither
 * failure names its own cause. That is why both live in one DLL.
 *
 *   1. The device ceiling governs every texture in the game. The one function that creates a
 *      texture clamps both axes to 1..256 and then CROPS rather than scales: a wider page is
 *      created at 256 and the conversion loop copies only the leftmost 256 columns of each row.
 *      MaxTextureSize moves that clamp.
 *
 *   2. The world page scratch governs only the level geometry textures. The level loader reads
 *      every world texture page into one fixed 64 KB block and then reads width times height
 *      bytes into it without checking either number, so a page over 65536 pixels writes past the
 *      end of a heap block during level load. MaxWorldPageSize moves the size of that block.
 *
 * Why the second one matters even though the first exists: raising MaxTextureSize alone does
 * nothing at all for the world. 98 of the game's 116 world pages are already 256 by 256, which is
 * exactly the 65536 pixels the fixed block holds. That was measured after a session in which the
 * ceiling had been raised as far as it goes and the levels looked unchanged, which read like a
 * patch that had not installed when in fact the second limit was doing its job silently.
 *
 * The two sites are independent: different functions, different modules, no shared state and no
 * ordering constraint between them. Either can fail to resolve without affecting the other, and
 * the log says which of the two applied. That independence is the obvious seam in this file, and
 * it was looked at and not taken: each half is one signature and one operand write, and what makes
 * the file long is the byte evidence rather than the code. The seam that was taken runs the other
 * way: deciding whether a requested size may be used at all is arithmetic with no engine in it, so
 * it is texture_size.c and a test observes it without the game.
 *
 * ============================== What this cannot do ===========================================
 *
 * The engine never asks the device for its own maximum texture size. std3D_open decodes the
 * device description into the record at +0x138 and names +0x1DC dwMaxBufferSize and
 * +0x1E0 dwMaxVertexCount, but nothing on the texture path consults a maximum dimension: the
 * clamp is the only limit that exists anywhere on that path. So a ceiling the device cannot honour
 * is not caught here. It reaches CreateSurface, fails there, and takes the function's existing
 * failure path, which releases what it made and leaves a well formed empty record with
 * bResident = 0. That is why running out of texture memory in this engine shows as untextured
 * geometry rather than a crash, and a too large ceiling ends up in the same place.
 *
 * Both values have to stay powers of two, and that is enforced by this feature rather than by the
 * device for the same reason: D3DPTEXTURECAPS_POW2 is a capability the engine also never checks.
 * The clamped size is handed straight to surface creation, and the page builder at 0x0040EC33
 * derives its addressing masks by subtracting one from each axis, which is the mask form of a
 * power of two and nothing else. That check lives in texture_size.c with the accepted ranges and
 * the conversion from pixels to bytes.
 */
#include "large_textures.h"

#include "texture_size.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LARGE_TEXTURES_SECTION "large_textures"

/* --- 0x00488757  the clamp that governs every texture ---------------------------------------- *
 *
 * std3D_AddToTextureCache at 0x00488730 is the only texture creation path in the engine, and
 * its clamp is one register rather than two comparisons:
 *
 *   00488746  8B 46 0C        mov  eax, [esi+0x0C]      the source width
 *   ...
 *   00488757  BF 00 01 00 00  mov  edi, 0x100           the ceiling, loaded once
 *   0048875C  72 0E           jb   0048876C             width < 1
 *   0048875E  3B C7           cmp  eax, edi
 *   00488760  77 05           ja   00488767             width > ceiling
 *   00488762  89 45 E0        mov  [ebp-0x20], eax      width as given
 *   00488765  EB 08           jmp  0048876F
 *   00488767  89 7D E0        mov  [ebp-0x20], edi      width = ceiling
 *   0048876A  EB 03           jmp  0048876F
 *   0048876C  89 4D E0        mov  [ebp-0x20], ecx      width = 1
 *   0048876F  8B 46 10        mov  eax, [esi+0x10]      the source height
 *   00488772  3B C1           cmp  eax, ecx
 *   00488774  72 0E           jb   00488784
 *   00488776  3B C7           cmp  eax, edi             the same register, not a second literal
 *   00488778  77 05           ja   0048877F
 *   0048877A  89 45 E8        mov  [ebp-0x18], eax
 *   0048877D  EB 08           jmp  00488787
 *   0048877F  89 7D E8        mov  [ebp-0x18], edi      height = ceiling
 *   00488782  EB 03           jmp  00488787
 *   00488784  89 4D E8        mov  [ebp-0x18], ecx      height = 1
 *   00488787  8B 45 E8        mov  eax, [ebp-0x18]
 *   0048878A  B9 1F 00 00 00  mov  ecx, 0x1F
 *   0048878F  0F AF 45 E0     imul eax, [ebp-0x20]
 *   00488793  D1 E0           shl  eax, 1               costBytes = width * height * 2
 *
 * So one immediate at 0x00488758 governs both axes, and changing it is safe because nothing else
 * in the function depends on its value. A register scan over the whole function finds edi loaded
 * with the ceiling exactly once and read only by the four clamp instructions above; at
 * 0x0048879A it is reloaded with lea edi, [ebp-0xA4] for the descriptor clear and never
 * carries the ceiling again.
 *
 * It crops, it does not scale. The clamped width and height are used both for the staging surface
 * and as the loop bounds of the format conversion, while the source is walked with the source's
 * own pitch, so a 512 wide page contributes its leftmost 256 columns and the rest is discarded.
 * There is no resampling anywhere on this path.
 *
 * The site is unique in every shipped executable, and the operand is read out of the image rather
 * than assumed:
 *
 *   retail WMAIN.EXE                      0x00488757   imm 0x100
 *   wmain.exe                             0x00488757   imm 0x100
 *   obi.exe, the Edit Tool recompile      0x004886F7   imm 0x100
 *
 * Three rows and not five: the German retail executable is byte identical to the English one, so
 * it is the same build rather than a separate image.
 *
 * The immediate is a WILDCARD in the pattern on purpose, because this patch rewrites exactly those
 * four bytes. A pattern that spelled 00 01 00 00 out would match before the patch and stop
 * matching afterwards, and the symptom of that is a second generation of the DLL in the same
 * process resolving nothing, logging "did not resolve", and switching itself off with nothing else
 * to show for it. patch_repoint_operand supplies the safety the pattern gives up: it refuses to
 * write unless the operand still holds the expected 0x100. */
static const uint8_t SIG_TEXTURE_CEILING[] = {
    0xBF, 0x00, 0x00, 0x00, 0x00,   /* mov edi, ceiling   (the operand is wildcarded) */
    0x72, 0x0E,                     /* jb  width-is-zero */
    0x3B, 0xC7,                     /* cmp eax, edi */
    0x77, 0x05,                     /* ja  width-too-big */
    0x89, 0x45, 0xE0,               /* mov [ebp-0x20], eax */
    0xEB, 0x08,
    0x89, 0x7D, 0xE0,               /* mov [ebp-0x20], edi */
    0xEB, 0x03,
    0x89, 0x4D, 0xE0,               /* mov [ebp-0x20], ecx */
    0x8B, 0x46, 0x10                /* mov eax, [esi+0x10]   the height, clamped against edi too */
};
static const uint8_t MSK_TEXTURE_CEILING[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
#define OFFSET_CEILING_OPERAND  1u

/* The engine's authored ceiling, and the value the operand must hold before it is written. */
#define ORIGINAL_CEILING   0x100u

/* --- 0x0041DAB6  the fixed block every world page is read into ------------------------------- *
 *
 * bapworld_loadTex at 0x0041DAB0 is the level loader's texture pass, and the whole limit is
 * the argument of one allocation at the top of it:
 *
 *   0041DAB0  55                 push ebp
 *   0041DAB1  8B EC              mov  ebp, esp
 *   0041DAB3  83 EC 1C           sub  esp, 0x1C
 *   0041DAB6  68 00 00 01 00     push 0x10000            the size, and it appears exactly once
 *   0041DABB  E8 D0 77 07 00     call 00495290           malloc
 *   0041DAC0  83 C4 04           add  esp, 4
 *   0041DAC3  89 45 FC           mov  [ebp-4], eax
 *   0041DAC6  83 7D FC 00        cmp  [ebp-4], 0
 *   0041DACA  75 07              jne  0041DAD3
 *   0041DACC  33 C0              xor  eax, eax
 *   0041DACE  E9 FA 00 00 00     jmp  0041DBCD           the level load failure path
 *
 * and this is the read that overruns it, further down the same loop:
 *
 *   0041DB19  8B 45 EC           mov  eax, [ebp-0x14]    width, out of the 16 byte sub header
 *   0041DB1C  0F AF 45 F0        imul eax, [ebp-0x10]    times height
 *   0041DB20  50                 push eax                no bound test anywhere
 *   0041DB21  8B 4D FC           mov  ecx, [ebp-4]
 *   0041DB24  51                 push ecx                the 64 KB block
 *
 * The block is used for exactly two things and then freed on every exit path: it is the
 * destination of that read, and it is the source pointer handed to the page builder. Nothing else
 * in the function depends on its size, which is what makes the one immediate the whole limit.
 *
 * The sub esp, 0x1C at the head of the pattern is what makes it unique; without it this is a
 * common idiom. Census over the retail image's .text, allocate-and-bail forms with the size
 * immediate wildcarded:
 *
 *   push imm32                                                    5 sites hold 0x10000
 *   push imm32 + call + add esp,4 + mov [ebp-4],eax              21 sites
 *   the same + cmp [ebp-4],0 / jne 7 / xor eax,eax / jmp rel32    3 sites
 *   the same with 83 EC 1C in front                               1 site, the one below
 *
 * The three sites of the third row are 0x00401C5D (push 0xB8), 0x0041DAB6 (push 0x10000) and
 * 0x0048C377 (push 0x90), so even that shorter form is separable by its immediate. The pattern
 * used here does not have to be: it is unique on its own.
 *
 * One hazard worth writing down, because it is invisible until it bites. The pattern begins three
 * bytes into the function, so it overlaps the five bytes a trampoline detour would write over the
 * prologue. Nothing in this project detours bapworld_loadTex today. The moment something does,
 * this pattern has to move past the prologue and disambiguate by its immediate instead, or it will
 * quietly resolve to nothing.
 *
 * The size immediate is wildcarded for the same reason as the ceiling above, and
 * patch_repoint_operand provides the same guarantee: the write is refused unless the operand
 * still holds the expected 0x10000. */
static const uint8_t SIG_WORLD_SCRATCH[] = {
    0x83, 0xEC, 0x1C,                     /* sub  esp, 0x1C     what makes this unique */
    0x68, 0x00, 0x00, 0x00, 0x00,         /* push scratchBytes  (wildcarded) */
    0xE8, 0x00, 0x00, 0x00, 0x00,         /* call malloc        (relative, wildcarded) */
    0x83, 0xC4, 0x04,                     /* add  esp, 4 */
    0x89, 0x45, 0xFC,                     /* mov  [ebp-4], eax */
    0x83, 0x7D, 0xFC, 0x00,               /* cmp  [ebp-4], 0 */
    0x75, 0x07,                           /* jne  carry-on */
    0x33, 0xC0,                           /* xor  eax, eax */
    0xE9, 0x00, 0x00, 0x00, 0x00          /* jmp  the failure path (relative, wildcarded) */
};
static const uint8_t MSK_WORLD_SCRATCH[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_SCRATCH_OPERAND  4u

/* The measurement that made this second patch necessary. Census over the 11 shipped levels: 116
 * world pages, of which 98 are 256 by 256, 14 are 64 by 64 and 4 are 128 by 128. Since 256 times
 * 256 is 65536, which is exactly the fixed block, the stock loader can grow only the 18 small
 * pages, and only as far as 256:
 *
 *   upscale factor                   x2          x4          x8         x16
 *   pages in the level files      18/116      14/116       0/116       0/116
 *   pages in the other files    2725/2725   2725/2725   2725/2725   2725/2725
 *
 * Only the first row goes through this loader, and that is why a session run with the ceiling
 * raised as far as it goes showed no change in the levels at all.
 *
 * What growing this block does NOT change: the world's page array is still 64 entries, it is
 * inline at world+0xC4 and ends before numLights at world+0x1C4; the palette count is still
 * checked hard by bapworld_loadPals; and a page still has to be a power of two, because
 * bapmat_buildPage at 0x0040EC33 stores widthMinus1 and heightMinus1 and uses them as
 * addressing masks. */

/* The engine's own world page scratch, in bytes, and the value the operand must still hold. A
 * world page is one palette index per pixel, so this is also the pixel count: exactly 256 by 256,
 * which is exactly the size 98 of the 116 shipped world pages already are. */
#define ORIGINAL_SCRATCH_BYTES  0x10000u

/* The same block expressed as an axis, which is the unit the ini key uses. */
#define ORIGINAL_WORLD_PAGE     0x100u

/* Read one size from the ini and say out loud what became of it. Returns 0 when the value is
 * unusable or is already the engine's own, which is the caller's signal to leave that site alone.
 * Every refusal is logged, because a size that was silently ignored looks exactly like a size that
 * was applied and did nothing. The decision itself is texture_size_check; what is left here is the
 * ini and the log. */
static uint32_t read_size(const char *key, int32_t fallback, uint32_t original,
                          uint32_t low, uint32_t high)
{
    int32_t configured = ini_read_int(LARGE_TEXTURES_SECTION, key, fallback);

    switch (texture_size_check(configured, original, low, high)) {
    case TEXTURE_SIZE_APPLY:
        return (uint32_t)configured;

    case TEXTURE_SIZE_UNCHANGED:
        log_info("%s is the engine's own %u, nothing to do", key, (unsigned)original);
        return 0u;

    case TEXTURE_SIZE_NOT_POWER_OF_TWO:
        log_warning("%s %d is not a power of two. The size reaches surface creation unchanged and "
                    "the page builder derives its addressing masks by subtracting one from each "
                    "axis, so this is declined rather than applied.", key, (int)configured);
        return 0u;

    case TEXTURE_SIZE_BELOW_MINIMUM:
    case TEXTURE_SIZE_ABOVE_MAXIMUM:
    default:
        log_warning("%s %d is outside %u..%u, leaving the engine's own value alone",
                    key, (int)configured, (unsigned)low, (unsigned)high);
        return 0u;
    }
}

/* The first site: the clamp that governs every texture in the game. */
static bool apply_device_ceiling(uint32_t ceiling)
{
    uintptr_t site = signature_find_unique(SIG_TEXTURE_CEILING, MSK_TEXTURE_CEILING,
                                           sizeof SIG_TEXTURE_CEILING);

    if (site == 0) {
        log_warning("the texture size clamp did not resolve, the 256 ceiling stays");
        return false;
    }
    if (patch_repoint_operand(site + OFFSET_CEILING_OPERAND, ORIGINAL_CEILING, ceiling) !=
        PATCH_RESULT_OK) {
        log_warning("the texture ceiling at %08X was not rewritten", (unsigned)site);
        return false;
    }
    log_info("texture page ceiling %u -> %u at %08X, both axes from one register. This is the "
             "identity on the original artwork; it exists so replacement textures are not cropped "
             "at 256. The engine does not ask the device for its own maximum, so a size it cannot "
             "create degrades to untextured through the engine's existing failure path.",
             (unsigned)ORIGINAL_CEILING, (unsigned)ceiling, (unsigned)site);
    return true;
}

/* The second site: the fixed block the level loader reads every world page into.
 *
 * The argument here is an AXIS and the immediate is a BYTE COUNT. They are related by squaring,
 * because a world page is one palette index per pixel and the loader reads width times height
 * bytes. Doing that arithmetic here rather than in the ini is deliberate: a player should say how
 * big a texture may be, in the same unit as the other key, and not have to know that the engine
 * happens to size this buffer in bytes. */
static bool apply_world_page_scratch(uint32_t axis)
{
    uintptr_t site;
    uint32_t  bytes = texture_size_world_page_bytes(axis);

    site = signature_find_unique(SIG_WORLD_SCRATCH, MSK_WORLD_SCRATCH, sizeof SIG_WORLD_SCRATCH);
    if (site == 0) {
        log_warning("the world page scratch allocation did not resolve. World pages stay capped "
                    "at 65536 pixels; anything larger would be written past the end of that "
                    "block.");
        return false;
    }
    if (patch_repoint_operand(site + OFFSET_SCRATCH_OPERAND, ORIGINAL_SCRATCH_BYTES, bytes) !=
        PATCH_RESULT_OK) {
        log_warning("the world page scratch size at %08X was not rewritten", (unsigned)site);
        return false;
    }
    log_info("world page scratch %u -> %u bytes at %08X, so a level texture page may now be up to "
             "%u by %u. The loader reads width times height bytes into this one block and checks "
             "neither number, which is why the block has to grow first and why nothing larger may "
             "be written into a level file before it has. The allocation is transient: it is made "
             "once per level load and freed on every exit path.",
             (unsigned)ORIGINAL_SCRATCH_BYTES, (unsigned)bytes, (unsigned)site,
             (unsigned)axis, (unsigned)axis);
    return true;
}

void large_textures_install(void)
{
    uint32_t ceiling;
    uint32_t world_axis;
    bool     any = false;

    /* This comes before anything that logs. Without it every line this DLL writes is dropped,
     * including the warnings that would name the problem. */
    log_init("large_textures", false);

    /* And this one too, for the same reason: common/ is a static library, so every DLL carries its
     * own copy of the host image state. Another DLL having resolved it earlier does nothing for
     * us. Without it the scanner searches an empty range, both sites report zero matches, and the
     * log reads exactly like an unsupported executable. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, both texture limits stay as the engine has them");
        return;
    }

    if (!ini_read_bool(LARGE_TEXTURES_SECTION, "Enabled", true)) {
        log_info("disabled");
        return;
    }

    ceiling = read_size("MaxTextureSize", 1024, ORIGINAL_CEILING,
                        TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING);
    world_axis = read_size("MaxWorldPageSize", (int32_t)ORIGINAL_WORLD_PAGE, ORIGINAL_WORLD_PAGE,
                           TEXTURE_SIZE_MIN_WORLD_PAGE, TEXTURE_SIZE_MAX_WORLD_PAGE);
    if (ceiling == 0u && world_axis == 0u) {
        return;
    }

    if (ceiling != 0u) {
        any = apply_device_ceiling(ceiling) || any;
    }
    if (world_axis != 0u) {
        /* The device ceiling still applies to a world page after it has been loaded, so a world
         * page larger than that ceiling would be cropped on upload even though it now loads. Say
         * so here instead of letting the player discover it as a wrong looking texture. */
        uint32_t effective = texture_size_effective_ceiling(ceiling, ORIGINAL_CEILING);

        if (world_axis > effective) {
            log_warning("MaxWorldPageSize %u is larger than the device ceiling %u. A world page "
                        "would load but then be cropped on upload. Raise MaxTextureSize to at "
                        "least %u.", (unsigned)world_axis, (unsigned)effective,
                        (unsigned)world_axis);
        }
        any = apply_world_page_scratch(world_axis) || any;
    }
    if (!any) {
        log_warning("nothing was applied");
    }
}
