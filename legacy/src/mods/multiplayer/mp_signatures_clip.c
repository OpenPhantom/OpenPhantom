/* mp_signatures_clip.c: the engine's dispatcher of a clip's own events, as a byte pattern.
 *
 * Its own table because the two main pattern tables stand near their size limit, and because it
 * belongs to the body and pose building block and to nothing else. The function is called, never
 * hulled, from this feature; it is declared as a detour target all the same, because another
 * feature may hull it and a pattern that begins with the bytes a hull overwrites would then match
 * nothing. With the prologue declared, the resolver anchors on the tail and proves the head is the
 * authored one or a branch.
 */
#include "mp_signatures_clip.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The dispatcher the draw calls once per advanced track, with the frames the advance moved:
 * `void __cdecl (bapObj *obj, int slot, float dt)`, three arguments the caller clears (the draw
 * adds 0x0C after the call). It fires every event of the clip whose frame lies from the track's
 * time less dt up to its time, the upper end excluded.
 *
 *   00411897  55 8B EC 83 EC 2C        push ebp; mov ebp, esp; sub esp, 0x2c
 *   0041189D  8B 45 08                 the object
 *   004118A0  8B 88 9C 00 00 00        its drawn thing
 *   004118A6  8B 51 18 / 89 55 E0      the puppet
 *   004118AC  8B                       the slot follows
 *
 * Twenty two bytes and no wildcard; the shipped images hold them once. Prologue six: push ebp,
 * mov ebp esp, sub esp 0x2c. */
static const uint8_t SIG_MP_CLIP_DISPATCH_EVENTS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0x9C, 0x00, 0x00, 0x00,
    0x8B, 0x51, 0x18, 0x89, 0x55, 0xE0, 0x8B
};
#define CLIP_DISPATCH_EVENTS_PROLOGUE 6u

static signature_t clip_sites[MP_CLIP_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("bapobj_dispatch_clip_events", SIG_MP_CLIP_DISPATCH_EVENTS,
                           CLIP_DISPATCH_EVENTS_PROLOGUE)
};

_Static_assert(sizeof(clip_sites) / sizeof(clip_sites[0]) == (size_t)MP_CLIP_SITE_COUNT,
               "the clip site table and mp_clip_site_t differ in length");

static bool resolved_once;

uintptr_t mp_signatures_clip_address(mp_clip_site_t site)
{
    if ((size_t)site >= (size_t)MP_CLIP_SITE_COUNT) {
        return 0u;
    }
    if (!resolved_once) {
        size_t resolved;

        resolved_once = true;
        resolved = signature_resolve_table(clip_sites, MP_CLIP_SITE_COUNT);
        log_info("%u of %u clip site(s) resolved", (unsigned)resolved,
                 (unsigned)MP_CLIP_SITE_COUNT);
    }
    return clip_sites[site].address;
}
