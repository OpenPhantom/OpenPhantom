/* pointer_cage.c: the drawn menu cursor is caged in a 640x480 island, wherever the screen ends.
 *
 * ==============================================================================================
 * What is already right, and must not be touched
 *
 * The engine centres its menus by itself. It computes
 *
 *     g_menuOriginX = (W - 640) / 2        written at startup and again on every mode change
 *     g_menuOriginY = (H - 480) / 2
 *
 * and ADDS both in the widget draw path and in the widget hit test, 18 references and 16
 * references respectively, which is what guarantees that what is drawn and what is clickable cannot
 * drift apart. A 640x480 menu therefore already appears in the middle of a 1920x1080 screen and
 * already responds where it appears. None of that is changed here.
 *
 * ==============================================================================================
 * What is wrong: the clamp on the cursor the menus draw for themselves
 *
 * Inside the window procedure the engine clamps its own cursor position to
 *
 *     [originX, originX + 0x25F] x [originY, originY + 0x1BF]
 *
 * i.e. 607x447, which is 640-33 and 480-33, the 33 is the 32-pixel cursor quad plus one. The
 * cursor coordinates themselves are ABSOLUTE screen coordinates while the hit test adds the origin
 * to the WIDGET, so widening this clamp needs no coordinate work at all: a cursor outside the
 * 640x480 island simply hits nothing, which is already true today for everything outside it.
 *
 * ==============================================================================================
 * WHY THE CAGE IS THE CANVAS AND NOT THE SCREEN. The engine's clamp is load-bearing.
 *
 * The paragraph above is true and it is incomplete, and the missing half is the ERASE.
 * The pause screens do not repaint every pixel every frame; they repair themselves through the
 * menu toolkit's damage rectangles, which live in canvas coordinates and are clipped against the
 * same hard-coded 640x480 every blit in that toolkit clips against. A cursor quad that sits
 * partially or wholly outside the island therefore cannot be expressed as a damage rectangle at
 * all: it is drawn (the draw is an absolute-coordinate textured quad and clips against the
 * SCREEN), it is never erased, and every crossing of the island's edge would leave a permanent
 * stamp of the cursor's blue glow on the border for as long as the screen is open. Reported from
 * a 3840x2160 session as blue blobs bleeding out from behind the pause menu, following the mouse.
 * The front end would never show it, because its 3-D room repaints every pixel every frame; the
 * pause subpages are exactly the screens that do not.
 *
 * So the shipped clamp is not a small-mindedness to correct. It is the guarantee that the cursor
 * never goes where the erase cannot follow, and this file keeps that guarantee rather than
 * breaking it: the cage is widened to the CANVAS, which is exactly the region that repaints, and
 * never to the screen. An earlier version of this file did widen it to the display mode, which
 * is the fault above, and that was invisible only for as long as the feature never armed.
 * menu_island_clip.c repairs the same class of defect for the sprites the WIDGETS draw; that one
 * IS reproduced, and the two are independent: the widget smears survive WidenMenuCursorArea=0.
 *
 * The block, and every offset this file reads or writes, measured from the match base S:
 *
 *   S+0x00  A1 <originX>          mov eax,[g_menuOriginX]        <- operand at S+0x01
 *   S+0x05  89 45 F8              [ebp-8] = it
 *   S+0x08  8B 0D <cursorX>       mov ecx,[g_cursorX]
 *   S+0x0E  3B 4D F8 / 7D 0B      if (cursorX < origin)
 *   S+0x13  8B 55 F8
 *   S+0x16  89 15 <cursorX>           cursorX = origin
 *   S+0x1C  EB 1F
 *   S+0x1E  8B 45 F8
 *   S+0x21  05 5F 02 00 00        add eax,0x25F                  <- OPCODE S+0x21, IMM S+0x22
 *   S+0x26  39 05 <cursorX> / 7E 0F
 *   S+0x2E  8B 4D F8
 *   S+0x31  81 C1 5F 02 00 00     add ecx,0x25F                  <- OPCODE S+0x31, IMM S+0x33
 *   S+0x37  89 0D <cursorX>           cursorX = origin + 0x25F
 *   S+0x3D  8B 15 <originY>       mov edx,[g_menuOriginY]        <- operand at S+0x3F
 *   ... the identical shape again for Y, with 0x1BF ...
 *   S+0x5E  81 C2 BF 01 00 00     add edx,0x1BF                  <- OPCODE S+0x5E, IMM S+0x60
 *   S+0x6F  05 BF 01 00 00        add eax,0x1BF                  <- OPCODE S+0x6F, IMM S+0x70
 *   S+0x74  A3 <cursorY>
 *
 * So the whole repair is the four immediates:
 *
 *     the four immediates 0x25F/0x1BF  ->  canvas width - 33 and canvas height - 33
 *
 * The two origin operands are read back before anything is written, as proof that the block is
 * the one the listing describes, and are never written to. An earlier version repointed them at
 * zero cells to make the clamp screen relative, which is the fault described above; removing
 * that removed the only partial state this feature could be left in, and the ordering hazard
 * with it. There is one write step now, and it either happens or it does not.
 *
 * ==============================================================================================
 * The signature contains the immediates it overwrites
 *
 * 0x25F and 0x1BF are part of the pattern; they are what identifies this as the 640x480 cage
 * rather than some other pair of clamps, so after a successful install the pattern no longer
 * matches anything. The addresses are therefore cached at install time and the site is never
 * re-resolved. The clamps are computed ABSOLUTELY from the canvas size and never as a delta on
 * what is already there, so writing them twice writes the same numbers.
 *
 * A consequence worth naming: if an earlier generation of this DLL is already in the process and
 * has already patched the block, this resolve finds nothing and the feature declines with a log
 * line. That is the correct answer, the cage is already wide, and it is reported rather than
 * silently treated as a failure to find the engine.
 *
 * ==============================================================================================
 * There is nothing to refresh
 *
 * The canvas does not change while the game runs; the display mode does. Sizing the cage from
 * the canvas is therefore a single write at install time, with no per-frame poll, no mode change
 * detour and nothing to resolve out of the graphics layer. An earlier version needed all three,
 * and needed them because it was sizing the cage from the wrong thing.
 */
#include "pointer_cage.h"

/* For the canvas the cage is sized from. */
#include "menu_scale.h"

#include "window_fit.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The cursor quad is 32 pixels and the engine's own clamp leaves one more, which is where 33 comes
 * from: 0x25F is 640-33 and 0x1BF is 480-33. Keeping the engine's own margin means the cursor is
 * still fully drawn at the far edge of any mode. The number itself lives in the header, so a test
 * can state it rather than copy it. */
#define CURSOR_MARGIN POINTER_CAGE_MARGIN

/* The two clamps the retail block ships with, kept as named constants because they appear both in
 * the pattern and in the rollback. */
#define SHIPPED_CAGE_WIDTH  0x25F
#define SHIPPED_CAGE_HEIGHT 0x1BF

/* Below this the arithmetic would produce a cage of zero or negative extent. No display mode this
 * engine will accept is anywhere near it; the test exists so that a garbage width read during a
 * mode change cannot write a cage nothing can move inside. */
#define MIN_USABLE_EXTENT (CURSOR_MARGIN + 1)

/* --- 0x00460C04  swrle_windowProc: the drawn cursor's clamp ----------------------------------- *
 *
 * 121 bytes. The ten absolute operands are wildcarded; two origin cells and eight references to
 * the two cursor cells, and the four immediates are NOT, because they are what identifies this as
 * the 640x480 cage. Unique in all three builds, and in the recompiled obi.exe it sits at
 * 0x00460BA4 instead: an address table would have written 0x60 into the middle of another
 * function, which is the whole reason nothing here is written down. */
static const uint8_t SIG_CURSOR_CAGE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x45, 0xF8,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x3B, 0x4D, 0xF8,
    0x7D, 0x0B,
    0x8B, 0x55, 0xF8,
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x1F,
    0x8B, 0x45, 0xF8,
    0x05, 0x5F, 0x02, 0x00, 0x00,
    0x39, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x7E, 0x0F,
    0x8B, 0x4D, 0xF8,
    0x81, 0xC1, 0x5F, 0x02, 0x00, 0x00,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x55, 0xF8,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x3B, 0x45, 0xF8,
    0x7D, 0x0B,
    0x8B, 0x4D, 0xF8,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x1E,
    0x8B, 0x55, 0xF8,
    0x81, 0xC2, 0xBF, 0x01, 0x00, 0x00,
    0x39, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x7E, 0x0D,
    0x8B, 0x45, 0xF8,
    0x05, 0xBF, 0x01, 0x00, 0x00,
    0xA3, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CURSOR_CAGE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_CURSOR_CAGE) == sizeof(MSK_CURSOR_CAGE),
               "the cursor-cage pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CURSOR_CAGE) == 121,
               "the cursor-cage pattern is not the 121-byte block it was measured from");

/* The two origin operands, repointed at cells of our own holding zero. */
#define OFFSET_ORIGIN_X_OPERAND 0x01u
#define OFFSET_ORIGIN_Y_OPERAND 0x3Fu

/* The four clamp immediates, and the opcode byte(s) in front of each. Every one is verified before
 * it is written: a matched pattern proves the block, and these prove that the offsets inside it are
 * still where the listing above says. */
#define OFFSET_WIDTH_IMM_1  0x22u
#define OFFSET_WIDTH_OP_1   0x21u
#define OFFSET_WIDTH_IMM_2  0x33u
#define OFFSET_WIDTH_OP_2   0x31u
#define OFFSET_HEIGHT_IMM_1 0x60u
#define OFFSET_HEIGHT_OP_1  0x5Eu
#define OFFSET_HEIGHT_IMM_2 0x70u
#define OFFSET_HEIGHT_OP_2  0x6Fu

enum {
    SITE_CURSOR_CAGE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("cursor_cage", SIG_CURSOR_CAGE, MSK_CURSOR_CAGE)
};

/* The two cells the origin operands are repointed at. They are read by the engine on every mouse
 * message and never written by anything, so a plain zero is the whole of it: the clamp becomes
 * [0, W-33] x [0, H-33] in absolute screen coordinates, which is the whole display mode. */
static const int32_t cage_origin_zero_x = 0;
static const int32_t cage_origin_zero_y = 0;

typedef struct pointer_cage_state {
    bool installed;
    bool active;

    /* Cached at install time and never re-resolved: the pattern contains the immediates the
     * install overwrites, so after a successful install it matches nothing. */
    uintptr_t width_immediates[2];
    uintptr_t height_immediates[2];

    int32_t applied_width;
    int32_t applied_height;

    bool warned_implausible;

} pointer_cage_state_t;

static pointer_cage_state_t cage_state;

/* ============================================================================================ */
bool pointer_cage_extent(int mode_width, int mode_height,
                         int *out_clamp_width, int *out_clamp_height)
{
    if (mode_width < MIN_USABLE_EXTENT || mode_height < MIN_USABLE_EXTENT) {
        return false;
    }

    *out_clamp_width  = mode_width  - CURSOR_MARGIN;
    *out_clamp_height = mode_height - CURSOR_MARGIN;
    return true;
}

/* ============================================================================================ */
static bool opcode_matches(uintptr_t address, const uint8_t *expected, size_t size,
                           const char *what)
{
    if (!patch_validate_bytes(address, expected, size)) {
        log_warning("the menu cursor clamp: %s at %08X is not the instruction it should be, so "
                    "nothing is patched", what, (unsigned)address);
        return false;
    }
    return true;
}

/* Every byte this file reads or writes, proved before the first write. The pattern
 * already matched, so this is belt and braces, but the four offsets are hand-derived from a
 * listing and that is exactly the class of thing that is wrong silently. */
static bool cage_offsets_are_sane(uintptr_t site)
{
    static const uint8_t ADD_EAX_IMM32[] = { 0x05 };
    static const uint8_t ADD_ECX_IMM32[] = { 0x81, 0xC1 };
    static const uint8_t ADD_EDX_IMM32[] = { 0x81, 0xC2 };
    static const uint8_t MOV_EAX_ABS[]   = { 0xA1 };
    static const uint8_t MOV_EDX_ABS[]   = { 0x8B, 0x15 };

    return opcode_matches(site + OFFSET_ORIGIN_X_OPERAND - 1u, MOV_EAX_ABS, sizeof(MOV_EAX_ABS),
                          "the load of the horizontal menu origin") &&
           opcode_matches(site + OFFSET_WIDTH_OP_1, ADD_EAX_IMM32, sizeof(ADD_EAX_IMM32),
                          "the first width clamp") &&
           opcode_matches(site + OFFSET_WIDTH_OP_2, ADD_ECX_IMM32, sizeof(ADD_ECX_IMM32),
                          "the second width clamp") &&
           opcode_matches(site + OFFSET_ORIGIN_Y_OPERAND - 2u, MOV_EDX_ABS, sizeof(MOV_EDX_ABS),
                          "the load of the vertical menu origin") &&
           opcode_matches(site + OFFSET_HEIGHT_OP_1, ADD_EDX_IMM32, sizeof(ADD_EDX_IMM32),
                          "the first height clamp") &&
           opcode_matches(site + OFFSET_HEIGHT_OP_2, ADD_EAX_IMM32, sizeof(ADD_EAX_IMM32),
                          "the second height clamp");
}

/* Writes the four clamps ABSOLUTELY from the canvas size. Never `imm += delta`, so writing them
 * a second time writes the same numbers rather than drifting. */
static bool write_clamps(int32_t width, int32_t height)
{
    int      clamp_width = 0;
    int      clamp_height = 0;
    uint32_t cage_width;
    uint32_t cage_height;
    size_t   index;

    if (!pointer_cage_extent((int)width, (int)height, &clamp_width, &clamp_height)) {
        return false;
    }
    cage_width  = (uint32_t)clamp_width;
    cage_height = (uint32_t)clamp_height;

    for (index = 0; index < 2; ++index) {
        if (patch_write_u32(cage_state.width_immediates[index], cage_width) != PATCH_RESULT_OK ||
            patch_write_u32(cage_state.height_immediates[index], cage_height) != PATCH_RESULT_OK) {
            return false;
        }
    }

    cage_state.applied_width  = width;
    cage_state.applied_height = height;
    return true;
}

/* ============================================================================================ */
static bool install_cage(uintptr_t site, int32_t width, int32_t height)
{
    /* Read, not written. The two origin operands name the cells the engine itself maintains, and
     * reading them back is the last check that this block is the one the listing describes before
     * any immediate is touched. Nothing here has a claim on their contents. */
    uint32_t shipped_origin_x = 0;
    uint32_t shipped_origin_y = 0;

    cage_state.width_immediates[0]  = site + OFFSET_WIDTH_IMM_1;
    cage_state.width_immediates[1]  = site + OFFSET_WIDTH_IMM_2;
    cage_state.height_immediates[0] = site + OFFSET_HEIGHT_IMM_1;
    cage_state.height_immediates[1] = site + OFFSET_HEIGHT_IMM_2;


    if (!memory_read_u32(site + OFFSET_ORIGIN_X_OPERAND, &shipped_origin_x) ||
        !memory_read_u32(site + OFFSET_ORIGIN_Y_OPERAND, &shipped_origin_y)) {
        log_error("the menu cursor clamp's two origin operands could not be read back, so there "
                  "would be nothing to roll back to, nothing is patched");
        return false;
    }

    /* The whole of it. The cage becomes [origin, origin + canvas - 33]: larger than it shipped,
     * still anchored at the menu origin, and every widget still reachable. */
    if (!write_clamps(width, height)) {
        log_error("the menu cursor clamp could not be widened, nothing has been changed and the "
                  "cursor still moves in a 640x480 island");
        return false;
    }

    /* The operands were read as a check and are not written; see the file header for what used
     * to happen here and why it stopped. */
    (void)shipped_origin_x;
    (void)shipped_origin_y;

    cage_state.active = true;
    return true;
}

void pointer_cage_install(bool enabled, int32_t canvas_width, int32_t canvas_height)
{
    uintptr_t site;

    if (cage_state.installed) {
        return;
    }
    cage_state.installed = true;

    if (!enabled) {
        log_info("WidenMenuCursorArea=0, the engine's own menu cursor clamp is left exactly as it "
                 "shipped. That is the right setting at MenuScale=1, where the two are identical; "
                 "with the canvas scaled it leaves the cursor in a 607x447 box while the widgets "
                 "sit outside it, which is why MenuScale declines to install alongside this.");
        return;
    }

    /* The canvas, not the display mode, and that is the whole of this feature.
     *
     * The engine clamps the drawn menu cursor to `g_menuOriginX + immediate`, so the immediate is a
     * canvas extent and the clamp is already anchored where the menus are. The canvas is also
     * exactly the region the menus repaint: a widget erases by drawing over itself, clipped to the
     * canvas. Allowing the cursor outside it means crossing pixels that nothing ever repaints, and
     * every one of them keeps a copy of the cursor until the screen closes.
     *
     * This module used to widen the clamp to the whole display mode, which is that mistake, and it
     * was invisible for as long as the feature never armed. It arms now.
     *
     * Deriving the clamp from the canvas also removes every reason this was fragile. Nothing here
     * needs the display mode, so there is no accessor to resolve, nothing to re-read when the mode
     * changes, and no waiting on graphics startup. At MenuScale=1 the immediates come out 607 and
     * 447, which is what the engine shipped, so this is then a no-op that writes the same numbers. */
    if (canvas_width  < MENU_SCALE_CANVAS_WIDTH)  { canvas_width  = MENU_SCALE_CANVAS_WIDTH;  }
    if (canvas_height < MENU_SCALE_CANVAS_HEIGHT) { canvas_height = MENU_SCALE_CANVAS_HEIGHT; }

    signature_resolve_table(sites, SITE_COUNT);

    site = sites[SITE_CURSOR_CAGE].address;
    if (site == 0) {
        log_warning("the menu cursor clamp did not resolve, so it is left as it shipped. NOTE that "
                    "this pattern contains the two clamp values it patches, so an ALREADY WIDENED "
                    "clamp, an earlier generation of this DLL still in the process, also fails "
                    "to resolve here. In that case the cursor already reaches the whole canvas and "
                    "nothing is wrong.");
        return;
    }
    if (!cage_offsets_are_sane(site)) {
        return;
    }
    if (!install_cage(site, canvas_width, canvas_height)) {
        return;
    }

    if (canvas_width == MENU_SCALE_CANVAS_WIDTH) {
        log_info("the menu cursor clamp is the canvas, 607x447 from the menu origin, which is what "
                 "the engine shipped (patched at %08X, same values). It becomes larger only when "
                 "MenuScale does.", (unsigned)site);
    } else {
        log_info("the drawn menu cursor may now travel %dx%d from the menu origin, following the "
                 "%dx%d canvas the menus are drawn on (patched at %08X). It is deliberately NOT "
                 "allowed outside it: nothing repaints out there, so a cursor crossing it would "
                 "stamp a copy of itself on every pixel until the screen closed.",
                 (int)(canvas_width - CURSOR_MARGIN), (int)(canvas_height - CURSOR_MARGIN),
                 (int)canvas_width, (int)canvas_height, (unsigned)site);
    }
}

bool pointer_cage_is_active(void)
{
    return cage_state.active;
}

