/* mp_enemy_shield_rule.c: a droideka's shield as sixteen bits, and the director commands that bring
 * a replica to it.
 *
 * The colours are the director's own five, read out of its command 2: red, blue, violet, yellow and
 * cyan; the pool paints a shield green until one of them is asked for. The radii are the four the
 * shipped scripts give command 5, 1.0, 1.5, 1.9 and 2.0, each written with a trailing 0.0001, and
 * the 0.5 every hang starts at.
 */
#include "unittest.h"

#include "mp_enemy_shield_rule.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static mp_enemy_shield_t want_of(bool hangs, bool shown, uint32_t colour)
{
    mp_enemy_shield_t s;

    memset(&s, 0, sizeof s);
    s.read   = true;
    s.hangs  = hangs;
    s.shown  = shown;
    s.colour = colour;
    return s;
}

static mp_enemy_shield_here_t here_of(int32_t id, bool in_use, bool shown, uint32_t colour)
{
    mp_enemy_shield_here_t h;

    memset(&h, 0, sizeof h);
    h.id     = id;
    h.in_use = in_use;
    h.shown  = shown;
    h.colour = colour;
    return h;
}

static void check_the_byte(void)
{
    uint32_t          colour;
    mp_enemy_shield_t back;
    mp_enemy_shield_t none;

    ut_section("the byte goes round");
    for (colour = 0; colour <= 5u; ++colour) {
        mp_enemy_shield_t s = want_of(true, (colour & 1u) != 0u, colour);

        back = mp_enemy_shield_unpack(mp_enemy_shield_pack(&s));
        ut_checkf(back.read && back.hangs && back.shown == s.shown && back.colour == colour,
                  "a hanging shield in colour %u", (unsigned)colour);
    }
    none = want_of(false, true, 3u);
    back = mp_enemy_shield_unpack(mp_enemy_shield_pack(&none));
    ut_check(back.read && !back.hangs && !back.shown && back.colour == 0u,
             "no shield says nothing of shown or colour");
    ut_check(mp_enemy_shield_pack(&none) != 0u,
             "and is not 0, which would be a sender that could not read the body");
    back = mp_enemy_shield_unpack(0u);
    ut_check(!back.read, "0 says nothing at all");
    back = mp_enemy_shield_unpack(MP_ENEMY_SHIELD_READ | MP_ENEMY_SHIELD_HANGS |
                                  (7u << MP_ENEMY_SHIELD_COLOUR_SHIFT));
    ut_check(back.hangs && back.colour == 0u, "a colour code nobody paints is the default");

    ut_section("the colour a pool slot holds, by the director's operand");
    ut_check(mp_enemy_shield_colour(0xFFu, 0x00u, 0x00u) == 1u, "red is 1");
    ut_check(mp_enemy_shield_colour(0x00u, 0x00u, 0xFFu) == 2u, "blue is 2");
    ut_check(mp_enemy_shield_colour(0xC8u, 0x00u, 0xFFu) == 3u, "violet is 3");
    ut_check(mp_enemy_shield_colour(0xFFu, 0xFFu, 0x00u) == 4u, "yellow is 4");
    ut_check(mp_enemy_shield_colour(0x00u, 0xFFu, 0xFFu) == 5u, "cyan is 5");
    ut_check(mp_enemy_shield_colour(0x00u, 0xE6u, 0x40u) == 0u, "the pool's own green is 0");
    ut_check(mp_enemy_shield_colour(0xFFu, 0xFFu, 0xFFu) == 0u,
             "and so is the white a burning bolt wears, which no script asks for");
}

static void check_the_plan(void)
{
    mp_enemy_shield_t      want;
    mp_enemy_shield_here_t here;
    mp_enemy_shield_plan_t plan;

    ut_section("what brings a replica to the host's shield");

    want = want_of(true, true, 2u);
    here = here_of(-1, false, false, 0u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.create && plan.colour == 2u && !plan.release && !plan.show,
             "none here and a blue one on the host: command 2 with operand 2, which shows it");

    want = want_of(true, false, 2u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.create && plan.show && !plan.shown,
             "a hidden one: hung, then command 4 with 0");

    here = here_of(3, false, false, 0u);
    want = want_of(true, true, 1u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.release && plan.create && !plan.recolour,
             "a word that names a slot no longer in use shows nothing, and the director would not "
             "hang over it: taken off first, which only sets the word back");

    here = here_of(3, true, true, 4u);
    want = want_of(true, true, 1u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.release && plan.create && plan.recolour && plan.colour == 1u,
             "a shield in another colour is taken off and hung again, since only command 2 paints");

    here = here_of(3, true, true, 1u);
    want = want_of(true, false, 1u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.show && !plan.shown && !plan.create && !plan.release,
             "the same shield hidden on the host: command 4 alone");

    want = want_of(false, false, 0u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.release && !plan.create && !plan.show, "none on the host: command 3 alone");

    here = here_of(3, false, false, 0u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(!plan.release && !plan.create && !plan.show,
             "and nothing for a stale word that shows nothing, as on the host");

    here = here_of(3, true, true, 5u);
    want = want_of(true, true, 5u);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(!plan.release && !plan.create && !plan.show && mp_enemy_shield_agrees(&want, &here),
             "a replica that already agrees is left alone");

    memset(&want, 0, sizeof want);
    plan = mp_enemy_shield_plan(&want, &here);
    ut_check(!plan.release && !plan.create && !plan.show,
             "a record from a host that could not read the body changes nothing");
}

static void check_the_radius(void)
{
    mp_enemy_shield_t      want;
    mp_enemy_shield_t      back;
    mp_enemy_shield_here_t here;
    mp_enemy_shield_plan_t plan;
    float                  nan_value;
    uint32_t               nan_bits = 0x7FC00000u;

    ut_section("the radius in eighths, compared in eighths");

    ut_check(mp_enemy_shield_eighths(0.5f) == 4u && mp_enemy_shield_eighths(1.0001f) == 8u &&
                 mp_enemy_shield_eighths(1.50015f) == 12u &&
                 mp_enemy_shield_eighths(1.90019f) == 15u &&
                 mp_enemy_shield_eighths(2.0002f) == 16u,
             "a hang's 0.5 is 4, and the scripts' 1.0, 1.5, 1.9 and 2.0 are 8, 12, 15 and 16");
    ut_check(mp_enemy_shield_eighths(mp_enemy_shield_radius_of(15u)) == 15u,
             "1.9 comes back as 1.875, which is 15 again: sized once, and then it agrees");
    memcpy(&nan_value, &nan_bits, sizeof nan_value);
    ut_check(mp_enemy_shield_eighths(0.0f) == 0u && mp_enemy_shield_eighths(-1.0f) == 0u &&
                 mp_enemy_shield_eighths(nan_value) == 0u,
             "nought, below nought and not a number are no radius");
    ut_check(mp_enemy_shield_eighths(0.01f) == 1u && mp_enemy_shield_eighths(100.0f) == 255u,
             "a radius above nought is at least one eighth, and one past the byte is the byte");

    want        = want_of(true, true, 2u);
    want.radius = 15u;
    back        = mp_enemy_shield_unpack(mp_enemy_shield_pack(&want));
    ut_checkf(back.radius == 15u && back.colour == 2u && back.shown,
              "the radius rides in the high byte beside the rest (%04X)",
              (unsigned)mp_enemy_shield_pack(&want));
    ut_check((mp_enemy_shield_pack(&want) & MP_ENEMY_SHIELD_RADIUS_MASK) == (15u << 8),
             "and nowhere else");
    want = want_of(false, false, 0u);
    want.radius = 15u;
    ut_check((mp_enemy_shield_pack(&want) & MP_ENEMY_SHIELD_RADIUS_MASK) == 0u,
             "no shield carries no radius");

    want        = want_of(true, true, 2u);
    want.radius = 16u;
    here        = here_of(-1, false, false, 0u);
    plan        = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.create && plan.size && plan.radius == 16u,
             "a shield hung here is sized after the hang, which gives every shield 0.5");

    here        = here_of(3, true, true, 4u);
    want.colour = 1u;
    plan        = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.recolour && plan.create && plan.size,
             "and so is one hung again for another colour");

    here        = here_of(3, true, true, 2u);
    here.radius = 4u;
    want        = want_of(true, true, 2u);
    want.radius = 16u;
    plan        = mp_enemy_shield_plan(&want, &here);
    ut_check(plan.size && !plan.show && !plan.create && !plan.release,
             "the same shield at another size: command 5 alone");

    here.radius = 16u;
    ut_check(mp_enemy_shield_agrees(&want, &here), "at the same size it agrees");
    here.radius = 4u;
    want.radius = 0u;
    ut_check(mp_enemy_shield_agrees(&want, &here),
             "and a record that names no size never asks for one");
}

int main(void)
{
    check_the_byte();
    check_the_plan();
    check_the_radius();

    return ut_summary("mp_enemy_shield_rule");
}
