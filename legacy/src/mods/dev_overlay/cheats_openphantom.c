/* cheats_openphantom.c: unlimited ammunition, unlimited health, no fog, and free camera. The third
 * cheat this project adds, no fog, is a different enough shape - see cheats_no_fog.h - that it
 * lives in its own file and this one only dispatches CHEATS_OWN_NO_FOG's queries to it. Free
 * camera is documented next to its own two signatures below rather than up here, the two sites it
 * needs each carrying their own byte evidence at the point they are used.
 *
 * ==============================================================================================
 * THE TWO SITES, READ OUT OF THE RETAIL EXECUTABLE
 *
 * Ammunition is spent in one place. 0x00459FD4, and the whole function is this:
 *
 *   00459FD4  55 8B EC                 push ebp; mov ebp,esp
 *   00459FD7  83 3D 7C D5 86 00 00     cmp  [0086D57C],0        ; the player status record
 *   00459FDE  75 02 EB 35              return when it is null
 *   00459FE2  8B 45 08                 mov  eax,[ebp+8]         ; weaponId
 *   00459FE5  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459FEB  8B 54 81 10              mov  edx,[ecx+eax*4+0x10] ; ammo[weaponId], base +0x10
 *   00459FEF  2B 55 0C                 sub  edx,[ebp+0x0C]       ; minus the shot's cost
 *   00459FF2  8B 45 08                 mov  eax,[ebp+8]
 *   00459FF5  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459FFB  89 54 81 10              mov  [ecx+eax*4+0x10],edx
 *   00459FFF  8B 15 7C D5 86 00        mov  edx,[0086D57C]
 *   0045A005  8B 45 08                 mov  eax,[ebp+8]
 *   0045A008  3B 42 08                 cmp  eax,[edx+8]          ; the weapon in hand
 *   0045A00B  75 0A                    jnz  past the flash
 *   0045A00D  C7 05 24 FB 6C 00 ...    the weapon bar's flash target
 *
 * Damage is applied in one place. 0x00459ECE:
 *
 *   00459ECE  55 8B EC                 push ebp; mov ebp,esp
 *   00459ED1  83 3D 7C D5 86 00 00     cmp  [0086D57C],0
 *   00459ED8  75 02 EB 3C              return when it is null
 *   00459EDC  A1 7C D5 86 00           mov  eax,[0086D57C]
 *   00459EE1  8B 08                    mov  ecx,[eax]            ; health, at +0x00
 *   00459EE3  2B 4D 08                 sub  ecx,[ebp+8]          ; minus the damage
 *   00459EE6  8B 15 7C D5 86 00        mov  edx,[0086D57C]
 *   00459EEC  89 0A                    mov  [edx],ecx
 *   00459EEE  A1 7C D5 86 00           mov  eax,[0086D57C]
 *   00459EF1  83 38 00                 cmp  dword ptr [eax],0
 *   00459EF4  7D 0C                    jge  past the clamp
 *   00459EF6  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459EFC  C7 01 00 00 00 00        mov  [ecx],0              ; never below zero
 *   00459F02  C7 05 00 FB 6C 00 ...    the health bar's flash target
 *
 * Two things follow from these listings and both shape the design.
 *
 * FIRST, the ammunition base offset is +0x10 and the weapon in hand is at +0x08. The front end
 * reads its ammunition readout from the same +0x10 base, independently, which is the cross check
 * that this is the array and not a neighbour.
 *
 * SECOND, both functions begin with the identical ten bytes, because both start by testing the same
 * record pointer, and so do several of their neighbours in the same compiland. A pattern that
 * stopped at the prologue would match a handful of functions and this file would patch whichever
 * came first. Each pattern here therefore reaches into the body, to the instruction that names what
 * the function actually does: the scaled index load for ammunition, the health load and subtract
 * for damage. The record pointer inside them is masked and never depended on.
 *
 * ==============================================================================================
 * WHY A DETOUR THAT DECLINES, RATHER THAN A TOPPED UP COUNTER
 *
 * The obvious implementation of unlimited ammunition writes the magazine full once a frame. That
 * fights the pickup code, makes the weapon bar flash on frames nothing happened, and writes a value
 * into the save. Declining to subtract does none of that: the counter simply never moves, every
 * other consumer sees exactly what it saw before, and switching the cheat off leaves a state the
 * game could have reached on its own.
 *
 * The same argument holds for damage, with one addition. Returning early also skips the health
 * bar's flash, which is correct rather than a side effect: nothing hurt the player, so nothing
 * should flash. Death, the hurt animation and the knockback all hang off health having fallen, so
 * none of them can fire either.
 *
 * What this deliberately does NOT cover, because it is a different mechanism and would need its own
 * evidence: anything that sets health directly rather than subtracting from it. A scripted death
 * and the console's own "kill me now" both go elsewhere and are not blocked here.
 * ============================================================================================ */
#include "cheats_openphantom.h"

#include "cheats_no_fog.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0x00459FD4, thirty bytes: prologue, the null test, and the scaled load that is this
 * function's own. The record pointer appears twice and is masked in both places. */
static const uint8_t SIG_USE_AMMO[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [record],0               */
    0x75, 0x02,                                      /* jnz +2                       */
    0xEB, 0x00,                                      /* jmp out                      */
    0x8B, 0x45, 0x08,                                /* mov eax,[ebp+8]   weaponId   */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,              /* mov ecx,[record]             */
    0x8B, 0x54, 0x81, 0x10,                          /* mov edx,[ecx+eax*4+0x10]     */
    0x2B, 0x55, 0x0C                                 /* sub edx,[ebp+0x0C]           */
};
/* THE LAST THREE BYTES ARE THE WHOLE POINT. The function that GIVES ammunition sits twenty nine
 * bytes earlier and is identical up to here:
 *
 *   00459FA6  8B 54 81 10 03 55 0C     mov edx,[ecx+eax*4+0x10]; ADD edx,[ebp+0x0C]
 *   00459FEB  8B 54 81 10 2B 55 0C     mov edx,[ecx+eax*4+0x10]; SUB edx,[ebp+0x0C]
 *
 * Stopping at the load matched both, and the first of the two is the one that hands out pickups.
 * Detouring that instead would have made every pickup a no-op while the cheat was on, which is the
 * opposite of unlimited ammunition and would have looked like a pickup bug. */
static const uint8_t MSK_USE_AMMO[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_USE_AMMO) == sizeof(MSK_USE_AMMO),
               "the use ammo pattern and its mask are different lengths");

/* 0x00459ECE, twenty four bytes: prologue, the null test, and the health load and subtract. */
static const uint8_t SIG_DAMAGE[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [record],0               */
    0x75, 0x02,                                      /* jnz +2                       */
    0xEB, 0x00,                                      /* jmp out                      */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[record]             */
    0x8B, 0x08,                                      /* mov ecx,[eax]     health     */
    0x2B, 0x4D, 0x08                                 /* sub ecx,[ebp+8]   the damage */
};
static const uint8_t MSK_DAMAGE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DAMAGE) == sizeof(MSK_DAMAGE),
               "the damage pattern and its mask are different lengths");

/* TEN, AND NOT FIVE, AND THE DIFFERENCE IS A CRASH.
 *
 * The trampoline copies these bytes verbatim and appends a jump past them; there is no length
 * disassembler anywhere in the shared code. So the size has to land on a real instruction boundary.
 * Both functions open:
 *
 *   55                      push ebp                 boundary at 0
 *   8B EC                   mov ebp,esp              boundary at 1
 *   83 3D xx xx xx xx 00    cmp [record],0           boundary at 3, and the next is at 10
 *
 * Five is inside that `cmp`. A trampoline built from it decodes as a compare against a wild address
 * assembled out of the jump this patch had just written, and the original is called on every path
 * where the cheat is OFF, so the game would have died on the first shot fired and the first hit
 * taken. Ten is the first boundary at or past the five bytes a jump needs. */
#define STATUS_PROLOGUE_SIZE 10u

/* Free camera, site one of two: the simulation pause flag.
 *
 * Every fly-mode attempt above fought the player's own state machine one function at a time
 * because the player kept simulating. FUN_0043e9f2, the per-frame pump, gates the entire
 * fixed-timestep substep driver on two cells - confirmed byte-for-byte against the running image:
 *
 *   0043ea13  83 3D 44134838 00     cmp [00881344],0        ; a movie/quit-adjacent state, untouched
 *   0043ea1a  75 13                 jnz past the substep call
 *   0043ea1c  83 3D 00000000 00     cmp [SIM PAUSE FLAG],0  ; THE CELL THIS FEATURE USES
 *   0043ea23  75 0A                 jnz past the substep call
 *   0043ea25  6A 00                 push 0
 *   0043ea27  E8 00000000           call FUN_004756fc       ; the whole substep loop
 *   0043ea2c  83 C4 04              add esp,4
 *
 * FUN_004756fc is the fixed-timestep driver: it is where every registered per-tick callback runs,
 * including FUN_0047582a's table, which is where the player task installs FUN_00447d38 (Plr_
 * RunPhases, the whole player simulation). A non-zero pause flag skips the call to FUN_004756fc
 * entirely, so nothing registered on it runs at all - not just the player, the whole world. That
 * is confirmed as the SAME cell the retail ESC pause menu sets
 * (gameplay_open_pause_menu at 0x0043FAB5 writes it before its own blocking menu loop and clears it
 * after), so this is not a guessed side door, it is the mechanism the game already uses to pause
 * itself. This feature only ever writes the flag directly - it does not call the two
 * task_broadcast_cmd notifications retail's own pause menu also makes (audio ducking and similar),
 * a deliberate, smaller freeze: enough to stop the world moving under a free camera, not a
 * reproduction of the retail pause experience.
 *
 * The second cmp's operand, not the first, is what this pattern exists to read - the first is kept
 * as fixed, literal bytes purely for the uniqueness it adds, the same way other patterns in this
 * file keep a neighbouring instruction literal without needing its value. Not a detour target:
 * nothing here is ever overwritten, only read once for the address of the flag. */
static const uint8_t SIG_SIM_PAUSE_GATE[] = {
    0x83, 0x3D, 0x44, 0x13, 0x88, 0x00, 0x00,        /* cmp [00881344],0                    */
    0x75, 0x00,                                      /* jnz +N                               */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [sim pause flag],0               */
    0x75, 0x00,                                      /* jnz +N                               */
    0x6A, 0x00,                                      /* push 0                               */
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the substep driver              */
    0x83, 0xC4, 0x04                                 /* add esp,4                            */
};
static const uint8_t MSK_SIM_PAUSE_GATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SIM_PAUSE_GATE) == sizeof(MSK_SIM_PAUSE_GATE),
               "the sim-pause-gate pattern and its mask are different lengths");
#define OFFSET_SIM_PAUSE_FLAG  0x0Bu   /* operand of the second cmp - the flag's own address */

/* Free camera, site two of two: the camera object pointer, and its per-frame update.
 *
 * SIG_CAMERA_VIEW is copied verbatim from enhanced_input's camera_sites.c (0x00418EDD, inside
 * updateCam's follow blend: `mov eax,[gView]; fld [eax+0x38]` - the previous camera yaw, read
 * immediately before the offset that feature adds to it). Same retail image, same bytes either
 * way, and that mod's own extensive cross-checking already established what they are; this file
 * resolves its own copy rather than reaching into another mod's DLL, the same independence every
 * other site in this file already keeps.
 *
 * updateCam itself, 0x00418544, is THE proven multi-tenant detour target of this whole project -
 * signature.h's own docs name it as chained by two DLLs already, and the chaining exists
 * specifically so a second detour on the same prologue does not have to scan for bytes the first
 * detour already overwrote. SIG_CAMERA_UPDATE and CAMERA_UPDATE_PROLOGUE_SIZE are copied from
 * camera_sites.c unchanged for exactly that reason: matching bytes, not just a matching address.
 *
 * WHERE THE CAMERA'S OWN POSITION LIVES, DISASSEMBLED DIRECTLY RATHER THAN TAKEN ON TRUST:
 *
 *   0041872f  MOV EAX,[gView]
 *   00418734  ADD EAX,0x14
 *   00418737  MOV ECX,[EBP-0x38] / MOV [EAX],ECX        ; anchor X  = view+0x14
 *   0041873c  MOV EDX,[EBP-0x34] / MOV [EAX+4],EDX       ; anchor Y  = view+0x18
 *   00418742  MOV ECX,[EBP-0x30] / MOV [EAX+8],ECX       ; anchor Z  = view+0x1c
 *   00418748  MOV EDX,[gView] / ADD EDX,0x34
 *   00418751  MOV EAX,[EBP-0x10] / MOV [EDX],EAX         ; euler.x   = view+0x34  (pitch, degrees)
 *   00418756  MOV ECX,[EBP-0xc] / MOV [EDX+4],ECX        ; euler.y   = view+0x38  (yaw, degrees)
 *   0041875c  MOV EAX,[EBP-8] / MOV [EDX+8],EAX          ; euler.z   = view+0x3c  (roll, untouched)
 *
 * written unconditionally, before the state (follow/fixed/world-fixed) dispatch that starts at
 * 0x004187ce even begins - camera_sites.c only ever traces the follow-state arm, so this is new.
 * The source, traced back further, is SetCamTarget (FUN_004184cc): the player's own per-tick
 * dispatch (FUN_00447d38 case 3) is one of its five call sites, feeding it the player's own
 * position. That is the actual coupling this feature breaks - not the state field, which changing
 * alone does nothing, since every state still re-derives a TARGET offset from the same anchor
 * every call. Freezing the simulation (the site above) stops SetCamTarget from ever being called
 * again, which stops the anchor's own feed cold; nothing then contends with a write made AFTER
 * calling the original updateCam, since this file's write runs strictly after the whole original
 * function - including its own second, later write to yaw alone at 0x00418fa1, part of the
 * follow-blend arm - has already finished. Roll (+0x3c) is left alone; a free camera has no use
 * for it and neither does anything reading euler.x/euler.y elsewhere. */
static const uint8_t SIG_CAMERA_VIEW[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                     /* mov eax,[gView]                    */
    0xD9, 0x40, 0x38,                                 /* fld [eax+0x38]  previous camera yaw */
    0xD9, 0x5D, 0xE0,
    0xD9, 0x45, 0xD8,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00                /* fadd [camera yaw offset]            */
};
static const uint8_t MSK_CAMERA_VIEW[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_CAMERA_VIEW) == sizeof(MSK_CAMERA_VIEW),
               "the camera-view pattern and its mask are different lengths");
#define OFFSET_CAMERA_VIEW_OBJECT   0x01u   /* operand of mov eax,[gView]                       */

/* 0x00418544, nine bytes: push ebp / mov ebp,esp / sub esp,0x88 - identical to camera_sites.c's
 * own CAMERA_UPDATE_PROLOGUE_SIZE, deliberately, since a mismatched prologue size on the SAME
 * chained site would misplace the trampoline for whichever DLL installs second. */
static const uint8_t SIG_CAMERA_UPDATE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x01,
    0x6A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x1C,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x4D, 0xC4
};
static const uint8_t MSK_CAMERA_UPDATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CAMERA_UPDATE) == sizeof(MSK_CAMERA_UPDATE),
               "the camera-update pattern and its mask are different lengths");
#define CAMERA_UPDATE_PROLOGUE_SIZE  9u

#define CAMERA_ANCHOR_X_OFFSET   0x14u
#define CAMERA_ANCHOR_Y_OFFSET   0x18u
#define CAMERA_ANCHOR_Z_OFFSET   0x1Cu
#define CAMERA_EULER_PITCH_OFFSET 0x34u
#define CAMERA_EULER_YAW_OFFSET   0x38u

#define DEG_TO_RAD               0.017453293f
#define FREECAM_MOVE_SPEED       12.0f    /* world units/second, a first guess like every other
                                            * tuned number in this project, pending a field round */
#define FREECAM_MOUSE_DEGREES_PER_COUNT 0.15f   /* degrees of turn per pixel of cursor delta */
#define FREECAM_PITCH_LIMIT      89.0f    /* degrees; kept short of vertical to avoid a gimbal flip */

typedef void (__cdecl *use_ammo_fn_t)(int32_t weapon_id, int32_t amount);
typedef void (__cdecl *damage_fn_t)(int32_t amount);
typedef void (__cdecl *camera_update_fn_t)(void);

typedef struct own_cheat {
    const char *name;
    bool        available;
    bool        on;
} own_cheat_t;

typedef struct cheats_own_state {
    bool                    installed;
    own_cheat_t             cheats[CHEATS_OWN_COUNT];
    detour_t                ammo_detour;
    detour_t                damage_detour;
    detour_t                camera_update_detour;
    use_ammo_fn_t           ammo_original;
    damage_fn_t             damage_original;
    camera_update_fn_t      camera_update_original;
    uintptr_t               camera_view_address;       /* address OF the camera object pointer;
                                                          * 0 = unresolved, free camera unavailable */
    uintptr_t               sim_pause_flag_address;     /* address of the sim-freeze cell; 0 =
                                                          * unresolved, free camera unavailable */
} cheats_own_state_t;

static cheats_own_state_t own_state;

/* ============================================================================================ */
/* The hooks. Each is the whole feature: ask, and either decline or hand on unchanged. */

static void __cdecl hook_use_ammo(int32_t weapon_id, int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].on) {
        return;
    }
    own_state.ammo_original(weapon_id, amount);
}

static void __cdecl hook_damage(int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].on) {
        return;
    }
    own_state.damage_original(amount);
}

/* GetAsyncKeyState reads the physical key regardless of which window has it, so without this the
 * player would drift while the game sits alt-tabbed in the background and something else entirely
 * is holding E or Q. Compares the foreground window's owning process to this one instead of
 * tracking a HWND of our own, which needs nothing set up anywhere else in this DLL. */
static bool is_game_foreground(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner_pid = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner_pid);
    return owner_pid == GetCurrentProcessId();
}

/* Free camera. Owned entirely by this file, seeded from wherever the camera already is the moment
 * the cheat switches on: nothing else may be trusted to hold still. */
static float freecam_x, freecam_y, freecam_z;
static float freecam_yaw, freecam_pitch;   /* degrees */
static bool  freecam_valid = false;
static bool  freecam_cursor_hidden = false;   /* true while THIS cheat owns the OS cursor */

static LARGE_INTEGER freecam_perf_frequency;
static LONGLONG      freecam_last_tick;
static POINT         freecam_cursor_anchor;

/* The exit hotkey. Free camera's own mouse look claims the cursor, which leaves both the dev panel
 * and the game's own pause menu unreachable - no cursor, no click, and Escape does not work either
 * while the simulation this cheat paused is what would normally answer it. Without a keyboard-only
 * way out, turning this cheat on is a one-way door. 0 = unbound; the panel's hotkey row (see
 * overlay_model.c) is how a key gets into this. */
static int32_t freecam_hotkey;
static bool    freecam_hotkey_was_down;

int32_t cheats_openphantom_freecam_hotkey(void)
{
    return freecam_hotkey;
}

void cheats_openphantom_freecam_set_hotkey(int32_t virtual_key)
{
    freecam_hotkey = virtual_key;
    freecam_hotkey_was_down = false;   /* do not treat the binding key's own release as an edge */
}

/* Chained: this file's own override runs strictly AFTER the original updateCam, on every DLL's
 * behalf whichever order they loaded in (see common/detour.h). Calling the original unconditionally,
 * before even looking at whether the cheat is on, is what keeps this a well-behaved link in that
 * chain rather than a break in it - enhanced_input's own free-look feature is chained on this exact
 * same site and must keep running underneath this one regardless of what this cheat is doing. */
static void __cdecl hook_camera_update(void)
{
    void **view_slot;
    void  *view;

    own_state.camera_update_original();

    if (!own_state.cheats[CHEATS_OWN_FREECAM].on) {
        if (freecam_valid) {
            /* Falling edge: hand the world back. The camera itself needs no un-write - the very
             * next updateCam call recomputes it from the player the ordinary way, since nothing
             * here touches state (+0x00) or anything else the follow logic reads. */
            if (own_state.sim_pause_flag_address != 0) {
                *(int32_t *)own_state.sim_pause_flag_address = 0;
            }
            if (freecam_cursor_hidden) {
                ShowCursor(TRUE);
                freecam_cursor_hidden = false;
            }
            freecam_valid = false;
        }
        return;
    }

    if (own_state.camera_view_address == 0) {
        return;
    }
    view_slot = (void **)own_state.camera_view_address;
    view = *view_slot;
    if (view == NULL) {
        return;
    }

    if (!freecam_valid) {
        /* Rising edge: seed from wherever the camera already is, so switching on never snaps the
         * view, and pause the simulation - see SIG_SIM_PAUSE_GATE's own comment for why this one
         * flag is enough to stop the player and the whole world simulating out from under a
         * camera that is no longer looking through the player's own eyes. */
        freecam_x     = *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET);
        freecam_y     = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET);
        freecam_z     = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET);
        freecam_pitch = *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET);
        freecam_yaw   = *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET);
        if (own_state.sim_pause_flag_address != 0) {
            *(int32_t *)own_state.sim_pause_flag_address = 1;
        }
        QueryPerformanceFrequency(&freecam_perf_frequency);
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            freecam_last_tick = now.QuadPart;
        }
        GetCursorPos(&freecam_cursor_anchor);
        ShowCursor(FALSE);
        freecam_cursor_hidden = true;
        freecam_valid = true;
    }

    /* Panel-aware mouse handling was tried here (skip look/movement and leave the cursor alone
     * while the dev panel is open) and REVERTED after a field-reported regression: closing the
     * panel left freecam_yaw climbing on its own every frame with no mouse input at all, and
     * looking unresponsive at the same time - both symptoms of the OS cursor being unable to reach
     * this cheat's anchor point, most likely enhanced_resolution's own ClipCursor confinement
     * (focus_guard.c) interacting with the re-anchor on panel close. Reverted to the simple,
     * previously-working shape rather than chase that live.
     *
     * That attempt was solving the wrong problem anyway. The actual complaint was not "I want the
     * panel usable while flying", it was "I have no way OUT of free camera once the mouse is
     * claimed" - the panel is unreachable without a working cursor and Escape does nothing while
     * this cheat has the simulation paused, so a player who did not already know a hotkey existed
     * was simply stuck. The exit hotkey below is the actual fix: keyboard only, needs no cursor at
     * all, and does not touch how the mouse behaves while flying. Whether the panel itself is ever
     * usable mid-flight is a separate question, unresolved, and no longer the one that mattered. */
    if (freecam_hotkey != 0 && is_game_foreground()) {
        bool hotkey_down = (GetAsyncKeyState(freecam_hotkey) & 0x8000) != 0;

        if (hotkey_down && !freecam_hotkey_was_down) {
            freecam_hotkey_was_down = hotkey_down;
            own_state.cheats[CHEATS_OWN_FREECAM].on = false;
            return;   /* the falling-edge cleanup above runs on the NEXT call to this hook */
        }
        freecam_hotkey_was_down = hotkey_down;
    }

    if (is_game_foreground()) {
        float dt;
        POINT cursor_now;

        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            dt = (freecam_perf_frequency.QuadPart > 0)
                     ? (float)((double)(now.QuadPart - freecam_last_tick) /
                               (double)freecam_perf_frequency.QuadPart)
                     : 0.0f;
            freecam_last_tick = now.QuadPart;
        }
        if (dt < 0.0f || dt > 0.25f) {
            dt = 0.0f;   /* a stall, or the very first tick since the rising edge above */
        }

        /* Mouse look: cursor-delta polling rather than enhanced_input's own raw-input thread.
         * Reusing that thread would mean either reaching into another mod's DLL (this project's
         * mods do not depend on each other) or duplicating its hard-won shim workaround for
         * WMAIN.EXE's own application-compatibility fix (see raw_mouse.c's own header comment) -
         * both worse than the jitter this simpler path accepts for what is a debug camera, not
         * competitive aim. */
        if (GetCursorPos(&cursor_now)) {
            long dx = cursor_now.x - freecam_cursor_anchor.x;
            long dy = cursor_now.y - freecam_cursor_anchor.y;

            if (dx != 0 || dy != 0) {
                /* Yaw (X) field-tested inverted from the first build and flipped, and stayed
                 * flipped - confirmed correct. Pitch (Y) was flipped in that same round on the
                 * assumption both axes were backward together; field testing showed that guess
                 * wrong - X's flip was right, Y's undid a pitch sign that was already correct - so
                 * this puts pitch back to its first-build sign while keeping yaw's fix. */
                freecam_yaw   -= (float)dx * FREECAM_MOUSE_DEGREES_PER_COUNT;
                freecam_pitch -= (float)dy * FREECAM_MOUSE_DEGREES_PER_COUNT;
                if (freecam_pitch > FREECAM_PITCH_LIMIT) {
                    freecam_pitch = FREECAM_PITCH_LIMIT;
                }
                if (freecam_pitch < -FREECAM_PITCH_LIMIT) {
                    freecam_pitch = -FREECAM_PITCH_LIMIT;
                }
                SetCursorPos(freecam_cursor_anchor.x, freecam_cursor_anchor.y);
            }
        }

        /* WASD along the full 3D view direction (so looking up while holding W climbs, exactly
         * the free-fly feel the pitch-redirect attempt on the PLAYER tried and failed at - the
         * difference is that nothing here is fighting a third-person camera's own resting angle,
         * because nothing here is a third-person camera anymore, it is this cheat's own camera).
         * E/Q add a world-vertical on top, independent of pitch, for straight up/down without
         * needing to look at the sky or the floor first.
         *
         * FIELD-TESTED, WRONG, THEN FIXED FROM THE ENGINE'S OWN CODE RATHER THAN A SECOND GUESS.
         * The first build of this used fwd_x=+sin(yaw)*cos(pitch) and right_y=-sin(yaw), a self-
         * consistent guess with no independent evidence behind it. Field test: "W doesn't always
         * go forward" - correct near yaw=0, where sin(0)=0 hides the error, and increasingly wrong
         * as yaw grew. Rather than guess a second time, this is now read out of the engine itself:
         * FUN_004181b9 (the function that builds the render eye position every frame) calls
         * FUN_0047cfbc, a generic euler-degrees-to-rotation-matrix utility used at roughly fifty
         * sites across the binary, whose matrix decodes to
         *
         *     right   = ( cos(yaw),             sin(yaw),            0          )
         *     forward = (-sin(yaw)*cos(pitch),   cos(yaw)*cos(pitch), sin(pitch))
         *
         * at zero roll (this camera's roll, +0x3c, is never written by anything this file found).
         * Independently cross-checked against the game's OWN built-in debug free-cam (arrow keys,
         * FUN_004190c1 -> FUN_00418349): its translation is worldX += dx*cos(yaw)-dy*sin(yaw),
         * worldY += dx*sin(yaw)+dy*cos(yaw), which for pure forward (dx=0,dy=1) gives exactly
         * (-sin(yaw), cos(yaw)), matching the formula above at pitch=0. Two independent sites
         * agree, which is what "confirmed" means in this file rather than "guessed once more". */
        {
            float forward = 0.0f;
            float strafe  = 0.0f;
            float vertical = 0.0f;

            if ((GetAsyncKeyState('W') & 0x8000) != 0) { forward  += 1.0f; }
            if ((GetAsyncKeyState('S') & 0x8000) != 0) { forward  -= 1.0f; }
            if ((GetAsyncKeyState('D') & 0x8000) != 0) { strafe   += 1.0f; }
            if ((GetAsyncKeyState('A') & 0x8000) != 0) { strafe   -= 1.0f; }
            if ((GetAsyncKeyState('E') & 0x8000) != 0) { vertical += 1.0f; }
            if ((GetAsyncKeyState('Q') & 0x8000) != 0) { vertical -= 1.0f; }

            if (forward != 0.0f || strafe != 0.0f || vertical != 0.0f) {
                float yaw_rad   = freecam_yaw * DEG_TO_RAD;
                float pitch_rad = freecam_pitch * DEG_TO_RAD;
                float cos_yaw   = cosf(yaw_rad);
                float sin_yaw   = sinf(yaw_rad);
                float cos_pitch = cosf(pitch_rad);
                float sin_pitch = sinf(pitch_rad);
                float fwd_x     = -sin_yaw * cos_pitch;
                float fwd_y     = cos_yaw * cos_pitch;
                float fwd_z     = sin_pitch;
                float right_x   = cos_yaw;
                float right_y   = sin_yaw;
                float planar    = sqrtf(forward * forward + strafe * strafe);
                float scale     = (planar > 1.0f) ? (1.0f / planar) : 1.0f;   /* no diagonal boost */
                float speed     = FREECAM_MOVE_SPEED * dt;

                freecam_x += (fwd_x * forward + right_x * strafe) * scale * speed;
                freecam_y += (fwd_y * forward + right_y * strafe) * scale * speed;
                freecam_z += fwd_z * forward * scale * speed + vertical * speed;
            }
        }
    }

    *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET)    = freecam_x;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET)    = freecam_y;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET)    = freecam_z;
    *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET) = freecam_pitch;
    *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET)   = freecam_yaw;
}

/* ============================================================================================ */

static bool install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const void *hook, detour_t *detour, size_t prologue_size,
                        const char *what)
{
    uintptr_t site = signature_find_detour_target(bytes, mask, size, prologue_size);

    if (site == 0) {
        log_warning("%s did not resolve, so that cheat is offered as unavailable rather than as a "
                    "row that ticks and does nothing", what);
        return false;
    }
    if (!detour_install(detour, site, hook, prologue_size)) {
        log_warning("%s at %08X could not be detoured, that cheat stays unavailable",
                    what, (unsigned)site);
        return false;
    }
    log_info("%s hooked at %08X", what, (unsigned)site);
    return true;
}

/* Free camera needs both sites - the pause flag and the camera object pointer - to mean anything:
 * a camera that could roam but never stopped the world moving underneath it is not this feature,
 * and neither is a pause with nothing to look through. A partial resolve here is not offered as
 * half a feature; either both resolve or the cheat says why and stays unavailable. */
static bool install_freecam(void)
{
    uintptr_t pause_site;
    uintptr_t view_site;
    uintptr_t update_site;
    uint32_t  pause_flag_address = 0;
    uint32_t  view_address = 0;

    pause_site = signature_find_unique(SIG_SIM_PAUSE_GATE, MSK_SIM_PAUSE_GATE,
                                       sizeof SIG_SIM_PAUSE_GATE);
    if (pause_site == 0 ||
        !memory_read_u32(pause_site + OFFSET_SIM_PAUSE_FLAG, &pause_flag_address) ||
        !memory_is_inside_image(pause_flag_address, sizeof(int32_t))) {
        log_warning("free camera: the simulation pause gate did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    view_site = signature_find_unique(SIG_CAMERA_VIEW, MSK_CAMERA_VIEW, sizeof SIG_CAMERA_VIEW);
    if (view_site == 0 ||
        !memory_read_u32(view_site + OFFSET_CAMERA_VIEW_OBJECT, &view_address) ||
        !memory_is_inside_image(view_address, sizeof(void *))) {
        log_warning("free camera: the camera object site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    update_site = signature_find_detour_target(SIG_CAMERA_UPDATE, MSK_CAMERA_UPDATE,
                                               sizeof SIG_CAMERA_UPDATE,
                                               CAMERA_UPDATE_PROLOGUE_SIZE);
    if (update_site == 0) {
        log_warning("free camera: the camera update site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }
    if (!detour_install(&own_state.camera_update_detour, update_site,
                        (const void *)&hook_camera_update, CAMERA_UPDATE_PROLOGUE_SIZE)) {
        log_warning("free camera: the camera update site at %08X could not be detoured, that "
                    "cheat stays unavailable", (unsigned)update_site);
        return false;
    }

    own_state.camera_update_original = (camera_update_fn_t)own_state.camera_update_detour.original;
    own_state.sim_pause_flag_address = (uintptr_t)pause_flag_address;
    own_state.camera_view_address    = (uintptr_t)view_address;
    log_info("free camera: pause flag at %08X, camera object pointer at %08X, update chained at "
             "%08X - WASD moves along the view, mouse looks, E/Q move vertically",
             (unsigned)pause_flag_address, (unsigned)view_address, (unsigned)update_site);
    return true;
}

bool cheats_openphantom_install(void)
{
    if (own_state.installed) {
        return true;
    }

    own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].name = "Unlimited ammunition";
    own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].name = "Unlimited health";
    own_state.cheats[CHEATS_OWN_NO_FOG].name = "No fog";
    own_state.cheats[CHEATS_OWN_FREECAM].name = "Free camera";

    if (install_one(SIG_USE_AMMO, MSK_USE_AMMO, sizeof SIG_USE_AMMO,
                    (const void *)&hook_use_ammo, &own_state.ammo_detour, STATUS_PROLOGUE_SIZE,
                    "the ammunition spend")) {
        own_state.ammo_original = (use_ammo_fn_t)own_state.ammo_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available = true;
    }

    if (install_one(SIG_DAMAGE, MSK_DAMAGE, sizeof SIG_DAMAGE,
                    (const void *)&hook_damage, &own_state.damage_detour, STATUS_PROLOGUE_SIZE,
                    "the damage application")) {
        own_state.damage_original = (damage_fn_t)own_state.damage_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available = true;
    }

    if (install_freecam()) {
        own_state.cheats[CHEATS_OWN_FREECAM].available = true;
    }

    /* A different shape - see cheats_no_fog.h - so it owns its own state and this only asks it. */
    (void)cheats_no_fog_install();

    own_state.installed = true;

    /* All four stand for the life of the process whether used or not: each detour costs one
     * comparison per call while off, and the fog tick costs one comparison per frame while off.
     * That is the price of being able to switch any of them from the panel at any moment. If none
     * resolved there is nothing to switch, and the caller says so once. */
    return own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available ||
           own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available ||
           own_state.cheats[CHEATS_OWN_FREECAM].available ||
           cheats_no_fog_is_available();
}

const char *cheats_openphantom_name(cheats_own_id_t id)
{
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return NULL;
    }
    return own_state.cheats[id].name;
}

bool cheats_openphantom_is_available(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_is_available();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].available;
}

bool cheats_openphantom_is_on(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_is_on();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].on;
}

bool cheats_openphantom_toggle(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_toggle();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT ||
        !own_state.cheats[id].available) {
        return false;
    }
    /* Turning free camera ON without an exit hotkey bound is a one-way door: the mouse it claims
     * is also what the dev panel and the game's own pause menu need, and there is no way to close
     * this cheat again without one. The authoritative gate lives here rather than only in the
     * panel's own row.available, so nothing that reaches this function directly can bypass it. */
    if (id == CHEATS_OWN_FREECAM && !own_state.cheats[id].on && freecam_hotkey == 0) {
        return false;
    }
    own_state.cheats[id].on = !own_state.cheats[id].on;
    return own_state.cheats[id].on;
}
