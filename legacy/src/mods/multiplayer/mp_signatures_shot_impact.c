/* mp_signatures_shot_impact.c: the shot impact and its two sub shot spawns as byte patterns. See
 * the header.
 *
 * A table of its own because the main pattern table stands at its size limit, and each building
 * block keeps its patterns in a file of its own.
 */
#include "mp_signatures_shot_impact.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* shot_impact(shot *pShot, void *pVictim): a shot flagged to make no impact effect leaves at once;
 * every other one keeps its position for the burst and goes on to its sparks, its emitters, the
 * sub shot of an explosive and the area ring of the four kinds that have one.
 *
 *   0045391C  55 8B EC 83 EC 2C              push ebp; mov ebp, esp; sub esp, 0x2C
 *   00453922  C7 45 F4 FF FF FF FF           the sound index starts at -1
 *   00453929  8B 45 08 8B 88 84 00 00 00     the shot's flag word
 *   00453932  81 E1 00 10 00 00 85 C9 74 05  no impact effect?
 *   0045393C  E9 ..                          then straight to the end
 *   00453941  8B 55 08 83 C2 20 8B 02 89 45 E8   the burst at the shot's position
 *
 * The jump to the end is a wildcard. Prologue six: push ebp, mov ebp esp and the three byte
 * sub esp, the first boundary at or past the five a branch needs, with no relative operand. */
static const uint8_t SIG_MP_SHOT_IMPACT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0xC7, 0x45, 0xF4, 0xFF, 0xFF, 0xFF,
    0xFF, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0x84, 0x00, 0x00, 0x00, 0x81, 0xE1,
    0x00, 0x10, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x05, 0xE9, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x55, 0x08, 0x83, 0xC2, 0x20, 0x8B, 0x02, 0x89, 0x45, 0xE8
};
static const uint8_t MSK_MP_SHOT_IMPACT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define SHOT_IMPACT_PROLOGUE 6u

/* The sub shot of an explosive: the ring when the shot's flags lack 0x400, the fireball when they
 * carry it, both at the burst position with no angles and with shooter class 0.
 *
 *   00453ABF  8B 45 08 8B 88 84 00 00 00     the shot's flag word
 *   00453AC8  81 E1 00 04 00 00 85 C9 75 19  the alternative sub shot?
 *   00453AD2  6A 00 6A 00 6A 00 8D 55 E8 52  class 0, yaw 0, pitch 0, the burst position
 *   00453ADC  6A 1D E8 ..                    shot_spawn(0x1D, ...)
 *   00453AE3  83 C4 14 89 45 E0 EB 17        kept in a local nobody reads again
 *   00453AEB  6A 00 6A 00 6A 00 8D 45 E8 50  the same arguments
 *   00453AF5  6A 1E E8 ..                    shot_spawn(0x1E, ...)
 *   00453AFC  83 C4 14 89 45 E0
 *
 * The two call displacements are wildcards; the class, the kinds and the flag are literal, so the
 * sub shots a hull ties to their parent are the ones this pattern proved. Not a function head, so
 * no prologue. */
static const uint8_t SIG_MP_IMPACT_SUB_SHOTS[] = {
    0x8B, 0x45, 0x08, 0x8B, 0x88, 0x84, 0x00, 0x00, 0x00, 0x81, 0xE1, 0x00,
    0x04, 0x00, 0x00, 0x85, 0xC9, 0x75, 0x19, 0x6A, 0x00, 0x6A, 0x00, 0x6A,
    0x00, 0x8D, 0x55, 0xE8, 0x52, 0x6A, 0x1D, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x14, 0x89, 0x45, 0xE0, 0xEB, 0x17, 0x6A, 0x00, 0x6A, 0x00,
    0x6A, 0x00, 0x8D, 0x45, 0xE8, 0x50, 0x6A, 0x1E, 0xE8, 0x00, 0x00, 0x00,
    0x00, 0x83, 0xC4, 0x14, 0x89, 0x45, 0xE0
};
static const uint8_t MSK_MP_IMPACT_SUB_SHOTS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* The impact's length in the retail image, head to the next function. The spawns lie inside it. */
#define SHOT_IMPACT_BYTES 950u

_Static_assert(sizeof SIG_MP_SHOT_IMPACT == sizeof MSK_MP_SHOT_IMPACT &&
                   sizeof SIG_MP_IMPACT_SUB_SHOTS == sizeof MSK_MP_IMPACT_SUB_SHOTS,
               "a shot impact pattern and its mask differ in length");
_Static_assert(MP_SHOT_IMPACT_RING_CALL + 5u <= sizeof SIG_MP_IMPACT_SUB_SHOTS &&
                   MP_SHOT_IMPACT_FIREBALL_CALL + 5u <= sizeof SIG_MP_IMPACT_SUB_SHOTS,
               "a call read out of a site lies inside the pattern that finds it");

static signature_t impact_sites[MP_SHOT_IMPACT_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("shot_impact", SIG_MP_SHOT_IMPACT, MSK_MP_SHOT_IMPACT,
                                  SHOT_IMPACT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("shot_impact_sub_shots", SIG_MP_IMPACT_SUB_SHOTS,
                           MSK_MP_IMPACT_SUB_SHOTS),
};

_Static_assert(sizeof(impact_sites) / sizeof(impact_sites[0]) ==
                   (size_t)MP_SHOT_IMPACT_SITE_COUNT,
               "the shot impact site table and mp_shot_impact_site_t differ in length");

static bool   resolved_once;
static size_t resolved_count;

static size_t resolve(void)
{
    if (resolved_once) {
        return resolved_count;
    }
    resolved_once  = true;
    resolved_count = signature_resolve_table(impact_sites, MP_SHOT_IMPACT_SITE_COUNT);
    log_info("%u of %u shot impact site(s) resolved", (unsigned)resolved_count,
             (unsigned)MP_SHOT_IMPACT_SITE_COUNT);
    return resolved_count;
}

uintptr_t mp_signatures_shot_impact_address(mp_shot_impact_site_t site)
{
    if ((size_t)site >= (size_t)MP_SHOT_IMPACT_SITE_COUNT) {
        return 0u;
    }
    (void)resolve();
    return impact_sites[site].address;
}

size_t mp_signatures_shot_impact_prologue(mp_shot_impact_site_t site)
{
    if ((size_t)site >= (size_t)MP_SHOT_IMPACT_SITE_COUNT) {
        return 0u;
    }
    return impact_sites[site].detour_prologue;
}

static bool calls(uintptr_t site, size_t offset, uintptr_t target)
{
    uintptr_t called = 0u;

    return patch_read_call_target(site + offset, &called) && called == target;
}

bool mp_signatures_shot_impact_proves_sub_shots(uintptr_t shot_spawn)
{
    uintptr_t head = mp_signatures_shot_impact_address(MP_SHOT_IMPACT_SITE_HEAD);
    uintptr_t subs = mp_signatures_shot_impact_address(MP_SHOT_IMPACT_SITE_SUB_SHOTS);

    if (head == 0u || subs == 0u || shot_spawn == 0u || subs <= head ||
        subs + sizeof SIG_MP_IMPACT_SUB_SHOTS > head + SHOT_IMPACT_BYTES) {
        return false;
    }
    return calls(subs, MP_SHOT_IMPACT_RING_CALL, shot_spawn) &&
           calls(subs, MP_SHOT_IMPACT_FIREBALL_CALL, shot_spawn);
}
