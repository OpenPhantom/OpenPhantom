/* node_verts.c: see node_verts.h. */
#include "node_verts.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RENDER_GUARD_SECTION "render_guard"

/* --- the failure arms of bapobj_getNodeMeshVerts and its twin, four sites in two shapes ---- *
 * Each pattern matches twice, once in each function, which is the count the install expects.
 *
 *   0x004137CB / 0x004138A7   3B 48 54  cmp ecx,[eax+0x54]     the node index against the count
 *                             72 0B     jb  +0x0B
 *                             D9 05 <0.0f>                     the float return, replaced
 *                             E9 <rel32>                       to the epilogue
 *
 *   0x004137F5 / 0x004138D1   83 78 4C 00  cmp dword [eax+0x4C],0   the vertex table present
 *                             7D 08        jge +0x08
 *                             D9 05 <0.0f>                          the float return, replaced
 *                             EB 5F        to the epilogue */
static const uint8_t SIG_NODE_RANGE_RETURN[] = {
    0x8B, 0x55, 0xEC, 0x8B, 0x42, 0x04, 0x8B, 0x4D, 0x0C, 0x3B, 0x48, 0x54, 0x72, 0x0B,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x87, 0x00, 0x00, 0x00
};
static const uint8_t MSK_NODE_RANGE_RETURN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define NODE_RANGE_FLD_OFFSET 14u

static const uint8_t SIG_VERTS_MISSING_RETURN[] = {
    0x83, 0x78, 0x4C, 0x00, 0x7D, 0x08, 0xD9, 0x05, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x5F
};
static const uint8_t MSK_VERTS_MISSING_RETURN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};
#define VERTS_MISSING_FLD_OFFSET 6u
#define FAILURE_ARM_MATCHES      2u

/* --- the three callers, each `E8 <rel32> DD D8`: the pop after the call, replaced ---------- *
 *   0x00439B41  halo_draw:            push eax / call / fstp st(0) / add esp,0xC / mov ecx,...
 *   0x0044987F  Plr_CaptureBladeMesh: push ecx / call / fstp st(0) / add esp,0xC / mov edx,[cell]
 *   0x00449E3C  Plr_SetBladeSize:     push edx / call / fstp st(0) / add esp,0xC / the epilogue
 * Each matches once. */
static const uint8_t SIG_HALO_CALL[] = {
    0x8B, 0x45, 0x08, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xDD, 0xD8, 0x83, 0xC4, 0x0C,
    0x8B, 0x8D, 0xEC, 0xFE, 0xFF, 0xFF, 0x8B, 0x51, 0x08
};
static const uint8_t MSK_HALO_CALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t SIG_CAPTURE_CALL[] = {
    0x8B, 0x48, 0x0C, 0x51, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xDD, 0xD8, 0x83, 0xC4, 0x0C,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0xA1
};
static const uint8_t MSK_CAPTURE_CALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
static const uint8_t SIG_SET_BLADE_CALL[] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x51, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0xDD, 0xD8, 0x83, 0xC4, 0x0C, 0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3
};
static const uint8_t MSK_SET_BLADE_CALL[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define HALO_CALL_POP_OFFSET      9u
#define CAPTURE_CALL_POP_OFFSET   9u
#define SET_BLADE_CALL_POP_OFFSET 15u

_Static_assert(sizeof SIG_NODE_RANGE_RETURN == sizeof MSK_NODE_RANGE_RETURN, "mask length");
_Static_assert(sizeof SIG_VERTS_MISSING_RETURN == sizeof MSK_VERTS_MISSING_RETURN, "mask length");
_Static_assert(sizeof SIG_HALO_CALL == sizeof MSK_HALO_CALL, "mask length");
_Static_assert(sizeof SIG_CAPTURE_CALL == sizeof MSK_CAPTURE_CALL, "mask length");
_Static_assert(sizeof SIG_SET_BLADE_CALL == sizeof MSK_SET_BLADE_CALL, "mask length");

/* fld m32 is six bytes and fstp st(0) two; each becomes no-ops of its own length. */
static const uint8_t NOP6[6]    = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
static const uint8_t NOP2[2]    = { 0x90, 0x90 };
static const uint8_t FLD_M32[2] = { 0xD9, 0x05 };
static const uint8_t FSTP_ST0[2] = { 0xDD, 0xD8 };
#define FLD_M32_SIZE  6u
#define FSTP_ST0_SIZE 2u

static patch_journal_t journal;

/* One instruction replaced, refused when the bytes there are not the opcode expected. A six
 * byte write goes in two, the journal holding four bytes an entry. */
static bool replace(uintptr_t at, const uint8_t *opcode, const uint8_t *nops, size_t size,
                    const char *what)
{
    uint8_t have[2] = { 0, 0 };

    if (!memory_try_read(at, have, sizeof have) || have[0] != opcode[0] || have[1] != opcode[1]) {
        log_warning("%s at %08X reads %02X %02X, not the instruction expected, refused", what,
                    (unsigned)at, (unsigned)have[0], (unsigned)have[1]);
        return false;
    }
    if (patch_journal_write_bytes(&journal, at, nops, size > 4u ? 4u : size) != PATCH_RESULT_OK) {
        log_warning("%s at %08X could not be written, refused", what, (unsigned)at);
        return false;
    }
    if (size > 4u &&
        patch_journal_write_bytes(&journal, at + 4u, nops + 4, size - 4u) != PATCH_RESULT_OK) {
        log_warning("%s at %08X could not be written whole, refused", what, (unsigned)at);
        return false;
    }
    return true;
}

/* A pattern that must match exactly `expected` times, each hit given its fld replaced. */
static bool replace_failure_arms(const uint8_t *sig, const uint8_t *mask, size_t size,
                                 uint32_t fld_offset, const char *what)
{
    uintptr_t hits[SIGNATURE_MAX_REPORTED];
    size_t    count = signature_count_matches(sig, mask, size, hits, SIGNATURE_MAX_REPORTED);
    size_t    i;

    if (count != FAILURE_ARM_MATCHES) {
        log_warning("%s matched %u times, not the %u expected (one in each of the two "
                    "functions), refused", what, (unsigned)count, (unsigned)FAILURE_ARM_MATCHES);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!replace(hits[i] + fld_offset, FLD_M32, NOP6, FLD_M32_SIZE, what)) {
            return false;
        }
    }
    return true;
}

static bool replace_pop(const uint8_t *sig, const uint8_t *mask, size_t size,
                        uint32_t pop_offset, const char *what)
{
    uintptr_t site = signature_find_unique(sig, mask, size);

    if (site == 0) {
        log_warning("%s did not resolve, refused", what);
        return false;
    }
    return replace(site + pop_offset, FSTP_ST0, NOP2, FSTP_ST0_SIZE, what);
}

void node_verts_install(void)
{
    if (!ini_read_bool(RENDER_GUARD_SECTION, "BalanceNodeVerts", true)) {
        log_info("BalanceNodeVerts=0, so every halo drawn leaves the x87 stack pointer one "
                 "higher, as the game shipped");
        return;
    }
    patch_journal_reset(&journal);
    if (!replace_failure_arms(SIG_NODE_RANGE_RETURN, MSK_NODE_RANGE_RETURN,
                              sizeof SIG_NODE_RANGE_RETURN, NODE_RANGE_FLD_OFFSET,
                              "the node range return of the node vertex routines") ||
        !replace_failure_arms(SIG_VERTS_MISSING_RETURN, MSK_VERTS_MISSING_RETURN,
                              sizeof SIG_VERTS_MISSING_RETURN, VERTS_MISSING_FLD_OFFSET,
                              "the missing table return of the node vertex routines") ||
        !replace_pop(SIG_HALO_CALL, MSK_HALO_CALL, sizeof SIG_HALO_CALL, HALO_CALL_POP_OFFSET,
                     "halo_draw's pop after the node vertex copy") ||
        !replace_pop(SIG_CAPTURE_CALL, MSK_CAPTURE_CALL, sizeof SIG_CAPTURE_CALL,
                     CAPTURE_CALL_POP_OFFSET, "the blade capture's pop after the copy") ||
        !replace_pop(SIG_SET_BLADE_CALL, MSK_SET_BLADE_CALL, sizeof SIG_SET_BLADE_CALL,
                     SET_BLADE_CALL_POP_OFFSET, "the blade size's pop after the write")) {
        patch_journal_undo(&journal);
        log_warning("the x87 stack balance of the node vertex routines is left as shipped: one "
                    "of its seven sites refused and the others were put back");
        return;
    }
    log_info("the node vertex routines return nothing on every path and their three callers pop "
             "nothing: four float returns and three fstp st(0) are no-ops, so a halo drawn no "
             "longer leaves the x87 stack pointer one higher, the fault that drew a blade out "
             "of a hand");
}
