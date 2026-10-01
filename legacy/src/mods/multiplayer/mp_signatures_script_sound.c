/* mp_signatures_script_sound.c: the sound opcode's call and the two routines around it, as byte
 * patterns. Its own table, as the push blocks and the doors have theirs, because the main table's
 * file is at its size limit. The resolver, the two stage rule and the reporting are the shared
 * ones.
 */
#include "mp_signatures_script_sound.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The sound opcode, from the store of the call into the actor's last call to the call itself:
 *   mov ecx,[actor]; mov edx,[call]; mov [ecx+0x94],edx;     the actor's last call, written first
 *   mov eax,[actor]; add eax,0xD0; push eax;                 the actor's own position
 *   mov ecx,[handle]; push ecx;                              the loop cell or the shared cell
 *   mov edx,[op]; mov eax,[edx]; push eax;                   the call's index
 *   call bapsound_playCall
 * Only frame and structure offsets, no absolute address; the call's operand is masked, because the
 * session repoints it. */
static const uint8_t SIG_MP_SCRIPT_SOUND_CALL[36] = {
    0x8B, 0x4D, 0x08, 0x8B, 0x55, 0xA4, 0x89, 0x91, 0x94, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x08, 0x05, 0xD0, 0x00, 0x00, 0x00, 0x50, 0x8B, 0x4D, 0xA8,
    0x51, 0x8B, 0x55, 0xF8, 0x8B, 0x02, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_SCRIPT_SOUND_CALL[36] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define SCRIPT_SOUND_CALL 31u

/* bapsound_playCall, from its head to the index scaled by the record's size:
 *   push ebp; mov ebp,esp; push ecx; mov eax,[world];
 *   mov ecx,[call]; cmp ecx,[eax+0xCC4]; jb +2; jmp out;
 *   mov edx,[world]; mov eax,[edx+0xCC8]; mov [local],eax;
 *   mov ecx,[call]; shl ecx,6
 * The world cell is masked at both loads and read out of the first; the two field offsets and the
 * shift stay literal, because they are what the multiplayer reads the records with. The head is
 * nine bytes, a whole number of instructions, for a module that hulls it one day. */
static const uint8_t SIG_MP_SCRIPT_SOUND_PLAY_CALL[43] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4D, 0x08,
    0x3B, 0x88, 0xC4, 0x0C, 0x00, 0x00, 0x72, 0x02, 0xEB, 0x44, 0x8B, 0x15,
    0x00, 0x00, 0x00, 0x00, 0x8B, 0x82, 0xC8, 0x0C, 0x00, 0x00, 0x89, 0x45,
    0xFC, 0x8B, 0x4D, 0x08, 0xC1, 0xE1, 0x06
};
static const uint8_t MSK_MP_SCRIPT_SOUND_PLAY_CALL[43] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PLAY_CALL_PROLOGUE     9u
#define PLAY_CALL_LEVEL_CELL   0x05u
#define PLAY_CALL_LEVEL_AGAIN  0x18u

/* bapsound_pinChannel, its head to the test of the slot's sound:
 *   push ebp; mov ebp,esp; push ecx; mov eax,[channel]; shl eax,7; add eax,&bank;
 *   mov [local],eax; mov ecx,[local]; cmp dword [ecx+0xC],0; je out
 * The bank's operand is masked and read out; the stride and the slot's sound field stay literal.
 * The seven byte head is the one sound_lifetime_fix hulls. */
static const uint8_t SIG_MP_SCRIPT_SOUND_PIN_CHANNEL[27] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x07, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x0C,
    0x00, 0x74, 0x27
};
static const uint8_t MSK_MP_SCRIPT_SOUND_PIN_CHANNEL[27] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
#define PIN_CHANNEL_PROLOGUE 7u
#define PIN_CHANNEL_BANK     0x0Bu

/* The one call a session's arming repoints. */
SIGNATURE_REDIRECTED_CALL(SIG_MP_SCRIPT_SOUND_CALL, SCRIPT_SOUND_CALL);

_Static_assert(sizeof(SIG_MP_SCRIPT_SOUND_CALL) == sizeof(MSK_MP_SCRIPT_SOUND_CALL) &&
                   sizeof(SIG_MP_SCRIPT_SOUND_PLAY_CALL) ==
                       sizeof(MSK_MP_SCRIPT_SOUND_PLAY_CALL) &&
                   sizeof(SIG_MP_SCRIPT_SOUND_PIN_CHANNEL) ==
                       sizeof(MSK_MP_SCRIPT_SOUND_PIN_CHANNEL),
               "a script sound pattern and its mask differ in length");
_Static_assert(SCRIPT_SOUND_CALL + 5u == sizeof(SIG_MP_SCRIPT_SOUND_CALL),
               "the opcode's pattern ends on its call and the four operand bytes behind it");
_Static_assert(PLAY_CALL_LEVEL_AGAIN + 4u <= sizeof(SIG_MP_SCRIPT_SOUND_PLAY_CALL) &&
                   PIN_CHANNEL_BANK + 4u <= sizeof(SIG_MP_SCRIPT_SOUND_PIN_CHANNEL),
               "every operand read out lies inside its pattern");

/* The E8 of a near call. */
#define CALL_REL32_OPCODE 0xE8u

static signature_t sites[MP_SCRIPT_SOUND_SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("script_sound_call", SIG_MP_SCRIPT_SOUND_CALL,
                           MSK_MP_SCRIPT_SOUND_CALL),
    SIGNATURE_ENTRY_DETOUR_MASKED("script_sound_play_call", SIG_MP_SCRIPT_SOUND_PLAY_CALL,
                                  MSK_MP_SCRIPT_SOUND_PLAY_CALL, PLAY_CALL_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("script_sound_pin_channel", SIG_MP_SCRIPT_SOUND_PIN_CHANNEL,
                                  MSK_MP_SCRIPT_SOUND_PIN_CHANNEL, PIN_CHANNEL_PROLOGUE)
};

static bool   resolved_once;
static size_t resolved_count;

size_t mp_signatures_script_sound_resolve(void)
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
    resolved_count = signature_resolve_table(sites, MP_SCRIPT_SOUND_SITE_COUNT);
    QueryPerformanceCounter(&ended);
    log_info("the scripts' sound sites: %u of %u resolved, in %.3f ms",
             (unsigned)resolved_count, (unsigned)MP_SCRIPT_SOUND_SITE_COUNT,
             frequency.QuadPart != 0
                 ? (double)(ended.QuadPart - started.QuadPart) * 1000.0 /
                       (double)frequency.QuadPart
                 : 0.0);
    return resolved_count;
}

uintptr_t mp_signatures_script_sound_call(void)
{
    uintptr_t site   = sites[MP_SCRIPT_SOUND_SITE_CALL].address;
    uint8_t   opcode = 0u;

    if (site == 0u || !memory_try_read(site + SCRIPT_SOUND_CALL, &opcode, sizeof opcode) ||
        opcode != CALL_REL32_OPCODE) {
        return 0u;
    }
    return site + SCRIPT_SOUND_CALL;
}

uintptr_t mp_signatures_script_sound_address(mp_script_sound_site_t site)
{
    if ((size_t)site >= (size_t)MP_SCRIPT_SOUND_SITE_COUNT) {
        return 0u;
    }
    return sites[site].address;
}

/* The world cell is loaded twice inside the pattern, and both loads have to name the same cell. */
bool mp_signatures_script_sound_level_cell(uintptr_t *cell)
{
    const signature_t *play  = &sites[MP_SCRIPT_SOUND_SITE_PLAY_CALL];
    uintptr_t          first = 0u;
    uintptr_t          again = 0u;

    if (cell == NULL || play->address == 0u ||
        !signature_read_address_operand(play, PLAY_CALL_LEVEL_CELL, &first) ||
        !signature_read_address_operand(play, PLAY_CALL_LEVEL_AGAIN, &again) || first != again) {
        return false;
    }
    *cell = first;
    return true;
}

bool mp_signatures_script_sound_channel_bank(uintptr_t *bank)
{
    const signature_t *pin = &sites[MP_SCRIPT_SOUND_SITE_PIN_CHANNEL];

    return bank != NULL && pin->address != 0u &&
           signature_read_address_operand(pin, PIN_CHANNEL_BANK, bank);
}

const signature_t *mp_signatures_script_sound_sites(size_t *count)
{
    if (count != NULL) {
        *count = (size_t)MP_SCRIPT_SOUND_SITE_COUNT;
    }
    return sites;
}
