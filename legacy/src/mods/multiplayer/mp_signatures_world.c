/* mp_signatures_world.c: the ground probe, the head clearance probe and the walkable line, as
 * byte patterns.
 *
 * Three functions, one subject: whether a point in the world is a place a body may stand. The
 * re-entry uses all three together, which is why they are one table rather than three loose
 * lookups, and why they are not in either of the other two pattern files: those are split by
 * engine subsystem and this is a third subsystem, the world map.
 *
 * Two of the three patterns carry no address at all and need no mask. The third reaches past its
 * prologue into the gather call because that is where its identity is, and the three operands it
 * passes over on the way, one absolute and two call displacements, are wildcarded rather than
 * required. None of them is read back: the pattern needs their POSITIONS, not their values.
 *
 * Each of the three matches exactly once in each of the six images, the editor's recompile
 * included, and all eighteen matches land on 0x0040BE00, 0x0040C464 and 0x0040DAA1. That the
 * recompile agrees is worth recording rather than assuming: most of its code differs from retail
 * and most patterns in this tree do not survive it, so three that do are three functions the
 * recompile did not touch.
 */
#include "mp_signatures_world.h"

#include "mp_signatures.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The patterns.
 * ============================================================================================ */

/* THE ground probe. It reads its own output block before clearing it, so the first thing the body
 * does is load the caller's block and look for a previous mover in it; that load pair, the frame
 * size and the 0x22 dword clear that follows are what make twenty nine bytes unique.
 *
 * The 0x22 in the clear is the block's own size in dwords, 34 of them, which is the 0x88 bytes a
 * caller has to hand over. A build whose block were a different size would fail this pattern
 * rather than overrun a caller's buffer, which is the right way round.
 *
 * `push ebp; mov ebp, esp; sub esp, 0x7c`, six bytes, is the first instruction boundary past the
 * five a branch needs. Out of the retail image, `void __cdecl (const vec3 *pos, block *ctx)`:
 *
 *     0040BE00  55 / 8B EC / 83 EC 7C      push ebp; mov ebp, esp; sub esp, 0x7C
 *     0040BE06  57                         push edi
 *     0040BE07  C7 45 C8 00 00 00 00       mov  [ebp-0x38], 0
 *     0040BE0E  8B 45 0C / 8B 48 18        eax = the block; ecx = its bOnMover, BEFORE the clear
 *     0040BE14  89 4D A4 / 83 7D A4 00     store it and test it
 *     0040BE1B  74 1E                      je the grid walk
 *     0040BE3B  B9 22 00 00 00 / 33 C0     ecx = 0x22; eax = 0
 *     0040BE42  8B 7D 0C / F3 AB           edi = the block; rep stosd
 *
 * The two fields this feature reads out of the block: +0x00 is the SIGNED height of the floor
 * over the probed point, a step up positive, a drop negative and 3.4e38 for no floor at all,
 * which every caller compares for equality rather than magnitude; +0x18 is set when the accepted
 * polygon belongs to an active mover. That second field is read before the clear, and it is the
 * fast path: with a previous mover in the block the probe re-tests that mover's faces first and
 * can return without consulting the grid, which is right for a rider carried across substeps and
 * wrong for a search that wants each candidate judged on its own, so every block handed to this
 * probe by the re-entry is zeroed first. */
static const uint8_t SIG_MP_BAPMAP_PROBE_FLOOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x7C, 0x57,
    0xC7, 0x45, 0xC8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x0C, 0x8B, 0x48, 0x18, 0x89, 0x4D, 0xA4,
    0x83, 0x7D, 0xA4, 0x00, 0x74, 0x1E
};
/* Seven rather than the six this head also offers, because diagnostics hulls the same
 * function on seven for its world trace and loads earlier. Six would leave the seventh
 * byte, one of its pad NOPs, inside the tail this pattern anchors on, and stage two would
 * find nothing the day that trace is switched on. Both numbers are instruction
 * boundaries: push ebp, mov ebp esp, sub esp 0x7c is six, push edi makes seven. */
#define BAPMAP_PROBE_FLOOR_PROLOGUE 7u

/* The head clearance probe. Its opening is a broadphase gather with a literal zero radius, the
 * level pointer and the caller's point, and then the rewind of the candidate list. The absolute
 * operand is the level pointer and the two displacements are the gather and the rewind; all three
 * are wildcarded, because requiring them would pin the very addresses that move between the three
 * shipped images.
 *
 * The pattern reaches to the first read of the point that follows, so it ends on an instruction
 * boundary well past the tail a two stage resolve would fall back to.
 *
 * Same six byte prologue as the probe above. `f32 __cdecl (const vec3 *pos, u16 mask)`, the
 * result in ST0, and the gather at 0x0040C477 is a call to 0x40D03E with a literal zero radius,
 * the rewind at 0x0040C47F a call to 0x40D7BF.
 *
 * It is not a general head clearance test. The body's first statement per candidate polygon is
 * `if (!(surfaceFlags & mask)) continue;`, so a face without the mask is not a ceiling to it and a
 * mask of 0 makes it skip every face and answer "clear" unconditionally rather than "no filter".
 * The only mask the shipped game ever passes is the low ceiling flag, 0x0800, at all three of its
 * call sites, so what it answers is whether there is an AUTHORED crawl space over the point; an
 * ordinary ceiling, a crate or a doorframe is invisible to it. A face counts only when it
 * straddles the window from half a unit over the point to 2.8 units over it, 2.8 being the
 * player's standing height, and the return is 0.0 for clear, so callers test for equality with
 * zero. */
static const uint8_t SIG_MP_PLR_HEAD_CLEARANCE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70,
    0x6A, 0x00, 0x8B, 0x45, 0x08, 0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x08, 0x8B, 0x02, 0x89, 0x45, 0xBC
};
static const uint8_t MSK_MP_PLR_HEAD_CLEARANCE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLR_HEAD_CLEARANCE_PROLOGUE 6u

/* The walkable line. Its frame is 0x2c4 bytes, which is the sixty four cell list plus a whole
 * ground contact block plus the vectors, and that number alone nearly identifies it; the run after
 * it is the three axis subtraction of the two points it was handed, written out one axis at a
 * time into the frame. No address in any of it.
 *
 * `push ebp; mov ebp, esp; sub esp, imm32`, nine bytes, is the first instruction boundary past
 * five. `f32 __cdecl (world, const vec3 *from, const vec3 *to)`, and the engine's own two
 * callers at 0x00429DF4 and 0x0042BDC9 pass the level as the first argument and test the result
 * for non zero.
 *
 * ZERO MEANS CLEAR, the opposite of what a distance suggests. The body walks up to sixty four
 * cells along the line, runs the full ground probe at each cell's CENTRE and at the DESTINATION's
 * height, and stops at the first cell with no floor or a floor above the probe point; reaching
 * the end answers 0.0, otherwise the flat distance to the cell that stopped it. Two properties
 * decide how it is used: it probes at the destination's height, so both endpoints have to be at a
 * height something stands at or the answer is about a line nobody walks; and it clears the
 * block's mover field before every probe, which switches the ground probe's mover fast path off,
 * so it consults the static grid and cannot see a mover at all, which is why a body standing on a
 * lift makes the re-entry wait rather than search. Cell centres are one unit apart, so around a
 * two unit ring it answers about roughly the right cells, acceptable for "may a body appear here"
 * and not for a movement query. */
static const uint8_t SIG_MP_BAPMAP_WALKABLE_DIST[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC4, 0x02, 0x00, 0x00, 0x57,
    0x8B, 0x45, 0x10, 0x8B, 0x4D, 0x0C,
    0xD9, 0x00, 0xD8, 0x21, 0xD9, 0x9D, 0x4C, 0xFD, 0xFF, 0xFF,
    0x8B, 0x55, 0x10, 0x8B, 0x45, 0x0C,
    0xD9, 0x42, 0x04, 0xD8, 0x60, 0x04, 0xD9, 0x9D, 0x50, 0xFD, 0xFF, 0xFF
};
#define BAPMAP_WALKABLE_DIST_PROLOGUE 9u

/* ==============================================================================================
 * The table.
 * ============================================================================================ */

static signature_t world_sites[MP_WORLD_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("bapmap_probe_floor", SIG_MP_BAPMAP_PROBE_FLOOR,
                           BAPMAP_PROBE_FLOOR_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_head_clearance", SIG_MP_PLR_HEAD_CLEARANCE,
                                  MSK_MP_PLR_HEAD_CLEARANCE, PLR_HEAD_CLEARANCE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapmap_walkable_dist", SIG_MP_BAPMAP_WALKABLE_DIST,
                           BAPMAP_WALKABLE_DIST_PROLOGUE)
};

_Static_assert(sizeof(world_sites) / sizeof(world_sites[0]) == (size_t)MP_WORLD_SITE_COUNT,
               "the world site table and mp_world_site_t differ in length");
_Static_assert(sizeof(SIG_MP_PLR_HEAD_CLEARANCE) == sizeof(MSK_MP_PLR_HEAD_CLEARANCE),
               "the head clearance pattern and its mask differ in length");
_Static_assert(BAPMAP_PROBE_FLOOR_PROLOGUE <= sizeof(SIG_MP_BAPMAP_PROBE_FLOOR) &&
                   PLR_HEAD_CLEARANCE_PROLOGUE <= sizeof(SIG_MP_PLR_HEAD_CLEARANCE) &&
                   BAPMAP_WALKABLE_DIST_PROLOGUE <= sizeof(SIG_MP_BAPMAP_WALKABLE_DIST),
               "a declared prologue is longer than the pattern it belongs to");

/* ==============================================================================================
 * Resolution.
 * ============================================================================================ */

size_t mp_signatures_world_resolve(void)
{
    size_t resolved;

    /* The shared resolver already carries the two stage rule and logs one line per site, naming
     * the branch it took. Repeating it here would be a second implementation of the one thing in
     * this project that must not have two. */
    resolved = signature_resolve_table(world_sites, MP_WORLD_SITE_COUNT);
    log_info("%u of %u world probe site(s) resolved", (unsigned)resolved,
             (unsigned)MP_WORLD_SITE_COUNT);
    return resolved;
}

uintptr_t mp_signatures_world_address(mp_world_site_t site)
{
    if ((size_t)site >= (size_t)MP_WORLD_SITE_COUNT) {
        return 0u;
    }
    return world_sites[site].address;
}

const signature_t *mp_signatures_world_sites(size_t *count)
{
    if (count != NULL) {
        *count = (size_t)MP_WORLD_SITE_COUNT;
    }
    return world_sites;
}
