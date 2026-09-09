/* cheats_noclip.c: no clip. Everything the player can be stopped by, except the floor.
 *
 * SIZE NOTE: a little over 700 lines, and most of that is comment rather than code: the file is
 * five small hooks and one per-frame tick, and each hook needs its site explained because four
 * of the five were found by measuring the running game rather than by reading it, and the
 * reasons they exist are not recoverable from the code alone. The seam measured and rejected is
 * the glide: it shares nothing with the hooks but the flag they read, and splitting it would
 * separate the glide from the three hooks that exist ONLY because the glide puts the player in
 * the air, leaving both halves unexplained. Splitting by site was also rejected: five files of
 * forty lines each, none of which could state what the set of them is for.
 *
 * ============================ Why the earlier noclip failed ====================================
 *
 * This project shipped a noclip once and removed it, and cheats_openphantom.h still records the
 * reason: it "kept hitting player-physics bugs (falling through unmodelled floors, a ledge
 * pre-check, the mode-dispatch that gates the collision hook)". Every one of those was a symptom
 * of one mistake, which was the site. That version detoured 0x0044C36D, and 0x0044C36D is not a
 * collision routine at all: it is Plr_AutoVaultGate, phase 9 of the player's own locomotion phase
 * table. Suppressing it suppresses a whole phase of a state machine, so the player loses whatever
 * else that phase was responsible for, only while the dispatch happens to be in it, and the floor
 * goes with it. The failures were not noclip being hard. They were the wrong function.
 *
 * ============================ The site this uses instead =======================================
 *
 * bapmap_probeWall at 0x0040C1AE, the universal wall raycast. Every wall test the player makes on
 * the ground runs through it, and it does one job: sweep a sphere from `from` to `to` and return
 * the first face that blocks, or NULL for nothing in the way. So the whole cheat is one answer:
 * for the player's own blocking probes, nothing is in the way.
 *
 * That is the collision answer. There is a second, separate piece below, the glide,
 * which exists because being able to walk through a wall is not much use if the far side drops
 * the player out of the world.
 *
 * ============================ What this does NOT touch =========================================
 *
 * The floor. bapmap_probeFloor (0x0040BE00) is a separate function this never goes near, so the
 * player keeps standing on ground exactly as they always did, and the old noclip's worst symptom,
 * dropping through modelled floors, is not reachable from here. That is structural rather than
 * careful: the gate below only ever fires for probes asking for SURF_WALL_STEEP, and the engine's
 * own header defines that bit as "never a floor".
 *
 * The space between rooms is a different matter: geometry is only modelled where the player was
 * meant to be able to go, so there is genuinely no floor out there and a working floor probe
 * correctly reports none. That is what the glide answers, and it answers it without touching
 * the floor probe either. See the section on it below.
 *
 * ============================ People are not geometry ==========================================
 *
 * A character standing in a doorway is not a polygon and no wall probe can see one. Bodies are
 * made solid to each other by bapobj_cylinderPush, a cylinder-against-cylinder test on a different
 * layer entirely, and the player reaches it from phase 10. So no clip needs a site there too, and
 * without it walls are passable while a droid in the gap is not.
 *
 * NPCs stay solid to each other; only the player stops being part of the crowd. See
 * SIG_ACTOR_PUSH.
 *
 * ============================ What stays solid =================================================
 *
 * Only the player's own probes are answered. The AI walks the same function for its locomotion,
 * its line of sight and its path checks, so an ungated version would let every NPC in the level
 * walk through walls too. The gate is the `from` pointer: the player's blocking probes pass the
 * address of the player record's own position field, which nothing else in the image can pass.
 *
 * Two of the player's own probes are excluded by mask. The USE button probe asks for
 * SURF_USE_BUTTON and the push-block probe for SURF_PUSH_BLOCK_FACE, and both look for a face to
 * ACT ON rather than one to be stopped by. Answering "nothing there" to those would stop the
 * player pressing buttons and pushing blocks while the cheat was on, which is not what it is for.
 *
 * A FOURTH gate is not a collision test at all and no wall probe can reach it: phase 8 of mode
 * Fall catches a ledge the player is falling past and puts them on it. A glide keeps the player in
 * mode Fall the whole time they are clipping, so every edge they pass is a candidate, and the
 * walls that seemed not to work were the ones with a grabbable lip. They were not being blocked,
 * they were being caught. It took a census of the running game to find, because from the outside
 * it looks exactly like a wall that will not give. See SIG_LEDGE_GRAB.
 *
 * Two OTHER things can refuse to let the player move, and they are not walls: an invisible
 * SURF_AIR_BLOCK fence under the proposed position, and a close ceiling. Both live in
 * Plr_AirMoveGate rather than in any wall probe, and this file used to say they were out of scope
 * and left them alone. That was wrong, and it is what made the cheat look like it half worked; see
 * the third hook below for what changed and why. They are only ever consulted for a move made in
 * the AIR, which is why the fault was invisible until the glide started lifting the player.
 *
 * ============================ Falling into the space between rooms =============================
 *
 * Walking through a wall puts the player somewhere the level does not model, and there is no floor
 * out there to stand on, so the first working version of this cheat dropped them out of the world.
 * That is a real fault of the feature even though it is not a fault of the hook: a cheat that
 * loses the player is not usable however correct its collision answer was.
 *
 * A flat hover was tried first and taken out. It held the player's height whenever the cheat was
 * on, which worked, and which also made them permanently AIRBORNE, including while standing in an
 * ordinary room. That matters because the airborne tick does not use the swept probe above at all;
 * it uses the stationary sibling at 0x0040C870, which pushes the player back off any wall it
 * finds. So the hover silently disabled the very clipping it was meant to make usable, in exactly
 * the places the player spends all their time.
 *
 * What is here instead holds the height ONLY while there is nothing underneath. Over real floor
 * the player is left completely alone: gravity, stairs, slopes and jumping all behave as they
 * always did, and, which is the point, the player stays GROUNDED, so the swept probe stays the one
 * in use. The moment they step through a wall into unmodelled space the floor query answers
 * nothing, the height locks and they glide; when they reach the next room a floor exists again and
 * gravity simply resumes and sets them down. Nothing has to decide when to hand control back
 * because the same question answers both ways.
 *
 * "Nothing underneath" means nothing the player could be SET DOWN on, not nothing whatsoever. The
 * space between rooms very often has a lower storey or a ground plane far below it, and treating
 * that as somewhere to stand is what made the hover appear to cut out at random and drop the
 * player to their death. See GLIDE_SAFE_DROP_UNITS.
 *
 * Two more details are field corrections rather than design, and both are about the same moment,
 * the boundary where the floor runs out. The height is taken with a small LIFT rather than exactly
 * where the floor ended, because the plane the player was standing on is the worst possible place
 * to sample a height and holding it exactly leaves them skimming every sill on the way out. And
 * ending a glide takes several consecutive frames of finding a floor rather than one, because at
 * that boundary the query flickers, and releasing on the first positive frame lets gravity have it
 * before the glide re-arms one step lower. Both are sized at their own definitions below.
 *
 * The stationary probe is hooked as well, because the one state this DOES put the player into is
 * airborne over a void, and arriving at the far room's outer wall in that state would otherwise be
 * refused by the airborne push. Unlike the version that shipped greyed out, it is optional: if its
 * site does not resolve the cheat still installs and still works on the ground, and the log says
 * which half is covered. Its signature has to run past thirty five bytes, because 0x0040C870
 * shares its first TWENTY SIX with an unrelated function at 0x0040E4B7 and parts from it only at
 * the FPU instructions that add zLift to the sphere centre.
 *
 * ============================ Doors, and why there is no separate row ==========================
 *
 * A walk-through-doors-only variant was built and tested, telling a door leaf from a wall by the
 * mover owning the polygon: poly+0x2a names it, the mover table is an INLINE array at world+0x624
 * rather than a pointer to one, and mover+0x04 type 2 is a door. The identification worked and was
 * proven against the shipped levels, 165 door movers carrying 1895 wall-blocking faces across ten
 * of the eleven. Being selective also costs a hide-and-reprobe loop, because this probe answers
 * with the first face in CANDIDATE order rather than the nearest, so at a doorway the leaf and the
 * frame come back in no guaranteed order. It was dropped as unwanted rather than unworkable, and
 * the working form is in this file's history at 0.4.2.
 */
#include "cheats_openphantom.h"
#include "cheats_internal.h"

#include "floor_probe.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0040C1AE  bapmap_probeWall: the universal wall raycast -------------------------------- *
 *
 *   0040C1AE  55                 push ebp
 *   0040C1AF  8B EC              mov  ebp,esp
 *   0040C1B1  83 EC 2C           sub  esp,0x2c
 *   0040C1B4  C7 45 F8 00000000  mov  [ebp-8],0        the hit distance, cleared
 *   0040C1BB  C7 45 DC 00000000  mov  [ebp-0x24],0
 *   0040C1C2  8B 45 08           mov  eax,[ebp+8]      `from`
 *   0040C1C5  8B 08              mov  ecx,[eax]
 *   0040C1C7  89 4D EC           mov  [ebp-0x14],ecx   origin.x
 *
 * The six-byte prologue is a plain frame set-up with no rel32 in it, so it relocates into a
 * trampoline verbatim. The pattern runs past it to twenty eight bytes purely for uniqueness: the
 * frame set-up alone appears all over the image, and the two zeroed locals plus the first argument
 * load are what make this one site. Matched exactly once across the whole of .text, and verified
 * at THIS length rather than at some other length that was then not the one written down.
 *
 * The function is cdecl and returns the blocking bapPoly*, or NULL. Its five arguments are
 * (from, to, zLift, radius, mask); the two floats are ordinary stack arguments. */
static const uint8_t SIG_PROBE_WALL[] = {
    0x55,                                     /* push ebp                           */
    0x8B, 0xEC,                               /* mov  ebp,esp                       */
    0x83, 0xEC, 0x2C,                         /* sub  esp,0x2c                      */
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, /* mov  [ebp-8],0                     */
    0xC7, 0x45, 0xDC, 0x00, 0x00, 0x00, 0x00, /* mov  [ebp-0x24],0                  */
    0x8B, 0x45, 0x08,                         /* mov  eax,[ebp+8]     `from`        */
    0x8B, 0x08,                               /* mov  ecx,[eax]                     */
    0x89, 0x4D, 0xEC                          /* mov  [ebp-0x14],ecx  origin.x      */
};
static const uint8_t MSK_PROBE_WALL[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_PROBE_WALL == sizeof MSK_PROBE_WALL,
               "the wall probe pattern and its mask are different lengths");

/* Six, not twenty eight: only the frame set-up is overwritten and relocated. Everything past it in
 * the pattern above is context for signature_find_detour_target's own uniqueness and chaining
 * check, never copied. */
#define PROBE_WALL_PROLOGUE_SIZE 6u

/* --- 0x0040C870  the stationary wall probe, used by the airborne tick ------------------------- *
 *
 *   0040C870  55                 push ebp
 *   0040C871  8B EC              mov  ebp,esp
 *   0040C873  83 EC 4C           sub  esp,0x4c
 *   0040C876  8B 45 08           mov  eax,[ebp+8]      `pos`
 *   0040C879  8B 08              mov  ecx,[eax]
 *   0040C87B  89 4D C4           mov  [ebp-0x3c],ecx   the sphere centre, x
 *   0040C87E  8B 50 04           mov  edx,[eax+4]
 *   0040C881  89 55 C8           mov  [ebp-0x38],edx   y
 *   0040C884  8B 40 08           mov  eax,[eax+8]
 *   0040C887  89 45 CC           mov  [ebp-0x34],eax   z
 *   0040C88A  D9 45 CC           fld  [ebp-0x34]
 *   0040C88D  D8 45 0C           fadd [ebp+0xc]        + zLift
 *   0040C890  D9 5D CC           fstp [ebp-0x34]
 *
 * The engine's own name for it is not known, so it is described rather than named: it is the
 * sibling of the swept probe that tests a sphere sitting AT a point instead of sweeping one along
 * a segment, with the same gather, the same surface-flag filter and the same first-in-list rule.
 * Four arguments, (pos, zLift, radius, mask), and the same bapPoly* answer.
 *
 * The pattern has to run to thirty five bytes, and the reason is worth keeping: 0x0040E4B7 opens
 * with byte-for-byte the same TWENTY SIX. Same frame, same 0x4c of locals, same unpacking of the
 * vec3 argument into the same three stack slots. The two part only at the FPU instructions above,
 * which are this one adding zLift to the sphere centre. A twenty six byte pattern matches both and
 * resolves to neither; six bytes of it, the bare frame set-up, matches eight places in the image.
 * Verified unique at the length actually written here rather than at some other length. */
static const uint8_t SIG_PROBE_WALL_AT[] = {
    0x55,                                     /* push ebp                          */
    0x8B, 0xEC,                               /* mov  ebp,esp                      */
    0x83, 0xEC, 0x4C,                         /* sub  esp,0x4c                     */
    0x8B, 0x45, 0x08,                         /* mov  eax,[ebp+8]     `pos`        */
    0x8B, 0x08,                               /* mov  ecx,[eax]                    */
    0x89, 0x4D, 0xC4,                         /* mov  [ebp-0x3c],ecx  centre x     */
    0x8B, 0x50, 0x04,                         /* mov  edx,[eax+4]                  */
    0x89, 0x55, 0xC8,                         /* mov  [ebp-0x38],edx  y            */
    0x8B, 0x40, 0x08,                         /* mov  eax,[eax+8]                  */
    0x89, 0x45, 0xCC,                         /* mov  [ebp-0x34],eax  z            */
    0xD9, 0x45, 0xCC,                         /* fld  [ebp-0x34]                   */
    0xD8, 0x45, 0x0C,                         /* fadd [ebp+0xc]       + zLift      */
    0xD9, 0x5D, 0xCC                          /* fstp [ebp-0x34]                   */
};
static const uint8_t MSK_PROBE_WALL_AT[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_PROBE_WALL_AT == sizeof MSK_PROBE_WALL_AT,
               "the stationary wall probe pattern and its mask are different lengths");
#define PROBE_WALL_AT_PROLOGUE_SIZE 6u

/* --- 0x0044C59D  Plr_AirMoveGate: the veto on a move made in the air ------------------------- *
 *
 *   0044C59D  55                 push ebp
 *   0044C59E  8B EC              mov  ebp,esp
 *   0044C5A0  81 EC 94 000000    sub  esp,0x94
 *   0044C5A6  56                 push esi
 *   0044C5A7  57                 push edi
 *   0044C5A8  A1 20524B00        mov  eax,[0x4b5220]        the player record
 *   0044C5AD  83 B8 A0000000 00  cmp  [eax+0xa0],0          bMovedThisFrame
 *
 * The last chance the engine takes to veto the horizontal displacement before it is committed, and
 * it is the AIR one: it sits at phase 9 of the Jump, JediJump and FALL descriptors, where standing
 * has 0x0044C36D instead. A glide puts the player in Fall, so this is the gate every clipping move
 * goes through the moment their feet leave the ground, and it vetoes on three separate grounds:
 *
 *   - the floor under the PROPOSED position carries SURF_AIR_BLOCK, an invisible fence that only
 *     exists in the air. 73,360 faces across the eleven levels carry it, 22 per cent of every face
 *     in the game, so this is not a rare case;
 *   - the head clearance at the proposed position is inside (0, 3.0), i.e. there is a ceiling and
 *     it is close. Another 29,968 faces carry the low-ceiling flag;
 *   - a wall lies between here and there, which is the swept probe above and already answered.
 *
 * That third one is why the cheat looked like it half worked: on the ground it is the only gate
 * there is, so clipping worked; in the air the other two are still standing, so some walls let the
 * player through and others did not, with no pattern the player could see.
 *
 * Nine bytes are relocated, not six: the frame set-up here is `sub esp, imm32` rather than the
 * `imm8` form the two probes use, so the third instruction is six bytes and cannot be split.
 * Twenty three bytes of pattern against a first match at eleven, and the two absolute references to
 * the player record global are left unmasked, the same assumption every other module here already
 * makes about that address. */
static const uint8_t SIG_AIR_MOVE_GATE[] = {
    0x55,                                     /* push ebp                              */
    0x8B, 0xEC,                               /* mov  ebp,esp                          */
    0x81, 0xEC, 0x94, 0x00, 0x00, 0x00,       /* sub  esp,0x94                         */
    0x56,                                     /* push esi                              */
    0x57,                                     /* push edi                              */
    0xA1, 0x20, 0x52, 0x4B, 0x00,             /* mov  eax,[0x4b5220]  the player       */
    0x83, 0xB8, 0xA0, 0x00, 0x00, 0x00, 0x00  /* cmp  [eax+0xa0],0    bMovedThisFrame  */
};
static const uint8_t MSK_AIR_MOVE_GATE[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_AIR_MOVE_GATE == sizeof MSK_AIR_MOVE_GATE,
               "the air move gate pattern and its mask are different lengths");
#define AIR_MOVE_GATE_PROLOGUE_SIZE 9u

/* --- 0x0044C78E  Plr_TryGrabLedgeWhileFalling: phase 8 of mode Fall -------------------------- *
 *
 *   0044C78E  55                 push ebp
 *   0044C78F  8B EC              mov  ebp,esp
 *   0044C791  83 EC 2C           sub  esp,0x2c
 *   0044C794  A1 20524B00        mov  eax,[0x4b5220]     the player record
 *   0044C799  83 78 64 00        cmp  [eax+0x64],0       pAuxAction, the early out
 *
 * The last gate, and the one that took a census of the running game to find, because it is not a
 * collision test and no wall probe reaches it. It looks half a unit ahead and 0.7 up for an edge
 * the player is facing within thirty degrees, and if there is enough air beneath them it takes the
 * ledge: it enters Hang, clears the moved flag, and teleports the player onto the lip.
 *
 * A glide holds the player in the air, so they are in mode Fall, so this phase is running the
 * whole time they are clipping. Every edge they pass is a candidate. That is why some walls let
 * them through and others did not, with no pattern a player could see: the ones that refused were
 * the ones with a grabbable lip, and the player was not being blocked at all, they were being
 * caught. Nothing in the three collision hooks could ever have covered it.
 *
 * Fifteen bytes against a first unique match at twelve. Nine matches two places, so the pattern
 * has to reach the player-record load. */
static const uint8_t SIG_LEDGE_GRAB[] = {
    0x55,                                     /* push ebp                              */
    0x8B, 0xEC,                               /* mov  ebp,esp                          */
    0x83, 0xEC, 0x2C,                         /* sub  esp,0x2c                         */
    0xA1, 0x20, 0x52, 0x4B, 0x00,             /* mov  eax,[0x4b5220]  the player       */
    0x83, 0x78, 0x64, 0x00                    /* cmp  [eax+0x64],0    pAuxAction       */
};
static const uint8_t MSK_LEDGE_GRAB[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_LEDGE_GRAB == sizeof MSK_LEDGE_GRAB,
               "the ledge grab pattern and its mask are different lengths");
#define LEDGE_GRAB_PROLOGUE_SIZE 6u

/* --- 0x004131EB  bapobj_cylinderPush: bodies being solid to each other ----------------------- *
 *
 *   004131EB  55                 push ebp
 *   004131EC  8B EC              mov  ebp,esp
 *   004131EE  83 EC 78           sub  esp,0x78
 *   004131F1  8B 45 08           mov  eax,[ebp+8]        the object being moved
 *   004131F4  89 45 F0           mov  [ebp-0x10],eax
 *   004131F7  8B 4D F0           mov  ecx,[ebp-0x10]
 *   004131FA  83 79 14 00        cmp  [ecx+0x14],0       the early out
 *
 * THE ACTORS, which are a different subject from the geometry above and needed their own site.
 * Walls are polygons answered by a probe; a Gungan standing in a doorway is not a polygon at all,
 * it is a cylinder, and no wall probe has ever been able to see one. This is the layer that makes
 * bodies solid to each other: run against every other object in the actor band, it answers whether
 * the mover may stand at the proposed position and shoves whoever it displaces on the way.
 *
 * The player reaches it from Plr_ResolveCollision, phase 10, which is a wall slide done by RETRY
 * rather than by projection: it tries the whole move, then Y only, then X only, and takes the
 * first that is allowed. So this is called up to three times per frame with desiredPos mutated in
 * between, and the gate below catches all three because every one of them passes the address of
 * the player record's own desiredPos field. Nothing else in the image passes that address; the
 * NPC callers pass a local.
 *
 * Nineteen bytes against a first unique match at twelve. Six matches four places. */
static const uint8_t SIG_ACTOR_PUSH[] = {
    0x55,                                     /* push ebp                              */
    0x8B, 0xEC,                               /* mov  ebp,esp                          */
    0x83, 0xEC, 0x78,                         /* sub  esp,0x78                         */
    0x8B, 0x45, 0x08,                         /* mov  eax,[ebp+8]     the object       */
    0x89, 0x45, 0xF0,                         /* mov  [ebp-0x10],eax                   */
    0x8B, 0x4D, 0xF0,                         /* mov  ecx,[ebp-0x10]                   */
    0x83, 0x79, 0x14, 0x00                    /* cmp  [ecx+0x14],0                     */
};
static const uint8_t MSK_ACTOR_PUSH[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_ACTOR_PUSH == sizeof MSK_ACTOR_PUSH,
               "the actor push pattern and its mask are different lengths");
#define ACTOR_PUSH_PROLOGUE_SIZE 6u

/* What bapobj_cylinderPush answers when the move stands. Phase 10 compares against this exact
 * value and returns early on it, so it is the answer that means "allowed, and nothing further to
 * try", not merely a non-zero. */
#define ACTOR_PUSH_MOVE_ALLOWED 1

/* Surface bits, from the probe's own `mask` argument. The first two are what "a wall stopped me"
 * means; the second two mark a face the player is meant to find and USE, which is a different
 * question asked through the same function. */
#define SURF_WALL_STEEP       0x0001u
#define SURF_TOO_STEEP        0x0002u
#define SURF_PUSH_BLOCK_FACE  0x0080u
#define SURF_USE_BUTTON       0x0100u


/* Is this cheat actually acting right now. Two conditions, not one.
 *
 * The free camera test is not belt and braces, it is a second owner of the same field. That cheat
 * freezes the simulation and teleports the player to the camera on the way out, and this one holds
 * the player's height every frame from the same per-frame site; both writing in the same frame
 * makes the outcome depend on ordering. The toggle already refuses to have both on at once, so
 * this should never be reachable, and it is here precisely because "should never" is not a
 * guarantee: a saved state, a level change or a future caller reaching the flags directly would
 * all bypass the toggle, and the cost of asking is one comparison.
 *
 * Free camera wins, rather than this cheat, because it is the one the player cannot leave without
 * its own hotkey; standing down in its favour can never strand anybody. */
static bool noclip_is_active(void)
{
    return own_state.cheats[CHEATS_OWN_NOCLIP].on &&
           !own_state.cheats[CHEATS_OWN_FREECAM].on;
}

/* Is this probe one this file may answer differently: the cheat acting, the player's own question,
 * and a question about being STOPPED rather than about finding something to use.
 *
 * The `from` test is what keeps every NPC solid. The player's blocking probes all pass
 * &player->pos, the address of a field inside the one record at a known global; the AI passes the
 * address inside whichever actor record it is stepping. Comparing the pointer rather than the
 * position it holds means an NPC standing exactly where the player stands is still not the
 * player. */
static bool probe_belongs_to_a_clipping_player(const void *from, uint32_t mask)
{
    const uint8_t *player;

    if (!noclip_is_active()) {
        return false;
    }
    mask &= 0xFFFFu;
    if ((mask & (SURF_WALL_STEEP | SURF_TOO_STEEP)) == 0u) {
        return false;
    }
    if ((mask & (SURF_USE_BUTTON | SURF_PUSH_BLOCK_FACE)) != 0u) {
        return false;
    }

    if (!memory_try_readable(PLAYER_RECORD_PTR_ADDR, sizeof(uintptr_t))) {
        return false;
    }
    player = *(const uint8_t *const *)PLAYER_RECORD_PTR_ADDR;
    if (player == NULL) {
        return false;
    }
    return from == (const void *)(player + PLAYER_POSITION_OFFSET);
}

/* The original always runs first and its answer is what a normal frame returns, so with the cheat
 * off this costs one call and one flag test. It runs first rather than being skipped, because a
 * probe that is not the player's has to come back completely untouched, and because the gate has
 * nothing to decide about when nothing was in the way to begin with.
 *
 * When the cheat IS on and the probe is the player's own, the answer is simply that nothing
 * blocked them. No geometry is modified and there is nothing to restore afterwards, which is what
 * makes this the version that stayed: both of the larger builds described in this file's header
 * had to hide faces from the engine's own candidate loop and put them back, and both needed that
 * only because they were being SELECTIVE about which faces to pass through. Passing all of them
 * needs none of it.
 *
 * The player's other wall probes are untouched, so the ledge grab, the pull-up and the swing all
 * still find what they look for: the gate's `from` test only matches the blocking probes that
 * sweep from the player's own position field. */
static void *__cdecl hook_probe_wall(const void *from, const void *to,
                                     float z_lift, float radius, uint32_t mask)
{
    void *hit = own_state.probe_wall_original(from, to, z_lift, radius, mask);

    if (hit == NULL || !probe_belongs_to_a_clipping_player(from, mask)) {
        return hit;
    }
    return NULL;
}

/* The airborne wall test, answered the same way and for the same probes. Its mask is a u16 in the
 * engine's own signature but arrives as a pushed dword, which is why the gate masks the low
 * sixteen bits rather than trusting all of it. */
static void *__cdecl hook_probe_wall_at(const void *pos, float z_lift, float radius, uint32_t mask)
{
    void *hit = own_state.probe_wall_at_original(pos, z_lift, radius, mask);

    if (hit == NULL || !probe_belongs_to_a_clipping_player(pos, mask)) {
        return hit;
    }
    return NULL;
}


/* ---------------------------------------------------------------------------------------------
 * THE GLIDE. Gravity is suspended for exactly as long as there is nothing underneath the player,
 * and not one frame longer. See this file's header for why that is the whole design and why the
 * flat hover it replaced was worse rather than simpler.
 * ------------------------------------------------------------------------------------------- */

/* The Z component of a position, the third float of the vec3 at each of the two offsets
 * cheats_internal.h already names. */
#define VEC3_Z_OFFSET 8u
#define VEC3_Z        (VEC3_Z_OFFSET / sizeof(float))

/* Lifted a little on the way into a glide rather than held at exactly the height the floor ran
 * out at. Two reasons, and the second is the one that was actually visible in the game. The floor
 * runs out at the plane the player was standing ON, so holding that exact value leaves them
 * skimming it, catching on every lip and doorway sill along the way. And the point the floor runs
 * out at is the WORST place to sample a height, because it is the one place the answer is about to
 * change. Half a unit against a player 2.8 units tall is enough to clear both and small enough not
 * to read as a jump. */
#define GLIDE_LIFT_UNITS 0.5f

/* How many consecutive frames of "there is floor here" it takes to end a glide, and this is the
 * other half of the same field report. At the boundary the floor query flickers: the player is
 * straddling the edge of the modelled geometry, so one frame finds a floor and the next does not.
 * Releasing on the first frame that finds one lets gravity have that frame, then the glide re-arms
 * and captures a LOWER height, and repeating that is a stepped sink rather than a hold.
 *
 * Requiring agreement across a few frames costs nothing when the answer is genuine, since holding
 * for another twentieth of a second before dropping is not something a player can see, and it
 * removes the flicker entirely. Erring toward holding is also the right direction to err in: the
 * failure it produces is a brief float, and the one it prevents is being deposited in a void. */
#define GLIDE_RELEASE_FRAMES 4u

/* A floor further down than this does not count as somewhere to stand, and this is the second
 * field report rather than caution. The first version held only when the probe found NOTHING at
 * all, and the space between rooms is very often not nothing: there is a lower storey, or a large
 * ground plane, somewhere far below. The probe duly answered "floor found", the glide released
 * because it had what it was waiting for, and the player fell the whole way onto it. From inside
 * the cheat that reads as the hover cutting out at random.
 *
 * So the question the glide asks is not "is there anything under me" but "is there anything under
 * me I could be set down on". Six units is a little over twice the player's 2.8 unit height: high
 * enough that ordinary steps, kerbs and short drops still resolve normally, low enough that
 * nothing it does allow can hurt. */
#define GLIDE_SAFE_DROP_UNITS 6.0f

static bool     glide_holding;
static float    glide_height;
static unsigned glide_floor_frames;   /* consecutive frames with a floor, while holding */

/* Is there a floor under this point that the player could actually be set down on. Distinct from
 * "is there a floor", see GLIDE_SAFE_DROP_UNITS for why the difference is the whole bug. */
static bool glide_has_somewhere_to_stand(const float *position)
{
    float drop = 0.0f;

    switch (floor_probe_below(position, &drop)) {
    case FLOOR_PROBE_NONE:
        return false;
    case FLOOR_PROBE_FOUND:
        return drop <= GLIDE_SAFE_DROP_UNITS;
    case FLOOR_PROBE_UNAVAILABLE:
    default:
        return true;      /* cannot ask, so do not interfere */
    }
}


void cheats_noclip_tick(void)
{
    uint8_t *player;
    float   *position;
    float   *desired;
    float   *vertical_velocity;

    if (!noclip_is_active()) {
        glide_holding      = false;
        glide_floor_frames = 0;
        return;
    }

    if (!memory_try_readable(PLAYER_RECORD_PTR_ADDR, sizeof(uintptr_t))) {
        return;
    }
    player = *(uint8_t *const *)PLAYER_RECORD_PTR_ADDR;
    if (player == NULL ||
        !memory_try_readable((uintptr_t)(player + PLAYER_DESIRED_POSITION_OFFSET),
                             sizeof(float) * 3u)) {
        glide_holding      = false;
        glide_floor_frames = 0;
        return;
    }

    position          = (float *)(player + PLAYER_POSITION_OFFSET);
    desired           = (float *)(player + PLAYER_DESIRED_POSITION_OFFSET);
    vertical_velocity = (float *)(player + PLAYER_VERTICAL_VELOCITY_OFFSET);

    /* Renewed every frame the cheat is on, not only while holding, and that is a correction. The
     * grace used to be granted only on the frames the glide was actually holding, so the one case
     * that most needed it, the glide releasing when it should not have and the player falling, was
     * the exact case that got none. Death by falling is never a useful outcome of a cheat whose
     * whole subject is going where the floor is not. */
    cheats_openphantom_grant_fall_grace();

    /* The one question, and it answers both directions. An UNAVAILABLE probe reads as "there is
     * floor", because a build that cannot ask must not start acting as though the answer were bad
     * news; that is floor_probe.h's own rule for its callers. */
    if (glide_has_somewhere_to_stand(position)) {
        /* Not gliding, so this is the ordinary case and the player is left completely alone. */
        if (!glide_holding) {
            return;
        }
        /* Gliding, and a floor has appeared. Believed only once it has been there for a few frames
         * in a row, see GLIDE_RELEASE_FRAMES; until then the hold continues below. */
        if (++glide_floor_frames >= GLIDE_RELEASE_FRAMES) {
            glide_holding      = false;
            glide_floor_frames = 0;
            return;
        }
    } else {
        glide_floor_frames = 0;

        /* The rising edge, taken here rather than when the cheat is switched on, so the height held
         * is the one the player actually left the floor at rather than wherever they happened to be
         * standing when they opened the panel. */
        if (!glide_holding) {
            glide_height  = position[VEC3_Z] + GLIDE_LIFT_UNITS;
            glide_holding = true;
        }
    }

    /* Both copies, because the movement phase copies desiredPos over pos on any frame the player
     * moved, so writing one and not the other is undone before it is ever seen. */
    *vertical_velocity  = 0.0f;
    position[VEC3_Z]    = glide_height;
    desired[VEC3_Z]     = glide_height;

}

/* Veto nothing while the cheat is on.
 *
 * The whole function is a veto: it decides whether the horizontal move the
 * previous phase built is allowed to stand, and if it is, it does nothing at all. So declining to
 * run it is exactly "allow the move", with no state left half-written and nothing to undo, rather
 * than the suppression of a phase that also had other work to do. That distinction is the entire
 * lesson of the old noclip at 0x0044C36D, which suppressed a phase that DID have other work.
 *
 * This takes the wall veto with it, which is harmless duplication: the swept probe hook above
 * already answers that one, and it still has to, because on the ground this gate is not the one
 * running. Both are needed and neither is sufficient. */
static void __cdecl hook_air_move_gate(void)
{
    if (noclip_is_active()) {
        return;
    }
    own_state.air_move_gate_original();
}

/* Walk through people. Answers "the move stands" for the player's own three attempts and never
 * runs the original, so nobody is tested against and nobody is shoved.
 *
 * Gated on the DESTINATION pointer rather than on the object, for the same reason every other gate
 * in this file is: the player's calls pass the address of a field inside the one player record,
 * and an NPC's calls pass a local of their own, so an NPC standing exactly where the player stands
 * is still not the player. Identifying the player's body object instead would have meant chasing
 * a handle through two indirections every call to learn something a pointer comparison already
 * settles.
 *
 * NPCs stay solid to EACH OTHER. Only the player's own attempts are answered, so the crowd still
 * behaves like a crowd; it is only the player who is no longer part of it. An NPC walking into the
 * player can still shove them, which is left alone deliberately: suppressing that would mean
 * reaching into everyone else's collision loop to hide one body from it, which is a much larger
 * change than this, for a case the player can simply walk out of. */
static int32_t __cdecl hook_actor_push(void *obj, const void *new_pos)
{
    const uint8_t *player;

    if (noclip_is_active() && memory_try_readable(PLAYER_RECORD_PTR_ADDR, sizeof(uintptr_t))) {
        player = *(const uint8_t *const *)PLAYER_RECORD_PTR_ADDR;
        if (player != NULL &&
            new_pos == (const void *)(player + PLAYER_DESIRED_POSITION_OFFSET)) {
            return ACTOR_PUSH_MOVE_ALLOWED;
        }
    }
    return own_state.actor_push_original(obj, new_pos);
}

/* Do not catch ledges while clipping. Like the air move gate this is a phase whose entire effect
 * is one decision, so declining to run it is exactly "do not take the ledge" with nothing left
 * half done. Falling past a lip instead of grabbing it is the whole point of the cheat. */
static void __cdecl hook_ledge_grab(void)
{
    if (noclip_is_active()) {
        return;
    }
    own_state.ledge_grab_original();
}

void install_noclip(void)
{
    if (!cheats_install_one(SIG_PROBE_WALL, MSK_PROBE_WALL, sizeof SIG_PROBE_WALL,
                            (const void *)&hook_probe_wall, &own_state.probe_wall_detour,
                            PROBE_WALL_PROLOGUE_SIZE, "the wall probe")) {
        return;
    }
    own_state.probe_wall_original = (probe_wall_fn_t)own_state.probe_wall_detour.original;
    own_state.cheats[CHEATS_OWN_NOCLIP].available = true;

    /* Optional, deliberately, and this is a correction: an earlier build made this one required
     * and a signature that matched two functions instead of one then took the whole cheat down
     * with it, offered as unavailable. The player spends nearly all of their time on the ground,
     * where the swept probe above is the only one that matters, so half of this feature is worth
     * having and the log says which half. Only the glide over a void needs this one. */
    if (cheats_install_one(SIG_PROBE_WALL_AT, MSK_PROBE_WALL_AT, sizeof SIG_PROBE_WALL_AT,
                           (const void *)&hook_probe_wall_at, &own_state.probe_wall_at_detour,
                           PROBE_WALL_AT_PROLOGUE_SIZE, "the airborne wall probe")) {
        own_state.probe_wall_at_original =
            (probe_wall_at_fn_t)own_state.probe_wall_at_detour.original;
    }

    /* Optional for the same reason, and it is the one that makes clipping work while gliding
     * rather than only while standing. Without it the two vetoes it carries, the air-block fence
     * and the close ceiling, still stand and stop the player at some walls and not others. */
    if (cheats_install_one(SIG_AIR_MOVE_GATE, MSK_AIR_MOVE_GATE, sizeof SIG_AIR_MOVE_GATE,
                           (const void *)&hook_air_move_gate, &own_state.air_move_gate_detour,
                           AIR_MOVE_GATE_PROLOGUE_SIZE, "the air move gate")) {
        own_state.air_move_gate_original =
            (air_move_gate_fn_t)own_state.air_move_gate_detour.original;
    }

    /* Optional for the same reason again. Without it the player is caught by every grabbable lip
     * they glide past, which is the fault that presented as "some walls will not let me through"
     * and was not a collision fault at all. */
    if (cheats_install_one(SIG_LEDGE_GRAB, MSK_LEDGE_GRAB, sizeof SIG_LEDGE_GRAB,
                           (const void *)&hook_ledge_grab, &own_state.ledge_grab_detour,
                           LEDGE_GRAB_PROLOGUE_SIZE, "the falling ledge grab")) {
        own_state.ledge_grab_original = (ledge_grab_fn_t)own_state.ledge_grab_detour.original;
    }

    /* Optional again, and it is the one that covers people rather than geometry. Without it walls
     * are passable and a droid standing in the gap is not, which is a strange half of a cheat but
     * still a working one, so it degrades rather than disappearing. */
    if (cheats_install_one(SIG_ACTOR_PUSH, MSK_ACTOR_PUSH, sizeof SIG_ACTOR_PUSH,
                           (const void *)&hook_actor_push, &own_state.actor_push_detour,
                           ACTOR_PUSH_PROLOGUE_SIZE, "the actor push")) {
        own_state.actor_push_original = (actor_push_fn_t)own_state.actor_push_detour.original;
    }
}
