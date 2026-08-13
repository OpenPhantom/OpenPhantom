/* cheats_openphantom.c: unlimited ammunition and unlimited health.
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

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

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

typedef void (__cdecl *use_ammo_fn_t)(int32_t weapon_id, int32_t amount);
typedef void (__cdecl *damage_fn_t)(int32_t amount);

typedef struct own_cheat {
    const char *name;
    bool        available;
    bool        on;
} own_cheat_t;

typedef struct cheats_own_state {
    bool          installed;
    own_cheat_t   cheats[CHEATS_OWN_COUNT];
    detour_t      ammo_detour;
    detour_t      damage_detour;
    use_ammo_fn_t ammo_original;
    damage_fn_t   damage_original;
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

/* ============================================================================================ */

static bool install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const void *hook, detour_t *detour, const char *what)
{
    uintptr_t site = signature_find_detour_target(bytes, mask, size, STATUS_PROLOGUE_SIZE);

    if (site == 0) {
        log_warning("%s did not resolve, so that cheat is offered as unavailable rather than as a "
                    "row that ticks and does nothing", what);
        return false;
    }
    if (!detour_install(detour, site, hook, STATUS_PROLOGUE_SIZE)) {
        log_warning("%s at %08X could not be detoured, that cheat stays unavailable",
                    what, (unsigned)site);
        return false;
    }
    log_info("%s hooked at %08X", what, (unsigned)site);
    return true;
}

bool cheats_openphantom_install(void)
{
    if (own_state.installed) {
        return true;
    }

    own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].name = "Unlimited ammunition";
    own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].name = "Unlimited health";

    if (install_one(SIG_USE_AMMO, MSK_USE_AMMO, sizeof SIG_USE_AMMO,
                    (const void *)&hook_use_ammo, &own_state.ammo_detour,
                    "the ammunition spend")) {
        own_state.ammo_original = (use_ammo_fn_t)own_state.ammo_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available = true;
    }

    if (install_one(SIG_DAMAGE, MSK_DAMAGE, sizeof SIG_DAMAGE,
                    (const void *)&hook_damage, &own_state.damage_detour,
                    "the damage application")) {
        own_state.damage_original = (damage_fn_t)own_state.damage_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available = true;
    }

    own_state.installed = true;

    /* Both detours stand for the life of the process whether the cheats are used or not, and both
     * cost one comparison per call while they are off. That is the price of being able to switch
     * them from the panel at any moment, and it is the same bargain every other feature here makes.
     * If neither resolved there is nothing to switch, and the caller says so once. */
    return own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available ||
           own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available;
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
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].available;
}

bool cheats_openphantom_is_on(cheats_own_id_t id)
{
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].on;
}

bool cheats_openphantom_toggle(cheats_own_id_t id)
{
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT ||
        !own_state.cheats[id].available) {
        return false;
    }
    own_state.cheats[id].on = !own_state.cheats[id].on;
    return own_state.cheats[id].on;
}
