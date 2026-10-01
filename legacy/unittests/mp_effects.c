/* The impact effects a replicated blade owes its own body, driven over their edges with no game.
 *
 * Every decision here is one the field answers slowly and ambiguously. A module that answered the
 * victim's message would spark on the wrong body; one that ignored its own cooldown would fire an
 * effect on every substep a swing overlaps its target, which is a stream of voices and a wall of
 * sparks; one that answered a code it cannot name would put a flash wherever the point argument
 * happened to be. The numbers are pinned rather than the prose.
 */
#include "unittest.h"

#include "mp_effects.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Longer than the cooldown, so a case that is not about the cooldown is never about it. */
#define COLD (MP_EFFECTS_COOLDOWN_SUBSTEPS + 1u)

static mp_effects_verdict_t decide(uint32_t code, uint32_t b, bool armed, uint32_t since,
                                   int32_t *group)
{
    int32_t sink = -1;

    if (group == NULL) {
        group = &sink;
    }
    *group = -1;
    return mp_effects_decide(code, b, armed, since, group);
}

static void check_the_b_flag(void)
{
    ut_section("the b flag is the whole fork");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 0u, true, COLD, NULL) == MP_EFFECTS_NOTHING,
             "a contact with b clear is the victim's message and never plays an effect");
    ut_check(decide(2u, 0u, true, COLD, NULL) == MP_EFFECTS_NOTHING,
             "and that holds for a body contact too, which is what two bodies standing next to "
             "each other produce by the hundred");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, COLD, NULL) == MP_EFFECTS_DUE,
             "the same contact with b set is the actor's message and is due");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 0x7FFFFFFFu, true, COLD, NULL) == MP_EFFECTS_DUE,
             "any non-zero b is the actor's message; the engine tests it against zero");
}

static void check_the_armed_gate(void)
{
    ut_section("the blade has to be armed");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, false, COLD, NULL) == MP_EFFECTS_NOTHING,
             "a body whose contact sphere is off plays nothing: this is what stands in for the "
             "mode descriptor test a puppet cannot pass");
    ut_check(decide(2u, 1u, false, COLD, NULL) == MP_EFFECTS_NOTHING,
             "and it outranks the contact code");
}

static void check_the_voice_groups(void)
{
    int32_t group = -1;

    ut_section("which voice group a contact code names");
    ut_check(decide(2u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_BODY,
             "code 2, the enemy class, is a body contact and takes the body voice group");
    ut_check(decide(3u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_BODY,
             "so does code 3, the other code the engine's own arm names");
    ut_check(decide(1u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "code 1, player 0's own class, is what the pair pass sends the swinger when the "
             "victim is the other player: lit, and silent, exactly as a local swing is");
    ut_check(decide(5u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "class 5, the second player's body, is the same case seen from the other side");

    ut_check(decide(MP_EFFECTS_CODE_BLADE_PLAIN, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_BLADE,
             "code 0x21, a non-Jedi blade, is a blade contact and takes the blade voice group");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_BLADE,
             "and so does 0x25, the Jedi one");
    ut_check(MP_EFFECTS_VOICE_BODY != MP_EFFECTS_VOICE_BLADE,
             "the two groups are different sounds, or the split would be decoration");
}

static void check_codes_without_a_voice(void)
{
    int32_t group = 0;

    ut_section("a code that names no group is played without a voice, not swallowed");
    ut_check(decide(0u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "code 0 lights the hit and says nothing");
    ut_check(decide(0x0Au, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "a pickup code the same");
    ut_check(decide(0x1Fu, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "and the impulse code");
    ut_check(decide(0x24u, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "a damage code between the two blade codes is not a blade code and takes no voice");
    ut_check(decide(0xFFu, 1u, true, COLD, &group) == MP_EFFECTS_DUE &&
                 group == MP_EFFECTS_VOICE_NONE,
             "nor is a byte's worth");
    ut_check(MP_EFFECTS_VOICE_NONE != MP_EFFECTS_VOICE_BODY &&
                 MP_EFFECTS_VOICE_NONE != MP_EFFECTS_VOICE_BLADE,
             "and the no-voice value is neither of the two groups, or one would be played by "
             "accident");
}

static void check_the_cooldown(void)
{
    uint32_t substep;

    ut_section("the module's own cooldown");
    ut_check(MP_EFFECTS_COOLDOWN_SUBSTEPS == 10u,
             "the lock is ten substeps, which is the engine's 0.3 s at 32 substeps a second");

    for (substep = 0; substep < MP_EFFECTS_COOLDOWN_SUBSTEPS; ++substep) {
        ut_checkf(decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, substep, NULL) ==
                      MP_EFFECTS_LOCKED,
                  "%u substep(s) after the last effect the next one is still locked",
                  (unsigned)substep);
    }
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, MP_EFFECTS_COOLDOWN_SUBSTEPS, NULL) ==
                 MP_EFFECTS_DUE,
             "the substep the span is reached on is the first one that plays again");
    ut_check(decide(2u, 1u, true, 0u, NULL) == MP_EFFECTS_LOCKED,
             "the lock is not per voice group: a body contact inside it is swallowed as well");

    ut_section("what the lock does not outrank");
    ut_check(decide(0x1Fu, 1u, true, 0u, NULL) == MP_EFFECTS_LOCKED,
             "the lock is not per code either: a code with no voice group is swallowed by it too");
    ut_check(decide(MP_EFFECTS_CODE_BLADE_JEDI, 0u, true, 0u, NULL) == MP_EFFECTS_NOTHING,
             "and a victim's message inside the lock is still just a victim's message");
}

static void check_the_group_is_only_written_when_due(void)
{
    int32_t group = 99;

    ut_section("the voice group out parameter");
    group = 99;
    (void)mp_effects_decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, 0u, &group);
    ut_check(group == 99, "a locked contact leaves the caller's group untouched");
    group = 99;
    (void)mp_effects_decide(MP_EFFECTS_CODE_BLADE_JEDI, 0u, true, COLD, &group);
    ut_check(group == 99, "and so does a victim's message, which is not this module's at all");
    group = 99;
    (void)mp_effects_decide(0x1Fu, 1u, true, COLD, &group);
    ut_check(group == MP_EFFECTS_VOICE_NONE,
             "a code with no group is due and says so in the group, because the caller has to "
             "tell it apart from a group it should play");
    group = 99;
    (void)mp_effects_decide(MP_EFFECTS_CODE_BLADE_JEDI, 1u, true, COLD, &group);
    ut_check(group == MP_EFFECTS_VOICE_BLADE, "a due contact with a group writes it");
    ut_check(mp_effects_decide(2u, 1u, true, COLD, NULL) == MP_EFFECTS_DUE,
             "and a caller that does not want it may pass NULL");
}

/* The gate the swept blade runs behind, which is the whole of what a far body in a borrowed model
 * changed about the blade against the level. A field run cannot tell a sweep that did not happen
 * from a sweep that found nothing, so the three answers are pinned here instead. */
static void check_the_sweep_gate(void)
{
    ut_section("whether a far body's blade is swept against the level");
    ut_check(mp_effects_sweep_verdict(false, false) == MP_EFFECTS_SWEEP_RUN,
             "a body in its hero's own model is swept as the engine sweeps the player's");
    ut_check(mp_effects_sweep_verdict(false, true) == MP_EFFECTS_SWEEP_RUN,
             "and the weapon flag says nothing about a body that wears nothing");
    ut_check(mp_effects_sweep_verdict(true, false) == MP_EFFECTS_SWEEP_NONE,
             "a body in a borrowed model with no weapon at that rig's hand is not swept at all: "
             "its contact node was put back to 0 and there is nothing to sweep from");
    ut_check(mp_effects_sweep_verdict(true, true) == MP_EFFECTS_SWEEP_PROBE,
             "one that carries its player's weapon is swept, and only behind a sphere this "
             "module asks for itself, because that answer comes from the DLL that draws the "
             "weapon and can be no for a substep");
    ut_check(MP_EFFECTS_SWEEP_PROBE != MP_EFFECTS_SWEEP_RUN &&
                 MP_EFFECTS_SWEEP_NONE != MP_EFFECTS_SWEEP_RUN,
             "the three are three answers, or the probe and the refusal would be decoration");
}

/* Nothing has resolved the host image in this process, so every site is zero and every cell is
 * unknown. What the module owes the caller there is a counted refusal with nothing dereferenced,
 * and above all no engine call through a null pointer. */
static void check_engine_half_without_a_game(void)
{
    ut_section("a process with no game in it");
    mp_effects_resolve();
    ut_check(mp_effects_contacts() == 0u && mp_effects_played() == 0u,
             "resolving alone counts no contact and plays nothing");

    mp_effects_note_contact(1u, 0u);
    ut_check(mp_effects_contacts() == 0u,
             "a contact offered with no cells resolved is dropped without a count, because a "
             "count per contact would only be noise on a build where nothing resolved");
    ut_check(mp_effects_refused() == 0u,
             "and it is not a refusal either: the cells reported themselves at resolve time");

    mp_effects_run_in_window(1u, 0u, 0u);
    ut_check(mp_effects_played() == 0u && mp_effects_refused() == 0u,
             "a window with nothing pending plays nothing and refuses nothing");

    mp_effects_sweep_blade(1u, 0u, 0u);
    ut_check(mp_effects_sweeps() == 0u && mp_effects_sweeps_worn() == 0u &&
                 mp_effects_sweeps_unprobed() == 0u && mp_effects_sweeps_seeded() == 0u,
             "and a swept blade with no engine under it sweeps nothing and counts nothing");

    mp_effects_reset(1u);
    ut_check(mp_effects_suppressed() == 0u && mp_effects_without_voice() == 0u &&
                 mp_effects_blade_contacts() == 0u,
             "and every counter is still zero after a reset");
}

int main(void)
{
    check_the_b_flag();
    check_the_armed_gate();
    check_the_voice_groups();
    check_codes_without_a_voice();
    check_the_cooldown();
    check_the_group_is_only_written_when_due();
    check_the_sweep_gate();
    check_engine_half_without_a_game();

    return ut_summary("puppet impact effects");
}
