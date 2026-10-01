/* character_facing.c: the one decision in the two sided draw that can be checked without a game.
 *
 * Everything else in that module needs a running engine: the two patterns, the detour and the
 * question of whether the handle being drawn is the borrowed body all read live memory, and they
 * are checked by the lines the swap writes to the log.
 *
 * What is left is small and worth holding still. The engine's backface switch is not a boolean:
 * the shipped value is 0x103, written once at 0x0043F4D4, and only bit 0 is the drop. A version of
 * this that wrote a zero would have looked identical in every screenshot and would have cleared
 * two bits belonging to somebody else, and the restore would have put them back, so nothing would
 * ever have pointed at it.
 */
#include "unittest.h"

#include "character_facing.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    unsigned value;
    unsigned answered = 0;
    unsigned wrong = 0x100u;      /* above every byte value: no switch broke the rule */

    ut_section("the shipped switch");
    ut_check(character_facing_lower(0x03u) == 0x02u,
             "the low byte of the shipped 0x103 loses bit 0 and keeps bit 1");
    ut_check(character_facing_lower(0x01u) == 0x00u,
             "a switch that is nothing but the drop becomes nothing at all");
    ut_check(character_facing_lower(0xFFu) == 0xFEu,
             "every other bit survives, whatever they are");

    ut_section("nothing to do");
    ut_check(character_facing_lower(0x00u) == CHARACTER_FACING_KEEP,
             "a switch with the drop already off is left alone");
    ut_check(character_facing_lower(0x02u) == CHARACTER_FACING_KEEP,
             "the answer is the same when other bits are set");
    ut_check(CHARACTER_FACING_KEEP > 0xFFu,
             "the leave-it-alone answer is above every byte value, so no switch can spell it");

    /* The sweep counts rather than reporting each of the 256, because a per value line would bury
     * every other test in the suite and none of the 256 is interesting on its own. A failure is
     * still named: the first value that breaks the rule is carried out and printed. */
    ut_section("over all 256 switches");
    for (value = 0; value <= 0xFFu; ++value) {
        uint32_t lowered = character_facing_lower((uint8_t)value);
        bool     right;

        if (lowered == CHARACTER_FACING_KEEP) {
            right = (value & CHARACTER_FACING_CULL_BIT) == 0u;
        } else {
            answered++;
            right = (lowered ^ value) == CHARACTER_FACING_CULL_BIT &&
                    character_facing_lower((uint8_t)lowered) == CHARACTER_FACING_KEEP;
        }
        if (!right && wrong > 0xFFu) {
            wrong = value;
        }
    }
    ut_checkf(wrong > 0xFFu, "every one of the 256 switches changes in bit 0 and in no other bit "
                             "(first offender %03X, and 100 means there was none)", wrong);
    ut_check(answered == 128u, "half the switches carry the drop and half do not");

    ut_section("the module starts disarmed");
    ut_check(!character_facing_is_armed(), "nothing is drawn two sided until a swap arms it");
    ut_check(!character_facing_arm(0u), "a null handle is refused");
    ut_check(!character_facing_arm(0x1000u),
             "and so is a handle, with no draw to hook in a process without the engine");
    ut_check(!character_facing_is_armed(), "and a refused arm leaves nothing armed");

    return ut_summary("character facing");
}
