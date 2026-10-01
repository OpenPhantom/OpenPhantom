/* mp_signatures_puppet.c: the body, animation, sabre and impact effect sites, the second half of
 * the pattern table.
 *
 * SIZE NOTE: over 600 lines, and all but a few dozen of them are byte patterns and their masks.
 * There is no logic here to split; the file is a table, and the only meaningful seam is another
 * split by engine subsystem, which is what produced this file in the first place. If it has to
 * happen again, the sabre and impact effect block is the half that shares nothing with the rest.
 *
 * Every rule of the main pattern file holds here unchanged: an absolute address is never a
 * required byte, it is wildcarded and read out of the operand; a relative displacement is
 * wildcarded for the reason the alternate link demonstrates; and every function site is declared
 * as a detour target so the two stage resolver survives another module's branch on its head.
 * Five of these sites are load bearing for that rule today rather than in theory: the swing
 * starter carries a branch from the dev overlay's melee watch in any configuration where that DLL
 * is on, the deflect, the parry and the armed contact carry one from its combat guards, and the
 * push starter carries this feature's own hull.
 *
 * The one pattern here that is out of scale is the swing starter, 252 bytes where twenty would
 * find the function. It is that long on purpose: the site exists to carry the operand of the
 * swing table, the one place the table's base address can be read from, and that operand is the
 * compare against the overlay ordinal deep in the body. A shorter pattern would find the function
 * and not the table.
 *
 * Every pattern here matches exactly once, whole and by its tail behind the prologue, on all six
 * images, the recompile included; the recompile keeps the world draw, the object pool, the clip
 * players and the node sphere where retail has them and moves every site from the player record
 * onward by 0x60, with the data 0x50 lower.
 */
#include "mp_signatures_puppet.h"

#include "mp_signatures.h"
#include "mp_signatures_blade.h"
#include "mp_signatures_enemy.h"
#include "mp_signatures_foot.h"

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* The world draw. Its 0x4D8 frame is the 255 entry gather array with no bounds check, so a detour
 * that changes the frame size changes where an overrun lands. Two other DLLs already detour it,
 * and the puppet's node rotations make a third.
 *
 * The pattern reaches past the prologue as far as the frame set-up's three weight operands,
 * because that block is the only place the interpolation weight and the two cells that choose
 * between its live and its frozen form can be read from. All five absolute operands in the run
 * are wildcarded; three of them are the ones the cell table reads back, at +0x3C, +0x48 and
 * +0x51. */
static const uint8_t SIG_BAPOBJ_DRAW_ALL[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD8, 0x04, 0x00, 0x00,
    0xC7, 0x85, 0xF0, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xE4, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xDC, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xA8, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xEC, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x85, 0xE0, 0xFB, 0xFF, 0xFF,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x0C,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x8D, 0xE0, 0xFB, 0xFF, 0xFF,
    0x8B, 0x95, 0xE0, 0xFB, 0xFF, 0xFF,
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_BAPOBJ_DRAW_ALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define BAPOBJ_DRAW_ALL_PROLOGUE 9u

/* The pool's own allocator and its release, needed only so that the capacity instrument can put a
 * load on the pool it measures. thing_alloc names the object list a second time, which is worth
 * having: the cell then rests on two readings from two different functions.
 *
 * thing_free stops at 22 bytes on purpose. The next byte is the first of a string address, and a
 * pattern that ends inside an operand carries a fragment of an address as a required byte. That is
 * the same trap enemy_latchUsePressed fell into, and there it cost the recompile.
 */
static const uint8_t SIG_MP_THING_ALLOC[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0xDC, 0x01, 0x8A, 0x00, 0x50, 0xE8, 0xE1,
    0xC6, 0x05, 0x00, 0x83, 0xC4, 0x04, 0x89, 0x45, 0xFC, 0x83
};
static const uint8_t MSK_MP_THING_ALLOC[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define THING_ALLOC_PROLOGUE 9u

static const uint8_t SIG_MP_THING_FREE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC, 0x83, 0x7D,
    0xFC, 0x00, 0x75, 0x1B, 0x68, 0x99, 0x04, 0x00, 0x00, 0x68
};
#define THING_FREE_PROLOGUE 7u

/* Plays one clip on one body. The damage post calls it for the death clip, mode 4, the mode the
 * retail death path itself uses; the puppet calls it for every base clip. The pattern ends on the
 * assert line push and stops before the assert's string address. */
static const uint8_t SIG_BAPOBJ_PLAY_CLIP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xF0,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4D, 0xF0, 0x83, 0x79,
    0x14, 0x00, 0x75, 0x1B, 0x68, 0x09, 0x05, 0x00, 0x00
};
#define BAPOBJ_PLAY_CLIP_PROLOGUE 6u

/* Plays the upper-body overlay clip on one body, the same shape as the base clip player one
 * function along. The pattern ends before the assert string address. */
static const uint8_t SIG_BAPOBJ_PLAY_OVERLAY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xF4,
    0x8B, 0x4D, 0xF4, 0x83, 0x79, 0x14, 0x00, 0x75, 0x1B
};
#define BAPOBJ_PLAY_OVERLAY_PROLOGUE 6u

/* Stops the overlay clip: mode 1 fades the slot out over the seconds argument, any other mode
 * resets the track and writes -1 into the slot, which is the arm the puppet uses when the far
 * body's overlay has ended. The pattern carries the mode test, the slot load at +0xF8 and the
 * puppet load through the thing at +0x9C, and stops before the reset call's displacement. Its
 * near twin, the base clip stop one function earlier, reads the slot at +0xEC and parts company
 * at the tenth byte. No operand, so no mask. Out of the retail image, at 0x00412BAF,
 * `void __cdecl (void *obj, float seconds, int32_t mode)`:
 *
 *     00412BAF  55 8B EC 51                    push ebp / mov ebp, esp / push ecx
 *     00412BB3  8B 45 08 89 45 FC              eax = obj; [ebp-4] = obj
 *     00412BB9  83 7D 10 01 75 25              cmp mode, 1 / jne the reset arm
 *     00412BBF  8B 4D 0C 51                    push seconds
 *     00412BC3  8B 55 FC 8B 82 F8 00 00 00 50  push [obj+0xF8], the overlay slot
 *     00412BCD  8B 4D FC 8B 91 9C 00 00 00     edx = [obj+0x9C], the thing
 *     00412BD6  8B 42 18 50                    push [thing+0x18], the puppet
 *     00412BDA  E8 ..                          call the track fade, where the pattern stops
 *
 * The prologue of seven ends on `mov eax, [ebp+8]`; the base clip stop at 0x00412B1A opens with
 * the same seven bytes and then reads its slot through `8B 4D FC 83 B9 EC 00 00 00 00`. */
static const uint8_t SIG_MP_STOP_OVERLAY_CLIP[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC, 0x83, 0x7D,
    0x10, 0x01, 0x75, 0x25, 0x8B, 0x4D, 0x0C, 0x51, 0x8B, 0x55, 0xFC, 0x8B,
    0x82, 0xF8, 0x00, 0x00, 0x00, 0x50, 0x8B, 0x4D, 0xFC, 0x8B, 0x91, 0x9C,
    0x00, 0x00, 0x00, 0x8B, 0x42, 0x18, 0x50
};
#define STOP_OVERLAY_CLIP_PROLOGUE 7u

/* One skeleton node's sphere in world space, the only way to ask where a body's sabre node is
 * standing. It is carried because the effect layer needs that point to put a flash and a spark on
 * the puppet's blade; nothing of this feature detours it, but the dev overlay's borrowed weapon
 * draw does, with the same six byte prologue this pattern declares, so with that DLL loaded first
 * the whole pattern matches nothing and the site resolves through stage two on its tail, which
 * is unique on all six images. Its hook returns the radius in ST(0), so calling through it
 * answers what the engine answers. The pattern runs from the prologue through the null check on
 * the render thing to the assert line push, and stops before the two string addresses that
 * follow, so it holds no operand at all and needs no mask, which no other row here can say. The
 * prologue is six, the first instruction boundary at or past five.
 */
static const uint8_t SIG_MP_BAPOBJ_NODE_SPHERE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xCC,
    0x8B, 0x4D, 0xCC, 0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00, 0x89, 0x55, 0xC0,
    0x83, 0x7D, 0xC0, 0x00, 0x75, 0x1A, 0x68, 0x79, 0x0A, 0x00, 0x00
};
#define BAPOBJ_NODE_SPHERE_PROLOGUE 6u

/* The weapon selector, five modes in one entry. Mode 2 sets a slot with no test, which is what a
 * puppet's replicated weapon uses. The operand at +0x07 is the player pointer, masked; the frame
 * size and the early-out (no aux action) that follow are what keep the pattern unique. The tail
 * ends before the near jump's displacement, which is a relative operand. The cmp immediate at
 * +0x0e is wildcarded too, not because it varies but because 83 78 64 00 reads as the data-band
 * address 0x00647883, which the table's own no-absolute gate rightly refuses. */
static const uint8_t SIG_PLR_SET_WEAPON[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x83,
    0x78, 0x64, 0x00, 0x74, 0x07, 0x33, 0xC0, 0xE9
};
static const uint8_t MSK_PLR_SET_WEAPON[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_SET_WEAPON_PROLOGUE 6u

/* The blade's light tick, the one of the engine's two blade ticks a puppet still runs: it gives
 * the light's slot back and places the light at the blade node. The twelve byte frame, the player
 * pointer read (masked) and the add of 0x218, the embedded light inside the record, pin it; the
 * tail stops before the call's displacement, which is relative. The last zero byte of the 0x218
 * is masked, because 00 50 6A 00 reads as the data-band address 0x006A5000 to the same gate. */
static const uint8_t SIG_PLR_TICK_BLADE_LIGHT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x05,
    0x18, 0x02, 0x00, 0x00, 0x50, 0x6A, 0x00, 0x8B, 0x0D, 0x60, 0x00, 0x8A,
    0x00, 0x51, 0xE8
};
static const uint8_t MSK_PLR_TICK_BLADE_LIGHT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF
};
#define PLR_TICK_BLADE_LIGHT_PROLOGUE 6u

/* The force push starter: the early-out on a running aux action, then the meter moved into the
 * charge. The cmp immediate at +0x0b is masked for the same reason as in plr_set_weapon: the four
 * bytes 83 78 64 00 read as the data-band address 0x00647883, which the table's own no-absolute
 * gate refuses. The two six byte record moves at the end are what keep the pattern unique. */
static const uint8_t SIG_PLR_START_FORCE_PUSH[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x83, 0x78, 0x64, 0x00,
    0x74, 0x02, 0xEB, 0x4B, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x15,
    0x20, 0x52, 0x4B, 0x00, 0x8B, 0x82, 0xC0, 0x02, 0x00, 0x00, 0x89, 0x81,
    0xC4, 0x02, 0x00, 0x00
};
static const uint8_t MSK_PLR_START_FORCE_PUSH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_START_FORCE_PUSH_PROLOGUE 8u

/* The blade against the LEVEL, which is a different test from the blade against a body and lives
 * in its own function. Every substep a swing runs, the contact node's world position is taken and
 * a wall probe is swept from where that node stood last substep to where it stands now, at the
 * swing row's radius; a Jedi that catches a wall gets the impact voice, a flash, a spark emitter
 * and a scorch decal, and is pushed off the wall at one unit a second.
 *
 * The pattern runs from the frame set-up to the discarded radius: the node sphere's answer is a
 * float this caller does not want, and the fstp that throws it away is a distinctive tail. Masked
 * are the two reads of the player pointer, whose operand is the one thing nothing may take from a
 * head another module may have branched over, and the call displacement, which is relative. The
 * prologue is six, one instruction boundary past five. */
static const uint8_t SIG_MP_PLR_TEST_SWING_WORLD[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x8D, 0x45, 0xE0, 0x50, 0x8B, 0x0D,
    0x20, 0x52, 0x4B, 0x00, 0x8B, 0x51, 0x58, 0x52, 0xA1, 0x20, 0x52, 0x4B,
    0x00, 0x8B, 0x48, 0x0C, 0x51, 0xE8, 0x2D, 0x5B, 0xFC, 0xFF, 0xDD, 0xD8
};
static const uint8_t MSK_MP_PLR_TEST_SWING_WORLD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};
#define PLR_TEST_SWING_WORLD_PROLOGUE 6u

/* The swing starter, the one function that arms a sabre swing: it stamps the contact node,
 * radius, direction class and impact code out of a row of the swing table, then plays the row's
 * clip, on the overlay channel when the clip is the midair one and on the base channel otherwise.
 * That last compare is where the pattern ends, because its operand is the swing table itself, the
 * base of the 28 rows whose clip column the puppet must never start from state. The earlier
 * table reads name the node, radius, direction and impact columns, which are the base plus a
 * field offset and so are not the base; they are masked like every other operand.
 *
 * Masked besides those: the player pointer at every one of its twelve reads, the two call
 * displacements, and the zero immediate of the hero test at +0xA8, whose four bytes 83 7A 6C 00
 * read as the data-band address 0x006C7A83 to the table's own gate. The prologue is eight, the
 * first instruction boundary at or past five, and it ends on the player pointer load, whose
 * operand is exactly the one nothing may read out of this site: the dev overlay's melee watch
 * writes its branch over these eight bytes, and a read there would return that branch's jump
 * distance. The player pointer is read from this site at +0x20 instead, behind the head.
 *
 * The head and the closing compare, out of the retail image at 0x0044E858:
 *
 *     0044E858  55 8B EC A1 [20 52 4B 00]        push ebp / mov ebp, esp / eax = pr   (+0x04)
 *     0044E860  8B 4D 08 89 88 A8 01 00 00       pr->swingIndex = row
 *     0044E869  8B 55 08 C1 E2 05                edx = row * 0x20
 *     0044E86F  8B 82 [08 4E 4B 00] 50           push table[row].node, table + 8      (+0x19)
 *     0044E876  8B 0D [20 52 4B 00]              ecx = pr, the cell's reading         (+0x20)
 *     0044E87C  8B 51 0C 52 E8 [..]              push pr->hActor / call findNodeByNameId
 *     ...       the radius, direction class and impact stores through table + 0xC, + 0x14
 *               and + 0x10 at +0x59, +0x7D and +0x98
 *     0044E8FD  83 7A 6C [00] 74 0B              hero index 0?                        (+0xA8)
 *     0044E90E  C7 82 AC 00 00 00 25 00 00 00    contactCode = 0x25
 *     0044E934  C7 80 AC 00 00 00 21 00 00 00    contactCode = 0x21
 *     0044E94D  83 B9 [00 4E 4B 00] 55           cmp table[row].anim, 0x55            (+0xF7)
 *
 * The player pointer is masked at +0x04, +0x20, +0x32, +0x3B, +0x44, +0x5F, +0x74, +0x8F, +0xA1,
 * +0xAC, +0xB8, +0xCA and +0xDE, the two call displacements at +0x29 and +0xD3. The operand at
 * +0xF7 is the one the swing table cell is read from, because it is the only reading of the base
 * itself; on the recompile it is 0x004B4DB0 and the site 0x0044E7F8. */
static const uint8_t SIG_MP_PLR_START_SWING[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x4D, 0x08, 0x89, 0x88, 0xA8, 0x01, 0x00,
    0x00, 0x8B, 0x55, 0x08, 0xC1, 0xE2, 0x05, 0x8B, 0x82, 0x08, 0x4E, 0x4B, 0x00, 0x50, 0x8B, 0x0D,
    0x20, 0x52, 0x4B, 0x00, 0x8B, 0x51, 0x0C, 0x52, 0xE8, 0xBE, 0x57, 0xFC, 0xFF, 0x83, 0xC4, 0x08,
    0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0x89, 0x41, 0x58, 0x8B, 0x15, 0x20, 0x52, 0x4B, 0x00, 0x8B,
    0x42, 0x0C, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x51, 0x58, 0x89, 0x90, 0xA8, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x05, 0xD9, 0x80, 0x0C, 0x4E, 0x4B, 0x00, 0x8B, 0x0D, 0x20,
    0x52, 0x4B, 0x00, 0x8B, 0x51, 0x0C, 0xD9, 0x9A, 0xB0, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x08, 0xC1,
    0xE0, 0x05, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x51, 0x0C, 0x8B, 0x80, 0x14, 0x4E, 0x4B,
    0x00, 0x89, 0x82, 0xB4, 0x00, 0x00, 0x00, 0x8B, 0x4D, 0x08, 0xC1, 0xE1, 0x05, 0x8B, 0x15, 0x20,
    0x52, 0x4B, 0x00, 0x8B, 0x42, 0x0C, 0x8B, 0x89, 0x10, 0x4E, 0x4B, 0x00, 0x89, 0x48, 0x0C, 0x8B,
    0x15, 0x20, 0x52, 0x4B, 0x00, 0x83, 0x7A, 0x6C, 0x00, 0x74, 0x0B, 0xA1, 0x20, 0x52, 0x4B, 0x00,
    0x83, 0x78, 0x6C, 0x01, 0x75, 0x26, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x51, 0x0C, 0xC7,
    0x82, 0xAC, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x48,
    0x0C, 0x51, 0xE8, 0xC8, 0x44, 0xFC, 0xFF, 0x83, 0xC4, 0x04, 0xEB, 0x13, 0x8B, 0x15, 0x20, 0x52,
    0x4B, 0x00, 0x8B, 0x42, 0x0C, 0xC7, 0x80, 0xAC, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x8B,
    0x4D, 0x08, 0xC1, 0xE1, 0x05, 0x83, 0xB9, 0x00, 0x4E, 0x4B, 0x00, 0x55
};
static const uint8_t MSK_MP_PLR_START_SWING[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
#define PLR_START_SWING_PROLOGUE 8u

/* The three calls the armed contact's third arm makes, and the effect layer makes in its place
 * for a puppet whose blade connects. None of them is detoured; they are called.
 *
 * The impact voice enforces a global cooldown of a fifth of a second on the record it is called
 * against, which is why it may only ever run inside the puppet's own bank window. The pattern
 * carries that cooldown's read, its comparison against the float zero, and the 0.2 immediate it
 * writes back, which together are what make it unique; the player pointer at both readings and
 * the float zero's address are masked. The prologue is eight, the first instruction boundary at
 * or past five, because the second instruction reads the player pointer through a five byte mov.
 */
static const uint8_t SIG_MP_PLR_PLAY_IMPACT_VOICE[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0xD9, 0x40, 0x34, 0xD8,
    0x1D, 0xA4, 0x86, 0x4A, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x75, 0x02,
    0xEB, 0x39, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0xC7, 0x41, 0x34, 0xCD,
    0xCC, 0x4C, 0x3E
};
static const uint8_t MSK_MP_PLR_PLAY_IMPACT_VOICE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
#define PLR_PLAY_IMPACT_VOICE_PROLOGUE 8u

/* The impact flash. Nineteen bytes is the whole function, prologue to ret, and the pattern is all
 * of it: it forwards its one argument and a zero to the flash sprite spawner and returns. Only the
 * call displacement is masked, and with it the zero of the second push at +0x08: the four bytes
 * 08 50 6A 00 across three instructions read as the data-band address 0x006A5008, which the
 * table's own no-absolute gate rightly refuses. A pattern reaching past the ret would carry the
 * next function's head as a required byte, which is the one thing this site cannot afford, since
 * it is the shortest function in the table. The prologue is six.
 */
static const uint8_t SIG_MP_PLR_FLASH_AT[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0x6A, 0x00, 0xE8, 0xFD, 0x7A,
    0x00, 0x00, 0x83, 0xC4, 0x08, 0x5D, 0xC3
};
static const uint8_t MSK_MP_PLR_FLASH_AT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_FLASH_AT_PROLOGUE 6u

/* The impact spark. The whole function again: it unpacks the three floats of the point out of the
 * caller's vector and hands them to the built-in emitter spawner with template 1. The three
 * unpacking moves are what make it unique. Masked: the call displacement, and the zero of the
 * last push at +0x1A, because the four bytes 11 52 6A 00 across three instructions read as the
 * data-band address 0x006A5211 to the table's own no-absolute gate. The prologue is five, the
 * first instruction boundary at or past five.
 */
static const uint8_t SIG_MP_PLR_SPARK_AT[] = {
    0x55, 0x8B, 0xEC, 0x6A, 0x01, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x08, 0x51,
    0x8B, 0x55, 0x08, 0x8B, 0x42, 0x04, 0x50, 0x8B, 0x4D, 0x08, 0x8B, 0x11,
    0x52, 0x6A, 0x00, 0xE8, 0x5D, 0x36, 0xFD, 0xFF, 0x83, 0xC4, 0x14, 0x5D,
    0xC3
};
static const uint8_t MSK_MP_PLR_SPARK_AT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define PLR_SPARK_AT_PROLOGUE 5u

/* Projectile creation, and the site the class question turns on: it has three gates on
 * shooterClass == 1, and the pair filter discards a pair of equal classes, so at most one player
 * can carry class 1. The body module hulls it and the puppet spawns the far player's shots
 * through it. The pattern runs past the first gate to the second, the happy cheat's test, whose
 * operand at +0x24 is the cheat cell: a cheat set on one machine only remaps kind 7 to 9 there
 * and nowhere else, so the handshake has to compare it. That operand is the one masked byte run.
 * The twenty three bytes behind the old twenty byte form, which ended on the `75` at +0x13:
 *
 *     00453CE5  75 0D                        jne past the missile remap
 *     00453CE7  83 7D 18 01 75 07            shooterClass == 1?
 *     00453CED  C7 45 08 17 00 00 00         kind 8 becomes 0x17
 *     00453CF4  83 3D [A4 22 88 00] 00       cmp the happy cheat, 0              (+0x24)
 *     00453CFB  74 13                        where the pattern ends
 */
static const uint8_t SIG_SHOT_SPAWN[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x08,
    0x89, 0x45, 0xF8, 0x83, 0x7D, 0x08, 0x08, 0x75, 0x0D, 0x83, 0x7D, 0x18,
    0x01, 0x75, 0x07, 0xC7, 0x45, 0x08, 0x17, 0x00, 0x00, 0x00, 0x83, 0x3D,
    0xA4, 0x22, 0x88, 0x00, 0x00, 0x74, 0x13
};
static const uint8_t MSK_SHOT_SPAWN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
#define SHOT_SPAWN_PROLOGUE 9u

/* The four sabre sites, three hulled by the sender and one carried for its operands. All four
 * read the player through the same absolute pointer, masked at every reading; three of the four
 * heads carry a branch from the dev overlay's combat guards in any configuration where that DLL
 * is on, so each pattern reaches well past its prologue and the pointer is never read out of a
 * prologue. */

/* The armed contact, the one function every blade contact enters in either direction. Nothing
 * here detours it; the site exists to carry the two operands at +0x0E and +0x39, the addresses of
 * the deflect and the parry continuations the engine compares the player's aux slot against, and
 * which the puppet writes into that slot when it performs a block or a parry. The jne
 * displacement at +0x13 is masked because with its neighbours it reads as a data-band address,
 * `75 1C 6A 00` as 0x006A1C75. The first two arms, at 0x0044855C:
 *
 *     0044855C  55 8B EC 83 EC 0C                    push ebp / mov ebp, esp / sub esp, 0xC
 *     00448562  A1 [20 52 4B 00]                     eax = pr                          (+0x07)
 *     00448567  81 78 64 [44 BC 44 00]               cmp pr->pAuxAction, the deflect   (+0x0E)
 *     0044856E  75 [1C]                              jne the second arm
 *     00448570  6A 00 E8 [..] 83 C4 04               push 0 / call the impact voice
 *     0044857A  8B 0D [20 52 4B 00] C7 41 38 01 00 00 00     pr->blockHitLatch = 1
 *     00448587  E9 [99 01 00 00]                     jmp the end
 *     0044858C  8B 15 [20 52 4B 00]                  edx = pr
 *     00448592  81 7A 64 [A0 BC 44 00]               cmp pr->pAuxAction, the parry     (+0x39)
 *
 * The dev overlay detours this head with prologue 6 and reads the same two operands; both sit
 * behind that prologue, so they read from memory in either load order. */
static const uint8_t SIG_MP_PLR_ARMED_CONTACT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x81,
    0x78, 0x64, 0x44, 0xBC, 0x44, 0x00, 0x75, 0x1C, 0x6A, 0x00, 0xE8, 0x21,
    0x89, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00,
    0xC7, 0x41, 0x38, 0x01, 0x00, 0x00, 0x00, 0xE9, 0x99, 0x01, 0x00, 0x00,
    0x8B, 0x15, 0x20, 0x52, 0x4B, 0x00, 0x81, 0x7A, 0x64, 0xA0, 0xBC, 0x44,
    0x00
};
static const uint8_t MSK_MP_PLR_ARMED_CONTACT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00
};
#define PLR_ARMED_CONTACT_PROLOGUE 6u

/* The deflect: refused while an aux action runs or the shield timer is out, otherwise it picks a
 * block clip against the incoming bolt and answers 1 with the clip playing on the overlay. The
 * pattern carries the shield timer read at +0x94, which no other function opens with, and ends on
 * the second refusal's jump opcode. The nine byte prologue is the push, the frame move and the
 * 0x98 frame; the cmp immediate at +0x11 is masked for the data-band reason recorded at
 * plr_set_weapon. At 0x0044DBC6, to the second refusal's jump opcode:
 *
 *     0044DBC6  55 8B EC 81 EC 98 00 00 00            prologue 9
 *     0044DBCF  A1 [20 52 4B 00] 83 78 64 [00]        eax = pr; cmp pr->pAuxAction, 0
 *     0044DBD8  74 07 33 C0 E9 [6D 05 00 00]          refused: answer 0
 *     0044DBE1  8B 0D [20 52 4B 00] D9 81 94 00 00 00 fld pr->shieldTimer
 *     0044DBED  D8 1D [A4 86 4A 00] DF E0 F6 C4 41    fcomp the float zero / fnstsw / test
 *     0044DBF8  75 07 33 C0 E9                        refused: answer 0, the pattern's end
 */
static const uint8_t SIG_MP_PLR_START_BLOCK_SHOT[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00, 0xA1, 0x20, 0x52,
    0x4B, 0x00, 0x83, 0x78, 0x64, 0x00, 0x74, 0x07, 0x33, 0xC0, 0xE9, 0x6D,
    0x05, 0x00, 0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00, 0xD9, 0x81, 0x94,
    0x00, 0x00, 0x00, 0xD8, 0x1D, 0xA4, 0x86, 0x4A, 0x00, 0xDF, 0xE0, 0xF6,
    0xC4, 0x41, 0x75, 0x07, 0x33, 0xC0, 0xE9
};
static const uint8_t MSK_MP_PLR_START_BLOCK_SHOT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_START_BLOCK_SHOT_PROLOGUE 9u

/* The parry, one key further along: the same aux refusal behind a zeroed local, then a cone
 * search for the attacker. The pattern ends on the refusal's jump; the tail behind the six byte
 * prologue is unique on every image by the zeroed local alone, so the search's immediates are
 * not carried, which keeps the pattern free of the two instruction windows that would otherwise
 * read as addresses: the 43 byte form carrying the search's `push 4 / push 1.5 / push 90.0` is
 * also unique everywhere, but its windows `6A 04 68 00` and `C0 3F 68 00` read as data band
 * addresses to the table's own gate. The cmp immediate at +0x15 is masked for the data-band
 * reason. */
static const uint8_t SIG_MP_PLR_BLOCK_ATTACK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x83, 0x78, 0x64, 0x00, 0x74, 0x07,
    0x33, 0xC0, 0xE9, 0x02, 0x01, 0x00, 0x00
};
static const uint8_t MSK_MP_PLR_BLOCK_ATTACK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define PLR_BLOCK_ATTACK_PROLOGUE 6u

/* The end of every sabre action: it zeroes the direction class, puts the unarmed impact identity
 * 0x29 back on the body, and clears the contact node, code and radius, which is what switches the
 * blade's contact sphere off. The pattern carries the first two stores; the 0x29 store is what
 * makes it unique. Nothing else in this tree detours it, but the prologue is declared all the
 * same, because this feature's own hull lands on it. Prologue eight: the push, the frame move and
 * the five byte player pointer load, whose operand is masked and never read from this site. */
static const uint8_t SIG_MP_PLR_CLEAR_SWING_CONTACT[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x48, 0x0C, 0xC7,
    0x81, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x15, 0x20,
    0x52, 0x4B, 0x00, 0x8B, 0x42, 0x0C, 0xC7, 0x40, 0x0C, 0x29, 0x00, 0x00,
    0x00
};
static const uint8_t MSK_MP_PLR_CLEAR_SWING_CONTACT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define PLR_CLEAR_SWING_CONTACT_PROLOGUE 8u

/* One animation track advanced by a frame count, the savegame restore's way of putting a track at
 * a position: with a zero time step and a non-zero frame override it advances by exactly the
 * override, arming the marker and event latches on the way as a real advance would, and ramps the
 * weight by nothing. The puppet seeds a freshly started clip through it. The pattern carries the
 * suspended-puppet test, the slot arithmetic (a 0x14C stride built out of three lea instructions,
 * which is what makes it unique among the track functions) and the free-slot test up to the held
 * flag test; both loads of the float zero the early-outs return are masked, because that constant
 * sits in the data band. The prologue is five: the push and the four byte load of the puppet.
 * At 0x00483D20, `float __cdecl (void *puppet, float dt, uint32_t slot, float framesOverride)`:
 *
 *     00483D20  53 8B 5C 24 08               push ebx / ebx = puppet
 *     00483D25  56 57                        push esi / push edi
 *     00483D27  83 3B 00 74 0A               suspended? answer 0.0
 *     00483D2C  D9 05 [38 8C 4A 00]          fld the float zero at 0x004A8C38    (masked)
 *     00483D32  5F 5E 5B C3                  pop / pop / pop / ret
 *     00483D36  8B 7C 24 18                  edi = slot
 *     00483D3A  8D 04 BF 8D 0C C7 8D 14 4F   slot * 0x14C out of three lea
 *     00483D43  8B 44 93 08 8D 74 93 08      eax = track.flags; esi = &track
 *     00483D4B  85 C0 75 0A                  flags == 0? answer 0.0
 *     00483D4F  D9 05 [38 8C 4A 00]          fld the float zero                   (masked)
 *     00483D55  5F 5E 5B C3
 *     00483D59  A8 10 75 2B                  held? skip the advance, where the pattern ends
 *
 * The whole track loop at 0x00483C60 opens `53 8B 5C 24 08 55 33 ED` and is not a candidate. */
static const uint8_t SIG_MP_RDPUPPET_UPDATE_TRACK[] = {
    0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x57, 0x83, 0x3B, 0x00, 0x74, 0x0A,
    0xD9, 0x05, 0x38, 0x8C, 0x4A, 0x00, 0x5F, 0x5E, 0x5B, 0xC3, 0x8B, 0x7C,
    0x24, 0x18, 0x8D, 0x04, 0xBF, 0x8D, 0x0C, 0xC7, 0x8D, 0x14, 0x4F, 0x8B,
    0x44, 0x93, 0x08, 0x8D, 0x74, 0x93, 0x08, 0x85, 0xC0, 0x75, 0x0A, 0xD9,
    0x05, 0x38, 0x8C, 0x4A, 0x00, 0x5F, 0x5E, 0x5B, 0xC3, 0xA8, 0x10, 0x75,
    0x2B
};
static const uint8_t MSK_MP_RDPUPPET_UPDATE_TRACK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
/* Seven, although five is a boundary too: the push and the four byte load of the puppet end at
 * five, the pushes of esi and edi at seven. Nothing here hulls this head, it is only called, and
 * the prologue is what lets the search step over a branch another module wrote. OpenPhantom's
 * diagnostics hulls it on seven for its x87 trace; a search that skipped only five would read that
 * hull's padding where it expects 56 57 and resolve nothing. */
#define RDPUPPET_UPDATE_TRACK_PROLOGUE 7u

/* The hero asset name table, and it is a data anchor rather than a function.
 *
 * Inside the spawn: load the player record, read the hero index out of it at +0x6C, take that
 * entry of the four pointer name table, push it behind the actor tag 'BAFS' and call the resource
 * loader. Three things are read out of this one run and none of them is written down: the player
 * record, the table, and the loader itself as the target of the call.
 *
 * The pushed tag is what makes the site certain rather than probable. A second actor load written
 * the same way would still have to push that tag with the hero index scaled by four, and the
 * image carries no second one; the near twin in the restore path loads the record through the
 * `8b 15` form and does not answer this pattern at all.
 *
 * No prologue is declared because this is not a function entry, so the two stage resolver has
 * nothing to fall back to and needs nothing: no module detours the middle of the spawn, and the
 * lifecycle hull that does detour the spawn writes its branch over the ENTRY, 0x75 bytes in front
 * of these bytes. */
static const uint8_t SIG_MP_HERO_ASSET_TABLE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,              /* mov eax,[the player record]        */
    0x8B, 0x48, 0x6C,                          /* mov ecx,[eax+0x6c], the hero index */
    0x8B, 0x14, 0x8D, 0x00, 0x00, 0x00, 0x00,  /* mov edx,[ecx*4+the name table]     */
    0x52,                                      /* push the name                      */
    0x68, 0x53, 0x46, 0x41, 0x42,              /* push 'BAFS', the actor tag         */
    0xE8, 0x00, 0x00, 0x00, 0x00               /* call the resource loader           */
};
static const uint8_t MSK_MP_HERO_ASSET_TABLE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};

/* THE RELEASE, the other half of the loader.
 *
 * A load that finds something raises the node's use count at +0x1E, and this is the only thing in
 * the image that lowers it. Reaching it matters because a body's asset is proved loadable before
 * the spawn is allowed to bind it, and that proof takes a reference which has to be handed back.
 *
 * The pattern is the function's own arithmetic rather than its prologue: find the node for a data
 * pointer, read the sixteen bit count at node+0x1E, subtract one, store it back. The prologue is
 * the opening of hundreds of functions; the two 0x1E operands are what make this one place. */
static const uint8_t SIG_MP_RES_FREE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,        /* push ebp; mov ebp,esp; sub esp,8       */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,  /* mov [ebp-4],0                          */
    0x8B, 0x45, 0x08, 0x50,                    /* mov eax,[ebp+8]; push eax, the data    */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call the node lookup by data pointer   */
    0x83, 0xC4, 0x04,
    0x89, 0x45, 0xF8,
    0x83, 0x7D, 0xF8, 0x00,                    /* cmp [ebp-8],0                          */
    0x74, 0x00,                                /* jz out, no such node                   */
    0x8B, 0x4D, 0xF8,
    0x66, 0x8B, 0x51, 0x1E,                    /* mov dx,[ecx+0x1e], the use count       */
    0x66, 0x83, 0xEA, 0x01,                    /* sub dx,1                               */
    0x8B, 0x45, 0xF8,
    0x66, 0x89, 0x50, 0x1E                     /* mov [eax+0x1e],dx                      */
};
static const uint8_t MSK_MP_RES_FREE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
#define RES_FREE_PROLOGUE 6u

/* THE OBJECT SIZE, bapobj_setScale. It stores the three factors at +0x30, +0x34 and +0x38 and
 * then the render handle's culling radius from the largest of them, so a far body sized through
 * it is culled at the size it is drawn at; three plain stores left the radius at the bind's value.
 * The pattern is the prologue and the three stores, with no address in it, and it matches once,
 * whole and by its tail. The prologue is six: push ebp, mov ebp,esp, sub esp,0x10. */
static const uint8_t SIG_MP_BAPOBJ_SET_SCALE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,        /* push ebp; mov ebp,esp; sub esp,0x10    */
    0x8B, 0x45, 0x08,                          /* mov eax,[ebp+8], the object            */
    0x89, 0x45, 0xFC,
    0x8B, 0x4D, 0xFC,
    0x8B, 0x55, 0x0C,
    0x89, 0x51, 0x30,                          /* mov [ecx+0x30],edx, the x factor       */
    0x8B, 0x45, 0xFC,
    0x8B, 0x4D, 0x10,
    0x89, 0x48, 0x34,                          /* mov [eax+0x34],ecx, the y factor       */
    0x8B, 0x55, 0xFC,
    0x8B, 0x45, 0x14,
    0x89, 0x42, 0x38                           /* mov [edx+0x38],eax, the z factor       */
};
#define BAPOBJ_SET_SCALE_PROLOGUE 6u

/* --- 0x00416787  bapsound_playName, the named one shot ----------------------------------------
 *
 * `void bapsound_playName(const char *wav, u32 flags)`. It funnels into bapsound_play through
 * bapsound_playByName with a NULL position, so a call made inside a far body's window is given
 * that body's place by mp_sound's anchor without this file saying anything about where it is.
 *
 * The prologue is ELEVEN, not six, and that is the whole reason this comment is long. The head is
 *
 *     00416787  55                 push ebp
 *     00416788  8B EC              mov  ebp,esp
 *     0041678A  51                 push ecx
 *     0041678B  83 3D B8B45B00 00  cmp  dword [g_soundOn],0     SEVEN bytes
 *     00416792  75 05              jnz  ...
 *
 * so the first instruction boundary at or past five is 11. Six would cut the compare in half and
 * send a trampoline into the middle of an immediate. That is byte for byte the shape that cost
 * this feature a client crash once already, the same module and the same mistake: the
 * neighbour
 * bapsound_play 0x0041681F really does take six, and copying it here would be the same mistake in
 * the same module. The absolute operand is masked because it names a cell, not an instruction. */
static const uint8_t SIG_MP_BAPSOUND_PLAY_NAME[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75,
    0x05, 0x83, 0xC8, 0xFF, 0xEB, 0x26, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x0C, 0x50
};
static const uint8_t MSK_MP_BAPSOUND_PLAY_NAME[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MP_BAPSOUND_PLAY_NAME == sizeof MSK_MP_BAPSOUND_PLAY_NAME,
               "the named sound pattern and its mask are different lengths");
#define BAPSOUND_PLAY_NAME_PROLOGUE 11u

/* --- 0x004245D0  emitter_spawnBuiltinAt --------------------------------------------------------
 *
 * `void emitter_spawnBuiltinAt(const char *name, const f32 pos[3], i32 builtinId)`. Builtin 7 is
 * "Splurt", the blood the engine spawns at a struck node. A NULL name is the engine's own call.
 * The head is four register moves and needs no mask; the first boundary at or past five is 6. */
static const uint8_t SIG_MP_EMITTER_SPAWN_AT[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x10, 0x50, 0x8B, 0x4D, 0x0C, 0x8B, 0x51,
    0x08, 0x52
};
#define EMITTER_SPAWN_AT_PROLOGUE 6u

/* --- 0x00448812  the hurt voice's name, an anchor for its operand ------------------------------
 *
 * Inside Plr_ReceiveDamage, where the engine plays the sound a player makes when something hurts
 * them. The operand at +8 is the ONE cell that call reads:
 *
 *     00448816  6A 00              push 0
 *     00448818  8B 0D 48AA4A00     mov  ecx,[0x004AAA48]      g_soundName[52]
 *     0044881E  51                 push ecx
 *     0044881F  E8 ..              call bapsound_playName
 *     00448824  83 C4 08           add  esp,8
 *     00448827  8B 15 20524B00     mov  edx,[g_playerRecord]
 *     0044882D  8B 45 F4           mov  eax,[ebp-0Ch]         the variant that is rolled and
 *     00448830  89 82 B402 0000    mov  [edx+2B4h],eax        stored but never played
 *
 * The window reaches FORWARD rather than back, and that is not a matter of taste: the bytes just
 * before the push are `EB DC`, the jump of an empty while loop, and a pattern standing on a
 * displacement is a pattern a recompile moves. The signature test refused the first attempt for
 * exactly that. Every address and every displacement in the window is masked.
 *
 * The cell is taken from the operand rather than computed, although g_soundName is at 0x004AA978
 * and 0x004AA978 + 52*4 lands on the same address: two ways to the same number, and the operand is
 * the one that does not need the table's base, its stride or its length to be written down.
 *
 * Nothing is detoured here; the site exists so the cell falls out of it. */
static const uint8_t SIG_MP_HURT_VOICE_READ[] = {
    0x6A, 0x00, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x51, 0xE8, 0x00, 0x00,
    0x00, 0x00, 0x83, 0xC4, 0x08, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B,
    0x45, 0xF4, 0x89, 0x82, 0xB4, 0x02, 0x00, 0x00
};
static const uint8_t MSK_MP_HURT_VOICE_READ[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MP_HURT_VOICE_READ == sizeof MSK_MP_HURT_VOICE_READ,
               "the hurt voice pattern and its mask are different lengths");

/* The template, in the order of the puppet block of the enumeration. The static assert is what
 * ties the two files together: a site added to one and not the other is a compile error here
 * rather than a table one entry out of step with its names. */
static const signature_t puppet_sites[] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("bapobj_draw_all", SIG_BAPOBJ_DRAW_ALL,
                                  MSK_BAPOBJ_DRAW_ALL, BAPOBJ_DRAW_ALL_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("thing_alloc", SIG_MP_THING_ALLOC, MSK_MP_THING_ALLOC,
                                  THING_ALLOC_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("thing_free", SIG_MP_THING_FREE, THING_FREE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_play_clip", SIG_BAPOBJ_PLAY_CLIP, BAPOBJ_PLAY_CLIP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_play_overlay", SIG_BAPOBJ_PLAY_OVERLAY,
                           BAPOBJ_PLAY_OVERLAY_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_stop_overlay", SIG_MP_STOP_OVERLAY_CLIP,
                           STOP_OVERLAY_CLIP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_node_sphere", SIG_MP_BAPOBJ_NODE_SPHERE,
                           BAPOBJ_NODE_SPHERE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_armed_contact", SIG_MP_PLR_ARMED_CONTACT,
                                  MSK_MP_PLR_ARMED_CONTACT, PLR_ARMED_CONTACT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_tick_blade_light", SIG_PLR_TICK_BLADE_LIGHT,
                                  MSK_PLR_TICK_BLADE_LIGHT, PLR_TICK_BLADE_LIGHT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_set_weapon", SIG_PLR_SET_WEAPON, MSK_PLR_SET_WEAPON,
                                  PLR_SET_WEAPON_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_start_force_push", SIG_PLR_START_FORCE_PUSH,
                                  MSK_PLR_START_FORCE_PUSH, PLR_START_FORCE_PUSH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_start_block_shot", SIG_MP_PLR_START_BLOCK_SHOT,
                                  MSK_MP_PLR_START_BLOCK_SHOT, PLR_START_BLOCK_SHOT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_block_attack", SIG_MP_PLR_BLOCK_ATTACK,
                                  MSK_MP_PLR_BLOCK_ATTACK, PLR_BLOCK_ATTACK_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_test_swing_world", SIG_MP_PLR_TEST_SWING_WORLD,
                                  MSK_MP_PLR_TEST_SWING_WORLD, PLR_TEST_SWING_WORLD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_start_swing", SIG_MP_PLR_START_SWING,
                                  MSK_MP_PLR_START_SWING, PLR_START_SWING_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_clear_swing_contact", SIG_MP_PLR_CLEAR_SWING_CONTACT,
                                  MSK_MP_PLR_CLEAR_SWING_CONTACT,
                                  PLR_CLEAR_SWING_CONTACT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_play_impact_voice", SIG_MP_PLR_PLAY_IMPACT_VOICE,
                                  MSK_MP_PLR_PLAY_IMPACT_VOICE,
                                  PLR_PLAY_IMPACT_VOICE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_flash_at", SIG_MP_PLR_FLASH_AT, MSK_MP_PLR_FLASH_AT,
                                  PLR_FLASH_AT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_spark_at", SIG_MP_PLR_SPARK_AT, MSK_MP_PLR_SPARK_AT,
                                  PLR_SPARK_AT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("shot_spawn", SIG_SHOT_SPAWN, MSK_SHOT_SPAWN,
                                  SHOT_SPAWN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("rdpuppet_update_track", SIG_MP_RDPUPPET_UPDATE_TRACK,
                                  MSK_MP_RDPUPPET_UPDATE_TRACK, RDPUPPET_UPDATE_TRACK_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("hero_asset_table", SIG_MP_HERO_ASSET_TABLE, MSK_MP_HERO_ASSET_TABLE),
    SIGNATURE_ENTRY_DETOUR_MASKED("res_free", SIG_MP_RES_FREE, MSK_MP_RES_FREE, RES_FREE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_set_scale", SIG_MP_BAPOBJ_SET_SCALE,
                           BAPOBJ_SET_SCALE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapsound_play_name", SIG_MP_BAPSOUND_PLAY_NAME,
                                  MSK_MP_BAPSOUND_PLAY_NAME, BAPSOUND_PLAY_NAME_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("emitter_spawn_at", SIG_MP_EMITTER_SPAWN_AT,
                           EMITTER_SPAWN_AT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("hurt_voice_read", SIG_MP_HURT_VOICE_READ, MSK_MP_HURT_VOICE_READ),
    SIGNATURE_ENTRY_DETOUR("ai_start_emitter", SIG_MP_AI_START_EMITTER,
                           AI_START_EMITTER_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("enemy_detach_piece", SIG_MP_ENEMY_DETACH_PIECE,
                           ENEMY_DETACH_PIECE_PROLOGUE),
    /* The bytes of these two live in mp_signatures_foot.c; the rows live here, because a
     * table of its own would need a merge and mp_signatures.c has no room for one. */
    SIGNATURE_ENTRY_DETOUR_MASKED("footstep_tick", SIG_MP_FOOTSTEP_TICK,
                                  MSK_MP_FOOTSTEP_TICK, MP_FOOTSTEP_TICK_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("foot_run", SIG_MP_FOOT_RUN, MSK_MP_FOOT_RUN),
    SIGNATURE_ENTRY_DETOUR("light_set", SIG_MP_LIGHT_SET, SWITCH_ARM_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("emitter_set", SIG_MP_EMITTER_SET, SWITCH_ARM_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("sound_set", SIG_MP_SOUND_SET, SWITCH_ARM_PROLOGUE),
    /* The bytes of these two live in mp_signatures_blade.c, for the reason the footstep's do. */
    SIGNATURE_ENTRY_DETOUR_MASKED("thing_dispatch", SIG_MP_THING_DISPATCH, MSK_MP_THING_DISPATCH,
                                  MP_THING_DISPATCH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("halo_draw_for_thing", SIG_MP_HALO_DRAW_FOR_THING,
                           MP_HALO_DRAW_FOR_THING_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("shot_shatter_thing", SIG_MP_SHOT_SHATTER_THING,
                           SHOT_SHATTER_THING_PROLOGUE),
    /* A call rather than a head, so no prologue: nothing detours it, its operand is rewritten. */
    SIGNATURE_ENTRY_MASKED("enemy_tick_anchor_call", SIG_MP_ENEMY_TICK_ANCHOR_CALL,
                           MSK_MP_ENEMY_TICK_ANCHOR_CALL),
};

_Static_assert(sizeof(puppet_sites) / sizeof(puppet_sites[0]) ==
                   (size_t)MP_SITE_COUNT - (size_t)MP_SITE_PUPPET_FIRST,
               "the puppet site table and the puppet block of mp_site_t differ in length");
_Static_assert(sizeof(SIG_BAPOBJ_DRAW_ALL) == sizeof(MSK_BAPOBJ_DRAW_ALL),
               "the world draw pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_START_SWING) == sizeof(MSK_MP_PLR_START_SWING),
               "the swing pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_RDPUPPET_UPDATE_TRACK) == sizeof(MSK_MP_RDPUPPET_UPDATE_TRACK),
               "the update track pattern and its mask differ in length");
_Static_assert(sizeof(SIG_SHOT_SPAWN) == sizeof(MSK_SHOT_SPAWN),
               "the shot spawn pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_TEST_SWING_WORLD) == sizeof(MSK_MP_PLR_TEST_SWING_WORLD),
               "the blade-versus-world pattern and its mask must be the same length");
_Static_assert(sizeof(SIG_MP_PLR_ARMED_CONTACT) == sizeof(MSK_MP_PLR_ARMED_CONTACT),
               "the armed contact pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_START_BLOCK_SHOT) == sizeof(MSK_MP_PLR_START_BLOCK_SHOT),
               "the deflect pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_BLOCK_ATTACK) == sizeof(MSK_MP_PLR_BLOCK_ATTACK),
               "the parry pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_CLEAR_SWING_CONTACT) == sizeof(MSK_MP_PLR_CLEAR_SWING_CONTACT),
               "the swing end pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_PLAY_IMPACT_VOICE) == sizeof(MSK_MP_PLR_PLAY_IMPACT_VOICE),
               "the impact voice pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_FLASH_AT) == sizeof(MSK_MP_PLR_FLASH_AT),
               "the impact flash pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PLR_SPARK_AT) == sizeof(MSK_MP_PLR_SPARK_AT),
               "the impact spark pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_HERO_ASSET_TABLE) == sizeof(MSK_MP_HERO_ASSET_TABLE),
               "the hero asset table pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_RES_FREE) == sizeof(MSK_MP_RES_FREE),
               "the resource release pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_ENEMY_TICK_ANCHOR_CALL) == sizeof(MSK_MP_ENEMY_TICK_ANCHOR_CALL),
               "the entity loop's anchor call pattern and its mask differ in length");
_Static_assert(MP_HERO_ASSET_SITE_RES_ALLOC + 5u <= sizeof(SIG_MP_HERO_ASSET_TABLE) &&
                   MP_HERO_ASSET_SITE_TABLE + 4u <= sizeof(SIG_MP_HERO_ASSET_TABLE) &&
                   MP_HERO_ASSET_SITE_RECORD + 4u <= sizeof(SIG_MP_HERO_ASSET_TABLE),
               "a read offset reaches past the hero asset table pattern");

const signature_t *mp_signatures_puppet_sites(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(puppet_sites) / sizeof(puppet_sites[0]);
    }
    return puppet_sites;
}
