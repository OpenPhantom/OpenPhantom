/* mp_signatures_door.c: the two doors out of one world and into another, as byte patterns.
 *
 * Both are whole functions and both are hulled, so both declare a prologue. They are here rather
 * than in the main pattern table because that file stands at its size limit; the subject is its
 * own anyway, which is the tree's other reason for a table of its own.
 */
#include "mp_signatures_door.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* The load the two in-level screens go through. It is not save_restoregame itself: it is the
 * wrapper that broadcasts 6 first, which is the engine telling every module that the level is
 * being taken down, and only then restores. Holding the wrapper therefore holds the broadcast as
 * well, which is what makes the refusal a no-op rather than half a teardown.
 *
 *   00451936  55 8B EC              push ebp; mov ebp, esp
 *   00451939  6A 06 / 6A 00 / E8    module_broadcast(0, 6)
 *   00451945  6A 00                 the second argument of the restore
 *   00451947  8B 45 08 / 50         the slot index this was called with
 *   0045194B  E8 ..                 the name of that slot
 *   00451954  E8 ..                 save_restoregame
 *
 * The three call displacements are wildcards; nothing else in the image looks like this. Prologue
 * five: push ebp, mov ebp esp, push 6 is the first boundary at or past the five a branch needs. */
static const uint8_t SIG_MP_LOAD_FROM_SCREEN[] = {
    0x55, 0x8B, 0xEC, 0x6A, 0x06, 0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08, 0x6A, 0x00, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0x00, 0x00,
    0x00, 0x00, 0x83, 0xC4, 0x04, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83,
    0xC4, 0x08, 0x5D, 0xC3
};
static const uint8_t MSK_MP_LOAD_FROM_SCREEN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
#define LOAD_FROM_SCREEN_PROLOGUE 5u

/* The script director's extra functions: one entry, a command number in the second argument, and
 * a jump table of twenty entries. Command 1 writes 3 into the level outcome, which is the level
 * ending; command 14 writes the failure and its reason. Every other command is something else
 * entirely, so the hull reads the command rather than the door.
 *
 *   00429880  55 8B EC 83 EC 24     push ebp; mov ebp, esp; sub esp, 0x24
 *   00429886  8B 45 08 / 8B 48 34   the actor, and its record
 *   0042988F  C7 45 F8 00 ..        the answer, which is 0 for every arm
 *   00429896  8B 55 0C / 89 55 DC   the command
 *   0042989C  83 7D DC 13 / 0F 87   anything past 0x13 falls through
 *   004298A6  8B 45 DC / FF 24 85   and the rest is the jump table
 *
 * The table address and the fall-through displacement are wildcards. Prologue six: push ebp, mov
 * ebp esp, sub esp 0x24. */
static const uint8_t SIG_MP_DIRECTOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x34,
    0x89, 0x4D, 0xFC, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x55,
    0x0C, 0x89, 0x55, 0xDC, 0x83, 0x7D, 0xDC, 0x13, 0x0F, 0x87, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x45, 0xDC, 0xFF, 0x24, 0x85, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_DIRECTOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define DIRECTOR_PROLOGUE 6u

static signature_t door_sites[MP_DOOR_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("load_from_screen", SIG_MP_LOAD_FROM_SCREEN,
                                  MSK_MP_LOAD_FROM_SCREEN, LOAD_FROM_SCREEN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("director_extra_func", SIG_MP_DIRECTOR, MSK_MP_DIRECTOR,
                                  DIRECTOR_PROLOGUE)
};

_Static_assert(sizeof(door_sites) / sizeof(door_sites[0]) == (size_t)MP_DOOR_SITE_COUNT,
               "the door site table and mp_door_site_t differ in length");
_Static_assert(sizeof(SIG_MP_LOAD_FROM_SCREEN) == sizeof(MSK_MP_LOAD_FROM_SCREEN) &&
                   sizeof(SIG_MP_DIRECTOR) == sizeof(MSK_MP_DIRECTOR),
               "a door pattern and its mask differ in length");

static bool resolved_once;

size_t mp_signatures_door_resolve(void)
{
    size_t resolved;

    if (resolved_once) {
        return (door_sites[0].address != 0u ? 1u : 0u) + (door_sites[1].address != 0u ? 1u : 0u);
    }
    resolved_once = true;
    resolved = signature_resolve_table(door_sites, MP_DOOR_SITE_COUNT);
    log_info("%u of %u door site(s) resolved", (unsigned)resolved, (unsigned)MP_DOOR_SITE_COUNT);
    return resolved;
}

uintptr_t mp_signatures_door_address(mp_door_site_t site)
{
    if ((size_t)site >= (size_t)MP_DOOR_SITE_COUNT) {
        return 0u;
    }
    (void)mp_signatures_door_resolve();
    return door_sites[site].address;
}

size_t mp_signatures_door_prologue(mp_door_site_t site)
{
    if ((size_t)site >= (size_t)MP_DOOR_SITE_COUNT) {
        return 0u;
    }
    return door_sites[site].detour_prologue;
}
