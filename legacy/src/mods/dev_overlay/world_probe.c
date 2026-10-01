/* world_probe.c: see world_probe.h. */
#include "world_probe.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdint.h>

/* ==============================================================================================
 * The patterns. Both are taken from the multiplayer's own world probe table rather than written
 * again, and their prologue lengths with them: a length this file chose for itself would be a
 * second answer to a question that already has one.
 * ============================================================================================ */

/* The head clearance probe, 0x0040C464. Its opening is a broadphase gather with a literal zero
 * radius, the level pointer and the caller's point, then the rewind of the candidate list. The
 * absolute operand is the level pointer and the two displacements are the gather and the rewind;
 * all three are wildcarded, because requiring them would pin the very addresses that move between
 * the shipped images. */
static const uint8_t SIG_HEAD_CLEARANCE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70,
    0x6A, 0x00, 0x8B, 0x45, 0x08, 0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x08, 0x8B, 0x02, 0x89, 0x45, 0xBC
};
static const uint8_t MSK_HEAD_CLEARANCE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define HEAD_CLEARANCE_PROLOGUE 6u

/* The ray, 0x0040D933. Up to sixty four cells along the line, one cell gathered at a time, first
 * face that answers wins. Its one caller in the shipped game is the AI's line of sight test.
 *
 * Its head is the same three axis subtraction as the walkable line at 0x0040DAA1, and the two
 * would match each other's opening bytes; what separates them is the frame size, 0x228 against
 * 0x2c4, and the local slots the subtraction writes into. Both are in the pattern and neither is an
 * address, so the whole of it is literal. Measured: one match in the image.
 *
 * `push ebp; mov ebp, esp; sub esp, imm32`, nine bytes, is the first instruction boundary past the
 * five a branch needs. */
static const uint8_t SIG_RAYCAST_LINE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x28, 0x02, 0x00, 0x00, 0x57,
    0x8B, 0x45, 0x10, 0x8B, 0x4D, 0x0C,
    0xD9, 0x00, 0xD8, 0x21, 0xD9, 0x9D, 0xE0, 0xFD, 0xFF, 0xFF,
    0x8B, 0x55, 0x10, 0x8B, 0x45, 0x0C,
    0xD9, 0x42, 0x04, 0xD8, 0x60, 0x04, 0xD9, 0x9D, 0xE4, 0xFD, 0xFF, 0xFF
};
#define RAYCAST_LINE_PROLOGUE 9u

/* The only mask the shipped game ever hands the clearance probe. See the header: with it the probe
 * answers "is there an authored crawl space here", and with a zero mask it would answer "always
 * clear" rather than "no filter". */
#define SURF_LOW_CEILING 0x0800u

/* Both read out of the disassembly rather than assumed: the probes take pointers and plain values
 * and the caller cleans up, so cdecl throughout, and both answer a float on the x87 stack, which
 * is what a float return is in this convention. */
typedef float(__cdecl *head_clearance_fn_t)(const float *position, uint16_t mask);
typedef float(__cdecl *line_fn_t)(uintptr_t world, const float *from, const float *to);

static head_clearance_fn_t head_clearance;
static line_fn_t           raycast_line;
static bool                resolved;

void world_probe_resolve(void)
{
    if (resolved) {
        return;
    }
    resolved = true;

    head_clearance = (head_clearance_fn_t)(uintptr_t)signature_find_detour_target(
        SIG_HEAD_CLEARANCE, MSK_HEAD_CLEARANCE, sizeof SIG_HEAD_CLEARANCE,
        HEAD_CLEARANCE_PROLOGUE);
    raycast_line = (line_fn_t)(uintptr_t)signature_find_detour_target(
        SIG_RAYCAST_LINE, NULL, sizeof SIG_RAYCAST_LINE, RAYCAST_LINE_PROLOGUE);

    log_info("the world probes: head clearance %s, the ray %s. A spot is judged by the ones that "
             "answered and the others are not guessed at",
             (head_clearance != NULL) ? "resolved" : "did NOT resolve",
             (raycast_line != NULL) ? "resolved" : "did NOT resolve");
}

bool world_probe_has_headroom(void)
{
    return head_clearance != NULL;
}

bool world_probe_headroom(const float *at)
{
    if (head_clearance == NULL || at == NULL) {
        return false;
    }
    /* Zero means clear. */
    return head_clearance(at, (uint16_t)SURF_LOW_CEILING) == 0.0f;
}

bool world_probe_line_hit(const void *world, const float *from, const float *to, float *distance)
{
    float hit;

    if (raycast_line == NULL || world == NULL || from == NULL || to == NULL || distance == NULL) {
        return false;
    }
    /* The engine answers the distance along the normalised line plus 1e-5, so that a strike at the
     * very start is never read as the zero that means "clear"; the 1e-5 is taken back off. */
    hit = raycast_line((uintptr_t)world, from, to);
    if (!(hit > 0.0f)) {
        return false;
    }
    *distance = (hit > 1e-5f) ? hit - 1e-5f : 0.0f;
    return true;
}
