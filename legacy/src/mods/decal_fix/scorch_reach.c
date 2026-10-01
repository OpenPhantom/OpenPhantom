/* scorch_reach.c: see scorch_reach.h. */
#include "scorch_reach.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- fx_scorch 0x00456E8B, the query box at +0x28 ---------------------------------------------- *
 *
 *   00456EB3  D9 45 10                 fld  [ebp+0x10]         the mark's size
 *   00456EB6  D8 0D <2.0f>             fmul                    the box is twice it
 *   00456EBC  D9 55 FC                 fst  [ebp-4]            boxSize
 *   00456EBF  D8 1D <1.9f>             fcomp                   against the cap
 *   00456EC5  DF E0 F6 C4 41           fnstsw ax / test ah,0x41
 *   00456ECA  75 07                    jne  +7                 under the cap: keep it
 *   00456ECC  C7 45 FC 33 33 F3 3F     mov  [ebp-4],1.9f       over it: the cap
 *   00456ED3  6A 00 8B 55 FC 52        push 0 / push boxSize   bapmap_gatherCellsUncached
 *   00456ED9  8B 45 08 50              push [ebp+8]            the impact point
 *
 * Then the gathered polygons are each tested against a sphere of the full size
 * (inter_sphereVsTriangle at +0x11A) and handed to the projector, so past a size of 0.95 the two
 * halves of the same function disagree about how far the mark reaches. The jne becomes a jmp:
 * the store of the cap is never taken and the box stays size times two. Nothing else in the
 * function reads the cap. The two constant cells are masked and the 1.9f immediate in the store
 * is matched, since it is the thing being stepped over. */
static const uint8_t SIG_SCORCH_BOX[] = {
    0xD9, 0x45, 0x10,
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x55, 0xFC,
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00,
    0xDF, 0xE0, 0xF6, 0xC4, 0x41,
    0x75, 0x07,
    0xC7, 0x45, 0xFC, 0x33, 0x33, 0xF3, 0x3F,
    0x6A, 0x00, 0x8B, 0x55, 0xFC, 0x52,
    0x8B, 0x45, 0x08, 0x50
};
static const uint8_t MSK_SCORCH_BOX[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SCORCH_BOX == sizeof MSK_SCORCH_BOX,
               "the scorch box pattern and its mask are different lengths");
#define SCORCH_BOX_JUMP_OFFSET 23u        /* the 0x75 */

static const uint8_t JUMP_ALWAYS[] = { 0xEB };

/* --- fx_scorch 0x00456E8B, the quad's second triangle at +0x143 -------------------------------- *
 *
 *   00456FC1  85 C0 75 31              the first triangle, verts 0-1-2, touched: go project
 *   00456FC5  8B 55 F4 33 C0 8A 42 25  the polygon's vertex count, a byte at +0x25
 *   00456FCD  83 F8 03 7E 1F           three or fewer: next polygon
 *   00456FD2  8D 4D C0 51              lea ecx,[ebp-0x40] / push    &verts[1], so verts 1-2-3
 *   00456FD6  8B 55 F8 52              push [ebp-8]                 the mark's size
 *   00456FDA  8B 45 08 50              push [ebp+8]                 the impact point
 *   00456FDE  E8 <rel32>               call inter_sphereVsTriangle  0x0046DBCA
 *   00456FE3  83 C4 0C 85 C0 75 05     touched: go project
 *
 * verts is four vec3 in a row at [ebp-0x4C], so the pointer handed over is verts + 12 and the
 * triangle it names is 1-2-3. The right second triangle, 0-2-3, is not three vectors in a row,
 * so the argument cannot be fixed with a different displacement. inter_sphereVsTriangle is
 * detoured instead and, for a call returning to 00456FE3 and no other, is handed a copy of
 * verts 0, 2 and 3. The call's own displacement is masked and checked against the function the
 * detour lands on, so the site and the callee are proven to be each other's. */
static const uint8_t SIG_SCORCH_QUAD_CALL[] = {
    0x85, 0xC0, 0x75, 0x31,
    0x8B, 0x55, 0xF4, 0x33, 0xC0, 0x8A, 0x42, 0x25,
    0x83, 0xF8, 0x03, 0x7E, 0x1F,
    0x8D, 0x4D, 0xC0, 0x51,
    0x8B, 0x55, 0xF8, 0x52,
    0x8B, 0x45, 0x08, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C, 0x85, 0xC0, 0x75, 0x05
};
static const uint8_t MSK_SCORCH_QUAD_CALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SCORCH_QUAD_CALL == sizeof MSK_SCORCH_QUAD_CALL,
               "the quad call pattern and its mask are different lengths");
#define SCORCH_QUAD_CALL_OFFSET   29u     /* the E8 */
#define SCORCH_QUAD_RETURN_OFFSET 34u     /* the instruction after it */

/* --- inter_sphereVsTriangle 0x0046DBCA -------------------------------------------------------- *
 *   55 8B EC 83 EC 60            push ebp / mov ebp,esp / sub esp,0x60
 *   D9 45 0C D8 4D 0C D9 5D F0   fld [radius] / fmul [radius] / fstp [ebp-0x10]   radius squared
 *   C7 45 FC 00 00 00 00         mov [ebp-4],0
 * No absolute operand in the head. Prologue six bytes. */
static const uint8_t SIG_SPHERE_VS_TRIANGLE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x60,
    0xD9, 0x45, 0x0C, 0xD8, 0x4D, 0x0C, 0xD9, 0x5D, 0xF0,
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00
};
#define SPHERE_VS_TRIANGLE_PROLOGUE 6u

typedef int32_t (__cdecl *sphere_vs_triangle_fn_t)(const float *centre, float radius,
                                                   const float *triangle);

/* --- fx_scorchProjectPoly 0x00457032, the facing test at +0xEC --------------------------------- *
 *
 *   0045711E  D9 95 7C FF FF FF        fstp [ebp-0x84]        cosIncidence, the shot direction
 *                                                             dotted with the polygon's normal
 *   00457124  D8 1D <0.0f>             fcomp                  above zero: facing away
 *   0045712A  DF E0 F6 C4 41 75 1A     fnstsw / test / jne    not above: keep the polygon
 *   00457131  renderFlags & 2          two sided: keep it
 *   0045713D  B8 01 00 00 00 E9        else return 1, the polygon is refused
 *
 * The shot direction points into the surface it hit, so for that polygon the dot is near minus
 * one. The same test runs on every polygon of the splash, and a neighbour that slopes away from
 * a low shot by more than the shot's own angle gives a small positive number and is refused. The
 * constant is a shared cell of zero; the operand is repointed to a cell of this DLL's holding
 * 0.7, which is the sine of about 45 degrees: a polygon turned that far past square-on to the
 * shot is still refused, one that has merely folded a little is not. The mirror and the type
 * choice read the same dot afterwards and are untouched. */
static const uint8_t SIG_SCORCH_FACING[] = {
    0xD9, 0x95, 0x7C, 0xFF, 0xFF, 0xFF,
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00,
    0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x75, 0x1A,
    0x8B, 0x45, 0x14, 0x33, 0xC9, 0x66, 0x8B, 0x48, 0x2C, 0x83, 0xE1, 0x02
};
static const uint8_t MSK_SCORCH_FACING[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SCORCH_FACING == sizeof MSK_SCORCH_FACING,
               "the facing test pattern and its mask are different lengths");
#define SCORCH_FACING_OPERAND_OFFSET 8u

/* The threshold the compare reads instead of zero. Static, since the operand points at it for
 * the life of the process. */
static const float SPLASH_FACING_LIMIT = 0.7f;

static struct {
    patch_journal_t journal;
    patch_journal_t facing_journal;
    detour_t        sphere_vs_triangle;
    uintptr_t       quad_return;       /* the return address of fx_scorch's second test */
    uint32_t        retested;
} reach;

/* verts 0, 2 and 3 out of the four the caller holds, for the one call that named 1, 2 and 3. */
static int32_t __cdecl hook_sphere_vs_triangle(const float *centre, float radius,
                                               const float *triangle)
{
    sphere_vs_triangle_fn_t original =
        (sphere_vs_triangle_fn_t)reach.sphere_vs_triangle.original;

    if ((uintptr_t)_ReturnAddress() == reach.quad_return) {
        float quad_023[9];

        memcpy(&quad_023[0], triangle - 3, 3 * sizeof(float));   /* vertex 0, before the pointer */
        memcpy(&quad_023[3], triangle + 3, 6 * sizeof(float));   /* vertices 2 and 3 */
        ++reach.retested;
        return original(centre, radius, quad_023);
    }
    return original(centre, radius, triangle);
}

static bool lift_box_cap(void)
{
    uintptr_t site = signature_find_unique(SIG_SCORCH_BOX, MSK_SCORCH_BOX, sizeof SIG_SCORCH_BOX);

    if (site == 0) {
        log_warning("the scorch query box did not resolve, so a burn wider than 0.95 units keeps "
                    "asking too few cells and a mark across two polygons keeps losing its far "
                    "half");
        return false;
    }
    patch_journal_reset(&reach.journal);
    if (patch_journal_write_bytes(&reach.journal, site + SCORCH_BOX_JUMP_OFFSET, JUMP_ALWAYS,
                                  sizeof JUMP_ALWAYS) != PATCH_RESULT_OK) {
        patch_journal_undo(&reach.journal);
        log_warning("the scorch query box's cap could not be stepped over, so it stands");
        return false;
    }
    log_info("a burn asks every cell its mark reaches: the 1.9 unit cap on the query box at "
             "%08X is stepped over, and the box is the mark's own size times two, the same reach "
             "the polygon test has", (unsigned)(site + SCORCH_BOX_JUMP_OFFSET));
    return true;
}

static bool retest_the_quad(void)
{
    uintptr_t call_site = signature_find_unique(SIG_SCORCH_QUAD_CALL, MSK_SCORCH_QUAD_CALL,
                                                sizeof SIG_SCORCH_QUAD_CALL);
    uintptr_t function  = signature_find_unique(SIG_SPHERE_VS_TRIANGLE, NULL,
                                                sizeof SIG_SPHERE_VS_TRIANGLE);
    uintptr_t target    = 0;

    if (call_site == 0 || function == 0) {
        log_warning("the quad's second sphere test did not resolve, so a mark centred across a "
                    "quad's fourth edge keeps missing that quad");
        return false;
    }
    if (!patch_read_call_target(call_site + SCORCH_QUAD_CALL_OFFSET, &target) ||
        target != function) {
        log_warning("the quad's second sphere test at %08X calls %08X, not the sphere test at "
                    "%08X, so it is left alone", (unsigned)(call_site + SCORCH_QUAD_CALL_OFFSET),
                    (unsigned)target, (unsigned)function);
        return false;
    }
    reach.quad_return = call_site + SCORCH_QUAD_RETURN_OFFSET;
    if (!detour_install(&reach.sphere_vs_triangle, function,
                        (const void *)hook_sphere_vs_triangle, SPHERE_VS_TRIANGLE_PROLOGUE)) {
        log_warning("the detour on inter_sphereVsTriangle failed, so a mark centred across a "
                    "quad's fourth edge keeps missing that quad");
        return false;
    }
    log_info("a quad is tested against the burn as triangles 0-1-2 and 0-2-3: the second test "
             "at %08X, which named 1-2-3 and left the edge from vertex 3 to vertex 0 untested, "
             "is handed the right three through the detour on inter_sphereVsTriangle at %08X",
             (unsigned)(call_site + SCORCH_QUAD_CALL_OFFSET), (unsigned)function);
    return true;
}

static bool widen_the_facing_test(void)
{
    uintptr_t site = signature_find_unique(SIG_SCORCH_FACING, MSK_SCORCH_FACING,
                                           sizeof SIG_SCORCH_FACING);
    uint32_t  cell = 0;
    float     zero = 1.0f;

    if (site == 0) {
        log_warning("the burn's facing test did not resolve, so a neighbour that slopes away "
                    "from the shot keeps refusing its share of the mark");
        return false;
    }
    if (!memory_read_image_cell(site + SCORCH_FACING_OPERAND_OFFSET, sizeof(float), &cell) ||
        !memory_read((uintptr_t)cell, &zero, sizeof zero) || zero != 0.0f) {
        log_warning("the burn's facing test at %08X compares against %08X, which does not hold "
                    "the zero expected, so it is left alone",
                    (unsigned)(site + SCORCH_FACING_OPERAND_OFFSET), (unsigned)cell);
        return false;
    }
    patch_journal_reset(&reach.facing_journal);
    if (patch_journal_repoint_operand(&reach.facing_journal, site + SCORCH_FACING_OPERAND_OFFSET,
                                      cell, (uint32_t)(uintptr_t)&SPLASH_FACING_LIMIT) !=
        PATCH_RESULT_OK) {
        patch_journal_undo(&reach.facing_journal);
        log_warning("the burn's facing test could not be repointed, so it stands at zero");
        return false;
    }
    log_info("a neighbour in the splash is stamped unless it faces more than about 45 degrees "
             "away from the shot: the facing test at %08X compares against %.1f instead of the "
             "zero at %08X", (unsigned)(site + SCORCH_FACING_OPERAND_OFFSET),
             (double)SPLASH_FACING_LIMIT, (unsigned)cell);
    return true;
}

bool scorch_reach_install(void)
{
    bool cap    = lift_box_cap();
    bool quad   = retest_the_quad();
    bool facing = widen_the_facing_test();

    return cap && quad && facing;
}
