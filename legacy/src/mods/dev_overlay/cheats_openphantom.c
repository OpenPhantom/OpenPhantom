/* cheats_openphantom.c: unlimited ammunition, unlimited health, no fog, invincible NPCs, one-shot
 * NPCs, giant player, tiny player, and free camera. The third cheat this project adds, no fog, is
 * a different enough shape - see cheats_no_fog.h - that it lives in its own file and this one only
 * dispatches CHEATS_OWN_NO_FOG's queries to it. Free camera is documented next to its own two
 * signatures below rather than up here, the two sites it needs each carrying their own byte
 * evidence at the point they are used - invincible NPCs and one-shot NPCs are the same, documented
 * next to SIG_NPC_DAMAGE_APPLY, and giant/tiny player next to SIG_THING_DRAW.
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
#include "cheats_internal.h"

#include "sim_pause.h"

#include "cheats_no_fog.h"
#include "cheats_original_actions.h"

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

/* --- 0x0040FE70  rdThing_Draw: every drawn object's own render call, including the player ------
 * SUB ESP,0x48 / MOV ECX,0xC / PUSH EBP / MOV EBP,[ESP+0x50] - no push-ebp;mov-ebp,esp frame at
 * all: this is one of the frame-pointer-omitted /O2 translation units this game's own toolchain
 * analysis (see engine/engine-identification.md) already found this build mixes with /Od per
 * source file. Still plain __cdecl(thing*, matrix[12]) at the ABI boundary regardless of how the
 * callee itself addresses its own params internally - the caller pushes two dwords and cleans its
 * own stack afterward (ADD ESP,8), the same shape hook_use_ammo/hook_damage above already detour,
 * so a normally-typed hook works here too, no naked-asm trick needed.
 *
 * Confirmed via xrefs to have exactly two callers: FUN_00417930's own switch(kind==1) arm - the
 * path every ordinary object takes, the player included - and emitter_drawParticles, for particle
 * sprites. Detouring the function's own entry rather than either call site catches both without
 * needing two patches; the particle path is untouched regardless, since this hook only ever acts
 * when the incoming thing is the player's own (see hook_thing_draw below).
 *
 * THE SCALE TRICK IS ALREADY IN THE RETAIL GAME. A few instructions past this prologue, gated
 * behind a specific cheat-flag slot and a hardcoded four-character model-name match - neither of
 * which this feature depends on, touches, or needs to fully identify - retail applies a flat 3.0x
 * scale to this exact incoming matrix, via a small, self-contained "compose a diagonal scale into
 * this transform" utility that this file calls directly rather than reimplementing: not something
 * built for this feature, something the shipped game already trusts to do this correctly for its
 * own reasons. See THING_DRAW_TO_SCALE_CALL_OFFSET below for how its address is found - not its
 * own independent signature, deliberately. */
static const uint8_t SIG_THING_DRAW[] = {
    0x83, 0xEC, 0x48,                    /* sub esp,0x48                */
    0xB9, 0x0C, 0x00, 0x00, 0x00,        /* mov ecx,0xC                 */
    0x55,                                /* push ebp                    */
    0x8B, 0x6C, 0x24, 0x50               /* mov ebp,[esp+0x50]          */
};
#define THING_DRAW_PROLOGUE_SIZE 8u

/* The matrix-scale composer at 0x0047E185, found as a CALL rel32 operand rather than matched by
 * its own signature. A hand-transcribed byte pattern for a thirteen-instruction function, derived
 * from disassembly text rather than raw bytes, is exactly the kind of thing that fails silently -
 * signature_find_unique just answers zero, indistinguishable from "this build does not have it" -
 * and a first attempt at exactly that here did fail silently. This is lower-risk: the CALL sits at
 * a fixed, confirmed offset from rdThing_Draw's own entry (0x67 bytes - measured directly off two
 * addresses already trusted for the detour above: entry 0x0040FE70, call instruction 0x0040FED7),
 * well past the eight bytes this file's own detour ever overwrites, so it reads correctly whether
 * this file's detour installed first or chained onto an existing one. The opcode byte is checked
 * before the operand is trusted, rather than assuming the offset is still right on some future
 * build - a wrong offset landing on a non-CALL byte would read a plausible but meaningless address,
 * and calling through that blind would be worse than refusing outright. */
#define THING_DRAW_TO_SCALE_CALL_OFFSET 0x67u

/* The player record pointer's own global cell - cross-confirmed by input_freeze.c/
 * framerate_stats.c as pPlayer, and independently here by disassembling Plr_AutoAim, which reads
 * [0x4B5220] then [that+0xC] and passes the result straight into bapobj_setNodeYaw's own bapObj
 * argument with no further resolution - so despite the "hActor" name at that offset, it is already
 * a raw bapObj pointer, not a handle needing translation through some pool lookup first. */
#define PLAYER_ACTOR_OFFSET    0x0Cu   /* bapObj*, confirmed via Plr_AutoAim as above            */
#define THING_OBJECT_OFFSET    0x9Cu   /* bapObj -> rdThing*, same OBJECT_THING dismemberment.c
                                        * already confirmed; redefined locally rather than shared,
                                        * this file does not otherwise depend on that one */

#define GIANT_PLAYER_SCALE 3.0f    /* the exact factor retail's own hardcoded special case uses */
#define TINY_PLAYER_SCALE  0.35f   /* a first guess for "small but still visible and playable" -
                                    * no retail precedent for this direction, unlike giant */



/* ============================================================================================ */
/* The hooks. Each is the whole feature: ask, and either decline or hand on unchanged. */

static void __cdecl hook_use_ammo(int32_t weapon_id, int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].on) {
        return;
    }
    own_state.ammo_original(weapon_id, amount);
}

void __cdecl hook_damage(int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].on) {
        return;
    }
    own_state.damage_original(amount);
}

static int32_t __cdecl hook_thing_draw(void *thing, float *matrix)
{
    if (own_state.scale_matrix_compose != NULL && thing != NULL) {
        void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

        if (player_record != NULL) {
            void *player_actor = *(void **)((char *)player_record + PLAYER_ACTOR_OFFSET);

            if (player_actor != NULL &&
                *(void **)((char *)player_actor + THING_OBJECT_OFFSET) == thing) {
                float scale_vec[3];

                if (own_state.cheats[CHEATS_OWN_GIANT_PLAYER].on) {
                    scale_vec[0] = scale_vec[1] = scale_vec[2] = GIANT_PLAYER_SCALE;
                    own_state.scale_matrix_compose(matrix, scale_vec);
                } else if (own_state.cheats[CHEATS_OWN_TINY_PLAYER].on) {
                    scale_vec[0] = scale_vec[1] = scale_vec[2] = TINY_PLAYER_SCALE;
                    own_state.scale_matrix_compose(matrix, scale_vec);
                }
            }
        }
    }
    return own_state.thing_draw_original(thing, matrix);
}

/* Jump boost. Calling the original FIRST and unconditionally is what makes this a boost and not a
 * reimplementation: the jump still happens exactly as retail built it, guard check and all, and
 * only once it has already decided to jump and written its own velocity does this cheat touch
 * anything, scaling whatever value is now sitting at +0xB4 - the fallback constant or the
 * per-character table value, whichever path the original just took. See SIG_JUMP_ENTRY's own
 * comment for why this needs two hooks rather than one. */
/* ============================================================================================ */

/* THE one state record, declared extern in cheats_internal.h and defined here because this file is
 * the one that runs the install pass which fills it.
 *
 * It was briefly defined in the header instead, which compiles and is wrong: a static in a header
 * gives every file that includes it a private copy, so the cheats that moved out wrote their
 * availability into records the panel never reads and each other's hooks looked uninstalled. It
 * showed up as rows greyed out for no reason, with the log saying a site was hooked a line above
 * the warning that it was not. */
cheats_own_state_t own_state;

bool cheats_install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
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

/* Both player-scale cheats live behind this one detour - see hook_thing_draw's own comment for why
 * one site answers both. Needs its own matrix-scale composer resolved first: a cheat that could
 * intercept the draw call but had nothing to scale with would not be half a feature, it would be a
 * detour doing nothing, so this refuses the whole thing rather than offering that. */
static void install_player_scale(void)
{
    uintptr_t call_site;
    uint8_t   opcode;
    uint32_t  rel32;
    uintptr_t scale_target;

    if (!cheats_install_one(SIG_THING_DRAW, NULL, sizeof SIG_THING_DRAW, (const void *)&hook_thing_draw,
                     &own_state.thing_draw_detour, THING_DRAW_PROLOGUE_SIZE,
                     "the object render call")) {
        return;
    }

    call_site = own_state.thing_draw_detour.target + THING_DRAW_TO_SCALE_CALL_OFFSET;
    if (!memory_read_u8(call_site, &opcode) || opcode != 0xE8 ||
        !memory_read_u32(call_site + 1, &rel32)) {
        log_warning("the matrix-scale composer's own call site at %08X did not check out, "
                    "giant/tiny player stay unavailable", (unsigned)call_site);
        return;
    }
    scale_target = call_site + 5 + rel32;
    if (!memory_is_executable_range(scale_target, 1)) {
        log_warning("the matrix-scale composer resolved to %08X, which is not executable, "
                    "refused", (unsigned)scale_target);
        return;
    }

    own_state.scale_matrix_compose = (scale_matrix_fn_t)scale_target;
    own_state.thing_draw_original  = (thing_draw_fn_t)own_state.thing_draw_detour.original;
    own_state.cheats[CHEATS_OWN_GIANT_PLAYER].available = true;
    own_state.cheats[CHEATS_OWN_TINY_PLAYER].available  = true;
    log_info("giant/tiny player: object render call hooked at %08X, matrix-scale composer at "
             "%08X", (unsigned)own_state.thing_draw_detour.target, (unsigned)scale_target);
}

bool cheats_openphantom_install(void)
{
    if (own_state.installed) {
        return true;
    }

    own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].name = "Unlimited ammunition";
    own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].name = "Unlimited health";
    own_state.cheats[CHEATS_OWN_NO_FOG].name = "No fog";
    own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].name = "Invincible NPCs";
    own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].name = "One-shot NPCs (your damage)";
    own_state.cheats[CHEATS_OWN_GIANT_PLAYER].name = "Giant player";
    own_state.cheats[CHEATS_OWN_TINY_PLAYER].name = "Tiny player";
    own_state.cheats[CHEATS_OWN_JUMP_BOOST].name = "Jump boost";
    own_state.cheats[CHEATS_OWN_FREECAM].name = "Free camera";

    if (cheats_install_one(SIG_USE_AMMO, MSK_USE_AMMO, sizeof SIG_USE_AMMO,
                    (const void *)&hook_use_ammo, &own_state.ammo_detour, STATUS_PROLOGUE_SIZE,
                    "the ammunition spend")) {
        own_state.ammo_original = (use_ammo_fn_t)own_state.ammo_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available = true;
    }

    if (cheats_install_one(SIG_DAMAGE, MSK_DAMAGE, sizeof SIG_DAMAGE,
                    (const void *)&hook_damage, &own_state.damage_detour, STATUS_PROLOGUE_SIZE,
                    "the damage application")) {
        own_state.damage_original = (damage_fn_t)own_state.damage_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available = true;
    }

    install_npc_damage();
    install_player_scale();
    install_jump_boost();
    install_fall_punishment_immunity();

    if (install_freecam()) {
        own_state.cheats[CHEATS_OWN_FREECAM].available = true;
    }

    /* A different shape - see cheats_no_fog.h - so it owns its own state and this only asks it. */
    (void)cheats_no_fog_install();

    own_state.installed = true;

    /* All six stand for the life of the process whether used or not: each detour costs one
     * comparison per call while off, and the fog tick costs one comparison per frame while off.
     * That is the price of being able to switch any of them from the panel at any moment. If none
     * resolved there is nothing to switch, and the caller says so once. */
    return own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available ||
           own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available ||
           own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].available ||
           own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].available ||
           own_state.cheats[CHEATS_OWN_GIANT_PLAYER].available ||
           own_state.cheats[CHEATS_OWN_TINY_PLAYER].available ||
           own_state.cheats[CHEATS_OWN_JUMP_BOOST].available ||
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

float cheats_openphantom_jump_boost_scale(void)
{
    return own_state.jump_boost_scale;
}

void cheats_openphantom_jump_boost_set_scale(float scale)
{
    if (scale < JUMP_BOOST_SCALE_MIN) {
        scale = JUMP_BOOST_SCALE_MIN;
    }
    if (scale > JUMP_BOOST_SCALE_MAX) {
        scale = JUMP_BOOST_SCALE_MAX;
    }
    own_state.jump_boost_scale = scale;
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
    if (id == CHEATS_OWN_FREECAM && !own_state.cheats[id].on && cheats_openphantom_freecam_hotkey() == 0) {
        return false;
    }
    own_state.cheats[id].on = !own_state.cheats[id].on;
    /* Giant and tiny player are mutually exclusive: turning one on turns the other off, rather
     * than leaving a row reading ON that has no visible effect because hook_thing_draw's own
     * precedence (giant checked first) is the one actually deciding what gets applied. */
    if (own_state.cheats[id].on) {
        if (id == CHEATS_OWN_GIANT_PLAYER) {
            own_state.cheats[CHEATS_OWN_TINY_PLAYER].on = false;
        } else if (id == CHEATS_OWN_TINY_PLAYER) {
            own_state.cheats[CHEATS_OWN_GIANT_PLAYER].on = false;
        }
    }
    return own_state.cheats[id].on;
}

/* --- "Skip to next level": the same signal a level's own exit trigger sends -------------------- *
 *
 * main_game_movie_sequencer_loop (0x0043EC42) is the campaign driver: it loads each level by name
 * off its own per-level table, keyed by an index at DAT_0088136c, then busy-polls
 * `while (DAT_00881368 == 2) sys_frame();` for as long as that level is actually running. When that
 * loop exits it reads DAT_00881368 again and, if it is 3, treats it as "level complete": broadcasts
 * task 6, increments its own level index, and loads the next entry off its own table - exactly the
 * housekeeping a real level exit needs, done by the driver itself.
 *
 * DAT_00881368 is written to 3 from exactly one place in the whole binary: FUN_00429880, case
 * (param_2 == 1). FUN_00429880 has exactly one caller anywhere: the level script interpreter
 * (FUN_00433d0b, dialogue_anim_fix.c's own "opcode 0x202" function), script opcode 0x606,
 * sub-command 1 - `FUN_00429880(param_1, *local_c, local_c[1], local_c[2])` when `*local_c == 1`.
 * That is almost certainly the literal command a level's own exit trigger/volume issues.
 *
 * This writes DAT_00881368 = 3 directly - the same value that one script command produces - rather
 * than calling FUN_00429880 (which needs a live script-context pointer this panel does not have) or
 * campaign_loadLevel (which needs the NEXT level's own path string, something
 * main_game_movie_sequencer_loop already works out for itself from its own table). The cell itself
 * is cheats_original_actions.c's own OP_CREDITS_VAR, exposed read-only from there rather than
 * re-resolved here: it is genuinely the same single global cell "gurshick" already found, not a
 * second one that happens to share a value.
 *
 * FIELD-UNTESTED. "gurshick" (the SAME cell, value 1) is confirmed to misbehave when triggered from
 * this panel rather than the retail console's own blocking loop - see
 * cheats_original_actions.c's own resolve_credits() comment. Value 3 takes a structurally different
 * path with no credits-sequence startup involved, so that specific failure mode should not apply,
 * but that is reasoning, not a field result. */
bool cheats_openphantom_end_level_is_available(void)
{
    return cheats_original_actions_level_status_cell() != NULL;
}

bool cheats_openphantom_end_level_invoke(void)
{
    volatile int32_t *cell = cheats_original_actions_level_status_cell();

    if (cell == NULL) {
        return false;
    }
    *cell = 3;
    return true;
}
