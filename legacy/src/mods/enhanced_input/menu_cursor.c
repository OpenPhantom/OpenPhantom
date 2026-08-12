/* menu_cursor.c: the menu pointer, moved by the device instead of by the screen pointer.
 *
 * ==============================================================================================
 * What the engine does, read out of the image rather than assumed
 *
 * The menu pointer is not a Windows cursor. It is a position the engine keeps itself, in two
 * integer cells, and draws itself. A mouse message arrives, the engine warps the system pointer
 * back to a fixed point, takes the difference between where the pointer was and that fixed point,
 * and adds it:
 *
 *     cursor += (messagePosition - centre)
 *
 * The whole path, in retail WMAIN.EXE, 829,952 bytes, ImageBase 0x400000:
 *
 *     the window procedure
 *       -> stdWin95_pumpMessages   0x00498BCA
 *       -> the idle callback       0x00460A54     registered, not called
 *          -> control_recentreMouse 0x0046A115    SetCursorPos(320, 240)
 *          -> cursor += (messagePosition - centre)
 *          -> the clamp
 *
 * The callback is reached through a table, which is why it has no relative caller: 0x0045DA2E
 * pushes 0x00460A54 into 0x00498DBE, the window module's callback registration. Its arguments are
 * (hwnd, msg, wParam, lParam, int *out); the message is [ebp+0x0C], which the head of the function
 * compares against 0x100, WM_KEYDOWN, and the packed position is [ebp+0x14].
 *
 * The recentring:
 *
 *     0046A118  movsx eax, word [ebp+8]      x
 *     0046A11C  cmp   eax, 0x140             320
 *     0046A132  cmp   edx, 0xF0              240
 *     0046A13A  xor   eax, eax               already centred, ignore
 *     0046A148  call  [0x008C1678]           SetCursorPos(320, 240)
 *
 * and the accumulation:
 *
 *     00460BCC  movsx edx, word [ebp+0x14]
 *     00460BD5  lea   ecx, [eax + edx - 0x140]    cursorX += (x - 320)
 *     00460BDC  mov   [0x004B6C98], ecx
 *     00460BF7  lea   edx, [ecx + eax - 0xF0]     cursorY += (y - 240)
 *     00460BFE  mov   [0x004B6C9C], edx
 *     00460C25  add   eax, 0x25F                  the 607 wide clamp
 *
 * A census of both cells is what made this safe to touch: ten references each, and every single
 * write is inside 0x00460A54. The only other function that mentions them is 0x00460A30, which
 * reads both. There is no second writer anywhere in the image.
 *
 * ==============================================================================================
 * Three defects, and which of them is which size
 *
 * The position and the difference are integers. `lea` on integers, no fractional part anywhere in
 * that chain, so movement that does not amount to a whole pixel in one message is not carried
 * over, it is discarded. At normal speed that is a few per cent. At a slowly moving hand, where
 * the per-message movement is one or two pixels, the difference between one and two is fifty per
 * cent. This is the defect this file repairs.
 *
 * The centre is hard coded at half of 640x480. On a 1920x1080 screen the pointer is warped to a
 * point in the upper left, which leaves it 320 pixels of room to the left and 1600 to the right,
 * so a fast movement can push the system pointer into the left screen edge and the operating
 * system discards the rest before the engine ever sees it. Travel is then lost in one direction
 * and not in the other. The cursor anchor in enhanced_resolution does not fix this: it converts
 * the same (320,240) from client into screen space, which on a window at screen 0,0 is the
 * identity.
 *
 * And the source is the system pointer, which carries whatever pointer acceleration the machine is
 * configured with, so the relationship between hand and cursor is not a constant. On the reporting
 * machine acceleration is switched off, so it contributes nothing there, but it is part of why the
 * mapping is not a constant in general.
 *
 * Reading from the device removes all three at once.
 *
 * ==============================================================================================
 * What was checked and found not to be a defect
 *
 * The message pump was the first suspicion and it is sound. 0x00498BCA is PeekMessage, GetMessage,
 * TranslateMessage, DispatchMessage; at 0x00498C72 it peeks again and at 0x00498C7A it jumps back
 * to 0x00498BEA. It drains the whole queue and does not process one message per frame. A patch had
 * nearly been built on the opposite assumption, so this was read before anything here was written.
 *
 * ==============================================================================================
 * What this does, and why the delta is substituted rather than the cells written
 *
 * It leaves every one of the mechanisms above in place and changes only the number that goes in.
 * The device is read directly, in counts; the position is accumulated in floating point so the
 * remainder survives; and the engine is handed a message whose coordinates are the centre plus
 * exactly the whole-pixel movement we want it to add. Its own accumulation, its own clamp, its own
 * hit testing and everything the resolution fix does to the cage are untouched, because they all
 * run exactly as they did.
 *
 * Writing the two cells directly would mean owning a copy of somebody else's arithmetic. The clamp
 * sits between the accumulation and the cells, and the pointer cage in enhanced_resolution rewrites
 * its immediates and repoints its origin operands. Getting the two out of step would put the
 * pointer outside the cage or inside a smaller one. Handing the engine a better delta needs none of
 * that.
 *
 * If the device reader is not delivering packets, this does nothing at all and the engine keeps its
 * own pointer motion. A frozen cursor is worse than a stepping one.
 *
 * ==============================================================================================
 * What this does not explain
 *
 * The field report that prompted it was a menu cursor stuttering badly during ordinary movement.
 * The integer loss is a few per cent at that speed and the screen-edge loss needs a fast flick. In
 * the same session's log the frame timing showed eleven hitches a second of about 20 ms with
 * vertical sync enabled, and that is the only measured quantity large enough to account for what
 * was reported. This file repairs real defects in the input path; it should not be credited with a
 * smoothness that the frame delivery is still costing.
 */
#include "menu_cursor.h"

#include "raw_mouse.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's own centre, and the message it acts on. Both are the engine's numbers, not ours:
 * the centre has to match what its own difference subtracts, the 0x140 and 0xF0 in the listing
 * above, or every message would be read as a jump of hundreds of pixels. */
#define ENGINE_CENTRE_X      320
#define ENGINE_CENTRE_Y      240
#define WM_MOUSEMOVE_MSG     0x0200u

/* One device count moves the pointer one pixel. That is what the system does with acceleration
 * switched off, so the feel is the one the player already has, and it is a setting rather than a
 * constant because a high resolution mouse may want less. */
#define DEFAULT_PIXELS_PER_COUNT 1.0f

/* A message packs two SIGNED 16-bit coordinates, which is what the two `movsx` above read them
 * back as. Anything beyond this in one message would wrap into a large movement in the opposite
 * direction, so it is clamped and the remainder is kept for the next one rather than dropped. */
#define MAX_DELTA_PER_MESSAGE 30000

/* The idle callback the menus install, which is where the pointer is accumulated. Registered
 * through the window module's callback table, so it has no relative caller and the anchor has to be
 * its own prologue.
 *
 *   55 8B EC             push ebp / mov ebp,esp
 *   83 EC 14             sub  esp, 0x14
 *   C7 45 FC 00...       mov  [ebp-4], 0
 *   C7 05 ?? ?? ?? ?? 00 mov  [a module cell], 0
 *   83 3D ?? ?? ?? ?? 00 cmp  [the menu pointer], 0
 *   75 07                jne  carry on
 *
 * The two absolute operands are wildcarded, and that is what makes the pattern resolve on the
 * recompile as well as on the retail builds:
 *
 *     retail WMAIN.EXE                     0x00460A54
 *     wmain.exe                            0x00460A54
 *     obi.exe, the recompile               0x004609F4
 *
 * The prologue the detour overwrites is the first six bytes, 55 8B EC 83 EC 14. */
static const uint8_t SIG_MENU_PUMP[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0xEC, 0x14,
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x07
};
static const uint8_t MSK_MENU_PUMP[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};
#define MENU_PUMP_PROLOGUE  6u

/* Five arguments, and only the fourth is touched. The others are passed through untouched because
 * this hook has no business knowing what they mean. */
typedef int32_t (__cdecl *menu_pump_fn_t)(void *hwnd, uint32_t message, uint32_t wparam,
                                          uint32_t lparam, void *out);

typedef struct menu_cursor_state {
    bool           installed;
    detour_t       pump;
    menu_pump_fn_t original;
    float          pixels_per_count;

    /* The remainder, and the whole point of the file. What did not amount to a whole pixel in this
     * message stays here and is paid out in the next one. */
    float          carry_x;
    float          carry_y;

    uint32_t       messages;      /* mouse messages seen */
    uint32_t       substituted;   /* of which we supplied the movement */
} menu_cursor_state_t;

static menu_cursor_state_t cursor_state;

/* Turns the device counts collected since the previous message into whole pixels, keeping what is
 * left over. Returns false when there is nothing to say, in which case the caller must pass the
 * message through unchanged rather than manufacture a zero movement: a zero would be read by the
 * engine as "the pointer is at the centre", which is its own early-out at 0x0046A13A.
 *
 * The counts come from a second pair of accumulators inside raw_mouse. Its primary take is
 * destructive by design and has exactly one consumer, the view turn, so the cursor gets the same
 * packets added to accumulators of its own and clears only those. The view path is left exactly as
 * it was, which is what keeps a working path out of this change's blast radius. The vertical axis
 * exists only on the cursor's side; the view turn never had a use for it. */
static bool take_whole_pixels(int *out_dx, int *out_dy)
{
    long  raw_x = 0;
    long  raw_y = 0;
    float want_x;
    float want_y;
    int   dx;
    int   dy;

    raw_mouse_take_cursor(&raw_x, &raw_y);

    want_x = cursor_state.carry_x + (float)raw_x * cursor_state.pixels_per_count;
    want_y = cursor_state.carry_y + (float)raw_y * cursor_state.pixels_per_count;

    dx = (int)want_x;                       /* toward zero, so the carry never changes sign */
    dy = (int)want_y;

    if (dx >  MAX_DELTA_PER_MESSAGE) { dx =  MAX_DELTA_PER_MESSAGE; }
    if (dx < -MAX_DELTA_PER_MESSAGE) { dx = -MAX_DELTA_PER_MESSAGE; }
    if (dy >  MAX_DELTA_PER_MESSAGE) { dy =  MAX_DELTA_PER_MESSAGE; }
    if (dy < -MAX_DELTA_PER_MESSAGE) { dy = -MAX_DELTA_PER_MESSAGE; }

    cursor_state.carry_x = want_x - (float)dx;
    cursor_state.carry_y = want_y - (float)dy;

    if (dx == 0 && dy == 0) {
        return false;
    }
    *out_dx = dx;
    *out_dy = dy;
    return true;
}

static int32_t __cdecl hook_menu_pump(void *hwnd, uint32_t message, uint32_t wparam,
                                      uint32_t lparam, void *out)
{
    int dx = 0;
    int dy = 0;

    if (message != WM_MOUSEMOVE_MSG || !raw_mouse_is_delivering()) {
        return cursor_state.original(hwnd, message, wparam, lparam, out);
    }

    ++cursor_state.messages;

    if (!take_whole_pixels(&dx, &dy)) {
        /* Nothing whole to hand over yet, and the message still has to be passed on because it also
         * drives the recentring. Passing the engine's own coordinates through would add its
         * quantised movement on top of ours and count the same motion twice, so the message is
         * given the centre, which is exactly the value its early-out ignores. */
        lparam = ((uint32_t)(ENGINE_CENTRE_Y & 0xFFFF) << 16) |
                 ((uint32_t)(ENGINE_CENTRE_X & 0xFFFF));
        return cursor_state.original(hwnd, message, wparam, lparam, out);
    }

    ++cursor_state.substituted;
    lparam = ((uint32_t)((ENGINE_CENTRE_Y + dy) & 0xFFFF) << 16) |
             ((uint32_t)((ENGINE_CENTRE_X + dx) & 0xFFFF));

    return cursor_state.original(hwnd, message, wparam, lparam, out);
}

bool menu_cursor_install(bool enabled)
{
    uintptr_t site;

    if (cursor_state.installed) {
        return true;
    }
    if (!enabled) {
        log_info("menu cursor left to the engine (MenuCursorRawInput=0)");
        return false;
    }

    cursor_state.pixels_per_count = DEFAULT_PIXELS_PER_COUNT;

    site = signature_find_detour_target(SIG_MENU_PUMP, MSK_MENU_PUMP, sizeof SIG_MENU_PUMP,
                                        MENU_PUMP_PROLOGUE);
    if (site == 0) {
        log_warning("the menu message handler did not resolve, so the pointer keeps stepping in "
                    "whole screen pixels");
        return false;
    }
    if (!detour_install(&cursor_state.pump, site, (const void *)hook_menu_pump,
                        MENU_PUMP_PROLOGUE)) {
        log_warning("the detour at %08X failed, the menu pointer is unchanged", (unsigned)site);
        return false;
    }

    cursor_state.original  = (menu_pump_fn_t)cursor_state.pump.original;
    cursor_state.installed = true;

    log_info("menu pointer driven from the device at %08X. The engine moves it by the difference "
             "between the system pointer and a hard-coded (320,240), in whole screen pixels, which "
             "loses everything under one pixel, loses travel at the screen edge because that centre "
             "is not the middle of a modern screen, and carries the machine's pointer acceleration. "
             "This hands its own accumulation a delta measured from the DEVICE instead, kept in "
             "floating point so the remainder survives. Its clamp, its hit testing and the "
             "resolution fix's cage all run exactly as before, because only the number changes. If "
             "the device reader ever stops delivering, the engine's own motion is handed back "
             "untouched.", (unsigned)site);
    return true;
}
