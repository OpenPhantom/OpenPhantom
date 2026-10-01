/* mp_signatures_effects.c: the blast and the zap arcs as byte patterns. See the header.
 *
 * A table of its own because the main pattern table stands at its size limit; the subject is its
 * own anyway. The two functions are declared with their prologues, the one hulled and the one only
 * called, because every function head says how much of it a detour would take.
 */
#include "mp_signatures_effects.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Plr_ExplodeAt(const vec3 *at, f32 pitch): the three sound fields, the spark emitter at the point,
 * the flash sprite and the blast's sound, and nothing that hurts anybody.
 *
 *   004512CE  55 8B EC 51 8B 45 08     push ebp; mov ebp, esp; push ecx; mov eax, [ebp+8]
 *   004512D5  89 45 FC                 the point kept for the sound
 *   004512D8  68 00 00 C8 42 6A 02 E8  bapsound_setField(2, 100.0)
 *   004512E7  68 00 00 C8 42 6A 04 E8  bapsound_setField(4, 100.0)
 *   004512F6  8B 4D 0C 51 6A 03 E8     bapsound_setField(3, pitch)
 *   00451304  6A 01 8B 55 08 8B 42 08  emitter_spawnBuiltin(0, x, y, z, 1), the z first
 *
 * The three call displacements are wildcards. Prologue seven: push ebp, mov ebp esp, push ecx and
 * the three byte load are the first boundary at or past the five a branch needs. */
static const uint8_t SIG_MP_EXPLODE_AT[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC, 0x68, 0x00,
    0x00, 0xC8, 0x42, 0x6A, 0x02, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x08, 0x68, 0x00, 0x00, 0xC8, 0x42, 0x6A, 0x04, 0xE8, 0x00, 0x00, 0x00,
    0x00, 0x83, 0xC4, 0x08, 0x8B, 0x4D, 0x0C, 0x51, 0x6A, 0x03, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x83, 0xC4, 0x08, 0x6A, 0x01, 0x8B, 0x55, 0x08, 0x8B,
    0x42, 0x08, 0x50
};
static const uint8_t MSK_MP_EXPLODE_AT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
#define EXPLODE_AT_PROLOGUE 7u

/* The end of the director's command 19: the pitch, the point the node sphere filled, the call,
 * and the director's answer read back for its return.
 *
 *   00429C25  8B 4D 14 51     push the second operand, the pitch
 *   00429C29  8D 55 E0 52     push the address of the point
 *   00429C2D  E8 ..           Plr_ExplodeAt
 *   00429C32  83 C4 08 8B 45 F8
 *
 * The call displacement is a wildcard. Not a function head, so no prologue. */
static const uint8_t SIG_MP_DIRECTOR_BLAST[] = {
    0x8B, 0x4D, 0x14, 0x51, 0x8D, 0x55, 0xE0, 0x52, 0xE8, 0x00, 0x00, 0x00,
    0x00, 0x83, 0xC4, 0x08, 0x8B, 0x45, 0xF8
};
static const uint8_t MSK_MP_DIRECTOR_BLAST[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* fxzappo_create(a, b, lifeSec, width, colourRGB, bHurts): the first free of the 64 arc slots,
 * cleared, filled, and its four points put on a or, with b, three on b and the last on a.
 *
 *   0043CD39  55 8B EC 83 EC 08       push ebp; mov ebp, esp; sub esp, 8
 *   0043CD3F  57                      push edi
 *   0043CD40  C7 45 FC 00 00 00 00    the slot counter from 0
 *   0043CD52  83 7D FC 40 7D 13       to 64
 *   0043CD5B  6B C9 78 83 B9 .. 00    a slot is 0x78 bytes, free while its first word is 0
 *   0043CD6B  83 7D FC 40 75 07 33 C0 E9 ..   none free answers 0
 *
 * The pool address and the jump to the end are wildcards. Prologue six: push ebp, mov ebp esp,
 * sub esp 8. */
static const uint8_t SIG_MP_FXZAPPO_CREATE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x57, 0xC7, 0x45, 0xFC, 0x00, 0x00,
    0x00, 0x00, 0xEB, 0x09, 0x8B, 0x45, 0xFC, 0x83, 0xC0, 0x01, 0x89, 0x45,
    0xFC, 0x83, 0x7D, 0xFC, 0x40, 0x7D, 0x13, 0x8B, 0x4D, 0xFC, 0x6B, 0xC9,
    0x78, 0x83, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x02, 0xEB, 0x02,
    0xEB, 0xDE, 0x83, 0x7D, 0xFC, 0x40, 0x75, 0x07, 0x33, 0xC0, 0xE9, 0x00,
    0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_FXZAPPO_CREATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00
};
#define FXZAPPO_CREATE_PROLOGUE 6u

/* The player zap's spawn arm, shot_handlerPlayerZap: its two arcs from the player it names to the
 * zap's own body, one second and a second and a half, both 16 wide, both with bHurts 0.
 *
 *   00455F39  6A 00 68 00 00 FF F0 68 00 00 80 41 68 00 00 80 3F   0, colour, 16.0, 1.0
 *   00455F4A  8B 4D 08 8B 91 A0 00 00 00 52                         the zap's body, shot +0xA0
 *   00455F54  8B 45 FC 50 E8 ..                                     the player, fxzappo_create
 *   00455F60  6A 00 68 00 00 FF C8 68 00 00 80 41 68 00 00 C0 3F   0, colour, 16.0, 1.5
 *   00455F7F  E8 ..                                                 fxzappo_create again
 *
 * The two call displacements are wildcards; the parameters are literal, so a client replaying the
 * arcs with them replays what this pattern proved. Not a function head, so no prologue. */
static const uint8_t SIG_MP_PLAYER_ZAP_ARCS[] = {
    0x6A, 0x00, 0x68, 0x00, 0x00, 0xFF, 0xF0, 0x68, 0x00, 0x00, 0x80, 0x41,
    0x68, 0x00, 0x00, 0x80, 0x3F, 0x8B, 0x4D, 0x08, 0x8B, 0x91, 0xA0, 0x00,
    0x00, 0x00, 0x52, 0x8B, 0x45, 0xFC, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x18, 0x6A, 0x00, 0x68, 0x00, 0x00, 0xFF, 0xC8, 0x68, 0x00,
    0x00, 0x80, 0x41, 0x68, 0x00, 0x00, 0xC0, 0x3F, 0x8B, 0x4D, 0x08, 0x8B,
    0x91, 0xA0, 0x00, 0x00, 0x00, 0x52, 0x8B, 0x45, 0xFC, 0x50, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x83, 0xC4, 0x18
};
static const uint8_t MSK_MP_PLAYER_ZAP_ARCS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};

_Static_assert(sizeof SIG_MP_EXPLODE_AT == sizeof MSK_MP_EXPLODE_AT &&
                   sizeof SIG_MP_DIRECTOR_BLAST == sizeof MSK_MP_DIRECTOR_BLAST &&
                   sizeof SIG_MP_FXZAPPO_CREATE == sizeof MSK_MP_FXZAPPO_CREATE &&
                   sizeof SIG_MP_PLAYER_ZAP_ARCS == sizeof MSK_MP_PLAYER_ZAP_ARCS,
               "an effects pattern and its mask differ in length");
_Static_assert(MP_EFFECTS_DIRECTOR_BLAST_CALL + 5u <= sizeof SIG_MP_DIRECTOR_BLAST &&
                   MP_EFFECTS_ZAP_SECOND_CALL + 5u <= sizeof SIG_MP_PLAYER_ZAP_ARCS,
               "a call read out of a site lies inside the pattern that finds it");

static signature_t effects_sites[MP_EFFECTS_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("explode_at", SIG_MP_EXPLODE_AT, MSK_MP_EXPLODE_AT,
                                  EXPLODE_AT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("director_blast_call", SIG_MP_DIRECTOR_BLAST, MSK_MP_DIRECTOR_BLAST),
    SIGNATURE_ENTRY_DETOUR_MASKED("fxzappo_create", SIG_MP_FXZAPPO_CREATE, MSK_MP_FXZAPPO_CREATE,
                                  FXZAPPO_CREATE_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("player_zap_arcs", SIG_MP_PLAYER_ZAP_ARCS, MSK_MP_PLAYER_ZAP_ARCS),
};

_Static_assert(sizeof(effects_sites) / sizeof(effects_sites[0]) == (size_t)MP_EFFECTS_SITE_COUNT,
               "the effects site table and mp_effects_site_t differ in length");

static bool   resolved_once;
static size_t resolved_count;

static size_t resolve(void)
{
    if (resolved_once) {
        return resolved_count;
    }
    resolved_once  = true;
    resolved_count = signature_resolve_table(effects_sites, MP_EFFECTS_SITE_COUNT);
    log_info("%u of %u effects site(s) resolved", (unsigned)resolved_count,
             (unsigned)MP_EFFECTS_SITE_COUNT);
    return resolved_count;
}

uintptr_t mp_signatures_effects_address(mp_effects_site_t site)
{
    if ((size_t)site >= (size_t)MP_EFFECTS_SITE_COUNT) {
        return 0u;
    }
    (void)resolve();
    return effects_sites[site].address;
}

size_t mp_signatures_effects_prologue(mp_effects_site_t site)
{
    if ((size_t)site >= (size_t)MP_EFFECTS_SITE_COUNT) {
        return 0u;
    }
    return effects_sites[site].detour_prologue;
}

uintptr_t mp_signatures_effects_callee(mp_effects_site_t site, size_t offset,
                                       mp_effects_site_t named)
{
    uintptr_t at     = mp_signatures_effects_address(site);
    uintptr_t target = mp_signatures_effects_address(named);
    uintptr_t called = 0u;

    if (at == 0u || target == 0u || offset + 5u > effects_sites[site].size ||
        !patch_read_call_target(at + offset, &called) || called != target) {
        return 0u;
    }
    return called;
}
