/* pad_axis_mute.c: see pad_axis_mute.h. */
#include "pad_axis_mute.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdint.h>


/* --- 0x0048D38D  stdControl_readAxis(axisIndex) ---------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 EC 14              sub  esp,0x14
 *   83 3D <joyOn> 00      cmp  dword ptr [g_joystickPresent], 0
 *   74 09                 je   +9
 *   83 3D <enabled> 00    cmp  dword ptr [g_joystickEnabled], 0
 *   75 0B                 jne  +0x0B
 *   D9 05 <zero>          fld  dword ptr [0.0]
 *   E9 <rel32>            jmp  the exit
 *   8B 45 08              mov  eax,[ebp+8]              ; the axis index
 *   6B C0 18              imul eax,eax,0x18             ; 24 bytes an axis record
 *   8B 88 <axisTable>     mov  ecx,[eax + g_axisRecord]
 *   83 E1 02              and  ecx,2                    ; the present bit
 *   85 C9                 test ecx,ecx
 *
 * Fifty two bytes. Four absolute operands and the one relative jump are masked, which leaves
 * thirty two literal bytes, and with those wildcarded the pattern still matches exactly once in
 * the retail image. The imul by 0x18 and the AND with 2 are what make it this function rather than
 * one of the many that open the same way.
 *
 * The prologue is the first six bytes, push ebp / mov ebp,esp / sub esp,0x14, an exact instruction
 * boundary carrying nothing relative. The two stage resolver needs that to find this site again
 * after something has detoured it.
 *
 * The axis record is 0x18 bytes and its first dword carries the flags; the read of that field is
 * in the pattern only as evidence. Nothing here reads or writes the table. */
static const uint8_t SIG_READ_AXIS[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0xEC, 0x14,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x09,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x0B,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xE9, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x08,
    0x6B, 0xC0, 0x18,
    0x8B, 0x88, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xE1, 0x02,
    0x85, 0xC9
};
static const uint8_t MSK_READ_AXIS[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_READ_AXIS == sizeof MSK_READ_AXIS,
               "the axis reader pattern and its mask are different lengths");
#define READ_AXIS_PROLOGUE 6u

/* The value the engine's own function returns for an axis it will not read, taken from the same
 * cell it loads on all four of its refusal paths. A flat zero, and it has to be this rather than
 * anything merely small: control_readAxis compares what comes back against zero exactly, and only
 * a value that fails that test is added to the sum. */
#define AXIS_NEUTRAL 0.0f

/* Returns in st(0) and cleans up nothing, so it is a plain __cdecl float. The caller stores what
 * comes back into a float before it does anything with it. */
typedef float (__cdecl *read_axis_fn_t)(int axis);

static detour_t axis_detour;
static int      muted_axis = -1;

static float __cdecl hook_read_axis(int axis)
{
    read_axis_fn_t original = (read_axis_fn_t)axis_detour.original;

    if (axis == muted_axis) {
        return AXIS_NEUTRAL;
    }
    return original(axis);
}

bool pad_axis_mute_install(int axis)
{
    uintptr_t target;

    target = signature_find_detour_target(SIG_READ_AXIS, MSK_READ_AXIS, sizeof SIG_READ_AXIS,
                                          READ_AXIS_PROLOGUE);
    if (target == 0) {
        log_warning("the joystick axis reader was not found, so the game goes on reading every "
                    "axis it is bound to. On a pad with the shipped bindings that means the right "
                    "stick pushed up or down walks the player, which is the defect this would "
                    "have taken out; nothing else about the pad is affected");
        return false;
    }
    if (!detour_install(&axis_detour, target, (const void *)hook_read_axis, READ_AXIS_PROLOGUE)) {
        log_warning("the joystick axis reader at %08X could not be detoured, so the right stick "
                    "goes on walking the player", (unsigned)target);
        return false;
    }

    muted_axis = axis;
    log_info("the game's own joystick reading is answered with a flat zero for axis %d, the right "
             "stick's vertical. The shipped bindings put that axis on the same forward and back "
             "control as the left stick's, so pushing the right stick up walked the player while "
             "the left stick was the one steering. Every other axis, the buttons and the hat reach "
             "the game exactly as before. PadEngineRightStick=1 hands the axis back",
             axis);
    return true;
}
