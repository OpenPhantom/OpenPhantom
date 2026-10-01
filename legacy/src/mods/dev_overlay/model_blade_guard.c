/* model_blade_guard.c: see the header. */
#include "model_blade_guard.h"

#include "character_buoyancy.h"
#include "character_model.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Six bytes, the same prologue every detour in this mod declares: what the detour writes over,
 * and the head the search steps over when another module has already written a branch there. */
#define BLADE_GUARD_PROLOGUE 6u

#define PLAYER_SABRE_NODE 0x4Cu   /* the node id the blade hangs on, 0 when the rig has none */

/* Resizing the blade. Its second assert tests the sabre node id and is fatal. The pattern reaches
 * past both asserts to that test, because the player record operand in it is the one address this
 * guard needs and reading it out of the matched operand is better than a second anchor. */
static const uint8_t SIG_SET_BLADE_SIZE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0x53, 0x56, 0x57,
    0xD9, 0x45, 0x08,                                /* fld [ebp+8]      the requested size  */
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00,              /* fcomp [a constant]                   */
    0xDF, 0xE0,
    0xF6, 0xC4, 0x41,
    0x75, 0x00,
    0x68, 0x47, 0x08, 0x00, 0x00,                    /* push 2119, the first asserted line    */
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x50, 0x18,                                /* the fatal handler                     */
    0x83, 0xC4, 0x0C,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,              /* mov edx,[the player record]           */
    0x83, 0x7A, 0x4C, 0x00,                          /* cmp [edx+0x4C],0   the sabre node     */
    0x75, 0x00
};
static const uint8_t MSK_SET_BLADE_SIZE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00
};
_Static_assert(sizeof(SIG_SET_BLADE_SIZE) == sizeof(MSK_SET_BLADE_SIZE),
               "the set blade size pattern and its mask are different lengths");

#define OFFSET_BLADE_PLAYER_RECORD 0x35u  /* the operand of that mov, inside the pattern above */

typedef void (__cdecl *set_blade_size_fn_t)(float size);

static struct {
    bool                armed;
    uintptr_t           player_record;
    detour_t            detour;
    set_blade_size_fn_t original;
} guard;

/* The resize, declined in the two cases a borrowed model creates and passed on in every other. */
static void __cdecl hook_set_blade_size(float size)
{
    uint32_t block = 0;
    uint32_t node = 0;

    /* The mesh under the blade belongs to the .baf. While the player wears somebody else's model,
     * a resize writes into an asset he does not own, and every actor built from that asset carries
     * it for the rest of the level. His own blade node is still there, so the test below cannot
     * see this case. */
    if (character_model_borrows_shared_mesh()) {
        return;
    }
    if (memory_try_read(guard.player_record, &block, sizeof block) && block != 0 &&
        memory_try_read((uintptr_t)block + PLAYER_SABRE_NODE, &node, sizeof node) && node == 0) {
        return;
    }
    guard.original(size);
}

bool model_blade_guard_install(void)
{
    uintptr_t site;
    uint32_t  record = 0;

    if (guard.armed) {
        return true;
    }
    site = signature_find_detour_target(SIG_SET_BLADE_SIZE, MSK_SET_BLADE_SIZE,
                                        sizeof SIG_SET_BLADE_SIZE, BLADE_GUARD_PROLOGUE);
    if (site == 0) {
        log_warning("the blade resize did not resolve, so no borrowed model may be offered: the "
                    "first scripted weapon draw on a rig without a blade node would end the "
                    "process");
        return false;
    }
    /* Read before the detour is placed. The detour overwrites the prologue and not the operand, so
     * the order does not matter to the bytes; it keeps the two steps independent of each other. */
    if (!memory_read_u32(site + OFFSET_BLADE_PLAYER_RECORD, &record) || record == 0) {
        log_warning("the blade resize at %08X names no player record, so no borrowed model may be "
                    "offered", (unsigned)site);
        return false;
    }
    guard.player_record = (uintptr_t)record;
    character_buoyancy_bind_player_record(guard.player_record);
    if (!detour_install(&guard.detour, site, (const void *)&hook_set_blade_size,
                        BLADE_GUARD_PROLOGUE)) {
        log_warning("the blade resize at %08X could not be detoured", (unsigned)site);
        return false;
    }
    guard.original = (set_blade_size_fn_t)guard.detour.original;
    guard.armed    = true;
    log_info("the blade resize is guarded at %08X, so a borrowed rig without a blade node "
             "declines it instead of ending the process", (unsigned)site);
    return true;
}

bool model_blade_guard_is_armed(void)
{
    return guard.armed;
}
