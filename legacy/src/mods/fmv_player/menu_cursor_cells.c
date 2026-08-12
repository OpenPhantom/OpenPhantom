/* menu_cursor_cells.c: the drawn menu cursor's position, found by pattern and written directly.
 *
 * ==============================================================================================
 * THE DEFECT THIS SERVES
 *
 * The message pump in vlc_playback.c is scoped to the overlay's own window, so the game window's
 * WM_MOUSEMOVE messages are no longer eaten while a movie plays - they queue up, unprocessed, for
 * as long as the movie runs. That is the correct behaviour and it made a second defect visible:
 * after the intro movies the DRAWN menu cursor, not the OS one, appeared in the wrong place.
 *
 * Two repairs were tried before the mechanism was understood, and both were field-tested wrong.
 * The first warped the real pointer to the engine's own (320,240) client anchor before resuming;
 * the drawn cursor then reliably appeared at that literal point rather than at the menu's centre.
 * The second warped to the game window's actual client-rect centre, computed at run time, on the
 * reasoning that (320,240) is only the true centre when the window is exactly 640x480. That
 * worked once and landed wrong on a later launch, with the game's resolution matching the desktop
 * both times, so a resolution mismatch was not the cause either.
 *
 * ==============================================================================================
 * WHY: THE POSITION IS AN ACCUMULATOR
 *
 * The window procedure's WM_MOUSEMOVE case, reached only after the engine's own recentring call
 * at 0x0046A115 has confirmed the message is real movement rather than the echo of its own warp:
 *
 *   00460BCC  0F BF 55 14           movsx edx, word ptr [ebp+0x14]     ; client x
 *   00460BD0  A1 98 6C 4B 00        mov   eax,[g_menuCursorX]          ; 0x004B6C98
 *   00460BD5  8D 8C 10 C0 FE FF FF  lea   ecx,[eax+edx-0x140]          ; += client_x - 320
 *   00460BDC  89 0D 98 6C 4B 00     mov   [g_menuCursorX],ecx
 *   00460BE2  8B 55 14 / C1 EA 10   the high word of the same lParam   ; client y
 *   00460BE8  81 E2 FF FF 00 00
 *   00460BEE  0F BF C2
 *   00460BF1  8B 0D 9C 6C 4B 00     mov   ecx,[g_menuCursorY]          ; 0x004B6C9C
 *   00460BF7  8D 94 01 10 FF FF FF  lea   edx,[ecx+eax-0xF0]           ; += client_y - 240
 *   00460BFE  89 15 9C 6C 4B 00     mov   [g_menuCursorY],edx
 *   00460C04  A1 58 FD 6C 00        mov   eax,[g_menuOriginX]          ; 0x006CFD58
 *   00460C09  89 45 F8              mov   [ebp-8],eax
 *   ...       clamp g_menuCursorX to [originX, originX+0x25F]
 *   00460C41  8B 15 5C FD 6C 00     mov   edx,[g_menuOriginY]          ; 0x006CFD5C
 *   ...       clamp g_menuCursorY to [originY, originY+0x1BF]
 *
 * So a synthetic move only ever ADDS a delta to whatever the cells already held, which is why
 * warping could not put the drawn cursor anywhere in particular. Writing the cells does, with no
 * message, no prior value and no resolution-dependent arithmetic involved at all. The two origin
 * cells are the same pair the resolution fix's cursor cage documents, which is a cross-check on
 * all four addresses from a second, independently written direction.
 *
 * ==============================================================================================
 * WHY THIS IS A PATTERN AND NOT FOUR CONSTANTS
 *
 * Because the constants are wrong on a build that ships with the game. Measured over every image
 * to hand: the block above sits at 0x00460BCC in all five retail executables, including the German
 * one, and at 0x00460B6C in the Edit Tool's own recompile of the same engine - where the cells are
 * at 0x004B6C48 / 0x004B6C4C and 0x006CFD08 / 0x006CFD0C, 0x50 bytes below the retail ones. Four
 * hardcoded addresses would therefore have written two dwords into whatever else lives at
 * 0x004B6C98 on that build, silently, because a wrong address in the same data section is still
 * perfectly readable and writable. That is the failure the whole signature discipline in this tree
 * exists to prevent, and it is not hypothetical here, it is measured.
 *
 * The pattern is 64 bytes with five absolute operands wildcarded, and it matches exactly once in
 * every image tested. Five things are then checked before any of it is believed: the X cell must
 * be the same in the load and the store, the same for Y, Y must be four bytes past X, the origin
 * pair must be reachable at the fixed distance below with the expected opcode, and originY must be
 * four bytes past originX. Every cell must also lie inside the host image.
 */
#include "menu_cursor_cells.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4, "menu_cursor_cells reads 32-bit engine operands");

/* --- 0x00460BCC  the window procedure's WM_MOUSEMOVE case ------------------------------------ */
static const uint8_t SIG_CURSOR_ACCUMULATOR[] = {
    0x0F, 0xBF, 0x55, 0x14,                         /* movsx edx,word[ebp+0x14]   */
    0xA1, 0x00, 0x00, 0x00, 0x00,                   /* mov eax,[cursorX]          */
    0x8D, 0x8C, 0x10, 0xC0, 0xFE, 0xFF, 0xFF,       /* lea ecx,[eax+edx-0x140]    */
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,             /* mov [cursorX],ecx          */
    0x8B, 0x55, 0x14,                               /* mov edx,[ebp+0x14]         */
    0xC1, 0xEA, 0x10,                               /* shr edx,0x10               */
    0x81, 0xE2, 0xFF, 0xFF, 0x00, 0x00,             /* and edx,0xFFFF             */
    0x0F, 0xBF, 0xC2,                               /* movsx eax,dx               */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,             /* mov ecx,[cursorY]          */
    0x8D, 0x94, 0x01, 0x10, 0xFF, 0xFF, 0xFF,       /* lea edx,[ecx+eax-0xF0]     */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,             /* mov [cursorY],edx          */
    0xA1, 0x00, 0x00, 0x00, 0x00,                   /* mov eax,[originX]          */
    0x89, 0x45, 0xF8                                /* mov [ebp-8],eax            */
};
static const uint8_t MSK_CURSOR_ACCUMULATOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CURSOR_ACCUMULATOR) == sizeof(MSK_CURSOR_ACCUMULATOR),
               "the accumulator pattern and its mask are different lengths");

#define OFFSET_CURSOR_X_LOAD   0x05u
#define OFFSET_CURSOR_X_STORE  0x12u
#define OFFSET_CURSOR_Y_LOAD   0x27u
#define OFFSET_CURSOR_Y_STORE  0x34u
#define OFFSET_ORIGIN_X_LOAD   0x39u

/* `mov edx,[originY]`, measured from the match base. The clamp between it and the match is
 * straight-line code of fixed length in every image measured, which is what makes the distance a
 * constant worth checking rather than a constant worth trusting. */
#define OFFSET_ORIGIN_Y_SITE   0x75u
#define ORIGIN_Y_OPERAND       0x02u
static const uint8_t ORIGIN_Y_HEAD[] = { 0x8B, 0x15 };

/* The 640x480 menu space's own half-extent, which is the same 0x140 / 0xF0 every delta above is
 * measured from, and the middle of the clamp range [origin, origin+0x25F] x [origin, origin+0x1BF]
 * (607x447: 640 and 480 less the 33-pixel cursor quad). Comfortably clear of either edge. */
#define MENU_HALF_WIDTH  320
#define MENU_HALF_HEIGHT 240

/* Wide enough that no display mode this engine will ever be handed exceeds it, narrow enough that
 * a garbage read cannot pass by accident. The origin is (W-640)/2, so this allows a 17000-pixel
 * mode and then some. */
#define MENU_ORIGIN_MAX 8192u

typedef struct menu_cursor_cells {
    bool      resolved;
    bool      warned;
    uintptr_t cursor_x;
    uintptr_t cursor_y;
    uintptr_t origin_x;
    uintptr_t origin_y;
} menu_cursor_cells_t;

static menu_cursor_cells_t cells;

static bool read_cell(uintptr_t site, size_t offset, const char *what, uintptr_t *out)
{
    uint32_t address = 0;

    *out = 0;
    if (!memory_read_u32(site + offset, &address) || address == 0) {
        log_warning("the %s operand at %08X could not be read", what, (unsigned)(site + offset));
        return false;
    }
    if (!memory_is_inside_image((uintptr_t)address, sizeof(uint32_t))) {
        log_warning("the %s operand reads back as %08X, outside the host image, refused", what,
                    (unsigned)address);
        return false;
    }
    *out = (uintptr_t)address;
    return true;
}

bool menu_cursor_cells_resolve(void)
{
    uintptr_t site;
    uintptr_t cursor_x_store = 0;
    uintptr_t cursor_y_store = 0;
    uint8_t   head[sizeof(ORIGIN_Y_HEAD)];

    if (cells.resolved) {
        return true;
    }

    site = signature_find_unique(SIG_CURSOR_ACCUMULATOR, MSK_CURSOR_ACCUMULATOR,
                                 sizeof SIG_CURSOR_ACCUMULATOR);
    if (site == 0) {
        log_warning("the menu cursor accumulator did not resolve, so the drawn menu cursor is not "
                    "recentred after a movie. It is left wherever the engine's own queued mouse "
                    "messages put it, which is where it was before this DLL existed.");
        return false;
    }

    if (!read_cell(site, OFFSET_CURSOR_X_LOAD,  "menu cursor X",       &cells.cursor_x) ||
        !read_cell(site, OFFSET_CURSOR_X_STORE, "menu cursor X store", &cursor_x_store) ||
        !read_cell(site, OFFSET_CURSOR_Y_LOAD,  "menu cursor Y",       &cells.cursor_y) ||
        !read_cell(site, OFFSET_CURSOR_Y_STORE, "menu cursor Y store", &cursor_y_store) ||
        !read_cell(site, OFFSET_ORIGIN_X_LOAD,  "menu origin X",       &cells.origin_x)) {
        return false;
    }
    if (cells.cursor_x != cursor_x_store || cells.cursor_y != cursor_y_store) {
        log_warning("the accumulator loads and stores different cells (X %08X/%08X, Y %08X/%08X), "
                    "this is not the window procedure this file describes, refused",
                    (unsigned)cells.cursor_x, (unsigned)cursor_x_store,
                    (unsigned)cells.cursor_y, (unsigned)cursor_y_store);
        return false;
    }
    if (cells.cursor_y != cells.cursor_x + sizeof(uint32_t)) {
        log_warning("the menu cursor cells are not adjacent (%08X and %08X), refused",
                    (unsigned)cells.cursor_x, (unsigned)cells.cursor_y);
        return false;
    }

    if (!memory_read(site + OFFSET_ORIGIN_Y_SITE, head, sizeof head) ||
        head[0] != ORIGIN_Y_HEAD[0] || head[1] != ORIGIN_Y_HEAD[1] ||
        !read_cell(site, OFFSET_ORIGIN_Y_SITE + ORIGIN_Y_OPERAND, "menu origin Y",
                   &cells.origin_y)) {
        log_warning("no `mov edx,[originY]` sits 0x%X bytes after %08X, refused",
                    (unsigned)OFFSET_ORIGIN_Y_SITE, (unsigned)site);
        return false;
    }
    if (cells.origin_y != cells.origin_x + sizeof(uint32_t)) {
        log_warning("the menu origin cells are not adjacent (%08X and %08X), refused",
                    (unsigned)cells.origin_x, (unsigned)cells.origin_y);
        return false;
    }

    cells.resolved = true;
    log_info("the drawn menu cursor lives at %08X/%08X, its island origin at %08X/%08X, all four "
             "read out of the window procedure's own operands at %08X. After each movie the cursor "
             "is written to the middle of that island directly, because the engine moves it by "
             "ACCUMULATION and warping the real pointer only adds a delta to whatever it already "
             "held.",
             (unsigned)cells.cursor_x, (unsigned)cells.cursor_y,
             (unsigned)cells.origin_x, (unsigned)cells.origin_y, (unsigned)site);
    return true;
}

void menu_cursor_cells_recentre(void)
{
    uint32_t origin_x = 0;
    uint32_t origin_y = 0;

    if (!cells.resolved) {
        return;
    }
    if (!memory_read_u32(cells.origin_x, &origin_x) ||
        !memory_read_u32(cells.origin_y, &origin_y)) {
        return;                        /* not readable at all - best effort, say nothing */
    }
    if (origin_x > MENU_ORIGIN_MAX || origin_y > MENU_ORIGIN_MAX) {
        if (!cells.warned) {
            cells.warned = true;
            log_warning("the menu origin read back as %u,%u, outside any plausible display mode, "
                        "so the drawn menu cursor is not recentred. Reported once.",
                        (unsigned)origin_x, (unsigned)origin_y);
        }
        return;
    }

    (void)patch_write_u32(cells.cursor_x, origin_x + MENU_HALF_WIDTH);
    (void)patch_write_u32(cells.cursor_y, origin_y + MENU_HALF_HEIGHT);
}
