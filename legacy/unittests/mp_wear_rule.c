/* The three rules for a far body that may wear a borrowed model: the rebuild, whose rig the
 * rotations fit, and the size. Each case is one a draft of the rules got wrong. */
#include "unittest.h"

#include "mp_wear_rule.h"

#include "common/model_wear_note.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A float out of its bits, so the test needs no NaN or infinity out of the library's macros. */
static float from_bits(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static mp_wear_answer_t none(void)
{
    mp_wear_answer_t answer = { false, MODEL_WEAR_STATE_NONE, MODEL_WEAR_REASON_NONE, "", 0.0f,
                                false };

    return answer;
}

static mp_wear_answer_t worn_answer(const char *echo, float scale)
{
    mp_wear_answer_t answer = { true, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, echo, scale,
                                false };

    return answer;
}

static mp_wear_answer_t refused(uint8_t reason, const char *echo)
{
    mp_wear_answer_t answer = { true, MODEL_WEAR_STATE_REFUSED, reason, echo, 0.0f, false };

    return answer;
}

static void check_the_rebuild(void)
{
    mp_wear_answer_t answer;
    const uint32_t   settled = MP_WEAR_SETTLE_MS;

    ut_section("a body is built again only on an answer for it and a wish that has settled");

    answer = refused(MODEL_WEAR_REASON_WEARS_OTHER, "anakin.baf");
    ut_check(mp_wear_rule_rebuild(true, &answer, "padme.baf", settled),
             "a body that still wears anakin.baf is rebuilt for padme.baf: the echo names what it "
             "wears, not the model that was refused");
    ut_check(!mp_wear_rule_rebuild(true, &answer, "padme.baf", settled - 1u),
             "but not before the wish has stood a full table period and the answer's way");
    ut_check(!mp_wear_rule_rebuild(true, &answer, "padme.baf", 1000u),
             "and not after the one second the repeated table can lag an event, which is the "
             "flip the wait is for");
    ut_check(!mp_wear_rule_rebuild(true, &answer, "padme.baf", 48u * 1000u / 64u),
             "the wait is time and not substeps: the 48 it once was are 750 ms at the 60fps "
             "cheat's substep of 1/64, and a wish that stood that long has not settled");
    ut_check(!mp_wear_rule_rebuild(true, &answer, "ANAKIN.BAF", settled),
             "an echo that is the wish in another case is the wish");

    answer = none();
    ut_check(!mp_wear_rule_rebuild(true, &answer, "padme.baf", 60000u),
             "a worn body with no answer for it waits, however long: an owed answer that never "
             "comes must not rebuild it every half second");

    answer = worn_answer("anakin.baf", 1.1f);
    ut_check(!mp_wear_rule_rebuild(true, &answer, "anakin.baf", settled),
             "a body that wears what is wished stands");
    ut_check(mp_wear_rule_rebuild(true, &answer, "", settled),
             "and one whose player took the model off, or a deathmatch emptied the wish, is "
             "rebuilt as its hero");

    ut_check(!mp_wear_rule_rebuild(false, &answer, "anakin.baf", settled),
             "a model that is the hero's own, answered WORN and not worn to the engine, stands");
    answer = refused(MODEL_WEAR_REASON_FIT, "");
    ut_check(!mp_wear_rule_rebuild(false, &answer, "anakin.baf", settled),
             "a hero body whose model was refused stays the hero, with no rebuild to try again");

    answer = refused(MODEL_WEAR_REASON_BROKEN, "");
    ut_check(mp_wear_rule_rebuild(false, &answer, "anakin.baf", settled),
             "a body the overlay could neither dress nor undress is rebuilt although the engine "
             "calls it not worn");
    ut_check(mp_wear_rule_rebuild(false, &answer, "anakin.baf", 0u),
             "and at once, with no wait: half its arrays are the model's until a new body stands");
    ut_check(!mp_wear_rule_rebuild(true, NULL, "anakin.baf", settled),
             "and no answer at all is the same as none for this body");
}

/* What a deathmatch asks for. A wearer with no weapon at the borrowed rig's hand strikes nobody
 * on every machine that dressed him, which is a look outside a deathmatch and the game inside
 * one, so the deathmatch is the one mode that takes the model back. */
static void check_the_deathmatch(void)
{
    mp_wear_answer_t answer;

    ut_section("what a deathmatch asks for, and what it takes back");
    answer = none();
    ut_check(!mp_wear_rule_deathmatch_asks(false, &answer),
             "with no word from the overlay about where a borrowed weapon is drawn from, a "
             "deathmatch asks for no model at all");
    ut_check(mp_wear_rule_deathmatch_asks(true, &answer),
             "with that word, and nothing answered yet, it asks");

    answer = worn_answer("anakin.baf", 1.1f);
    ut_check(!mp_wear_rule_deathmatch_asks(true, &answer),
             "a body the overlay dressed and hung no weapon on empties the wish again");
    answer.weapon = true;
    ut_check(mp_wear_rule_deathmatch_asks(true, &answer),
             "and one that carries its player's own weapon keeps it");
    ut_check(!mp_wear_rule_deathmatch_asks(false, &answer),
             "the bit outranks the answer: an overlay that cannot hang a weapon at all is no "
             "overlay for a deathmatch, whatever it managed for one body");

    answer = refused(MODEL_WEAR_REASON_FIT, "");
    ut_check(mp_wear_rule_deathmatch_asks(true, &answer),
             "a refusal is no body wearing anything, so it takes nothing back");
}

static void check_whose_rig(void)
{
    mp_wear_answer_t answer = none();

    ut_section("the rotations fit only a body that shows the sender's rig");

    ut_check(mp_wear_rule_rig_is_senders(false, &answer, ""),
             "a sender in his hero and a body in its hero: one rig");
    ut_check(!mp_wear_rule_rig_is_senders(true, &answer, ""),
             "a sender who took the model off while this body still wears it: two rigs");
    ut_check(!mp_wear_rule_rig_is_senders(false, &answer, "anakin.baf"),
             "a sender in a model and a body in its hero, which is also what a deathmatch leaves: "
             "the wish is empty there, the sender's model is not");

    answer = worn_answer("anakin.baf", 1.1f);
    ut_check(mp_wear_rule_rig_is_senders(true, &answer, "anakin.baf"),
             "a body the overlay dressed in the sender's model");
    ut_check(mp_wear_rule_rig_is_senders(false, &answer, "anakin.baf"),
             "and one whose model is its hero's own, answered WORN without a rebind");
    ut_check(!mp_wear_rule_rig_is_senders(true, &answer, "padme.baf"),
             "but not a body still in the model the sender has just taken off");

    answer = refused(MODEL_WEAR_REASON_WEARS_OTHER, "anakin.baf");
    ut_check(!mp_wear_rule_rig_is_senders(true, &answer, "anakin.baf"),
             "a refusal is no WORN, even when its echo names the sender's model");
    ut_check(!mp_wear_rule_rig_is_senders(true, NULL, "anakin.baf") &&
                 mp_wear_rule_rig_is_senders(false, NULL, NULL),
             "no answer and no sender read as none");
}

static void check_the_size(void)
{
    mp_wear_answer_t answer = none();
    float            size = 0.0f;

    ut_section("one size: the far player's factor times the scale of what the body shows");

    ut_check(mp_wear_rule_scale(0.0f, 1.1f, false, &answer, &size), "a hero body has a size");
    ut_near(size, 1.1, 1e-6, "a factor never asked for is 1.0 times the actor's own scale");
    ut_check(mp_wear_rule_scale(3.0f, 1.1f, false, &answer, &size) && fabs(size - 3.3) < 1e-5,
             "a giant hero of scale 1.1 is 3.3, not 3.0");

    ut_check(!mp_wear_rule_scale(2.0f, 1.1f, true, &answer, &size),
             "a worn body with no WORN answer is not sized: its own scale is unknown, and a "
             "factor times nothing would draw it at nothing");
    answer = refused(MODEL_WEAR_REASON_WEARS_OTHER, "anakin.baf");
    ut_check(!mp_wear_rule_scale(2.0f, 1.1f, true, &answer, &size), "nor with a refusal");
    answer = worn_answer("padme.baf", 0.8f);
    ut_check(mp_wear_rule_scale(2.0f, 1.1f, true, &answer, &size) && fabs(size - 1.6) < 1e-5,
             "a dressed body is its factor times the worn asset's own scale");
    ut_check(mp_wear_rule_scale(2.0f, 1.1f, false, &answer, &size) && fabs(size - 2.2) < 1e-5,
             "and a body answered WORN that the engine calls not worn is its actor's");

    answer = none();
    ut_check(!mp_wear_rule_scale(1.0f, 0.0f, false, &answer, &size) &&
                 !mp_wear_rule_scale(1.0f, -1.0f, false, &answer, &size),
             "an actor scale that did not read is written nowhere");
    ut_check(mp_wear_rule_scale(from_bits(0x7FC00000u), 1.1f, false, &answer, &size) &&
                 fabs(size - 1.1) < 1e-6,
             "a factor that is no number reads as never asked");
    ut_check(!mp_wear_rule_scale(from_bits(0x7F800000u), 1.1f, false, &answer, &size) &&
                 !mp_wear_rule_scale(1.0f, 1.1f, false, &answer, NULL),
             "an endless product and nowhere to put it are refused");
}

/* The two steps of Plr_TickBladeState 0x00449A84, which the module repeats for a body whose mesh
 * this side may not write. The numbers are the shipped substep of 1/32 of a second and the
 * constants at 0x004A86A0 and 0x004A86D0: a blade takes half a second to light and a tenth to
 * withdraw. */
static void check_the_blade_step(void)
{
    const float          dt = 1.0f / 32.0f;
    mp_wear_blade_step_t step;

    ut_section("the blade length of a body whose mesh this side may not write");

    ut_check(mp_wear_rule_blade_step(0.0f, dt, 1u, 1u, &step) && step.grew && !step.shrank &&
                 fabs(step.size - 0.0625) < 1e-6,
             "the sabre out and the blade folded: it grows by twice the substep");
    ut_check(fabs(step.power - 0.0625) < 1e-6 && fabs(step.range - 0.09375) < 1e-6,
             "and the two light fields are the length and one and a half times it");

    ut_check(mp_wear_rule_blade_step(0.95f, dt, 1u, 1u, &step) && step.grew &&
                 fabs(step.size - 1.0) < 1e-6,
             "a step past full is clamped at full");
    ut_check(!mp_wear_rule_blade_step(1.0f, dt, 1u, 1u, &step),
             "a blade already full takes no step at all, so nothing is written");

    ut_check(mp_wear_rule_blade_step(1.0f, dt, 3u, 3u, &step) && !step.grew && step.shrank &&
                 fabs(step.size - 0.6875) < 1e-6,
             "another weapon requested: it shrinks by ten times the substep");
    ut_check(mp_wear_rule_blade_step(0.1f, dt, 3u, 3u, &step) && step.shrank &&
                 step.size == 0.0f && step.power == 0.0f && step.range == 0.0f,
             "a step past nothing is clamped at nothing, and the light goes out with it");
    ut_check(!mp_wear_rule_blade_step(0.0f, dt, 3u, 3u, &step),
             "a blade already withdrawn takes no step");

    ut_check(mp_wear_rule_blade_step(0.5f, dt, 1u, 3u, &step) && step.grew && step.shrank &&
                 fabs(step.size - (0.5 + 2.0 / 32.0 - 10.0 / 32.0)) < 1e-6,
             "a sabre being put away still holds the slot while the change is in flight, so both "
             "steps run in one substep and the second reads what the first left");

    ut_check(!mp_wear_rule_blade_step(from_bits(0x7FC00000u), dt, 1u, 1u, &step) &&
                 !mp_wear_rule_blade_step(0.5f, from_bits(0x7F800000u), 1u, 1u, &step) &&
                 !mp_wear_rule_blade_step(0.5f, -dt, 1u, 1u, &step),
             "a length or a substep that is no number, and a substep running backwards, are "
             "written nowhere");
    ut_check(mp_wear_rule_blade_step(1.5f, dt, 1u, 1u, &step) && fabs(step.size - 1.0) < 1e-6,
             "a length outside the range is stepped and clamped rather than refused, so a block "
             "that holds one comes back inside it");
    ut_check(!mp_wear_rule_blade_step(0.5f, dt, 1u, 1u, NULL), "and nowhere to put it is refused");
}

static void check_the_names(void)
{
    ut_section("one name, whatever its case");
    ut_check(mp_wear_rule_same_name("Anakin.BAF", "anakin.baf"), "case does not tell names apart");
    ut_check(!mp_wear_rule_same_name("anakin.baf", "anakin.ba"), "a shorter name is another");
    ut_check(mp_wear_rule_same_name(NULL, "") && !mp_wear_rule_same_name(NULL, "a"),
             "NULL reads as the empty name");
}

int main(void)
{
    check_the_rebuild();
    check_the_deathmatch();
    check_whose_rig();
    check_the_size();
    check_the_blade_step();
    check_the_names();

    return ut_summary("mp_wear_rule");
}
