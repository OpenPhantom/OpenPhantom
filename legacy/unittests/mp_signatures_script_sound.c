/* mp_signatures_script_sound.c: the shape of the scripts' sound table, checked without a game.
 *
 * Where the three sites are only the real executables can prove. What a test can prove
 * here is that the table is capable of being right:
 *
 *   the repointed call ends its pattern, with the four operand bytes behind its E8 masked, or the
 *   pattern stops finding its own site the moment the session repoints it;
 *   no pattern requires an absolute address, or it resolves for one build and one load order;
 *   every operand the module reads out is masked in its own pattern, or the pattern pins the very
 *   value the read exists to report;
 *   a detour head is long enough for the branch and leaves a tail to anchor on;
 *   and a process with no game in it resolves nothing and answers zero rather than an address.
 */
#include "unittest.h"

#include "mp_signatures_script_sound.h"

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where an address of the retail image lies, as a value inside a pattern. */
#define IMAGE_LOW  0x00400000u
#define IMAGE_HIGH 0x00900000u

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool required(const signature_t *site, size_t offset)
{
    return site->mask == NULL || site->mask[offset] != 0u;
}

static bool window_masked(const signature_t *site, size_t offset)
{
    size_t i;

    for (i = 0; i < 4u; ++i) {
        if (site->mask == NULL || site->mask[offset + i] != 0u) {
            return false;
        }
    }
    return true;
}

static void check_no_address(const signature_t *sites, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        size_t offset;
        bool   clean = true;

        for (offset = 0; offset + 4u <= sites[i].size; ++offset) {
            uint32_t value = read_le32(&sites[i].bytes[offset]);

            if (required(&sites[i], offset) && required(&sites[i], offset + 1u) &&
                required(&sites[i], offset + 2u) && required(&sites[i], offset + 3u) &&
                value >= IMAGE_LOW && value < IMAGE_HIGH) {
                clean = false;
            }
        }
        ut_checkf(clean, "%s requires no absolute address", sites[i].name);
    }
}

int main(void)
{
    size_t             count = 0;
    const signature_t *sites = mp_signatures_script_sound_sites(&count);
    const signature_t *call;
    const signature_t *play;
    const signature_t *pin;

    ut_section("the table");
    ut_check(count == (size_t)MP_SCRIPT_SOUND_SITE_COUNT && count == 3u, "three sites");
    call = &sites[MP_SCRIPT_SOUND_SITE_CALL];
    play = &sites[MP_SCRIPT_SOUND_SITE_PLAY_CALL];
    pin  = &sites[MP_SCRIPT_SOUND_SITE_PIN_CHANNEL];
    check_no_address(sites, count);

    ut_section("the repointed call");
    ut_check(call->size == 36u && call->bytes[31] == 0xE8u && call->mask != NULL &&
                 call->mask[31] == 0xFFu && window_masked(call, 32u),
             "the opcode's pattern ends on its E8 at +31 with the operand behind it masked");
    ut_check(call->detour_prologue == 0u, "and it is no detour target: its call is repointed");
    ut_check(read_le32(&call->bytes[16]) == 0xD0u && call->bytes[15] == 0x05u,
             "the position it pushes is the actor's own, +0xD0, which the hull subtracts again");

    ut_section("the two routines");
    ut_check(window_masked(play, 0x05u) && window_masked(play, 0x18u),
             "the play routine's two loads of the world cell are masked and read out");
    ut_check(read_le32(&play->bytes[0x0E]) == MP_SCRIPT_SOUND_LEVEL_CALL_COUNT &&
                 read_le32(&play->bytes[0x1E]) == MP_SCRIPT_SOUND_LEVEL_CALLS &&
                 play->bytes[0x2A] == 6u,
             "its pattern pins the count and the table of sound calls and the record's size");
    ut_check(window_masked(pin, 0x0Bu) && pin->bytes[0x09] == 0x07u,
             "the pin routine's channel bank is masked and read out, the stride of 0x80 pinned");
    ut_check(play->detour_prologue >= 5u && play->detour_prologue < play->size &&
                 pin->detour_prologue >= 5u && pin->detour_prologue < pin->size,
             "both heads cover a branch and leave a tail to anchor on");

    ut_section("a process with no game in it");
    ut_check(mp_signatures_script_sound_resolve() == 0u, "nothing resolves");
    {
        uintptr_t cell = 1u;
        uintptr_t bank = 1u;

        ut_check(mp_signatures_script_sound_call() == 0u &&
                     mp_signatures_script_sound_address(MP_SCRIPT_SOUND_SITE_PLAY_CALL) == 0u &&
                     mp_signatures_script_sound_address(MP_SCRIPT_SOUND_SITE_COUNT) == 0u,
                 "no call and no routine is answered, and past the end none ever is");
        ut_check(!mp_signatures_script_sound_level_cell(&cell) &&
                     !mp_signatures_script_sound_channel_bank(&bank),
                 "and no operand is read out of a site that did not resolve");
    }
    return ut_summary("the scripts' sound sites");
}
