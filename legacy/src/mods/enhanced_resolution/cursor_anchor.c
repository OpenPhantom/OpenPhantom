/* cursor_anchor.c: the engine re-centres the mouse pointer in the wrong coordinate space.
 *
 * ==============================================================================================
 * BYTE BASIS, the whole defect is nine instructions
 *
 * control_recentreMouse takes ONE argument: the lParam of a WM_MOUSEMOVE, which Windows fills with
 * the pointer position in CLIENT coordinates. It compares that against (320,240) and, when it does
 * not match, warps the pointer with SetCursorPos, which takes SCREEN coordinates.
 *
 *   0046A115  55 8B EC              push ebp / mov ebp,esp
 *   0046A118  0F BF 45 08           movsx eax, word [ebp+8]      ; (short)LOWORD  CLIENT x
 *   0046A11C  3D 40 01 00 00        cmp  eax, 0x140              ; 320
 *   0046A121  75 1B                 jne  0046A13E
 *   0046A123  8B 4D 08 C1 E9 10     mov  ecx,[ebp+8] / shr ecx,16
 *   0046A129  81 E1 FF FF 00 00     and  ecx,0xFFFF
 *   0046A12F  0F BF D1              movsx edx,cx                 ; (short)HIWORD  CLIENT y
 *   0046A132  81 FA F0 00 00 00     cmp  edx,0xF0                ; 240
 *   0046A138  75 04                 jne  0046A13E
 *   0046A13A  33 C0                 xor  eax,eax                 ; RETURN 0 = "already centred"
 *   0046A13C  EB 15                 jmp  0046A153
 *   0046A13E  68 F0 00 00 00        push 0xF0
 *   0046A143  68 40 01 00 00        push 0x140
 *   0046A148  FF 15 <SetCursorPos>  call                         ; SCREEN coordinates
 *   0046A14E  B8 01 00 00 00        mov  eax,1                   ; RETURN 1 = "I moved it"
 *
 * Let C be the client origin in screen coordinates. The early-out needs the pointer at screen
 * C + (320,240); the warp puts it at screen (320,240), i.e. at client (320,240) - C. The two agree
 * for exactly one value of C, namely (0,0).
 *
 *   C == (0,0)   the warp lands on the centre, the next message takes the early-out, the pointer
 *                is pinned inside the window and can never leave it. this is the confinement,
 *                the engine imports neither ClipCursor nor ShowCursor; its whole USER32 cursor
 *                vocabulary is SetCursorPos, SetCapture, ReleaseCapture and SetCursor(NULL).
 *   C != (0,0)   the early-out can never be true, and the pointer comes to rest at screen
 *                (320,240), a point that need not be inside the window at all. With the window
 *                on a monitor whose origin is at -1920,0 that point is on the other display.
 *
 * The return value is not decoration. The caller inside the menu message filter reads it:
 *
 *   00460BB2  8B 4D 14              mov  ecx,[ebp+0x14]          ; the WM_MOUSEMOVE lParam
 *   00460BB6  E8 <rel32>            call control_recentreMouse
 *   00460BBE  85 C0 / 75 0A         test eax,eax / jne 00460BCC
 *   00460BC2  ...                   0 -> the message is SWALLOWED (this was our own warp's echo)
 *   00460BCC  0F BF 55 14           movsx edx, word [ebp+0x14]
 *   00460BD5  8D 4C 02 C0           lea  ecx,[eax+edx-0x140]     ; the menu cursor's delta,
 *   00460BDC  89 0D <g_menuCursorX> mov  [..],ecx                ; measured FROM 320
 *
 * So 0 means "that was the echo, ignore it" and 1 means "a real movement, take the delta". Fixing
 * only the warp and not the early-out would leave every message reporting a delta of C, which is
 * how a 640x480 menu cursor ends up slammed against its clamp on a window at -1920,0.
 *
 * There are three callers and they are all message filters, so the leaf is the one place that
 * covers all of them: 00460BB6 (the menu filter, installed while a screen is open), 0046A1A8 (the
 * permanent control filter) and 00497EAE (a third window filter).
 *
 * ---- the second site: the same defect, once per activation ------------------------------------
 *
 *   0046A155  55 8B EC              push ebp / mov ebp,esp
 *   0046A158  68 F0 00 00 00        push 0xF0
 *   0046A15D  68 40 01 00 00        push 0x140
 *   0046A162  FF 15 <SetCursorPos>  call                         ; the same wrong space
 *   0046A168  E8 <rel32>            call stdWin_getWindow        ; <- the operand we read
 *   0046A16D  50 / FF 15 <SetCapture>
 *   0046A174  6A 00 / FF 15 <SetCursor>                          ; hide the pointer over us
 *   0046A17C  5D C3
 *
 * and stdWin_getWindow is three instructions whose operand is the engine's own window handle:
 *
 *   00498967  55 8B EC              push ebp / mov ebp,esp
 *   0049896A  A1 <g_window>         mov  eax,[abs32]             ; <- read, never written down
 *   0049896F  5D C3
 *
 * ---- focus, which this file only has to stay out of the way of ------------------------------
 * The control filter routes WM_ACTIVATEAPP to exactly these two functions:
 *
 *   0046A193  cmp msg,0x1C / je 0046A1B2          ; WM_ACTIVATEAPP
 *   0046A1B2  cmp wParam,1 / jne 0046A1BF
 *   0046A1B8  call 0046A155                       ; activated   -> capture, and the warp above
 *   0046A1BF  call 0046A17E                       ; deactivated -> ReleaseCapture
 *
 * That is the engine's whole focus handling, and in the installation this was written for it never
 * runs: a graphics wrapper in front of the window procedure filters WM_ACTIVATEAPP out. We add
 * nothing to it and take no capture of our own, so there is nothing of ours to release here. What
 * we do add is a refusal: while the foreground window belongs to another process the warp is
 * skipped entirely, so an Alt-Tab frees the pointer whether or not the message got through.
 *
 * ---- what is deliberately NOT covered --------------------------------------------------------
 * There is a third `push 0xF0 / push 0x140 / call SetCursorPos` at the very end of the input
 * startup routine, inside a large function that is not worth detouring for one warp that happens
 * once. It runs after the graphics have been set up, so it can park the pointer outside the window
 * for as long as the hand is off the mouse; the first mouse movement then brings it back through
 * the hook below. That is a transient, and it is named here rather than hidden.
 *
 * SIZE NOTE: over the preferred 400 lines, and the excess is the listing above: a pattern
 * without the disassembly it was cut from is a magic number, and the
 * asymmetry between "compare in client space" and "warp in screen space" is invisible in the C.
 * ============================================================================================ */
#include "cursor_anchor.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4, "cursor_anchor reads 32-bit engine pointers out of operands");

/* The engine's idea of "the middle", in its own 640x480 client space. These are the two immediates
 * at 0046A13E/0046A143 and at 0046A158/0046A15D, and the same two the delta is measured from. */
#define ENGINE_CENTRE_X 0x140
#define ENGINE_CENTRE_Y 0x0F0

/* --- 0x0046A115  control_recentreMouse -------------------------------------------------------
 * Address-free: every byte of this pattern is an opcode, a register field or an immediate, and it
 * stops before the SetCursorPos import slot. Unique in all three builds including the
 * recompiled obi.exe, where it sits 0x60 lower. */
static const uint8_t SIG_CURSOR_RECENTRE[] = {
    0x55, 0x8B, 0xEC,
    0x0F, 0xBF, 0x45, 0x08,
    0x3D, 0x40, 0x01, 0x00, 0x00,
    0x75, 0x1B,
    0x8B, 0x4D, 0x08,
    0xC1, 0xE9, 0x10,
    0x81, 0xE1, 0xFF, 0xFF, 0x00, 0x00,
    0x0F, 0xBF, 0xD1,
    0x81, 0xFA, 0xF0, 0x00, 0x00, 0x00,
    0x75, 0x04,
    0x33, 0xC0,
    0xEB, 0x15,
    0x68, 0xF0, 0x00, 0x00, 0x00,
    0x68, 0x40, 0x01, 0x00, 0x00
};
/* `push ebp / mov ebp,esp / movsx eax,word [ebp+8]` = 3 + 4. The first instruction boundary at or
 * past the five bytes a `jmp rel32` needs. */
#define CURSOR_RECENTRE_PROLOGUE 7u

/* --- 0x0046A155  control_captureMouse --------------------------------------------------------
 * The three import slots and the rel32 are wildcarded, so the pattern survives a rebased import
 * table. Also unique in all three builds, the identical tail also occurs at the end of the input
 * startup routine, and the `55 8B EC` in front is what tells the two apart. */
static const uint8_t SIG_CURSOR_CAPTURE[] = {
    0x55, 0x8B, 0xEC,
    0x68, 0xF0, 0x00, 0x00, 0x00,
    0x68, 0x40, 0x01, 0x00, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x00,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_CURSOR_CAPTURE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CURSOR_CAPTURE) == sizeof(MSK_CURSOR_CAPTURE),
               "the capture pattern and its mask are different lengths");

/* `push ebp / mov ebp,esp / push 0xF0` = 3 + 5. */
#define CURSOR_CAPTURE_PROLOGUE 8u
/* The rel32 of `call stdWin_getWindow`; the 0xE8 that owns it sits one byte earlier. */
#define OFFSET_CAPTURE_WINDOW_CALL 0x14u

/* stdWin_getWindow, checked byte for byte before its operand is believed. */
static const uint8_t GET_WINDOW_HEAD[] = { 0x55, 0x8B, 0xEC, 0xA1 };
static const uint8_t GET_WINDOW_TAIL[] = { 0x5D, 0xC3 };
#define OFFSET_GET_WINDOW_OPERAND 0x04u
#define OFFSET_GET_WINDOW_TAIL    0x08u
#define GET_WINDOW_SIZE           0x0Au

enum {
    CURSOR_SITE_RECENTRE,
    CURSOR_SITE_CAPTURE,
    CURSOR_SITE_COUNT
};

static signature_t sites[CURSOR_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("control_recentre_mouse", SIG_CURSOR_RECENTRE,
                           CURSOR_RECENTRE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("control_capture_mouse", SIG_CURSOR_CAPTURE, MSK_CURSOR_CAPTURE,
                                  CURSOR_CAPTURE_PROLOGUE)
};

/* Both are __cdecl: the three call sites of the first clean up with `add esp,4`, and the second
 * takes no argument and its callers clean up nothing. */
typedef int32_t (__cdecl *recentre_fn_t)(int32_t packed_client_point);
typedef void    (__cdecl *capture_fn_t)(void);

typedef struct cursor_anchor_state {
    bool          installed;
    bool          active;

    HWND         *window_slot;      /* the engine's own handle cell, or NULL on the fallback */
    DWORD         process_id;

    detour_t      recentre_detour;
    detour_t      capture_detour;
    recentre_fn_t original_recentre;
    capture_fn_t  original_capture;

    bool          warned_no_window;
} cursor_anchor_state_t;

static cursor_anchor_state_t cursor_state;

/* ============================================================================================ */
int cursor_anchor_signed_word(uint32_t value)
{
    uint32_t word = value & 0xFFFFu;

    /* What `movsx eax, word ptr` does, written so that no cast has to be trusted: a client
     * coordinate left of or above the client area arrives as a negative 16-bit value. */
    if ((word & 0x8000u) != 0u) {
        return (int)word - 0x10000;
    }
    return (int)word;
}

bool cursor_anchor_point_is_centre(uint32_t packed_client_point)
{
    return cursor_anchor_signed_word(packed_client_point) == ENGINE_CENTRE_X &&
           cursor_anchor_signed_word(packed_client_point >> 16) == ENGINE_CENTRE_Y;
}

/* ============================================================================================ */
static bool foreground_belongs_to_us(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner);
    return owner == cursor_state.process_id;
}

static HWND anchor_window(void)
{
    if (cursor_state.window_slot != NULL) {
        return *cursor_state.window_slot;
    }
    /* The fallback branch, named in the install log: inside the window procedure this is the
     * game's own top-level window whenever the game is the active application, and NULL when it
     * is not, which is the case we refuse anyway. */
    return GetActiveWindow();
}

/* The whole repair: the same (320,240) the engine wants, converted into the space SetCursorPos
 * actually speaks. Returns false when the window could not be asked, so the caller can fall back
 * to the engine's own behaviour rather than silently stop re-centring. */
static bool warp_to_client_centre(void)
{
    POINT centre;
    HWND  window = anchor_window();

    centre.x = ENGINE_CENTRE_X;
    centre.y = ENGINE_CENTRE_Y;

    if (window == NULL || !ClientToScreen(window, &centre)) {
        if (!cursor_state.warned_no_window) {
            cursor_state.warned_no_window = true;
            log_warning("the game window could not be asked for its client origin, the pointer "
                        "is re-centred the way the engine does it, which is only correct while "
                        "the window sits at screen 0,0");
        }
        return false;
    }

    SetCursorPos(centre.x, centre.y);
    return true;
}

static int32_t __cdecl hook_recentre_mouse(int32_t packed_client_point)
{
    if (!cursor_state.active || cursor_state.original_recentre == NULL) {
        /* Only reachable in the few instructions between the branch being written and the state
         * being armed, and the window does not exist that early. Answer the way the engine's own
         * early-out does, so no delta is invented. */
        return 0;
    }

    /* Alt-Tab has to free the pointer, and it must not depend on WM_ACTIVATEAPP reaching the
     * engine: a graphics wrapper in front of the window procedure can filter that message out. */
    if (!foreground_belongs_to_us()) {
        return 0;
    }

    /* The echo of our own last warp, read exactly the way the engine reads it. */
    if (cursor_anchor_point_is_centre((uint32_t)packed_client_point)) {
        return 0;
    }

    if (!warp_to_client_centre()) {
        return cursor_state.original_recentre(packed_client_point);
    }
    return 1;                           /* a real movement: the caller may take its delta */
}

static void __cdecl hook_capture_mouse(void)
{
    if (cursor_state.original_capture == NULL) {
        return;                         /* see hook_recentre_mouse: the same un-armed window */
    }

    /* SetCapture and the hidden cursor stay entirely the engine's; only the warp it did first is
     * repeated in the right place. Two SetCursorPos calls in a row are harmless, the second one
     * decides, and doing it this way keeps the original's other two effects untouched. */
    cursor_state.original_capture();

    if (cursor_state.active && foreground_belongs_to_us()) {
        (void)warp_to_client_centre();
    }
}

/* ============================================================================================ */
static bool resolve_window_slot(uintptr_t capture_site)
{
    uint32_t  relative = 0;
    uintptr_t getter;
    uint8_t   head[sizeof(GET_WINDOW_HEAD)];
    uint8_t   tail[sizeof(GET_WINDOW_TAIL)];
    uint32_t  slot = 0;
    uint8_t   call_opcode = 0;

    if (!memory_read_u8(capture_site + OFFSET_CAPTURE_WINDOW_CALL - 1u, &call_opcode) ||
        call_opcode != 0xE8) {
        log_warning("no `call rel32` where the window getter should be (found %02X)", call_opcode);
        return false;
    }
    if (!memory_read_u32(capture_site + OFFSET_CAPTURE_WINDOW_CALL, &relative)) {
        return false;
    }

    getter = capture_site + OFFSET_CAPTURE_WINDOW_CALL + sizeof(uint32_t)
           + (uintptr_t)(int32_t)relative;
    if (!memory_is_inside_image(getter, GET_WINDOW_SIZE)) {
        log_warning("the window getter would be at %08X, outside the image, refused",
                    (unsigned)getter);
        return false;
    }

    /* Three instructions, and both ends are checked before the operand between them is used. */
    if (!memory_read(getter, head, sizeof(head)) ||
        !memory_read(getter + OFFSET_GET_WINDOW_TAIL, tail, sizeof(tail)) ||
        memcmp(head, GET_WINDOW_HEAD, sizeof(head)) != 0 ||
        memcmp(tail, GET_WINDOW_TAIL, sizeof(tail)) != 0) {
        log_warning("%08X is not the three-instruction window getter, refused", (unsigned)getter);
        return false;
    }
    if (!memory_read_u32(getter + OFFSET_GET_WINDOW_OPERAND, &slot) ||
        !memory_is_inside_image(slot, sizeof(uint32_t))) {
        log_warning("the window handle cell would be at %08X, outside the image, refused",
                    (unsigned)slot);
        return false;
    }

    cursor_state.window_slot = (HWND *)(uintptr_t)slot;
    log_info("the engine's own window handle lives at %08X (read out of the getter at %08X, not "
             "written down), the pointer is anchored to that window's client area",
             (unsigned)slot, (unsigned)getter);
    return true;
}

static bool install_activation_reanchor(void)
{
    if (sites[CURSOR_SITE_CAPTURE].address != 0 &&
        detour_install(&cursor_state.capture_detour, sites[CURSOR_SITE_CAPTURE].address,
                       (const void *)hook_capture_mouse, CURSOR_CAPTURE_PROLOGUE)) {
        cursor_state.original_capture = (capture_fn_t)cursor_state.capture_detour.original;
        return true;
    }
    log_warning("the re-anchor on window activation is not installed, the pointer is still kept "
                "inside the window, but after an Alt-Tab back it is put there by the first mouse "
                "movement rather than immediately");
    return false;
}

bool cursor_anchor_install(bool enabled, bool window_is_moved)
{
    bool reanchored;

    if (cursor_state.installed) {
        return cursor_state.active;
    }
    cursor_state.installed = true;
    cursor_state.process_id = GetCurrentProcessId();

    if (!enabled) {
        log_info("KeepCursorInWindow=0, the engine re-centres the pointer in screen coordinates "
                 "as it always did, which lets it leave the window whenever the window is not at "
                 "screen 0,0");
        return false;
    }

    signature_resolve_table(sites, CURSOR_SITE_COUNT);

    if (sites[CURSOR_SITE_RECENTRE].address == 0) {
        log_warning("control_recentre_mouse did not resolve, the pointer keeps the engine's own "
                    "behaviour and can leave the window while it is moved off screen 0,0");
        return false;
    }

    if (sites[CURSOR_SITE_CAPTURE].address == 0 ||
        !resolve_window_slot(sites[CURSOR_SITE_CAPTURE].address)) {
        log_warning("the engine's window handle did not resolve, falling back to the active "
                    "window of this thread, which is the same window whenever the game is the "
                    "active application");
    }

    if (!detour_install(&cursor_state.recentre_detour, sites[CURSOR_SITE_RECENTRE].address,
                        (const void *)hook_recentre_mouse, CURSOR_RECENTRE_PROLOGUE)) {
        log_warning("control_recentre_mouse at %08X could not be detoured, the pointer keeps the "
                    "engine's own behaviour", (unsigned)sites[CURSOR_SITE_RECENTRE].address);
        return false;
    }
    cursor_state.original_recentre = (recentre_fn_t)cursor_state.recentre_detour.original;
    cursor_state.active = true;

    reanchored = install_activation_reanchor();

    log_info("the mouse pointer is anchored to the window's client area (hooked %08X; the "
             "re-anchor on activation is %s). The engine compares CLIENT coordinates against the "
             "SCREEN point it warps to, and the two agree at exactly one window origin: 0,0. "
             "This hook holds nothing global and skips the warp entirely while another application "
             "is in front; keeping the pointer from crossing the edge in the first place is a "
             "separate step.",
             (unsigned)sites[CURSOR_SITE_RECENTRE].address,
             reanchored ? "installed too" : "NOT installed");

    /* The honest half of the line above. What this repair is WORTH depends entirely on whether
     * anything moves the window, and nothing in this DLL does unless FitWindowToMode is on. */
    if (window_is_moved) {
        log_info("this repair is LOAD-BEARING in this session: FitWindowToMode is on, so the window "
                 "is moved off screen 0,0 and the engine's own re-centring would put the pointer "
                 "outside its own client area, on a monitor whose origin is negative, on another "
                 "display entirely.");
    } else {
        log_info("this repair is BELT-AND-BRACES in this session: nothing moves the window, so it "
                 "stays at screen 0,0 where the engine's own arithmetic is already correct and "
                 "this hook computes the identical warp. It is left on because it costs nothing "
                 "there and it is what makes an unusual setup, a wrapper that repositions the "
                 "window, a desktop with a negative monitor origin, survivable.");
    }
    return true;
}
