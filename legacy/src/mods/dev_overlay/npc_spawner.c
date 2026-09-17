/* npc_spawner.c: see npc_spawner.h.
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
 *   00437260  7C 31                         jl  -> NULL
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
 * ==============================================================================================
 * The record, and why a copy is spawned and never the level's own
 *
 * A placement is 0xD8 bytes and the engine reads these of it (the activation scan's gate chain,
 * 0x004371B8..0x0043721C, and spawn_actor's copies): +0x00 flags, bit 0 the scan's own filter and
 * bit 0x2000 a parked record the player's body hosts, which never gets a body of its own; +0x24
 * the starting yaw in degrees; +0x28 the range; +0xA8 the model index; +0xAC the position; +0xB8
 * a name of up to fifteen characters; +0xC8 the spawn state; +0xD0 the live actor's address,
 * written by spawn_actor and cleared by every delete; +0xD4 how many other placements a death
 * reveals, followed by their ids.
 *
 * The engine keeps one live actor per record and the death bookkeeping is written back into the
 * record: a second actor on the level's own record would take over its live word and its spawn
 * state, and the first one's death would reveal the placements the second one's should. So the
 * chosen record is copied into a record of this file's own, one per spawn from a ring of them,
 * with the reveal count zeroed and the position and yaw replaced; the engine writes its
 * bookkeeping into the copy and the level's directory never notices. The copy is handed over
 * with the ORIGINAL's index, because the engine stores that index in the actor and a save game
 * writes it out; a save made with spawned actors alive restores each as one more actor of its
 * source placement, which is the nearest thing to the truth a save can carry.
 *
 * The ring holds twice as many records as spawns may be alive, and a record whose live word is
 * still set is skipped, so a live actor's record is never rewritten under it: the engine clears
 * the live word on every delete path, and the level's close deletes every actor, so the ring
 * empties with the level. The live words are also what the cap counts and what the spacing
 * reads: each live spawn's actor carries its position at +0xD0, and a new spawn takes the first
 * of a fan of spots ahead of the player that no live spawn stands within a body's width of, so
 * a spawn key held down lays them out in an arc and does not stack them in one place for the
 * engine's push layer to sort out. The name is copied by strcpy inside the engine into a twelve
 * byte field; a copied record carries its source's name, so the copy overflows only where the
 * level's own placement already did.
 *
 * ==============================================================================================
 * Where the copy is put
 *
 * Three units ahead of the player along their heading, at the player's own height, turned
 * around to face them. The heading is the player record's +0x2A0 in degrees (0x0044A6B2 writes
 * it; enhanced_input reads it at the same offset) and the engine's own forward is
 * (-sin, cos) of it: the move phase writes the facing pair that way, and Plr_PointAhead, the
 * routine the engine itself uses to put a point ahead of the player, steps by the same pair. The
 * point may be inside a wall or another actor; the engine's own push layer sorts that out on the
 * first tick, as it does for a placement authored too close to one.
 *
 * What a level offers, and from which placement each kind is copied, is npc_census.c's; the
 * archive's creatures beyond those, and the record written for one, npc_foreign.c's; the
 * scripts a copy runs are spawn_scripts.c's; this file raises, keeps and removes.
 *
 * SIZE NOTE: over the 600 mark since a copy began carrying a script of its own. One seam is
 * left, if it grows again: the spacing, the fan of spots and the two tests on it, which shares
 * nothing with the raising and would stand on its own.
 */
#include "npc_spawner.h"

#include "actor_loader.h"
#include "cheats_internal.h"
#include "npc_census.h"
#include "npc_foreign.h"
#include "player_slot.h"
#include "spawn_scripts.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"
#include "common/text.h"

#include <math.h>
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

#define SPAWN_AHEAD_UNITS  2.5f
#define SPAWN_RING         (2u * NPC_SPAWNER_ALIVE_MAX)
#define SPOT_TAKEN_UNITS   0.6f    /* a spawn this near a spot stands on it: half a body's width */
#define ACTOR_POSITION     0xD0u   /* the live actor's position, vec3 */
#define ACTOR_SCRIPT       0x28u   /* the live actor's script, pScript */
#define ACTOR_IP           0x2Cu   /* its instruction pointer; 0 restarts the script */
#define CLASS_KILLABLE     2       /* the one class the contact handler hands damage to */
#define CLASS_ALLY         1       /* the player's own side: what a helper is raised as, so the
                                    * other helpers are not its enemies (target kind 2 is the
                                    * nearest class 2) and the enemies' fire goes past it */
#define CLASS_TRIPOD_GUN   4       /* the class the Use key mounts (Plr_GroundActions asks
                                    * bapobj_findObject for class 4 ahead of the player) */
#define TRIPOD_FILE        "tripod.baf"
/* A shooter's reload floor for the explosive shot kinds, seconds: the engine rearms after
 * rand() * the record's fire interval and fires no sooner than half a second after, so a copy
 * of a placement the level paced by other means (the bazooka man, a small interval and a script
 * that waits between shots) fired rockets at a blaster's rate (played 2026-09-16). A bolt of
 * any kind keeps the record's cadence, as the droid fighter's own script fires every tick it
 * can; a rocket, a tank shell, a grenade, a thermal detonator, an energy ball or a fireball
 * (the engine's own shot handler table) waits up to four seconds. */
#define RELOAD_HEAVY       4.0f
static bool shot_kind_is_heavy(uint16_t kind)
{
    return kind == 5 || kind == 8 || kind == 9 || kind == 10 || kind == 12 || kind == 23 ||
           kind == 30;
}
/* A flyer, a record whose move mode is 2 (a hover, the gunboat and the probe droid) or 4 to 6
 * (pitching toward where it goes, the STAP, the birds and the fish), has its height tracked
 * toward its move destination's z (move_trackZ), and a flyer sent at the player comes down to
 * their feet, as the level's gunboat does when its script sends it at them (played 2026-09-16:
 * spawned high, flew down, followed along the floor). So a flyer's scripts fly it to
 * destination 0, the placement's authored position, which for a copy is its ring record, and
 * npc_spawner_tick keeps that record this far over the player's feet every frame, about head
 * height; the copy spawns there too. Three units was a good bit too high (played 2026-09-16).
 * Mode 3 is not a flyer: the levels place walkers with no collision on it, trees, power-ups and
 * the droid fighter, and a copy of the fighter hung in the air until it was told apart (played
 * 2026-09-16). */
#define FLYER_HEIGHT       1.8f
static bool move_mode_flies(int32_t mode)
{
    return mode == 2 || (mode >= 4 && mode <= 6);
}
#define ACTOR_HEALTH       0x38u   /* the live actor's health, i32 */
#define ACTOR_STATE_FLAGS  0x14u   /* the live actor's stateFlags, from the record's flags */
#define ACTOR_STATE        0x20u   /* the live actor's state, 1 active, 0x10 parked */
#define ACTOR_BODY         0x34u   /* the live actor's object, pBody */
#define BODY_POSITION      0x18u   /* the object's position, vec3 */

/* Where a live spawn is and what state it is in when it is removed, for the log: the actor's
 * own position, its body's, its state and flags. A body whose position still reads 0 0 0 was
 * never ticked, the shape of a spawn removed with the menu never closed. */
static void describe_actor(const uint8_t *actor, char *out, uint32_t out_size)
{
    float    pos[3] = { 0.0f, 0.0f, 0.0f };
    float    body_pos[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t body = 0;
    int32_t  state = -1;
    uint32_t flags = 0;
    bool     body_read;

    (void)memory_try_read((uintptr_t)actor + ACTOR_POSITION, pos, sizeof pos);
    (void)memory_try_read((uintptr_t)actor + ACTOR_STATE, &state, sizeof state);
    (void)memory_try_read((uintptr_t)actor + ACTOR_STATE_FLAGS, &flags, sizeof flags);
    body_read = memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) && body != 0 &&
                memory_try_read((uintptr_t)body + BODY_POSITION, body_pos, sizeof body_pos);
    if (body_read) {
        text_format(out, out_size, "at %.1f %.1f %.1f, body %08X at %.1f %.1f %.1f, state %d, "
                    "flags %08X", (double)pos[0], (double)pos[1], (double)pos[2], body,
                    (double)body_pos[0], (double)body_pos[1], (double)body_pos[2], state, flags);
    } else {
        text_format(out, out_size, "at %.1f %.1f %.1f, no body, state %d, flags %08X",
                    (double)pos[0], (double)pos[1], (double)pos[2], state, flags);
    }
}
#define DEGREES_TO_RADIANS (3.14159265f / 180.0f)

/* The spots tried, ahead of the player along their heading and to its right (negative is the
 * left): a file straight ahead, one behind the other, then a file a body's width to the left,
 * then one to the right, eight deep each. A fan around the player was the first shape, and in
 * a tight room its sides and its back put copies in the walls (played 2026-09-16); ahead is
 * where the player is looking and is the one direction they can see is clear. The files are
 * as close as bodies stand, a rank a body's width wide and a step deep, so the block fits a
 * corridor (played 2026-09-17: at a body and a half across the block was wider than it need
 * be). Two bodies the engine finds too close it pushes apart on their first tick. */
#define SPOT_FILE_STEP  1.0f    /* one behind the other, a body's width apart */
#define SPOT_FILE_APART 1.3f    /* the left and right files off the middle one */
#define SPOT_FILE_DEPTH 8u      /* copies in each file */
#define SPOT_FILES      3u      /* the middle, the left, the right, in that order */
#define SPOT_COUNT      (SPOT_FILE_DEPTH * SPOT_FILES)
_Static_assert(SPOT_COUNT >= NPC_SPAWNER_ALIVE_MAX, "a spot for every copy the cap allows");

/* Spot i: file i / depth, place i % depth. */
static void spot(uint32_t i, float *across, float *ahead)
{
    static const float FILE_ACROSS[SPOT_FILES] = { 0.0f, -SPOT_FILE_APART, SPOT_FILE_APART };

    *across = FILE_ACROSS[i / SPOT_FILE_DEPTH];
    *ahead  = SPAWN_AHEAD_UNITS + (float)(i % SPOT_FILE_DEPTH) * SPOT_FILE_STEP;
}

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
#define DELETE_REASON_REMOVE 1     /* kDelete_Removed, a script's remove */

typedef void (__cdecl *delete_fn_t)(void *actor, int32_t reason);

static void resolve_delete(void);

typedef void *(__cdecl *spawn_fn_t)(void *record, int32_t index, int32_t script_override);

static struct {
    spawn_fn_t       spawn;           /* NULL until both findings agree */
    delete_fn_t      delete_actor;    /* NULL until its two findings agree; the row is then off */
    void *volatile  *level_pointer;   /* [g_level] */
    uint8_t          ring[SPAWN_RING][PLACE_SIZE];
    uint8_t          script[SPAWN_RING][SPAWN_SCRIPT_BYTES];   /* each copy's own script */
    uint32_t         ring_next;
    uint32_t         spawned;
    int32_t          health[SPAWN_RING];   /* each copy's health as last seen, for the log */
    uint32_t         behaviour;
} st;

uint32_t npc_spawner_behaviour(void)
{
    return st.behaviour;
}

void npc_spawner_set_behaviour(uint32_t behaviour)
{
    st.behaviour = (behaviour < SPAWN_BEHAVIOUR_COUNT) ? behaviour : 0u;
}

bool npc_spawner_install(void)
{
    uintptr_t entry;
    uintptr_t call;
    uint32_t  operand_a = 0;
    uint32_t  operand_b = 0;
    uint32_t  rel32 = 0;

    entry = signature_find_unique(SIG_SPAWN_ENTRY, MSK_SPAWN_ENTRY, sizeof SIG_SPAWN_ENTRY);
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
    st.level_pointer = (void *volatile *)(uintptr_t)operand_a;
    st.spawn         = (spawn_fn_t)entry;
    resolve_delete();
    (void)actor_loader_install();   /* without it the list is the level's own files alone */
    log_info("npc spawner: spawn_actor at %08X, called from %08X, world pointer at %08X; the "
             "group lists the loaded level's actor files and raises a copy of a placement %.1f "
             "units ahead of the player and on, up to %u alive at once", (unsigned)entry,
             (unsigned)call, (unsigned)operand_a, (double)SPAWN_AHEAD_UNITS,
             NPC_SPAWNER_ALIVE_MAX);
    return true;
}

/* The way back. Not a condition of the spawner: with it unresolved the remove row reads
 * unavailable and everything else stands. */
static void resolve_delete(void)
{
    uintptr_t entry  = signature_find_unique(SIG_DELETE_ENTRY, MSK_DELETE_ENTRY,
                                             sizeof SIG_DELETE_ENTRY);
    uintptr_t assert_site = signature_find_unique(SIG_DELETE_ASSERT, MSK_DELETE_ASSERT,
                                                  sizeof SIG_DELETE_ASSERT);

    if (entry == 0 || assert_site <= entry || assert_site >= entry + DELETE_SIZE) {
        log_warning("npc spawner: the delete routine did not resolve (entry %08X, its assert "
                    "%08X), so spawned actors cannot be removed from the panel", (unsigned)entry,
                    (unsigned)assert_site);
        return;
    }
    st.delete_actor = (delete_fn_t)entry;
    log_info("npc spawner: enemy_delete at %08X removes what was spawned", (unsigned)entry);
}

static const uint8_t *current_level(void)
{
    if (st.level_pointer == NULL) {
        return NULL;
    }
    return (const uint8_t *)*st.level_pointer;
}

bool npc_spawner_is_available(void)
{
    return st.spawn != NULL && current_level() != NULL && player_slot_current() != NULL;
}

/* The census follows the level: a world record is freed and replaced on every load, so a
 * pointer that moved is a new level. One that came back at the same address is caught by the
 * refresh the panel asks for when its list opens. The ring goes with it: the level's close
 * deletes every actor and each delete clears its record's live word, and the ring is cleared
 * here as well, so a level that went another way cannot leave a count. */
static void follow_level(void)
{
    const uint8_t *level = current_level();

    if (level != npc_census()->level) {
        memset(st.ring, 0, sizeof st.ring);
        actor_loader_release_all();   /* every body bound to one is gone with the old level */
        npc_census_count(level);
        npc_foreign_count();
    }
}

void npc_spawner_refresh(void)
{
    npc_census_count(current_level());
    npc_foreign_count();
}

uint32_t npc_spawner_kind_count(void)
{
    follow_level();
    return npc_census()->count + npc_foreign_kind_count();
}

bool npc_spawner_kind(uint32_t index, npc_spawner_kind_t *out)
{
    follow_level();
    if (out == NULL) {
        return false;
    }
    if (index < npc_census()->count) {
        *out = npc_census()->kind[index];
        return true;
    }
    return npc_foreign_kind(index - npc_census()->count, out);
}

int32_t npc_spawner_chosen(void)
{
    follow_level();
    return npc_census()->chosen;
}

void npc_spawner_choose(int32_t index)
{
    follow_level();
    npc_census()->chosen = (index >= 0 && (uint32_t)index < npc_spawner_kind_count()) ? index
                                                                                      : -1;
    /* The behaviour is left as it was: a default that followed the kind changed the choice
     * under the player's hands (played 2026-09-16, an Attack picked before the model went back
     * to Stand). */
}

static float wrap_degrees(float degrees)
{
    degrees = fmodf(degrees, 360.0f);
    return (degrees < 0.0f) ? degrees + 360.0f : degrees;
}

/* A ring record's live actor, or NULL: the word spawn_actor stores and every delete clears. */
static const uint8_t *ring_live_actor(uint32_t i)
{
    uint32_t live;

    memcpy(&live, st.ring[i] + PLACE_LIVE_ACTOR, sizeof live);
    return (const uint8_t *)(uintptr_t)live;
}

void npc_spawner_tick(void)
{
    const uint8_t *player = (const uint8_t *)player_slot_current();
    const float   *standing;
    float          over[3];
    uint32_t       i;

    if (st.spawn == NULL || current_level() == NULL || player == NULL ||
        current_level() != npc_census()->level) {
        return;
    }
    standing = (const float *)(player + PLAYER_POSITION_OFFSET);
    over[0]  = standing[0];
    over[1]  = standing[1];
    over[2]  = standing[2] + FLYER_HEIGHT;
    for (i = 0; i < SPAWN_RING; ++i) {
        const uint8_t *actor = ring_live_actor(i);
        int32_t        move_mode;
        int32_t        health;

        if (actor == NULL) {
            continue;
        }
        memcpy(&move_mode, st.ring[i] + PLACE_MOVE_MODE, sizeof move_mode);
        if (move_mode_flies(move_mode)) {
            memcpy(st.ring[i] + PLACE_POSITION, over, sizeof over);
        }
        /* Every hit a copy takes goes to the log, since a fight between copies is otherwise
         * invisible until one falls: a helper's swings against a level's hit points take a few
         * points each. */
        if (memory_try_read((uintptr_t)actor + ACTOR_HEALTH, &health, sizeof health) &&
            health != st.health[i]) {
            log_info("npc spawner: %s (%08X) health %d -> %d", st.ring[i] + PLACE_NAME,
                     (unsigned)(uintptr_t)actor, st.health[i], health);
            st.health[i] = health;
        }
    }
}

uint32_t npc_spawner_alive(void)
{
    uint32_t alive = 0;
    uint32_t i;

    follow_level();
    for (i = 0; i < SPAWN_RING; ++i) {
        if (ring_live_actor(i) != NULL) {
            alive++;
        }
    }
    return alive;
}

uint32_t npc_spawner_remove_all(void)
{
    uint32_t removed = 0;
    uint32_t i;

    follow_level();
    if (st.delete_actor == NULL || current_level() == NULL) {
        return 0;
    }
    for (i = 0; i < SPAWN_RING; ++i) {
        const uint8_t *actor = ring_live_actor(i);
        char           where[160];

        if (actor != NULL) {
            describe_actor(actor, where, sizeof where);
            log_info("npc spawner: removing %08X, %s", (unsigned)(uintptr_t)actor, where);
            st.delete_actor((void *)(uintptr_t)actor, DELETE_REASON_REMOVE);
            removed++;
        }
    }
    if (removed != 0) {
        log_info("npc spawner: removed %u spawned actors through the engine's own delete",
                 removed);
    }
    return removed;
}

/* How much room a spot has: the squared distance to the nearest live spawn, read from the
 * actor itself, and a large number with none alive. An actor whose position cannot be read is
 * taken as standing on the spot. */
static float spot_room(const float *at)
{
    float    room = 1.0e9f;
    uint32_t i;

    for (i = 0; i < SPAWN_RING; ++i) {
        const uint8_t *actor = ring_live_actor(i);
        float          pos[3];
        float          dx;
        float          dy;
        float          apart;

        if (actor == NULL) {
            continue;
        }
        if (!memory_try_read((uintptr_t)actor + ACTOR_POSITION, pos, sizeof pos)) {
            return 0.0f;
        }
        dx = pos[0] - at[0];
        dy = pos[1] - at[1];
        apart = dx * dx + dy * dy;
        if (apart < room) {
            room = apart;
        }
    }
    return room;
}

/* The first spot of the files no live spawn stands within half a body's width of, ahead of
 * the player along `heading`; when every spot has one, the spot with the most room round it, since
 * the engine's push layer sorts two bodies close together out on the first tick, as it does
 * for a placement authored too close, and a spawn refused for want of a spot read as the cap
 * (played 2026-09-17: followers crowding in front of the player stood on the near spots and
 * the count stopped at nine). */
static void free_spot(const float *standing, float heading, float *position)
{
    const float radians = heading * DEGREES_TO_RADIANS;
    const float fwd_x = -sinf(radians);   /* the engine's forward from the heading */
    const float fwd_y = cosf(radians);
    const float right_x = cosf(radians);
    const float right_y = sinf(radians);
    float       best[3] = { 0.0f, 0.0f, 0.0f };
    float       best_room = -1.0f;
    uint32_t    i;

    for (i = 0; i < SPOT_COUNT; ++i) {
        float across;
        float ahead;
        float room;

        spot(i, &across, &ahead);
        position[0] = standing[0] + fwd_x * ahead + right_x * across;
        position[1] = standing[1] + fwd_y * ahead + right_y * across;
        position[2] = standing[2];
        room = spot_room(position);
        if (room >= SPOT_TAKEN_UNITS * SPOT_TAKEN_UNITS) {
            return;
        }
        if (room > best_room) {
            best_room = room;
            memcpy(best, position, sizeof best);
        }
    }
    memcpy(position, best, sizeof best);
}

/* The record a copy is raised from, and its stem: an archive kind's is written here after the
 * file is brought in through the engine's loader and kept until the level changes, and the
 * placement index it carries is the level's first offered placement, so a save made with it
 * alive restores one more of that placement, the nearest thing a save can say; a level kind's
 * is its source placement read afresh from the level, not from the census, since the record is
 * copied whole and a level that changed under the panel fails the same tests it passed when it
 * was counted. */
static bool copy_source(const uint8_t *level, const npc_spawner_kind_t *kind, uint8_t *record,
                        char *stem, uint32_t stem_size, uint32_t *source, void **foreign_model)
{
    *foreign_model = NULL;
    if (kind->foreign) {
        *foreign_model = actor_loader_get(kind->file);
        if (*foreign_model == NULL) {
            log_warning("npc spawner: %s could not be loaded, nothing spawned", kind->file);
            return false;
        }
        *source = (npc_census()->count != 0) ? npc_census()->source[0] : 0u;
        text_format(stem, stem_size, "%s", kind->name);
        npc_foreign_write_record(level, record, stem, kind->file);
        return true;
    }
    *source = npc_census()->source[(uint32_t)npc_census()->chosen];
    if (!npc_census_read_placement(level, *source, record, stem, stem_size, NULL, 0u)) {
        log_warning("npc spawner: placement %u could not be read back, nothing spawned",
                    *source);
        return false;
    }
    return true;
}

/* The copy's own numbers over the source's. Mode 0 is every script's first mode; the source's
 * starting mode was its own script's. Every copy is class 2, whatever its source was: the
 * contact handler (enemy_onContact, 0x00436A68 region) hands a hit to enemy_receiveDamage for
 * class 2 and for no other, so a class 3 townsman or a class 1 story stand-in could not be
 * killed, and being killed is the one thing a spawned copy should reliably do. A source with no
 * hit points gets a few. Two exceptions: the tripod is class 4, the one class the Use key
 * mounts, so the player can take it over as they take over the level's own; and a helper is
 * class 1, see CLASS_ALLY. A shooter fires the record's first weapon, so a droid with a bazooka
 * fires its rockets and a guard his bolts, as the level's own do (played 2026-09-16: a bazooka
 * droid shooting bolts when the script named the bolt outright); a source whose first weapon
 * slot is empty gets the blaster, so the shot kind is never 0, and the reload is raised to the
 * weapon's floor, see RELOAD_HEAVY. A melee copy keeps its source's interval, which gates its
 * swings. */
static void settle_record(uint8_t *record, const npc_spawner_kind_t *kind, const float *position,
                          float facing, bool shoots)
{
    uint32_t zero = 0;
    int32_t  klass = (_stricmp(kind->file, TRIPOD_FILE) == 0) ? CLASS_TRIPOD_GUN
                   : (st.behaviour == SPAWN_BEHAVIOUR_HELP)   ? CLASS_ALLY
                                                              : CLASS_KILLABLE;
    int32_t  hit_points;

    memcpy(record + PLACE_POSITION, position, 3u * sizeof(float));
    memcpy(record + PLACE_START_YAW, &facing, sizeof facing);
    memcpy(record + PLACE_SPAWN_STATE, &zero, sizeof zero);
    memcpy(record + PLACE_LIVE_ACTOR, &zero, sizeof zero);
    memcpy(record + PLACE_REVEAL_COUNT, &zero, sizeof zero);
    memcpy(record + PLACE_START_MODE, &zero, sizeof zero);
    memcpy(record + PLACE_CLASS, &klass, sizeof klass);
    memcpy(&hit_points, record + PLACE_HIT_POINTS, sizeof hit_points);
    if (hit_points <= 0) {
        hit_points = PLACE_HIT_POINTS_LEAST;
        memcpy(record + PLACE_HIT_POINTS, &hit_points, sizeof hit_points);
    }
    if (shoots) {
        uint16_t weapon;
        float    reload;
        float    floor;

        memcpy(&weapon, record + PLACE_WEAPON_KINDS, sizeof weapon);
        if (weapon == 0) {
            weapon = PLACE_SHOT_KIND_BLASTER;
            memcpy(record + PLACE_WEAPON_KINDS, &weapon, sizeof weapon);
        }
        memcpy(&reload, record + PLACE_FIRE_INTERVAL, sizeof reload);
        floor = shot_kind_is_heavy(weapon) ? RELOAD_HEAVY : 0.0f;
        if (reload < floor) {
            memcpy(record + PLACE_FIRE_INTERVAL, &floor, sizeof floor);
        }
    }
}

/* The engine's own spawn, with the level's model table lent to a loaded file for the length of
 * the call: the spawn routine reads the table by the record's index three times, all inside
 * the one call, the eligibility test, the template, and the body's binding, and the slot is
 * put back after, so the level's table is as it was between any two frames. NULL when the
 * table could not be read or the engine spawned nothing. */
static void *raise_copy(const uint8_t *level, uint8_t *record, uint32_t source,
                        void *foreign_model)
{
    uint32_t models = 0;
    void    *lent = NULL;
    void    *actor;

    if (foreign_model == NULL) {
        return st.spawn(record, (int32_t)source, -1);
    }
    if (!memory_try_read((uintptr_t)level + WORLD_MODELS, &models, sizeof models) ||
        !memory_try_read((uintptr_t)models + 4u * NPC_FOREIGN_MODEL_SLOT, &lent, sizeof lent)) {
        log_warning("npc spawner: the level's model table could not be read, nothing spawned");
        return NULL;
    }
    memcpy((void *)(uintptr_t)(models + 4u * NPC_FOREIGN_MODEL_SLOT), &foreign_model,
           sizeof foreign_model);
    actor = st.spawn(record, (int32_t)source, -1);
    memcpy((void *)(uintptr_t)(models + 4u * NPC_FOREIGN_MODEL_SLOT), &lent, sizeof lent);
    return actor;
}

bool npc_spawner_spawn(void)
{
    const uint8_t *level = current_level();
    const uint8_t *player = (const uint8_t *)player_slot_current();
    uint8_t       *record = NULL;
    char           stem[NPC_SPAWNER_NAME_MAX];
    const float   *standing;
    float          heading;
    float          position[3];
    float          facing;
    uint32_t       zero = 0;
    uint32_t       source = 0;
    uint32_t       i;
    uint32_t       slot = 0;
    void          *actor;
    void          *script;
    void          *foreign_model;
    npc_spawner_kind_t kind;
    bool           shoots = false;
    bool           flies;
    int32_t        move_mode;
    char           clips[160];
    char           how[200];

    follow_level();
    if (st.spawn == NULL || level == NULL || player == NULL || npc_census()->chosen < 0) {
        return false;
    }
    if (npc_spawner_alive() >= NPC_SPAWNER_ALIVE_MAX) {
        log_info("npc spawner: %u spawned actors are alive in this level, the most this raises "
                 "at once; nothing spawned", NPC_SPAWNER_ALIVE_MAX);
        return false;
    }
    /* The next ring record no live actor still points at. There is always one: the ring holds
     * twice the cap. */
    for (i = 0; i < SPAWN_RING; ++i) {
        slot = (st.ring_next + i) % SPAWN_RING;
        if (ring_live_actor(slot) == NULL) {
            record       = st.ring[slot];
            st.ring_next = slot;
            break;
        }
    }
    if (record == NULL || !npc_spawner_kind((uint32_t)npc_census()->chosen, &kind) ||
        !copy_source(level, &kind, record, stem, sizeof stem, &source, &foreign_model)) {
        return false;
    }
    standing = (const float *)(player + PLAYER_POSITION_OFFSET);
    heading  = *(const float *)(player + PLAYER_HEADING_OFFSET);
    free_spot(standing, heading, position);
    /* Facing the player, but a tripod gun facing away, its back to them: it is mounted from
     * behind and fired forward (played 2026-09-16, one that faced the player wanted turning). */
    facing = (_stricmp(kind.file, TRIPOD_FILE) == 0) ? heading : wrap_degrees(heading + 180.0f);
    memcpy(&move_mode, record + PLACE_MOVE_MODE, sizeof move_mode);
    flies = move_mode_flies(move_mode);
    if (flies) {
        position[2] += FLYER_HEIGHT;
    }
    /* The copy's own script, laid out with this model's clips before anything is written, so
     * a file whose clips cannot be read is refused with the level untouched. */
    script = spawn_script_prepare((spawn_behaviour_t)st.behaviour, kind.file, flies,
                                  st.script[slot], sizeof st.script[slot], clips, sizeof clips,
                                  &shoots);
    if (script == NULL) {
        log_warning("npc spawner: no %s script could be laid out for %s, nothing spawned",
                    spawn_behaviour_name((spawn_behaviour_t)st.behaviour), stem);
        return false;
    }
    text_format(how, sizeof how, "%s, %s", spawn_behaviour_name((spawn_behaviour_t)st.behaviour),
                clips);
    settle_record(record, &kind, position, facing, shoots);
    actor = raise_copy(level, record, source, foreign_model);
    if (actor == NULL) {
        log_warning("npc spawner: the engine spawned nothing for %s from placement %u: its pool "
                    "had no room even after culling corpses, or no body could be allocated",
                    stem, source);
        return false;
    }
    /* The swap: the engine bound the source's script and will begin it on the next tick from
     * its first entry, and it restarts from the first entry whenever the pointer is 0. The
     * copy's own script goes in its place before that tick. The script index the engine
     * stored stays the source's, so a save made with the copy alive restores it as one more of
     * its placement, running the placement's own script, which is the one thing a save can say
     * about it. */
    memcpy((uint8_t *)actor + ACTOR_SCRIPT, &script, sizeof script);
    memcpy((uint8_t *)actor + ACTOR_IP, &zero, sizeof zero);
    memcpy(&st.health[slot], record + PLACE_HIT_POINTS, sizeof st.health[slot]);
    st.ring_next = (st.ring_next + 1u) % SPAWN_RING;
    st.spawned++;
    log_info("npc spawner: raised %s from placement %u at %.1f %.1f %.1f facing the player "
             "(who stands at %.1f %.1f %.1f, heading %.0f), actor %08X, %s, %u alive, %u this "
             "session", stem, source, (double)position[0], (double)position[1],
             (double)position[2], (double)standing[0], (double)standing[1],
             (double)standing[2], (double)heading, (unsigned)(uintptr_t)actor, how,
             npc_spawner_alive(), st.spawned);
    return true;
}
