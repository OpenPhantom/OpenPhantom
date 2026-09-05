/* cheats_npc_damage.c: invincible NPCs and one-shot NPCs, two answers to the same fifteen bytes.
 *
 * Both decide what happens at enemy_receiveDamage's single health write, so they share one detour
 * rather than taking a signature each. Split out of cheats_openphantom.c; nothing changed in the
 * move. */
#include "cheats_openphantom.h"
#include "cheats_internal.h"

#include "sim_pause.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004338EC  enemy_receiveDamage: the NPC health write ----------------------------------- *
 * Found hunting for the enemy-side counterpart to unlimited health, starting from dismemberment.c's
 * own DEATH GATE (0x0043707D), which is reached only when an NPC's health, at character record
 * +0x38, has fallen to zero or below, established there by the retail compare at 0x437070. That
 * pinned the FIELD; this pins the WRITE. Decompiling enemy_receiveDamage (0x00433803, named for the
 * six call sites Ghidra's own xrefs find into it, all message-dispatch arms) shows exactly one line
 * that ever changes it: `*(int*)(param_1+0x38) = *(int*)(param_1+0x38) - local_18;`. local_18 is
 * the damage this call computed (a flat 3, doubled to 6 for a severable-limb hit, the same doubling
 * dismemberment.c's own header already documents at 0x4338B3, a few instructions earlier in this
 * same function). Disassembled, that line is five back-to-back three-byte instructions, an exact
 * 15-byte block with no rel32 and nothing environment-dependent in it:
 *
 *   004338EC  8B 55 08     mov edx,[ebp+8]      victim (character*), this function's own param_1
 *   004338EF  8B 42 38     mov eax,[edx+0x38]   health
 *   004338F2  2B 45 EC     sub eax,[ebp-0x14]   minus local_18, the computed damage
 *   004338F5  8B 4D 08     mov ecx,[ebp+8]
 *   004338F8  89 41 38     mov [ecx+0x38],eax   health -= damage, written back
 *
 * Traced forward to the end of the function (0x00433985), NOTHING after this block ever reads EAX,
 * ECX or EDX left over from it; every later use of the victim record reloads it fresh from
 * [ebp+8], and the first flag-testing instruction after it (FCOMP/FNSTSW at 0x004338FE) sets its
 * own flags rather than reading the SUB's. That is what makes this block safe to detour as a whole
 * and either skip entirely or replace outright, rather than needing to preserve anything about how
 * it executed. Unlike updateCam's chained detour above, which must run the original underneath
 * every time, this site's own hook is free to decide whether the trampoline runs at all. */
static const uint8_t SIG_NPC_DAMAGE_APPLY[] = {
    0x8B, 0x55, 0x08,             /* mov edx,[ebp+8]      victim (character*)            */
    0x8B, 0x42, 0x38,             /* mov eax,[edx+0x38]   health                         */
    0x2B, 0x45, 0xEC,             /* sub eax,[ebp-0x14]   minus the computed damage      */
    0x8B, 0x4D, 0x08,             /* mov ecx,[ebp+8]                                     */
    0x89, 0x41, 0x38,             /* mov [ecx+0x38],eax   health -= damage, written back */
    0xD9, 0x45, 0xF8               /* fld [ebp-8]  the next instruction, kept only for the extra
                                     * uniqueness/chaining margin every other detour site in this
                                     * file also carries past its own prologue; never read for a
                                     * value */
};
/* Fifteen, not eighteen: the trampoline may only copy what the detour target's own site comment
 * proves is safe to relocate, and the three FLD bytes above are trailing context for
 * signature_find_detour_target's own chaining check, not part of what gets overwritten. */
#define NPC_DAMAGE_PROLOGUE_SIZE 15u


/* Invincible NPCs and one-shot NPCs, sharing SIG_NPC_DAMAGE_APPLY's own site rather than needing
 * one signature each: both are decisions about the same fifteen bytes, "does the write happen, and
 * with what value", so one detour answers both. Where hook_camera_update above must always run the
 * original underneath it (that site is chained, other DLLs' features depend on it happening), this
 * one is not chained by anything and its own site comment already proves nothing downstream reads
 * EAX/ECX/EDX or the SUB's flags, so this hook is free to skip the original three-instruction
 * write outright rather than only being able to run alongside it.
 *
 * A naked trampoline rather than a call-site redirect like hook_use_ammo/hook_damage above: those
 * two detour a CALL instruction, so the hook can simply BE the function and choose whether to call
 * `original`. This site is a straight-line block in the middle of enemy_receiveDamage's own body,
 * not a call, so what gets overwritten is regular code and the hook must be entered by JMP with the
 * surrounding registers intact, the same shape dismemberment.c's own death-gate hook already uses,
 * extended here with a choice of WHERE to resume: normal play calls the trampoline (the original
 * three instructions, unmodified); either cheat sets npc_damage_skip and resumes at
 * npc_damage_continue instead, immediately past the write, having already decided the outcome
 * itself. on_npc_damage() communicates that choice through the static flag rather than a return
 * value in EAX, because popad below would overwrite EAX with whatever it held before the call and
 * erase any answer left in a register. */
static void  *npc_damage_trampoline;   /* the original 15 bytes, replayed when neither cheat is on */
static void  *npc_damage_continue;     /* site + NPC_DAMAGE_PROLOGUE_SIZE, resumed when one skips it */
static bool   npc_damage_skip;

/* Character record fields the one shot cheat needs, all of them read by diagnostics in the running
 * game as well. stateFlags carries the bit that says the actor's own script is responsible for its
 * death; when it is clear, enemy_onContact's tail writes the death state itself. */
#define NPC_STATE_FLAGS_OFFSET   0x14u
#define NPC_STATE_OFFSET         0x20u
#define NPC_BODY_OFFSET          0x34u
#define NPC_BODY_CLASS_OFFSET    0x04u
#define NPC_SCRIPT_OWNS_DEATH    0x10000u
#define NPC_STATE_FADEOUT           12u

/* Who dealt the damage, on the ATTACKING object rather than on the victim. bapObj+0x08 is the
 * side a contact came from, and 1 is the player. shot_spawn writes it into every projectile it
 * creates from its own last argument, so a bolt carries the class of whoever fired it, and the
 * engine itself tests it against 1 in three places to give the player different ammunition. A
 * sabre cut arrives with the player body as the attacker, which carries the same class. */
#define NPC_ATTACKER_ARGUMENT    0x0Cu
#define NPC_SHOOTER_CLASS_OFFSET 0x08u
#define SHOOTER_CLASS_PLAYER        1

/* WHOSE SHOT WAS THAT. Without this the cheat is not "the player one shots NPCs", it is "every
 * source of damage in the game is lethal": a droid firing at another droid kills it outright, and
 * so does a stray bolt that happens to catch Qui-Gon or Jar Jar, which can end an escort without
 * anything appearing to have gone wrong. The victim is the only thing the hook used to read, and
 * the attacker was sitting in the next argument the whole time.
 *
 * A NULL attacker is not the player. The engine passes 0 for damage that came from no object at
 * all, which is how a fall is delivered. */
static bool damage_came_from_the_player(const char *frame_pointer)
{
    const char *attacker = *(const char *const *)(frame_pointer + NPC_ATTACKER_ARGUMENT);

    if (attacker == NULL) {
        return false;
    }
    return *(const int32_t *)(attacker + NPC_SHOOTER_CLASS_OFFSET) == SHOOTER_CLASS_PLAYER;
}

static void __cdecl on_npc_damage(char *frame_pointer)
{
    void *victim = *(void **)(frame_pointer + 0x08);   /* [ebp+8], enemy_receiveDamage's param_1 */

    npc_damage_skip = false;
    if (victim == NULL) {
        return;
    }
    /* Invincible wins if both happen to be on at once: refusing the hit outright is the more
     * obviously correct answer than a hit that is simultaneously "took no damage" and "died". */
    if (own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].on) {
        npc_damage_skip = true;      /* the write below never runs at all; health is untouched */
        return;
    }
    if (own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].on &&
        damage_came_from_the_player(frame_pointer)) {
        /* <=0 is what the death gate this function feeds (0x00437070, see dismemberment.c's own
         * DEATH GATE comment) tests for.
         *
         * THIS IS NOT INDISTINGUISHABLE FROM ORDINARY LETHAL DAMAGE, and this comment used to say
         * it was. It is indistinguishable to the GATE, which only asks whether health reached zero.
         * It is not indistinguishable to a script that gates its own death on a health BAND. The
         * scrapyard machine in Mos Espa asks for health at or below 900 of 999 while the player is
         * within two units, and runs its own explosion when it sees that. Ordinary damage walks
         * health down through the band and the script fires. One store of zero steps over the band
         * entirely, so the script never sees a value inside it.
         *
         * What that costs is visible in the game. When the engine takes the death instead, an actor
         * whose model has no death animation completes the death state in a single tick and lands in
         * the corpse state, which turns its collision off and leaves it drawn. It stays there for
         * twenty minutes of level time, solid to look at and walked straight through, and its script
         * never runs again because the interpreter only runs in the active state. Killed by hand
         * that same machine explodes correctly; killed by this cheat it became a permanent ghost.
         *
         * So the cheat cleans up after itself. If the script already owns the death, the 0x10000 bit
         * is set and everything it authored still happens, including its explosion, so nothing here
         * touches it. If the engine is about to take the death, this claims it instead and hands the
         * actor to the fade out state, which the engine finishes on its own: alpha decays, the actor
         * asks to be removed, and it is freed. That is a clean disappearance rather than a ghost. */
        uint32_t state_flags = *(const uint32_t *)((const char *)victim + NPC_STATE_FLAGS_OFFSET);

        *(int32_t *)((char *)victim + 0x38) = 0;

        if ((state_flags & NPC_SCRIPT_OWNS_DEATH) == 0) {
            void *body = *(void *const *)((const char *)victim + NPC_BODY_OFFSET);

            *(uint32_t *)((char *)victim + NPC_STATE_FLAGS_OFFSET) =
                state_flags | NPC_SCRIPT_OWNS_DEATH;
            if (body != NULL) {
                /* Collision off, which is what every death in this engine does first and what the
                 * fade state does not do for itself. */
                *(uint32_t *)((char *)body + NPC_BODY_CLASS_OFFSET) = 0;
            }
            *(uint32_t *)((char *)victim + NPC_STATE_OFFSET) = NPC_STATE_FADEOUT;
        }
        npc_damage_skip = true;
    }
}

/* WHY EVERY NAKED DETOUR BELOW SAVES THE x87 STACK.
 *
 * pushad saves the eight general purpose registers and pushfd saves EFLAGS. Neither touches the
 * FPU, and this is a 1999 build with no SSE: every float the engine holds lives in the eight deep
 * x87 register stack. All six detours in this section cut into the MIDDLE of engine functions, so
 * the engine has live x87 values at the moment we take control, and the C handler we call is
 * compiled by a modern MSVC that is free to use the FPU for whatever it likes.
 *
 * The danger is not an unbalanced handler, compiler output balances itself. It is that x87 has
 * only eight registers and no spill. If the engine is holding six values here and the handler
 * pushes three more, the ninth push does not fault: it marks the register indefinite. The engine
 * then carries on with a NaN where a coordinate used to be, and it surfaces somewhere else
 * entirely, frames later, as a camera that snaps to nowhere or a fall that never registers. Two
 * of these six sit in camera code and three in ground contact, which is exactly where the engine
 * is most likely to be holding a full stack.
 *
 * fnsave writes the whole x87 state out and reinitialises the unit, so the handler starts on a
 * clean FPU; frstor puts the engine's stack, tags and control word back exactly. 112 rather than
 * the 108 the state needs keeps esp on a 16 byte boundary. Every one of these is an edge rather
 * than per frame traffic, so the cost is irrelevant. diagnostics/diag_world.c's hook_ai_opcode
 * has done this since it was written; these six had not, which was an oversight rather than a
 * decision.
 */
static void __declspec(naked) hook_npc_damage(void)
{
    __asm {
        pushad
        pushfd
        sub    esp, 112
        fnsave [esp]
        push ebp
        call on_npc_damage
        add  esp, 4
        frstor [esp]
        add    esp, 112
        popfd
        popad
        cmp  byte ptr npc_damage_skip, 0
        jnz  npc_damage_resume_after
        jmp  dword ptr [npc_damage_trampoline]
    npc_damage_resume_after:
        jmp  dword ptr [npc_damage_continue]
    }
}

/* Both NPC cheats live behind this one detour; see on_npc_damage's own comment for why one site
 * answers both. */
void install_npc_damage(void)
{
    if (!cheats_install_one(SIG_NPC_DAMAGE_APPLY, NULL, sizeof SIG_NPC_DAMAGE_APPLY,
                     (const void *)&hook_npc_damage, &own_state.npc_damage_detour,
                     NPC_DAMAGE_PROLOGUE_SIZE, "the NPC health write")) {
        return;
    }
    npc_damage_trampoline = own_state.npc_damage_detour.original;
    npc_damage_continue   = (void *)(own_state.npc_damage_detour.target + NPC_DAMAGE_PROLOGUE_SIZE);
    own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].available = true;
    own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].available   = true;
}

