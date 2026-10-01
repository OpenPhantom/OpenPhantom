/* mp_signatures_crate.c: the eight calls a push block's life goes through, as byte patterns.
 *
 * Its own table, as the pause and the doors have theirs, because the main table's file is at its
 * size limit. The resolver, the two stage rule and the reporting are the shared ones.
 *
 * Each window runs back from the call over the instructions that push its arguments until it is
 * unique in every shipped image. The three short windows an earlier draft named for the push, the
 * drop and the crush matched 22, 25 and 3 times; these match once. Frame offsets are matched, the
 * two loads of the player pointer in the push window are masked (another build moves that cell),
 * and every call operand is masked.
 */
#include "mp_signatures_crate.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The push phase, from the block index out of the player record (+0x388) to the call:
 *   push edx; mov eax,[pr]; mov ecx,[eax+0x388]; push ecx; mov edx,[pr]; mov eax,[edx+0xC];
 *   push eax; call bapmap_pushBlock
 * The operand +0x388 is what makes it the push block's call; the pointer itself is masked. */
static const uint8_t SIG_MP_CRATE_PUSH_CALL[28] = {
    0x52, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x88, 0x88, 0x03, 0x00, 0x00, 0x51, 0x8B,
    0x15, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x42, 0x0C, 0x50, 0xE8, 0x2A, 0xC7, 0xFB, 0xFF
};
static const uint8_t MSK_MP_CRATE_PUSH_CALL[28] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_PUSH_CALL 23u

/* The landing: test the sink bit, skip over the call when it is clear, push the block, call the
 * sink, and return straight out of the pose function behind it. */
static const uint8_t SIG_MP_CRATE_LAND_SINK_CALL[13] = {
    0x85, 0xD2, 0x74, 0x11, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0x43, 0x09, 0x00, 0x00
};
static const uint8_t MSK_MP_CRATE_LAND_SINK_CALL[13] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_LAND_SINK_CALL 8u

/* The drop, inside the push: the caller's step, the target on the frame, the block, the call. */
static const uint8_t SIG_MP_CRATE_DROP_CALL[20] = {
    0x8B, 0x4D, 0x14, 0x51, 0x8D, 0x95, 0x60, 0xFF, 0xFF, 0xFF, 0x52, 0x8B, 0x45, 0xFC,
    0x50, 0xE8, 0x97, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_CRATE_DROP_CALL[20] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_DROP_CALL 15u

/* The crush, inside the sink: the block's radius read twice, once for the height and once for the
 * radius of the cylinder, then the call. */
static const uint8_t SIG_MP_CRATE_CRUSH_CALL[25] = {
    0x8B, 0x4D, 0x08, 0x8B, 0x91, 0xE4, 0x00, 0x00, 0x00, 0x52, 0x8B, 0x45, 0x08, 0x8B,
    0x88, 0xE4, 0x00, 0x00, 0x00, 0x51, 0xE8, 0x0E, 0x9C, 0x00, 0x00
};
static const uint8_t MSK_MP_CRATE_CRUSH_CALL[25] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_CRUSH_CALL 20u

/* The savegame's sink of a block it restores sunk: the saved tag's sink bit, then the call. Only
 * read, and it has to name the same function as the landing's call. */
static const uint8_t SIG_MP_CRATE_RESTORE_SINK_CALL[16] = {
    0x83, 0xE2, 0x10, 0x85, 0xD2, 0x74, 0x0C, 0x8B, 0x45, 0xF8, 0x50, 0xE8, 0x73, 0xF3,
    0xFF, 0xFF
};
static const uint8_t MSK_MP_CRATE_RESTORE_SINK_CALL[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00
};
#define CRATE_RESTORE_SINK_CALL 11u

/* The floor under the push's target: the contact block and the point, both on the frame. */
static const uint8_t SIG_MP_CRATE_PROBE_FLOOR_CALL[19] = {
    0x8D, 0x95, 0x74, 0xFF, 0xFF, 0xFF, 0x52, 0x8D, 0x85, 0x54, 0xFF, 0xFF, 0xFF, 0x50,
    0xE8, 0x43, 0x07, 0x00, 0x00
};
static const uint8_t MSK_MP_CRATE_PROBE_FLOOR_CALL[19] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_PROBE_FLOOR_CALL 14u

/* The attach: the target, the contact's polygon, the block. */
static const uint8_t SIG_MP_CRATE_ATTACH_CALL[20] = {
    0x8D, 0x85, 0x60, 0xFF, 0xFF, 0xFF, 0x50, 0x8B, 0x4D, 0x88, 0x51, 0x8B, 0x55, 0xFC,
    0x52, 0xE8, 0xAA, 0xE9, 0xFF, 0xFF
};
static const uint8_t MSK_MP_CRATE_ATTACH_CALL[20] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define CRATE_ATTACH_CALL 15u

/* The cell occupancy: the target, the start, the radius, the index. */
static const uint8_t SIG_MP_CRATE_NODE_POS_CALL[30] = {
    0x8D, 0x85, 0x60, 0xFF, 0xFF, 0xFF, 0x50, 0x8D, 0x8D, 0x48, 0xFF, 0xFF, 0xFF, 0x51,
    0x8B, 0x95, 0x40, 0xFF, 0xFF, 0xFF, 0x52, 0x8B, 0x45, 0x0C, 0x50, 0xE8, 0x55, 0xD8,
    0xFF, 0xFF
};
static const uint8_t MSK_MP_CRATE_NODE_POS_CALL[30] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00
};
#define CRATE_NODE_POS_CALL 25u

/* The four calls whose operands a session's transport repoints. */
SIGNATURE_REDIRECTED_CALL(SIG_MP_CRATE_PUSH_CALL, CRATE_PUSH_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_CRATE_LAND_SINK_CALL, CRATE_LAND_SINK_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_CRATE_DROP_CALL, CRATE_DROP_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_CRATE_CRUSH_CALL, CRATE_CRUSH_CALL);

/* The E8 of a near call. */
#define CALL_REL32_OPCODE 0xE8u

static signature_t crate_sites[MP_CRATE_CALL_COUNT] = {
    SIGNATURE_ENTRY_MASKED("crate_push_call", SIG_MP_CRATE_PUSH_CALL, MSK_MP_CRATE_PUSH_CALL),
    SIGNATURE_ENTRY_MASKED("crate_land_sink_call", SIG_MP_CRATE_LAND_SINK_CALL,
                           MSK_MP_CRATE_LAND_SINK_CALL),
    SIGNATURE_ENTRY_MASKED("crate_drop_call", SIG_MP_CRATE_DROP_CALL, MSK_MP_CRATE_DROP_CALL),
    SIGNATURE_ENTRY_MASKED("crate_crush_call", SIG_MP_CRATE_CRUSH_CALL, MSK_MP_CRATE_CRUSH_CALL),
    SIGNATURE_ENTRY_MASKED("crate_restore_sink_call", SIG_MP_CRATE_RESTORE_SINK_CALL,
                           MSK_MP_CRATE_RESTORE_SINK_CALL),
    SIGNATURE_ENTRY_MASKED("crate_probe_floor_call", SIG_MP_CRATE_PROBE_FLOOR_CALL,
                           MSK_MP_CRATE_PROBE_FLOOR_CALL),
    SIGNATURE_ENTRY_MASKED("crate_attach_call", SIG_MP_CRATE_ATTACH_CALL,
                           MSK_MP_CRATE_ATTACH_CALL),
    SIGNATURE_ENTRY_MASKED("crate_node_pos_call", SIG_MP_CRATE_NODE_POS_CALL,
                           MSK_MP_CRATE_NODE_POS_CALL)
};

static const size_t crate_call_offsets[MP_CRATE_CALL_COUNT] = {
    CRATE_PUSH_CALL, CRATE_LAND_SINK_CALL, CRATE_DROP_CALL, CRATE_CRUSH_CALL,
    CRATE_RESTORE_SINK_CALL, CRATE_PROBE_FLOOR_CALL, CRATE_ATTACH_CALL, CRATE_NODE_POS_CALL
};

_Static_assert(sizeof(SIG_MP_CRATE_PUSH_CALL) == sizeof(MSK_MP_CRATE_PUSH_CALL) &&
                   sizeof(SIG_MP_CRATE_LAND_SINK_CALL) == sizeof(MSK_MP_CRATE_LAND_SINK_CALL) &&
                   sizeof(SIG_MP_CRATE_DROP_CALL) == sizeof(MSK_MP_CRATE_DROP_CALL) &&
                   sizeof(SIG_MP_CRATE_CRUSH_CALL) == sizeof(MSK_MP_CRATE_CRUSH_CALL) &&
                   sizeof(SIG_MP_CRATE_RESTORE_SINK_CALL) ==
                       sizeof(MSK_MP_CRATE_RESTORE_SINK_CALL) &&
                   sizeof(SIG_MP_CRATE_PROBE_FLOOR_CALL) ==
                       sizeof(MSK_MP_CRATE_PROBE_FLOOR_CALL) &&
                   sizeof(SIG_MP_CRATE_ATTACH_CALL) == sizeof(MSK_MP_CRATE_ATTACH_CALL) &&
                   sizeof(SIG_MP_CRATE_NODE_POS_CALL) == sizeof(MSK_MP_CRATE_NODE_POS_CALL),
               "a push block pattern and its mask differ in length");
_Static_assert(CRATE_PUSH_CALL + 5u == sizeof(SIG_MP_CRATE_PUSH_CALL) &&
                   CRATE_LAND_SINK_CALL + 5u == sizeof(SIG_MP_CRATE_LAND_SINK_CALL) &&
                   CRATE_DROP_CALL + 5u == sizeof(SIG_MP_CRATE_DROP_CALL) &&
                   CRATE_CRUSH_CALL + 5u == sizeof(SIG_MP_CRATE_CRUSH_CALL) &&
                   CRATE_RESTORE_SINK_CALL + 5u == sizeof(SIG_MP_CRATE_RESTORE_SINK_CALL) &&
                   CRATE_PROBE_FLOOR_CALL + 5u == sizeof(SIG_MP_CRATE_PROBE_FLOOR_CALL) &&
                   CRATE_ATTACH_CALL + 5u == sizeof(SIG_MP_CRATE_ATTACH_CALL) &&
                   CRATE_NODE_POS_CALL + 5u == sizeof(SIG_MP_CRATE_NODE_POS_CALL),
               "every push block pattern ends on its call and the four operand bytes behind it");

static bool resolved_once;
static size_t resolved_count;

size_t mp_signatures_crate_resolve(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER ended;

    if (resolved_once) {
        return resolved_count;
    }
    resolved_once = true;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    resolved_count = signature_resolve_table(crate_sites, MP_CRATE_CALL_COUNT);
    QueryPerformanceCounter(&ended);
    log_info("the push block call sites: %u of %u resolved, in %.3f ms",
             (unsigned)resolved_count, (unsigned)MP_CRATE_CALL_COUNT,
             frequency.QuadPart != 0
                 ? (double)(ended.QuadPart - started.QuadPart) * 1000.0 /
                       (double)frequency.QuadPart
                 : 0.0);
    return resolved_count;
}

uintptr_t mp_signatures_crate_call(mp_crate_call_t call)
{
    uintptr_t site;
    uint8_t   opcode = 0u;

    if ((size_t)call >= (size_t)MP_CRATE_CALL_COUNT) {
        return 0u;
    }
    site = crate_sites[call].address;
    if (site == 0u || !memory_try_read(site + crate_call_offsets[call], &opcode, sizeof opcode) ||
        opcode != CALL_REL32_OPCODE) {
        return 0u;
    }
    return site + crate_call_offsets[call];
}

const signature_t *mp_signatures_crate_sites(size_t *count)
{
    if (count != NULL) {
        *count = (size_t)MP_CRATE_CALL_COUNT;
    }
    return crate_sites;
}
