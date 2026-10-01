/* mp_signatures.c: the multiplayer choke points, addressed by what they are.
 *
 * SIZE NOTE: well past 600 and at the hard limit; the
 * code in it is still under a hundred. The rest is the function site patterns with the reason each
 * one is cut where it is cut, which is exactly what the size rule forbids deleting to reach a
 * limit. Two seams have been taken: the pure data anchors live in mp_cell_sites.c, and the body,
 * animation and sabre sites live in mp_signatures_puppet.c as a second table merged behind this
 * one. The next addition has to split rather than trim, and the seam is the player lifecycle
 * cluster: save, restore, respawn, despawn, teleport and spawn hero, six patterns that belong
 * together. Do not split off the resolver. It would leave a file that is nothing but arrays.
 *
 * Three builds of the engine ship in this installation and all three are 829,952 bytes: the retail
 * WMAIN.EXE, an alternate link of it, and the Edit Tool's own recompile. The recompile moves code
 * by 0x60 and data by 0x50, and the alternate link moves two of the functions below by 8. An
 * address table would have written into a different function in both.
 *
 * One rule decides the shape of every pattern here, and the first draft of this table broke it
 * seven times: an absolute address is never a required byte. It is wildcarded, and the address is
 * read out of the matched operand instead. That is not a nicety. A pattern that requires an
 * operand cannot survive another module relocating what the operand names, and it cannot survive a
 * build where the datum sits somewhere else. Masking those seven operands cost no uniqueness on
 * any image and gained six sites on the recompile, which is the same argument in the other
 * direction. The same rule covers relative displacements, for the reason the alternate link
 * demonstrates: its ten differing bytes inside enemy_tickAll are all call displacements.
 *
 * Every function site is declared as a detour target even where nothing detours it yet, because
 * the two stage rule costs nothing on a clean site and rescues the anchor once something does.
 * Three of these functions are already detoured by other DLLs in this tree, so stage two is load
 * bearing rather than theoretical: bapmap_tickMover, Plr_RunPhases and, in the second table,
 * bapobj_drawAll.
 *
 * The rule was stated here long before it was followed everywhere. Twenty two heads declared
 * no prologue, and four of those are heads another DLL takes first because it loads earlier:
 * enhanced_resolution scales the menu widget rectangles over swmenu_open, hud_ratio_scaling
 * hulls the font drawer and the glyph scale pair, camera_handback_fix hulls Dialog_Close.
 * Stage one then matched nothing and stage two could not run, so each of those resolved to
 * zero and switched its feature off with one warning line: the multiplayer entry never
 * appeared in the menu, and the scoreboard never had a surface to draw on. Each head that
 * can carry a prologue now carries the first instruction boundary past five, decoded against
 * the retail image. swmenu_pump_frame cannot carry one: it reaches five bytes only across a
 * relative call, whose displacement means something else at a trampoline's address.
 */
#include "mp_signatures.h"

#include "mp_cell_sites.h"
#include "mp_lobby_sites.h"
#include "mp_menu_sites.h"

/* `push ebp; mov ebp, esp; sub esp, 8`, six bytes, which is the first instruction boundary
 * past the five a branch needs. */
#define SWMENU_TAKE_NAV_PROLOGUE 6u
#include "mp_signatures_enemy.h"
#include "mp_signatures_puppet.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The patterns.
 *
 * A prologue length is the whole instructions covering the five bytes a jmp rel32 overwrites. All
 * of them match exactly once on both strict builds; the header says how the three builds differ.
 * ============================================================================================ */

/* The one door every player side trigger of a mover goes through. A walk plate fires four ids, a
 * button unpacks four more, and a script names one; all of them call this. The bounds test against
 * the world's mover count is what makes twenty eight bytes enough, and it carries no address, so
 * the pattern needs no mask. */
static const uint8_t SIG_BAPMAP_OPEN_MOVER[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14,
    0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x0C,
    0x3B, 0x88, 0x20, 0x06
};
#define BAPMAP_OPEN_MOVER_PROLOGUE 6u

/* The script's close. Its body is the same two guards and one store, so the head is the only thing
 * that tells it from the opener: four bytes of frame setup against six. A jmp rel32 takes five, so
 * the declared prologue is the whole instruction that straddles the fifth byte, which is eight. */
static const uint8_t SIG_BAPMAP_CLOSE_MOVER[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14, 0x83, 0x7D,
    0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x0C, 0x3B, 0x88,
    0x20, 0x06, 0x00, 0x00
};
#define BAPMAP_CLOSE_MOVER_PROLOGUE 8u

/* The frame catch-all that brings every untouched mover up to the world clock. It is here for its
 * operands rather than for its body: it loads the world pointer three times in seventy bytes, once
 * for the null test, once for the mover count and once for the mover table, so one site yields a
 * three way agreement on the cell instead of a lookup. Nothing detours it. */
static const uint8_t SIG_BAPMAP_TICK_MOVERS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x83, 0x3D, 0x60, 0x00, 0x8A, 0x00,
    0x00, 0x75, 0x04, 0x33, 0xC0, 0xEB, 0x7D, 0xC7, 0x45, 0xFC, 0x00, 0x00,
    0x00, 0x00, 0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x09, 0x8B,
    0x45, 0xF4, 0x83, 0xC0, 0x01, 0x89, 0x45, 0xF4, 0x8B, 0x0D, 0x60, 0x00,
    0x8A, 0x00, 0x8B, 0x55, 0xF4, 0x3B, 0x91, 0x20, 0x06, 0x00, 0x00, 0x7D,
    0x53, 0x8B, 0x45, 0xF4, 0x8B, 0x0D, 0x60, 0x00, 0x8A, 0x00
};
static const uint8_t MSK_BAPMAP_TICK_MOVERS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define BAPMAP_TICK_MOVERS_PROLOGUE 6u

/* One mover integrated for one substep, and the freeze cell is its first test. Two DLLs in this
 * tree already detour it. */
static const uint8_t SIG_BAPMAP_TICK_MOVER[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x83, 0x3D, 0xCC, 0x5F, 0x5B, 0x00,
    0x00, 0x0F, 0x85, 0xC7, 0x04, 0x00, 0x00, 0x8B
};
static const uint8_t MSK_BAPMAP_TICK_MOVER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
#define BAPMAP_TICK_MOVER_PROLOGUE 6u

/* Which body a script command means. The movsx of the target kind argument is what makes twenty
 * bytes enough. */
static const uint8_t SIG_RESOLVE_TARGET[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00,
    0x00, 0x0F, 0xBF, 0x45, 0x10, 0x89, 0x45, 0xE8
};
#define RESOLVE_TARGET_PROLOGUE 6u

/* The AI tick. Worth 52 bytes rather than 20 because the suspend switch is not the second
 * instruction of the function but the second statement of the source: the jne at +0x0F jumps over
 * the registration block to reach it, and that switch is one dword that stops all AI. */
static const uint8_t SIG_ENEMY_TICK_ALL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0xA1, 0x24, 0x87, 0x86, 0x00, 0x83,
    0x78, 0x28, 0x00, 0x75, 0x1A, 0x8B, 0x0D, 0x24, 0x87, 0x86, 0x00, 0xC7,
    0x41, 0x18, 0x68, 0x6A, 0x43, 0x00, 0x8B, 0x15, 0x24, 0x87, 0x86, 0x00,
    0xC7, 0x42, 0x28, 0x01, 0x00, 0x00, 0x00, 0x83, 0x3D, 0x9C, 0x4D, 0x6C,
    0x00, 0x00, 0x74, 0x0A
};
static const uint8_t MSK_ENEMY_TICK_ALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF
};
#define ENEMY_TICK_ALL_PROLOGUE 6u

/* Fifteen bytes of function, of which a jmp rel32 overwrites five, so the pattern reaches past the
 * end of the function into the neighbouring setter in order to have a tail at all. The cost is
 * stated rather than hidden: this anchor holds only while that neighbour is unpatched. The
 * neighbour's operand is wholly masked, like every operand a cell is read from, so the site
 * resolves in the recompile as well; the price is that the thirteen bytes behind the prologue are
 * not unique (they match twenty seven times in the retail image), so stage two of the resolver
 * could not rescue this site if a foreign branch ever sat on its head. None does, and this table
 * is resolved once, before any hull of ours is written, which is what makes the hull on it safe. */
static const uint8_t SIG_ENEMY_LATCH_USE[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0xAC, 0x4D, 0x6C, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x5D, 0xC3, 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3, 0xB0, 0x4D,
    0x6C, 0x00
};
static const uint8_t MSK_ENEMY_LATCH_USE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00
};
#define ENEMY_LATCH_USE_PROLOGUE 13u

/* push ebp; mov ebp,esp; sub esp,0x44 */
#define ENEMY_ON_CONTACT_PROLOGUE 6u

/* The AI virtual machine. The pattern stays inside the prologue on purpose: from 0x0043516E the
 * retail build carries 33 bytes the alternate link does not, so anything cut past that point is a
 * pattern for one build only. */
static const uint8_t SIG_AI_RUN[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x08,
    0x50, 0xE8, 0x69, 0x17, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x89, 0x45, 0xF4,
    0xC7
};
static const uint8_t MSK_AI_RUN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define AI_RUN_PROLOGUE 9u

/* The activation scan, and the co-op wall inside it: a null body makes the whole scan return. */
const uint8_t SIG_ENEMY_ACTIVATION_SCAN[20] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xE8, 0xAC, 0x0B, 0x01, 0x00, 0x89,
    0x45, 0xFC, 0x83, 0x7D, 0xFC, 0x00, 0x75, 0x05
};
static const uint8_t MSK_ENEMY_ACTIVATION_SCAN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define ENEMY_ACTIVATION_SCAN_PROLOGUE 6u

/* An actor record becomes a body. The five bytes a detour takes end inside the push, so the
 * declared prologue is five and the head test covers those five bytes only. */
static const uint8_t SIG_SPAWN_ACTOR[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x56, 0x57, 0x8B, 0x45, 0x08, 0x83, 0xB8, 0xA8,
    0x00, 0x00, 0x00, 0x00, 0x7C, 0x31, 0x8B, 0x4D
};
#define SPAWN_ACTOR_PROLOGUE 5u

/* An actor leaves. The head is the player-hosted test and the speaker lock: the call in it is
 * player_resume and the absolute operand is the lock, both masked. Six clean prologue bytes. */
static const uint8_t SIG_MP_ENEMY_DELETE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14,
    0x81, 0xE1, 0x00, 0x20, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x0C, 0xE8, 0x86,
    0x97, 0x01, 0x00, 0xC7, 0x45, 0x0C, 0x03, 0x00, 0x00, 0x00, 0x8B, 0x55,
    0x08, 0xA1, 0x80, 0x21, 0x88, 0x00, 0x3B, 0x42
};
static const uint8_t MSK_MP_ENEMY_DELETE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};
#define ENEMY_DELETE_PROLOGUE 6u

/* The sound funnel. Twenty bytes: the prologue, the load of the sound module's own state pointer
 * and the test of the enable cell. Both absolutes are masked, so the pattern is the shape of the
 * function rather than the address of its data. */
static const uint8_t SIG_MP_BAPSOUND_PLAY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0xA1, 0x88, 0xAE, 0x5B, 0x00, 0x89,
    0x45, 0xFC, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00
};
static const uint8_t MSK_MP_BAPSOUND_PLAY[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define BAPSOUND_PLAY_PROLOGUE 6u

/* The blade clangs. The distinctive part is the cooldown test, which is why the pattern reaches
 * past it: `fld [cooldown]` against the world clock at +0x54, then the `fnstsw`/`test ah,41` pair
 * that turns the comparison into a branch. The two absolutes are the world pointer and the
 * cooldown cell and are masked. */
static const uint8_t SIG_MP_BLOCK_IMPACT_FX[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xA1, 0x60, 0x00, 0x8A, 0x00, 0xD9,
    0x05, 0xEC, 0x4C, 0x6C, 0x00, 0xD8, 0x58, 0x54, 0xDF, 0xE0, 0xF6, 0xC4,
    0x41
};
static const uint8_t MSK_MP_BLOCK_IMPACT_FX[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define BLOCK_IMPACT_FX_PROLOGUE 6u

/* The player takes a pickup. Its only caller is the player's contact handler, and its gate is bit
 * 3 of the pickup body's flags word. The rel32 behind the gate is the jump to the epilogue and is
 * masked because its distance moves with the build. */
static const uint8_t SIG_MP_PLR_PICKUP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x0C, 0x8B, 0x08, 0x83,
    0xE1, 0x08, 0x85, 0xC9, 0x74, 0x05, 0xE9, 0x32, 0x01, 0x00, 0x00, 0x83,
    0x7D, 0x08, 0x0D, 0x72, 0x10
};
static const uint8_t MSK_MP_PLR_PICKUP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_PICKUP_PROLOGUE 6u

/* The bootstrap anchor, and the reason the feature can install at all.
 *
 * A mod's entry point runs before the host's CRT start. At that moment module_install is a silent
 * no op and the task pool is cleared out later from under anything registered in it. This call is
 * the one clean success signal of the whole start: it has exactly one caller, so no reentrancy
 * guard is needed, both of its failure exits lead to shutdown, and on return the engine is fully
 * built with the four game tasks holding slots 0 to 3. Its prologue is nine clean bytes with no
 * relative operand, which sys_main, sys_frame and render_frameBegin cannot say; sys_main's five
 * byte window ends inside a call rel32 and so it is not detourable at all. */
static const uint8_t SIG_SYS_STARTUP[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x68, 0x68, 0xE9,
    0x4A, 0x00, 0x68, 0x64, 0xE9, 0x4A, 0x00, 0xA1, 0xF4, 0xE2, 0x4A, 0x00,
    0x50, 0x68, 0x4C, 0xE9, 0x4A, 0x00, 0x68, 0x80, 0x15, 0x88, 0x00
};
static const uint8_t MSK_SYS_STARTUP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define SYS_STARTUP_PROLOGUE 9u

/* The documented fallback for the anchor above, measured here so that the fallback is not found to
 * be unresolvable on the day it is needed. */
static const uint8_t SIG_CAMPAIGN_RUN[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0xC7, 0x85, 0x78,
    0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0xC7, 0x05, 0x48, 0x13, 0x88,
    0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMPAIGN_RUN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
#define CAMPAIGN_RUN_PROLOGUE 9u

/* The damage table lookup, named for what it does: it reads the difficulty and refuses an argument
 * outside 0x29 to 0x3D. An earlier draft of this table called it a damage receiver, which it is
 * not, and a receiver is what a damage detour would have to find. */
static const uint8_t SIG_IMPACT_LOOKUP[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0xA0, 0x2F, 0x87, 0x00, 0x89, 0x45, 0xFC,
    0x83, 0x7D, 0x08, 0x29, 0x7C, 0x06, 0x83, 0x7D
};
static const uint8_t MSK_IMPACT_LOOKUP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define IMPACT_LOOKUP_PROLOGUE 9u

/* The player's own task body. It opens with the same service record idiom enemy_tickAll opens
 * with, so the pattern has to run to 29 bytes to separate the two. */
static const uint8_t SIG_PLAYER_TICK_TASK[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0x24, 0x87, 0x86, 0x00, 0x83, 0x78, 0x28,
    0x00, 0x75, 0x0D, 0x8B, 0x0D, 0x24, 0x87, 0x86, 0x00, 0xC7, 0x41, 0x28,
    0x01, 0x00, 0x00, 0x00, 0x8B
};
static const uint8_t MSK_PLAYER_TICK_TASK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLAYER_TICK_TASK_PROLOGUE 9u

/* The hero spawn, and one of the five functions that address the player block absolutely: its
 * 0xEB dword clear empties bank 0 whatever the player pointer says. No other function in the image
 * performs that clear, which is what makes twenty bytes enough. */
static const uint8_t SIG_PLAYER_SPAWN_HERO[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x57, 0xB9, 0xEB, 0x00, 0x00, 0x00,
    0x33, 0xC0, 0xBF, 0x40, 0xF6, 0x6C, 0x00, 0xF3
};
static const uint8_t MSK_PLAYER_SPAWN_HERO[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
#define PLAYER_SPAWN_HERO_PROLOGUE 6u

/* The thirteen phase pipeline, which is where a second player bank would be swapped in and out.
 * diagnostics carries the same 43 bytes for its own purpose and already detours this site. */
static const uint8_t SIG_PLR_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
static const uint8_t MSK_PLR_RUN_PHASES[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define PLR_RUN_PHASES_PROLOGUE 6u

/* The entry of the function that selects the active status record. Its index test is why the site
 * is here: an index of 4 or more falls through to the assert handler, which is null in the retail
 * build, so the fourth status slot is a hard wall rather than a soft one. */
static const uint8_t SIG_STATUS_SET_ACTIVE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x53, 0x56, 0x57, 0x83, 0x7D, 0x08,
    0x00, 0x7C, 0x06, 0x83, 0x7D, 0x08, 0x04, 0x7C, 0x1A
};
#define STATUS_SET_ACTIVE_PROLOGUE 9u

/* The return-to-normal mode entry. Both operands are wildcards and the second one doubles as the
 * source of the stand descriptor cell; the two float pushes behind it are what keep the pattern
 * unique. The top half of the first float (the 1.0f) is wildcarded too, not because it varies but
 * because its bytes span into the next push opcode and read as a data-band address, which the
 * table's own no-absolute rule cannot tell from a real one. The prologue boundary is eight bytes
 * because the third instruction reads the player pointer through a five byte mov. */
static const uint8_t SIG_PLR_ENTER_STAND[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0xC7, 0x40, 0x60, 0x60,
    0x52, 0x4B, 0x00, 0x68, 0x00, 0x00, 0x80, 0x3F, 0x68, 0x00, 0x00, 0x00,
    0x40
};
static const uint8_t MSK_PLR_ENTER_STAND[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define PLR_ENTER_STAND_PROLOGUE 8u

/* The retail death, and the one detour the damage post places. Its first act after the frame is
 * nulling the current task's contact slot, which is one of the reasons a bank body must not run
 * it; the store to the dead flag at +0x394 is what closes the pattern without an operand. */
static const uint8_t SIG_PLR_ENTER_DEATH[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xA1, 0x24, 0x87, 0x86, 0x00, 0xC7,
    0x40, 0x18, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00,
    0xC7, 0x81, 0x94, 0x03, 0x00, 0x00
};
static const uint8_t MSK_PLR_ENTER_DEATH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_ENTER_DEATH_PROLOGUE 6u

/* The health write. It goes through the status pointer cell, whose CONTENT the tick banks along
 * with the hero block, so inside a bank window this writer lands on the bank's own record. The
 * damage post's revive calls it there to put the spawn health back; outside a window it writes
 * the player, which is why nothing of ours ever calls it unbanked. */
static const uint8_t SIG_STATUS_SET_HEALTH[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0x7C, 0xD5, 0x86, 0x00, 0x00, 0x75, 0x02,
    0xEB, 0x15, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0xC5, 0xFF, 0xFF, 0xFF, 0x83,
    0xC4, 0x04
};
static const uint8_t MSK_STATUS_SET_HEALTH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};
#define STATUS_SET_HEALTH_PROLOGUE 10u

/* The clamped absolute axis read, the one the steer takes the keyboard turn and the move drive
 * from. It opens with the same two zeroed locals and the same loop skip as the tap-versus-hold
 * read below; what separates the two is the frame size and which slots the locals sit in, so the
 * pattern carries both through the first loop instruction and stops before the binding table
 * operand a few bytes further on. */
static const uint8_t SIG_INPUT_DIGITAL_AXIS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x09, 0x8B, 0x45,
    0xF4, 0x83, 0xC0, 0x01
};
#define INPUT_DIGITAL_AXIS_PROLOGUE 6u

/* Analogue input. It takes no player argument, which is the reason a second player needs it. */
static const uint8_t SIG_INPUT_AXIS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0x69, 0xC0, 0x24, 0x01, 0x00, 0x00, 0x8B, 0x88,
    0x58, 0x37, 0x6D, 0x00
};
static const uint8_t MSK_INPUT_AXIS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00
};
#define INPUT_AXIS_PROLOGUE 6u

/* Button input, same reason. The optional out parameter test is what separates it from the axis
 * reader, which opens identically. */
static const uint8_t SIG_INPUT_IS_HELD[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x83, 0x7D, 0x0C, 0x00, 0x74, 0x09, 0x8B
};
#define INPUT_IS_HELD_PROLOGUE 6u

/* The tap-versus-hold read: the caller owns the accumulator, this function only advances it and
 * answers non-zero on exactly the release frame. The near-twin of the clamped axis read above;
 * see that entry for what keeps the two patterns apart. */
static const uint8_t SIG_INPUT_HOLD_RELEASE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0xC7, 0x45, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x09, 0x8B, 0x45,
    0xEC, 0x83, 0xC0, 0x01
};
#define INPUT_HOLD_RELEASE_PROLOGUE 6u

/* The module registry. A net module does not patch this, it calls it. */
static const uint8_t SIG_MODULE_INSTALL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x6A
};
#define MODULE_INSTALL_PROLOGUE 6u

/* Task registration. It returns a POINTER, and its failure value is -1 rather than null, so the
 * null check this tree usually writes would read a failure as success. */
static const uint8_t SIG_TASK_REGISTER[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x56, 0x57, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00
};
#define TASK_REGISTER_PROLOGUE 5u

/* ==============================================================================================
 * Data anchors. Not detour targets: they exist so that a cell can be read out of an operand
 * instead of written down as a number. The pure anchors live in mp_cell_sites.c since this file
 * crossed the hard size limit; what stays below are the anchor-section entries that are also
 * detour targets, because their prologue lengths belong beside their bytes.
 * ============================================================================================ */

/* The four lifecycle functions that address the hero block absolutely, alongside spawn_hero
 * above. Each gets a hull so that a call landing while a bank is swapped in operates on the
 * active bank instead of corrupting bank 0. All four prologues carry a masked operand, so on a
 * site already carrying somebody's branch the operand is read from the file, as everywhere. */

static const uint8_t SIG_PLAYER_SAVE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x48, 0x64,
    0x51, 0x68, 0xF0, 0x54, 0x4B, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_PLAYER_SAVE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define PLAYER_SAVE_PROLOGUE 9u

/* Thirty bytes rather than twenty on purpose: the twenty byte form's tail has a second candidate,
 * a sibling with the identical early-out arm and only a different frame size, which stage two
 * today rejects at the prologue and would ACCEPT the day anything detours that sibling's head.
 * Carrying the early-out's own jump, displacement masked, removes the twin for ten bytes. */
static const uint8_t SIG_PLAYER_RESTORE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF4, 0x01, 0x00, 0x00, 0x83, 0x7D, 0x08,
    0x05, 0x74, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00,
    0x00, 0x68, 0xAC, 0x03, 0x00, 0x00
};
static const uint8_t MSK_PLAYER_RESTORE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLAYER_RESTORE_PROLOGUE 9u

static const uint8_t SIG_PLAYER_RESPAWN_AT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0x44, 0xF6, 0x6C, 0x00, 0x01, 0x74, 0x02,
    0xEB, 0x55, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B
};
static const uint8_t MSK_PLAYER_RESPAWN_AT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
#define PLAYER_RESPAWN_AT_PROLOGUE 10u

static const uint8_t SIG_PLAYER_DESPAWN[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x83, 0x78, 0x0C, 0x00,
    0x74, 0x7C, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
static const uint8_t MSK_PLAYER_DESPAWN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define PLAYER_DESPAWN_PROLOGUE 8u

/* The teleport: it seats the player at a point and a heading, and it is the door every level warp
 * and every cutscene placement goes through. The pattern covers the rep stosd that clears the 0x88
 * byte ground contact block at the player record's +0x2CC and the store of the heading at +0x2A0,
 * so it says which function this is rather than only where a prologue sits. That clear is the
 * reason to call this rather than write three floats: without it the rider carry drags the player
 * back toward whatever he was standing on. Both operands name the player record and are
 * wildcarded, which is what lets the cell table read the same record out of this site as well.
 * The prologue is nine bytes, the first instruction boundary past the five a branch overwrites. */
static const uint8_t SIG_MP_PLAYER_TELEPORT[] = {
    0x55, 0x8B, 0xEC, 0x57, 0xB9, 0x22, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x8B,
    0x3D, 0x20, 0x52, 0x4B, 0x00, 0x81, 0xC7, 0xCC, 0x02, 0x00, 0x00, 0xF3,
    0xAB, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x4D, 0x0C, 0x89, 0x88, 0xA0,
    0x02, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_TELEPORT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
#define PLAYER_TELEPORT_PROLOGUE 9u


/* ==============================================================================================
 * The tables.
 * ============================================================================================ */

/* The rows below MP_SITE_PUPPET_FIRST are this file's; the rest are filled by the merge. */
static signature_t mp_sites[MP_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("bapmap_open_mover", SIG_BAPMAP_OPEN_MOVER,
                           BAPMAP_OPEN_MOVER_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapmap_close_mover", SIG_BAPMAP_CLOSE_MOVER,
                           BAPMAP_CLOSE_MOVER_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapmap_tick_mover", SIG_BAPMAP_TICK_MOVER,
                                  MSK_BAPMAP_TICK_MOVER, BAPMAP_TICK_MOVER_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapmap_tick_movers", SIG_BAPMAP_TICK_MOVERS,
                                  MSK_BAPMAP_TICK_MOVERS, BAPMAP_TICK_MOVERS_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("within_range", SIG_MP_WITHIN_RANGE, WITHIN_RANGE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("resolve_target", SIG_RESOLVE_TARGET, RESOLVE_TARGET_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_tick_all", SIG_ENEMY_TICK_ALL, MSK_ENEMY_TICK_ALL,
                                  ENEMY_TICK_ALL_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_latch_use", SIG_ENEMY_LATCH_USE, MSK_ENEMY_LATCH_USE,
                                  ENEMY_LATCH_USE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("ai_run", SIG_AI_RUN, MSK_AI_RUN, AI_RUN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_activation_scan", SIG_ENEMY_ACTIVATION_SCAN,
                                  MSK_ENEMY_ACTIVATION_SCAN, ENEMY_ACTIVATION_SCAN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("spawn_actor", SIG_SPAWN_ACTOR, SPAWN_ACTOR_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_delete", SIG_MP_ENEMY_DELETE, MSK_MP_ENEMY_DELETE,
                                  ENEMY_DELETE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("sys_startup", SIG_SYS_STARTUP, MSK_SYS_STARTUP,
                                  SYS_STARTUP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("campaign_run", SIG_CAMPAIGN_RUN, MSK_CAMPAIGN_RUN,
                                  CAMPAIGN_RUN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("impact_lookup", SIG_IMPACT_LOOKUP, MSK_IMPACT_LOOKUP,
                                  IMPACT_LOOKUP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_tick_task", SIG_PLAYER_TICK_TASK, MSK_PLAYER_TICK_TASK,
                                  PLAYER_TICK_TASK_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_spawn_hero", SIG_PLAYER_SPAWN_HERO,
                                  MSK_PLAYER_SPAWN_HERO, PLAYER_SPAWN_HERO_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_run_phases", SIG_PLR_RUN_PHASES, MSK_PLR_RUN_PHASES,
                                  PLR_RUN_PHASES_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_pickup", SIG_MP_PLR_PICKUP, MSK_MP_PLR_PICKUP,
                                  PLR_PICKUP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_enter_stand", SIG_PLR_ENTER_STAND, MSK_PLR_ENTER_STAND,
                                  PLR_ENTER_STAND_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_enter_death", SIG_PLR_ENTER_DEATH, MSK_PLR_ENTER_DEATH,
                                  PLR_ENTER_DEATH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("status_set_active", SIG_STATUS_SET_ACTIVE, STATUS_SET_ACTIVE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("status_set_health", SIG_STATUS_SET_HEALTH,
                                  MSK_STATUS_SET_HEALTH, STATUS_SET_HEALTH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("input_digital_axis", SIG_INPUT_DIGITAL_AXIS,
                           INPUT_DIGITAL_AXIS_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("input_axis", SIG_INPUT_AXIS, MSK_INPUT_AXIS,
                                  INPUT_AXIS_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("input_is_held", SIG_INPUT_IS_HELD, INPUT_IS_HELD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("input_hold_release", SIG_INPUT_HOLD_RELEASE,
                           INPUT_HOLD_RELEASE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("module_install", SIG_MODULE_INSTALL, MODULE_INSTALL_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("task_register", SIG_TASK_REGISTER, TASK_REGISTER_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("death_latch_set", SIG_DEATH_LATCH_SET, MSK_DEATH_LATCH_SET),
    SIGNATURE_ENTRY_MASKED("status_pointer_write", SIG_STATUS_POINTER_WRITE,
                           MSK_STATUS_POINTER_WRITE),
    SIGNATURE_ENTRY_MASKED("obj_list_walk", SIG_OBJ_LIST_WALK, MSK_OBJ_LIST_WALK),
    SIGNATURE_ENTRY_MASKED("substep_alpha_store", SIG_SUBSTEP_ALPHA_STORE,
                           MSK_SUBSTEP_ALPHA_STORE),
    SIGNATURE_ENTRY_MASKED("substep_rate_switch", SIG_SUBSTEP_RATE_SWITCH,
                           MSK_SUBSTEP_RATE_SWITCH),
    SIGNATURE_ENTRY_MASKED("frame_cap_release", SIG_FRAME_CAP_RELEASE, MSK_FRAME_CAP_RELEASE),
    SIGNATURE_ENTRY_MASKED("victim_is_player_set", SIG_VICTIM_IS_PLAYER_SET,
                           MSK_VICTIM_IS_PLAYER_SET),
    SIGNATURE_ENTRY_MASKED("victim_is_player_test", SIG_VICTIM_IS_PLAYER_TEST,
                           MSK_VICTIM_IS_PLAYER_TEST),
    SIGNATURE_ENTRY_MASKED("module_list_link", SIG_MODULE_LIST_LINK, MSK_MODULE_LIST_LINK),
    SIGNATURE_ENTRY_MASKED("task_staging", SIG_TASK_STAGING, MSK_TASK_STAGING),
    SIGNATURE_ENTRY_MASKED("clock_ticks", SIG_CLOCK_TICKS, MSK_CLOCK_TICKS),
    SIGNATURE_ENTRY_MASKED("task_count", SIG_MP_TASK_COUNT, MSK_MP_TASK_COUNT),
    SIGNATURE_ENTRY_MASKED("plr_attack_hold", SIG_PLR_ATTACK_HOLD, MSK_PLR_ATTACK_HOLD),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_save", SIG_PLAYER_SAVE, MSK_PLAYER_SAVE,
                                  PLAYER_SAVE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_restore", SIG_PLAYER_RESTORE, MSK_PLAYER_RESTORE,
                                  PLAYER_RESTORE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_respawn_at", SIG_PLAYER_RESPAWN_AT,
                                  MSK_PLAYER_RESPAWN_AT, PLAYER_RESPAWN_AT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_despawn", SIG_PLAYER_DESPAWN, MSK_PLAYER_DESPAWN,
                                  PLAYER_DESPAWN_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("contact_slot_store", SIG_CONTACT_SLOT_STORE, MSK_CONTACT_SLOT_STORE),
    SIGNATURE_ENTRY_DETOUR_MASKED("post_contact", SIG_POST_CONTACT, MSK_POST_CONTACT, 6u),
    SIGNATURE_ENTRY_MASKED("death_desc_store", SIG_DEATH_DESC_STORE, MSK_DEATH_DESC_STORE),
    SIGNATURE_ENTRY_DETOUR_MASKED("wkernel_create", SIG_WKERNEL_CREATE, MSK_WKERNEL_CREATE, 6u),
    SIGNATURE_ENTRY_MASKED("shot_kind_remap", SIG_MP_SHOT_KIND_REMAP, MSK_MP_SHOT_KIND_REMAP),
    SIGNATURE_ENTRY_MASKED("enemy_live_peak", SIG_ENEMY_LIVE_PEAK, MSK_ENEMY_LIVE_PEAK),
    SIGNATURE_ENTRY_MASKED("detail_level_tick", SIG_DETAIL_LEVEL_TICK,
                           MSK_DETAIL_LEVEL_TICK),
    SIGNATURE_ENTRY_MASKED("detail_level_scan", SIG_DETAIL_LEVEL_SCAN,
                           MSK_DETAIL_LEVEL_SCAN),
    SIGNATURE_ENTRY_MASKED("story_save_pair", SIG_STORY_SAVE_PAIR, MSK_STORY_SAVE_PAIR),
    SIGNATURE_ENTRY_MASKED("story_restore_pair", SIG_STORY_RESTORE_PAIR, MSK_STORY_RESTORE_PAIR),
    SIGNATURE_ENTRY_MASKED("story_copy_pair", SIG_STORY_COPY_PAIR, MSK_STORY_COPY_PAIR),
    SIGNATURE_ENTRY_MASKED("story_clear", SIG_STORY_CLEAR, MSK_STORY_CLEAR),
    SIGNATURE_ENTRY_MASKED("story_op_set_glob", SIG_STORY_OP_SET_GLOB, MSK_STORY_OP_SET_GLOB),
    SIGNATURE_ENTRY_MASKED("story_key_use", SIG_STORY_KEY_USE, MSK_STORY_KEY_USE),
    SIGNATURE_ENTRY_MASKED("story_pause_inv", SIG_STORY_PAUSE_INV, MSK_STORY_PAUSE_INV),
    SIGNATURE_ENTRY_MASKED("aiflag_save", SIG_AIFLAG_SAVE, MSK_AIFLAG_SAVE),
    SIGNATURE_ENTRY_MASKED("aiflag_restore", SIG_AIFLAG_RESTORE, MSK_AIFLAG_RESTORE),
    SIGNATURE_ENTRY_MASKED("aiflag_set_timed", SIG_AIFLAG_SET_TIMED, MSK_AIFLAG_SET_TIMED),
    SIGNATURE_ENTRY_MASKED("aiflag_expire", SIG_AIFLAG_EXPIRE, MSK_AIFLAG_EXPIRE),
    SIGNATURE_ENTRY_MASKED("aiflag_init_clear", SIG_AIFLAG_INIT_CLEAR, MSK_AIFLAG_INIT_CLEAR),
    SIGNATURE_ENTRY_MASKED("aiflag_close_clear", SIG_AIFLAG_CLOSE_CLEAR, MSK_AIFLAG_CLOSE_CLEAR),
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_on_contact", SIG_ENEMY_ON_CONTACT,
                                  MSK_ENEMY_ON_CONTACT, ENEMY_ON_CONTACT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("enemy_pool_new", SIG_ENEMY_POOL_NEW, MSK_ENEMY_POOL_NEW),
    SIGNATURE_ENTRY_MASKED("enemy_pool_walk", SIG_ENEMY_POOL_WALK, MSK_ENEMY_POOL_WALK),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_build", SIG_MP_SWMENU_BUILD, MSK_MP_SWMENU_BUILD, 6u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_open", SIG_MP_SWMENU_OPEN, MSK_MP_SWMENU_OPEN, 8u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_close", SIG_MP_SWMENU_CLOSE, MSK_MP_SWMENU_CLOSE, 8u),
    /* No prologue. The third instruction here is a relative call, so five bytes cannot be
     * reached without copying a displacement that means something else at the trampoline.
     * This is the only head in either table left on stage one alone. */
    SIGNATURE_ENTRY_MASKED("swmenu_pump_frame", SIG_MP_SWMENU_PUMP_FRAME,
                           MSK_MP_SWMENU_PUMP_FRAME),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_take_nav", SIG_MP_SWMENU_TAKE_NAV,
                                  MSK_MP_SWMENU_TAKE_NAV, SWMENU_TAKE_NAV_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_focus_id", SIG_MP_SWMENU_FOCUS_ID,
                                  MSK_MP_SWMENU_FOCUS_ID, 8u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_widget_state", SIG_MP_SWMENU_WIDGET_STATE,
                                  MSK_MP_SWMENU_WIDGET_STATE, 7u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_set_edit_text", SIG_MP_SWMENU_SET_EDIT_TEXT,
                                  MSK_MP_SWMENU_SET_EDIT_TEXT, 6u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_get_edit_text", SIG_MP_SWMENU_GET_EDIT_TEXT,
                                  MSK_MP_SWMENU_GET_EDIT_TEXT, 5u),
    SIGNATURE_ENTRY_DETOUR_MASKED("title_main_menu", SIG_MP_TITLE_MAIN_MENU,
                                  MSK_MP_TITLE_MAIN_MENU, 9u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_last_focus", SIG_MP_SWMENU_LAST_FOCUS,
                                  MSK_MP_SWMENU_LAST_FOCUS, 8u),
    SIGNATURE_ENTRY_DETOUR_MASKED("swwidget_focus_by_id", SIG_MP_SWWIDGET_FOCUS_BY_ID,
                                  MSK_MP_SWWIDGET_FOCUS_BY_ID, 6u),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapsound_play", SIG_MP_BAPSOUND_PLAY, MSK_MP_BAPSOUND_PLAY,
                                  BAPSOUND_PLAY_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("npc_block_impact_fx", SIG_MP_BLOCK_IMPACT_FX,
                                  MSK_MP_BLOCK_IMPACT_FX, BLOCK_IMPACT_FX_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("campaign_round", SIG_MP_CAMPAIGN_ROUND, MSK_MP_CAMPAIGN_ROUND),
    SIGNATURE_ENTRY_MASKED("campaign_load", SIG_MP_CAMPAIGN_LOAD, MSK_MP_CAMPAIGN_LOAD),
    SIGNATURE_ENTRY_MASKED("level_handover", SIG_MP_LEVEL_HANDOVER, MSK_MP_LEVEL_HANDOVER),
    SIGNATURE_ENTRY_DETOUR_MASKED("path_prefix", SIG_MP_PATH_PREFIX, MSK_MP_PATH_PREFIX, 5u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hero_swap", SIG_MP_HERO_SWAP, MSK_MP_HERO_SWAP, 6u),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_teleport", SIG_MP_PLAYER_TELEPORT,
                                  MSK_MP_PLAYER_TELEPORT, PLAYER_TELEPORT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("save_load_named", SIG_MP_SAVE_LOAD_NAMED,
                                  MSK_MP_SAVE_LOAD_NAMED, 6u),
    SIGNATURE_ENTRY_DETOUR_MASKED("load_game_screen", SIG_MP_LOAD_GAME_SCREEN,
                                  MSK_MP_LOAD_GAME_SCREEN, LOAD_GAME_SCREEN_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("contact_node_read", SIG_CONTACT_NODE_READ, MSK_CONTACT_NODE_READ)
};


static uintptr_t mp_death_latch_address[MP_DEATH_LATCH_MATCHES];

/* ==============================================================================================
 * Resolution.
 * ============================================================================================ */

/* The second table is copied behind the first once, so every accessor below sees one array
 * indexed by mp_site_t and the diagnostics walk both halves in one loop. The copy is of the
 * template rows, so the resolved addresses land here and the template stays what its file says.
 * The lengths agree by a static assert in the second file; nothing here has to check them. */
static void merge_puppet_sites(void)
{
    static bool        merged;
    size_t             count = 0;
    const signature_t *puppet = mp_signatures_puppet_sites(&count);
    size_t             index;

    if (merged) {
        return;
    }
    merged = true;
    for (index = 0;
         index < count && (size_t)MP_SITE_PUPPET_FIRST + index < MP_SITE_COUNT; ++index) {
        mp_sites[(size_t)MP_SITE_PUPPET_FIRST + index] = puppet[index];
    }
}

size_t mp_signatures_expected_matches(mp_site_t site)
{
    return (site == MP_SITE_DEATH_LATCH_SET) ? MP_DEATH_LATCH_MATCHES : 1u;
}

size_t mp_signatures_prologue(mp_site_t site)
{
    if ((size_t)site >= MP_SITE_COUNT) {
        return 0u;
    }
    merge_puppet_sites();
    return mp_sites[site].detour_prologue;
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    if ((size_t)site >= MP_SITE_COUNT) {
        return 0u;
    }
    merge_puppet_sites();
    return mp_sites[site].address;
}

uintptr_t mp_signatures_death_latch(size_t index)
{
    if (index >= MP_DEATH_LATCH_MATCHES) {
        return 0u;
    }
    return mp_death_latch_address[index];
}

const signature_t *mp_signatures_sites(size_t *count)
{
    merge_puppet_sites();
    if (count != NULL) {
        *count = MP_SITE_COUNT;
    }
    return mp_sites;
}

const signature_t *mp_signatures_site(mp_site_t site)
{
    if ((size_t)site >= MP_SITE_COUNT) {
        return NULL;
    }
    merge_puppet_sites();
    return &mp_sites[site];
}

/* The death latch is the one site whose count is two, so it cannot go through the same arm as the
 * rest, which treats anything but one as a build it does not know. Both addresses are kept: the
 * first is what the site reports, and the second is what makes the pair evidence rather than a
 * lookup. */
static void resolve_death_latch(bool log_every_site)
{
    signature_t *site = &mp_sites[MP_SITE_DEATH_LATCH_SET];
    size_t       hits;
    size_t       index;

    for (index = 0; index < MP_DEATH_LATCH_MATCHES; ++index) {
        mp_death_latch_address[index] = 0u;
    }

    hits = signature_count_matches(site->bytes, site->mask, site->size,
                                   mp_death_latch_address, MP_DEATH_LATCH_MATCHES);
    if (hits != MP_DEATH_LATCH_MATCHES) {
        site->address = 0u;
        for (index = 0; index < MP_DEATH_LATCH_MATCHES; ++index) {
            mp_death_latch_address[index] = 0u;
        }
        log_warning("  site %-22s -> NOT RESOLVED (%u matches, expected %u)",
                    site->name, (unsigned)hits, (unsigned)MP_DEATH_LATCH_MATCHES);
        return;
    }

    site->address = mp_death_latch_address[0];
    if (log_every_site) {
        log_info("  site %-22s -> %08X and %08X", site->name,
                 (unsigned)mp_death_latch_address[0], (unsigned)mp_death_latch_address[1]);
    }
}

/* Which arm answered for one site.
 *
 * The distinction is not bookkeeping. A site found by its tail is a site another module has already
 * written a branch over, and that is the single most useful thing this DLL can report about the
 * process it is running in: it cannot be seen offline, it changes with the load order, and it is
 * the precondition for every operand at that site being read from the file rather than from
 * memory. Reporting only the address would hide it, and a reader cannot reconstruct it afterwards.
 */
typedef enum resolve_arm {
    RESOLVE_FAILED,
    RESOLVE_WHOLE_PATTERN,
    RESOLVE_BY_TAIL
} resolve_arm_t;

static resolve_arm_t resolve_one(signature_t *site)
{
    uintptr_t address = 0u;
    size_t    hits;

    site->address = 0u;

    hits = signature_count_matches(site->bytes, site->mask, site->size, &address, 1);
    if (hits == 1u) {
        site->address = address;
        return RESOLVE_WHOLE_PATTERN;
    }

    if (site->detour_prologue == 0u) {
        return RESOLVE_FAILED;
    }

    address = signature_find_detour_target(site->bytes, site->mask, site->size,
                                           site->detour_prologue);
    if (address == 0u) {
        return RESOLVE_FAILED;
    }

    site->address = address;
    return RESOLVE_BY_TAIL;
}

size_t mp_signatures_resolve(bool log_every_site)
{
    size_t resolved = 0;
    size_t detoured = 0;
    size_t index;

    merge_puppet_sites();
    for (index = 0; index < MP_SITE_COUNT; ++index) {
        signature_t  *site = &mp_sites[index];
        resolve_arm_t arm;

        if (index == (size_t)MP_SITE_DEATH_LATCH_SET) {
            resolve_death_latch(log_every_site);
            resolved += (site->address != 0u) ? 1u : 0u;
            continue;
        }

        arm = resolve_one(site);
        switch (arm) {
        case RESOLVE_FAILED:
            log_warning("  site %-22s NOT RESOLVED, everything that needs it is disabled",
                        site->name);
            break;
        case RESOLVE_BY_TAIL:
            ++resolved;
            ++detoured;
            log_info("  site %-22s -> %08X, found by its tail because another module has already "
                     "branched over its head", site->name, (unsigned)site->address);
            break;
        case RESOLVE_WHOLE_PATTERN:
        default:
            ++resolved;
            if (log_every_site) {
                log_info("  site %-22s -> %08X", site->name, (unsigned)site->address);
            }
            break;
        }
    }

    log_info("%u of %u sites resolved, %u of them already carrying another module's branch",
             (unsigned)resolved, (unsigned)MP_SITE_COUNT, (unsigned)detoured);
    return resolved;
}
