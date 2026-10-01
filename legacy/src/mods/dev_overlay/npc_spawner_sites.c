/* npc_spawner_sites.c: see npc_spawner_sites.h.
 *
 * ==============================================================================================
 * The routine, and the two places it is found from
 *
 * spawn_actor, 0x00437250 in retail WMAIN.EXE, cdecl, three arguments: the placement record, its
 * index in the level's directory, and a script index or -1 for the record's own. It answers the
 * new actor or NULL. Its opening is the eligibility test, and the test names the world record's
 * model table twice through the world pointer:
 *
 *   00437250  55 8B EC 51 56 57             push ebp / mov ebp,esp / push ecx,esi,edi
 *   00437256  8B 45 08                      mov eax,[ebp+8]              the record
 *   00437259  83 B8 A8 00 00 00 00          cmp dword ptr [eax+0xA8],0   modelIndex < 0?
 *   00437260  7C 31                         jl  to the NULL answer
 *   00437262  8B 4D 08                      mov ecx,[ebp+8]
 *   00437265  8B 15 <g_level>               mov edx,[g_level]
 *   0043726B  8B 81 A8 00 00 00             mov eax,[ecx+0xA8]
 *   00437271  3B 82 E8 01 00 00             cmp eax,[edx+0x1E8]          >= nModel?
 *   00437277  7D 1A                         jge -> NULL
 *   00437279  8B 4D 08                      mov ecx,[ebp+8]
 *   0043727C  8B 91 A8 00 00 00             mov edx,[ecx+0xA8]
 *   00437282  A1 <g_level>                  mov eax,[g_level]
 *   00437287  8B 88 F4 01 00 00             mov ecx,[eax+0x1F4]          ppModel
 *   0043728D  83 3C 91 00                   cmp dword ptr [ecx+edx*4],0  a model there?
 *
 * The world pointer is the cell both operands name, 0x008A0060 in retail, and the two have to
 * agree. The routine is also found from the one caller that runs every level, the activation
 * scan, which is where this file learnt the argument order:
 *
 *   00437224  6A FF                         push -1                      the record's own script
 *   00437226  8B 4D F4 51                   push [ebp-0xC]               the placement index
 *   0043722A  8B 55 F8 52                   push [ebp-8]                 the record
 *   0043722E  E8 <rel32>                    call spawn_actor
 *   00437233  83 C4 0C                      add esp,0xC
 *   00437236  85 C0 74 0D                   test eax,eax / je
 *   0043723A  8B 45 F8                      mov eax,[ebp-8]
 *   0043723D  C7 80 C8 00 00 00 01 00 00 00 mov [eax+0xC8],1             spawnState = live
 *
 * The call's target has to be the routine the prologue pattern found, or neither is trusted.
 * The scan's loop head, 0x00437195 and 0x004371A6, is where the directory's count and pointer
 * come from: `cmp edx,[ecx+0x204]` and `mov ecx,[eax+0x20C]`.
 *
 * The way back is enemy_delete, 0x00437850, cdecl (actor, reason), the one routine every actor
 * leaves through: a script's remove, a despawn by range, a corpse culled, the level's close.
 * Its opening tests the hosted flag and turns any reason into the host release for a hosted
 * actor:
 *
 *   00437850  55 8B EC 83 EC 0C             push ebp / mov ebp,esp / sub esp,0xC
 *   00437856  8B 45 08                      mov eax,[ebp+8]              the actor
 *   00437859  8B 48 14                      mov ecx,[eax+0x14]           stateFlags
 *   0043785C  81 E1 00 20 00 00             and ecx,0x2000               hosted?
 *   00437862  85 C9 74 0C                   test ecx,ecx / je
 *   00437866  E8 <rel32>                    call player_resume
 *   0043786B  C7 45 0C 03 00 00 00          mov [ebp+0xC],3              reason = host release
 *
 * and its body, at 0x0043790E, asserts the reveal ids against the directory bound with the line
 * number 0xF8B, which is the second place it is known from: that assert has to lie inside the
 * 422 bytes the prologue opens. Reason 1, a script's remove, marks the record spent, reveals
 * what the record says a death reveals (nothing, in a copy), clears the live word, and gives
 * the effects, the body and the pool slot back.
 *
 */
#include "npc_spawner_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>
#include <string.h>

/* spawn_actor's opening, through the second model table load; the two world pointer operands
 * are masked and read back. */
static const uint8_t SIG_SPAWN_ENTRY[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x56, 0x57,                 /* the frame, then ecx, esi, edi pushed */
    0x8B, 0x45, 0x08,                                   /* mov eax,[ebp+8]                     */
    0x83, 0xB8, 0xA8, 0x00, 0x00, 0x00, 0x00,           /* cmp dword ptr [eax+0xA8],0          */
    0x7C, 0x31,                                         /* jl                                  */
    0x8B, 0x4D, 0x08,                                   /* mov ecx,[ebp+8]                     */
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,                 /* mov edx,[g_level]                   */
    0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00,                 /* mov eax,[ecx+0xA8]                  */
    0x3B, 0x82, 0xE8, 0x01, 0x00, 0x00,                 /* cmp eax,[edx+0x1E8]                 */
    0x7D, 0x1A,                                         /* jge                                 */
    0x8B, 0x4D, 0x08,                                   /* mov ecx,[ebp+8]                     */
    0x8B, 0x91, 0xA8, 0x00, 0x00, 0x00,                 /* mov edx,[ecx+0xA8]                  */
    0xA1, 0x00, 0x00, 0x00, 0x00,                       /* mov eax,[g_level]                   */
    0x8B, 0x88, 0xF4, 0x01, 0x00, 0x00,                 /* mov ecx,[eax+0x1F4]                 */
    0x83, 0x3C, 0x91, 0x00                              /* cmp dword ptr [ecx+edx*4],0         */
};
static const uint8_t MSK_SPAWN_ENTRY[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SPAWN_ENTRY == sizeof MSK_SPAWN_ENTRY, "mask length");

/* This pattern starts at a function HEAD, and heads are what other modules detour. In this
 * build multiplayer hulls spawn_actor with a five byte prologue, and both multiplayer and
 * view_distance_fix hull enemy_delete with six. A pattern that matches from the head
 * therefore finds nothing as soon as one of those DLLs has loaded first, and the load order
 * is alphabetical and states itself nowhere.
 *
 * So the head is DECLARED rather than matched: signature_find_detour_target skips those
 * bytes, anchors on the tail and accepts a head that is either the authored bytes or a
 * branch (src/common/signature.h), which is what overlay_sites.c already does for the four
 * heads it shares. Nothing else changes: the address is still proved a second time by the
 * activation scan's own call, and the two world pointer operands sit at 23 and 51, well
 * past the prologue. Calling a head another module has detoured is no different from any
 * other call to it: the jump at the head is the hook's, and it passes on. */
#define SPAWN_ENTRY_PROLOGUE 5u
#define ENTRY_LEVEL_OPERAND_A 23u   /* the imm32 of `mov edx,[g_level]` */
#define ENTRY_LEVEL_OPERAND_B 51u   /* the imm32 of `mov eax,[g_level]`, must be the same */

/* The activation scan's call, 0x00437224: the three pushes, the call, and the spawn state write
 * that follows a success. */
static const uint8_t SIG_SPAWN_CALL[] = {
    0x6A, 0xFF,                                         /* push -1                             */
    0x8B, 0x4D, 0xF4, 0x51,                             /* mov ecx,[ebp-0xC]; push ecx         */
    0x8B, 0x55, 0xF8, 0x52,                             /* mov edx,[ebp-8]; push edx           */
    0xE8, 0x00, 0x00, 0x00, 0x00,                       /* call spawn_actor                    */
    0x83, 0xC4, 0x0C,                                   /* add esp,0xC                         */
    0x85, 0xC0, 0x74, 0x0D,                             /* test eax,eax; je                    */
    0x8B, 0x45, 0xF8,                                   /* mov eax,[ebp-8]                     */
    0xC7, 0x80, 0xC8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00   /* mov [eax+0xC8],1          */
};
static const uint8_t MSK_SPAWN_CALL[] = {
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SPAWN_CALL == sizeof MSK_SPAWN_CALL, "mask length");
#define CALL_REL32_OFFSET 11u   /* the call's displacement */
#define CALL_NEXT_OFFSET  15u   /* the instruction after it, which the displacement is from */

/* enemy_delete's opening, through the reason rewrite; the call's displacement is masked. */
static const uint8_t SIG_DELETE_ENTRY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,                 /* push ebp; mov ebp,esp; sub esp,0xC  */
    0x8B, 0x45, 0x08,                                   /* mov eax,[ebp+8]                     */
    0x8B, 0x48, 0x14,                                   /* mov ecx,[eax+0x14]                  */
    0x81, 0xE1, 0x00, 0x20, 0x00, 0x00,                 /* and ecx,0x2000                      */
    0x85, 0xC9, 0x74, 0x0C,                             /* test ecx,ecx; je                    */
    0xE8, 0x00, 0x00, 0x00, 0x00,                       /* call player_resume                  */
    0xC7, 0x45, 0x0C, 0x03, 0x00, 0x00, 0x00            /* mov [ebp+0xC],3                     */
};
static const uint8_t MSK_DELETE_ENTRY[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DELETE_ENTRY == sizeof MSK_DELETE_ENTRY, "mask length");

/* A head as well, and this one is hulled twice over: see SPAWN_ENTRY_PROLOGUE. Six bytes,
 * which is what both hulls declare for it. The assert inside the body is not a head and is
 * still matched as it stands. */
#define DELETE_ENTRY_PROLOGUE 6u

/* The reveal assert inside it, `cmp [ebp-0xC],0x100 / jl / push 0xF8B`. */
static const uint8_t SIG_DELETE_ASSERT[] = {
    0x81, 0x7D, 0xF4, 0x00, 0x01, 0x00, 0x00,           /* cmp dword ptr [ebp-0xC],0x100       */
    0x7C, 0x1B,                                         /* jl                                  */
    0x68, 0x8B, 0x0F, 0x00, 0x00                        /* push 0xF8B                          */
};
static const uint8_t MSK_DELETE_ASSERT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DELETE_ASSERT == sizeof MSK_DELETE_ASSERT, "mask length");
#define DELETE_SIZE          422u

/* The way back. Not a condition of the spawner: with it unresolved the remove row reads
 * unavailable and everything else stands. */
static npc_delete_fn_t resolve_delete(void)
{
    uintptr_t entry  = signature_find_detour_target(SIG_DELETE_ENTRY, MSK_DELETE_ENTRY,
                                                   sizeof SIG_DELETE_ENTRY,
                                                   DELETE_ENTRY_PROLOGUE);
    uintptr_t assert_site = signature_find_unique(SIG_DELETE_ASSERT, MSK_DELETE_ASSERT,
                                                  sizeof SIG_DELETE_ASSERT);

    if (entry == 0 || assert_site <= entry || assert_site >= entry + DELETE_SIZE) {
        log_warning("npc spawner: the delete routine did not resolve (entry %08X, its assert "
                    "%08X), so spawned actors cannot be removed from the panel", (unsigned)entry,
                    (unsigned)assert_site);
        return NULL;
    }
    log_info("npc spawner: enemy_delete at %08X removes what was spawned", (unsigned)entry);
    return (npc_delete_fn_t)entry;
}

bool npc_spawner_sites_resolve(npc_spawner_sites_t *out)
{
    uintptr_t entry;
    uintptr_t call;
    uint32_t  operand_a = 0;
    uint32_t  operand_b = 0;
    uint32_t  rel32 = 0;

    memset(out, 0, sizeof *out);
    entry = signature_find_detour_target(SIG_SPAWN_ENTRY, MSK_SPAWN_ENTRY,
                                        sizeof SIG_SPAWN_ENTRY, SPAWN_ENTRY_PROLOGUE);
    call  = signature_find_unique(SIG_SPAWN_CALL, MSK_SPAWN_CALL, sizeof SIG_SPAWN_CALL);
    if (entry == 0 || call == 0) {
        log_warning("npc spawner: the spawn routine did not resolve (entry %08X, caller %08X), "
                    "so the group's rows are unavailable", (unsigned)entry, (unsigned)call);
        return false;
    }
    if (!memory_read_u32(call + CALL_REL32_OFFSET, &rel32) ||
        (uintptr_t)(call + CALL_NEXT_OFFSET + rel32) != entry) {
        log_warning("npc spawner: the activation scan at %08X calls %08X, not the routine found "
                    "at %08X, so the group's rows are unavailable", (unsigned)call,
                    (unsigned)(call + CALL_NEXT_OFFSET + rel32), (unsigned)entry);
        return false;
    }
    if (!memory_read_image_cell(entry + ENTRY_LEVEL_OPERAND_A, sizeof(void *), &operand_a) ||
        !memory_read_u32(entry + ENTRY_LEVEL_OPERAND_B, &operand_b) || operand_a != operand_b) {
        log_warning("npc spawner: the two world pointer operands in %08X disagree (%08X vs "
                    "%08X), so the group's rows are unavailable", (unsigned)entry,
                    (unsigned)operand_a, (unsigned)operand_b);
        return false;
    }
    out->level_pointer = (void *volatile *)(uintptr_t)operand_a;
    out->spawn         = (npc_spawn_fn_t)entry;
    out->caller        = call;
    out->delete_actor  = resolve_delete();
    return true;
}
