/* The host's description of the movers that are away from home, driven with no engine and no map.
 *
 * Three of these checks are transcriptions of the engine's own integrator and are the ones worth
 * arguing about. Which directions of a door and of a button hold the pose still decides which
 * movers this feature is allowed to write, and a direction read as standing when it is travelling
 * is a mover corrected in the middle of its journey. So the arms are walked one by one, every
 * value the three bit field can hold, rather than the two or three a happy path would visit.
 *
 * The fourth is the length family. Every recogniser on the reliable channel tells its message kind
 * apart by tag and length together, and this note is twelve bytes plus seven a mover, which is a
 * ladder rather than a length. The ladder is walked against the two messages on that channel that
 * carry no tag at all, because those are the only ones a length can be confused with.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_events.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Every shipped mover record has this travel length, in all eleven levels and at every type, so
 * the fraction the wire carries has a denominator that is the same on both machines without any
 * of it travelling. */
#define TEST_LENGTH 29.0f

static void check_the_rest_arms(void)
{
    unsigned dir;
    unsigned type;

    ut_section("which directions of a door hold its pose still");

    /* The door's arm has four. Closed sets the pose to zero and takes the mover off the list;
     * opening drives the pose up; latched pins it at the far end while the dwell runs; closing
     * drives it back down. Two of the four move it. */
    ut_check(mp_world_state_at_rest(2u, 0u), "closed and away is still");
    ut_check(!mp_world_state_at_rest(2u, 1u), "opening is not");
    ut_check(mp_world_state_at_rest(2u, 2u), "latched open is still: the arm pins the pose");
    ut_check(!mp_world_state_at_rest(2u, 3u),
             "and closing is NOT, however much a latched door looks like one: the arm reads the "
             "pose down by a whole substep of travel every tick");
    for (dir = 4u; dir <= MOVER_DIR_MAX; ++dir) {
        ut_checkf(!mp_world_state_at_rest(2u, dir),
                  "a door direction of %u is not an arm the engine has", dir);
    }

    ut_section("and which of a button's six");

    ut_check(mp_world_state_at_rest(6u, 0u), "closed and away is still");
    ut_check(!mp_world_state_at_rest(6u, 1u), "rising is not");
    ut_check(!mp_world_state_at_rest(6u, 2u),
             "held with the first dwell running is refused, because it will move again by itself "
             "and a description of it is out of date before it is applied");
    ut_check(mp_world_state_at_rest(6u, 3u), "held with nothing pending is still");
    ut_check(!mp_world_state_at_rest(6u, 4u), "falling is not");
    ut_check(!mp_world_state_at_rest(6u, 5u), "and neither is shut with the second dwell running");

    ut_section("the three one-shots rest in the door's two places");

    /* An elevator, a lift and a drawbridge are at rest at home or latched at the far end, and
     * nowhere else. Direction one is the run itself, and it is the one state that must never be
     * copied: a pose taken out of the middle of somebody else's run is a jump under the feet of
     * whoever is riding it. */
    for (type = 3u; type <= 5u; ++type) {
        ut_checkf(mp_world_state_at_rest(type, 0u), "type %u is at rest at home", type);
        ut_checkf(mp_world_state_at_rest(type, 2u), "type %u is at rest latched at the end", type);
        ut_checkf(!mp_world_state_at_rest(type, 1u), "type %u running is NOT at rest", type);
        for (dir = 3u; dir <= MOVER_DIR_MAX; ++dir) {
            ut_checkf(!mp_world_state_at_rest(type, dir),
                      "and type %u answers no to direction %u", type, dir);
        }
    }

    ut_section("no other type answers this question at all");

    for (dir = 0u; dir <= MOVER_DIR_MAX; ++dir) {
        ut_checkf(!mp_world_state_at_rest(0u, dir) && !mp_world_state_at_rest(1u, dir) &&
                  !mp_world_state_at_rest(7u, dir),
                  "direction %u of every uncorrected type answers no", dir);
    }
}

static void check_what_is_carried(void)
{
    unsigned type;

    ut_section("the door, the button and the three one-shots are corrected");

    /* The one-shots are applied as well, 204 of the 528 shipped movers. They are sent either way,
     * so applying them costs nothing on the wire. An elevator the joining player finds at the
     * bottom while the host stands at the top is a floor that player cannot reach. */
    for (type = 0u; type < MP_WORLD_MOVER_TYPES; ++type) {
        bool want = type == 2u || type == 6u || (type >= 3u && type <= 5u);

        ut_checkf(mp_world_state_carries(type) == want, "type %u is %s", type,
                  want ? "carried" : "left alone");
        ut_checkf(mp_world_state_is_one_shot(type) == (type >= 3u && type <= 5u),
                  "and type %u is %sa one-shot", type,
                  (type >= 3u && type <= 5u) ? "" : "not ");
    }
    ut_check(!mp_world_state_carries(MP_WORLD_TYPE_ALWAYS_ON),
             "a free runner is left to the phase controller, which shifts its time base rather "
             "than writing a pose it never travelled through");
    ut_check(!mp_world_state_carries(MP_WORLD_TYPE_PUSH_BLOCK),
             "and a push block has no timeline to correct");
}

static void check_away_from_home(void)
{
    unsigned type;

    ut_section("away from the position the level authored");

    /* Every one of the 528 shipped records is authored with direction zero, and the level reset
     * writes zero into that field for all of them, so the pair of the direction and the list
     * membership is the whole test. */
    for (type = 0u; type < MP_WORLD_MOVER_TYPES; ++type) {
        /* The active flag is not evidence for these two. The engine's own level opener sets it on
         * every free running mover and on every armed one-shot, and no arm anywhere clears the
         * free runner's, so read as evidence it would put all 59 free runners of the shipped
         * levels in every note from the first frame, up to 131 bytes a note in BIGCITY, to report
         * that a fan was turning on both machines. */
        bool ever    = type != MP_WORLD_TYPE_PUSH_BLOCK && type != 0u;
        bool by_flag = ever && type != 5u;

        ut_checkf(mp_world_state_disturbed(type, 0u, 0u) == false,
                  "type %u with neither set is at home", type);
        ut_checkf(mp_world_state_disturbed(type, 1u, 0u) == by_flag,
                  "type %u on the ticking list is away from home only if the flag means something "
                  "for it", type);
        ut_checkf(mp_world_state_disturbed(type, 0u, 2u) == ever,
                  "type %u with a direction is away from home unless it never travels", type);
        ut_checkf(mp_world_state_disturbed(type, 1u, 2u) == ever,
                  "and type %u with both agrees with the direction alone", type);
    }

    ut_check(mp_world_state_disturbed(2u, 0u, 2u),
             "a door that has latched open and left the ticking list is away from home, which is "
             "the case list membership alone can never describe");
    ut_check(!mp_world_state_disturbed(MP_WORLD_TYPE_PUSH_BLOCK, 1u, 3u),
             "a push block is refused however it looks: its direction field is a pointer store "
             "and its pose is always zero");
    ut_check(!mp_world_state_disturbed(MP_WORLD_MOVER_TYPES, 1u, 1u),
             "and a type the engine has no arm for is refused rather than described");
}

static void check_agreement(void)
{
    mp_world_entry_t wire;
    mp_world_entry_t local;

    ut_section("two descriptions of the same mover");

    memset(&wire, 0, sizeof wire);
    wire.id     = 7u;
    wire.type   = 2u;
    wire.dir    = 2u;
    wire.active = 1u;
    wire.pose   = 1.0f;
    wire.dwell  = 0.25f;
    local = wire;

    ut_check(mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "identical descriptions agree");

    local.pose = 1.0f - 0.0002f;
    ut_check(mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "and so do two that differ by less than MP_WORLD_STATE_SAME");

    local.pose = 1.0f - 0.01f;
    ut_check(!mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "a hundredth of the travel is a disagreement");

    local = wire;
    local.dir = 1u;
    ut_check(!mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "a different direction is one too");

    local = wire;
    local.active = 0u;
    ut_check(!mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "and so is a mover one side still ticks and the other does not");

    local = wire;
    local.dwell = 9.0f;
    ut_check(mp_world_state_agrees_within(&wire, &local, MP_WORLD_STATE_SAME),
             "the dwell is carried but is not part of the agreement: it is a countdown inside a "
             "phase, not the phase");
}

static void check_the_round_trip(void)
{
    mp_world_state_note_t note;
    mp_world_state_note_t got;
    uint8_t               buffer[MP_WORLD_STATE_MAX_BYTES];
    uint8_t               spoiled[MP_WORLD_STATE_MAX_BYTES];
    size_t                bytes;
    unsigned              i;

    ut_section("a note goes out and comes back");

    mp_world_state_note_init(&note, 0x01020304u, 57u, 3u);
    ut_check(mp_world_state_note_add(&note, 12u, 2u, 2u, 1u, 29.0f, TEST_LENGTH, 0.5f) ==
             MP_WORLD_ADD_OK, "a latched door goes in");
    ut_check(mp_world_state_note_add(&note, 40u, 6u, 3u, 1u, 29.0f, TEST_LENGTH, 0.0f) ==
             MP_WORLD_ADD_OK, "a held button goes in");
    ut_check(mp_world_state_note_add(&note, 3u, 0u, 0u, 1u, 14.5f, TEST_LENGTH, 0.0f) ==
             MP_WORLD_ADD_OK, "and so does a free runner, which travels but is not corrected");

    bytes = mp_world_state_encode(&note, buffer, sizeof buffer);
    ut_checkf(bytes == mp_world_state_bytes(3u), "three movers encode to %u bytes",
              (unsigned)bytes);
    ut_check(mp_world_state_is_note(buffer, bytes), "the recogniser knows its own");
    ut_check(mp_world_state_decode(buffer, bytes, &got), "and it decodes");
    ut_check(got.tick == 0x01020304u && got.level == 57u && got.generation == 3u && got.count == 3u,
             "with the tick, the level, the generation and the count back");
    ut_check(got.entry[0].id == 12u && got.entry[0].type == 2u && got.entry[0].dir == 2u &&
             got.entry[0].active == 1u,
             "and the first entry's id, type, direction and list membership back");
    ut_check(got.entry[0].pose > 0.999f && got.entry[1].pose > 0.999f,
             "a mover at the far end of its travel comes back at the far end");
    ut_check(got.entry[2].pose > 0.49f && got.entry[2].pose < 0.51f,
             "and one halfway comes back halfway");
    ut_check(got.entry[0].dwell > 0.499f && got.entry[0].dwell < 0.501f,
             "the dwell survives its trip through milliseconds");

    ut_section("a note that has been damaged is refused rather than half read");

    memcpy(spoiled, buffer, bytes);
    spoiled[11] = 9u;
    ut_check(!mp_world_state_is_note(spoiled, bytes) &&
             !mp_world_state_decode(spoiled, bytes, &got),
             "a count that disagrees with the length is refused: the two say the same thing twice "
             "so that a truncated note cannot read as a shorter valid one");

    memcpy(spoiled, buffer, bytes);
    spoiled[0] = MP_WORLD_DIGEST_TAG;
    ut_check(!mp_world_state_is_note(spoiled, bytes), "and so is another module's tag");

    ut_check(!mp_world_state_is_note(buffer, bytes - 1u),
             "a note one byte short is not a note of one fewer mover");
    ut_check(!mp_world_state_is_note(buffer, MP_WORLD_STATE_HEADER_BYTES - 1u),
             "and neither is anything shorter than the header");

    ut_section("what the encoder will not describe");

    ut_check(mp_world_state_note_add(&note, 5u, MP_WORLD_MOVER_TYPES, 0u, 1u, 1.0f, TEST_LENGTH,
                                     0.0f) == MP_WORLD_ADD_REFUSED,
             "a type the engine has no arm for");
    ut_check(mp_world_state_note_add(&note, 5u, 2u, MOVER_DIR_MAX + 1u, 1u, 1.0f, TEST_LENGTH,
                                     0.0f) == MP_WORLD_ADD_REFUSED,
             "a direction wider than the three bits it travels in");
    ut_check(mp_world_state_note_add(&note, 0x10000u, 2u, 0u, 1u, 1.0f, TEST_LENGTH, 0.0f) ==
             MP_WORLD_ADD_REFUSED, "an id wider than the two bytes it travels in");
    ut_check(mp_world_state_note_add(&note, 5u, 2u, 0u, 1u, 1.0f, 0.0f, 0.0f) ==
             MP_WORLD_ADD_REFUSED,
             "and a mover with no travel, which is the one the engine's own wrap loop hangs on");

    ut_section("a full note stops adding and says which reason it stopped for");

    mp_world_state_note_init(&note, 1u, 57u, 0u);
    for (i = 0; i < MP_WORLD_STATE_MAX_ENTRIES; ++i) {
        ut_check(mp_world_state_note_add(&note, i, 2u, 2u, 1u, 1.0f, TEST_LENGTH, 0.0f) ==
                 MP_WORLD_ADD_OK, "up to the cap every mover goes in");
    }
    ut_check(mp_world_state_note_add(&note, i, 2u, 2u, 1u, 1.0f, TEST_LENGTH, 0.0f) ==
             MP_WORLD_ADD_FULL, "and the one past it is a full note, not a refusal");
    bytes = mp_world_state_encode(&note, buffer, sizeof buffer);
    ut_checkf(bytes == sizeof buffer, "a full note is %u bytes", (unsigned)bytes);
    ut_check(mp_world_state_is_note(buffer, bytes) && mp_world_state_decode(buffer, bytes, &got) &&
             got.count == MP_WORLD_STATE_MAX_ENTRIES, "and it still round trips");
}

/* The cap is a level, not a habit: the largest shipped level holds a hundred movers, six of them
 * push blocks, so ninety four is the whole of it and nothing this feature can meet is larger. */
static void check_the_cap_covers_the_largest_level(void)
{
    ut_section("the cap holds the largest shipped level whole");

    ut_check(MP_WORLD_STATE_MAX_ENTRIES >= 94u,
             "ninety four movers is the largest level less its push blocks");
    ut_check(mp_world_state_fits(94u) && mp_world_state_fits(MP_WORLD_STATE_MAX_ENTRIES),
             "and a note that size is one the encoder will build");
    ut_check(!mp_world_state_fits(MP_WORLD_STATE_MAX_ENTRIES + 1u), "one past it is not");
    ut_check(MP_WORLD_STATE_MAX_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
             "the largest note fits in one reliable message, which the channel refuses outright "
             "otherwise");
}

/* The length family against the two messages on the reliable channel that carry no tag.
 *
 * Everything else on that channel is told apart by tag and length together, and this note's tag is
 * its own, so a shared length with a tagged message is harmless. The world slot note and the
 * acknowledgement have no tag at all, and a message whose length is one of theirs would be read as
 * one of them before its first byte was ever looked at. */
static void check_the_length_family(void)
{
    unsigned entries;

    ut_section("no note on the ladder can be read as a message that carries no tag");

    for (entries = 0; entries <= MP_WORLD_STATE_MAX_ENTRIES; ++entries) {
        unsigned note   = (unsigned)mp_world_state_bytes(entries);
        unsigned digest = MP_WORLD_DIGEST_HEADER_BYTES + entries * MP_WORLD_DIGEST_ENTRY_BYTES;

        ut_checkf(note != MP_EVENT_FOREIGN_SLOT_NOTE_BYTES &&
                  note != MP_EVENT_FOREIGN_ACK_BYTES,
                  "a note of %u mover(s) is %u bytes, neither the slot note nor the "
                  "acknowledgement", entries, note);
        ut_checkf(digest != MP_EVENT_FOREIGN_SLOT_NOTE_BYTES &&
                  digest != MP_EVENT_FOREIGN_ACK_BYTES,
                  "and the digest of %u mover(s), now %u bytes, is neither", entries, digest);
    }

    ut_section("the two families are the same shape and have to be told apart by tag");

    ut_check(MP_WORLD_STATE_ENTRY_BYTES == MP_WORLD_DIGEST_ENTRY_BYTES,
             "the digest and the note carry the same entry, which is why the packing is written "
             "once");
    ut_check(MP_WORLD_STATE_TAG != MP_WORLD_DIGEST_TAG,
             "so the tag is the whole of what separates a note from a digest of the same length");
    ut_check(MP_WORLD_STATE_TAG != MP_EVENT_SHOT && MP_WORLD_STATE_TAG != MP_EVENT_PUSH &&
             MP_WORLD_STATE_TAG != MP_EVENT_SABRE && MP_WORLD_STATE_TAG != MP_EVENT_WEAPON &&
             MP_WORLD_STATE_TAG != MP_EVENT_MOVER && MP_WORLD_STATE_TAG != MP_EVENT_SPAWN &&
             MP_WORLD_STATE_TAG != MP_EVENT_DESPAWN && MP_WORLD_STATE_TAG != MP_EVENT_SKIN &&
             MP_WORLD_STATE_TAG != MP_EVENT_PICKUP && MP_WORLD_STATE_TAG != MP_EVENT_USE &&
             MP_WORLD_STATE_TAG != MP_EVENT_HIT && MP_WORLD_STATE_TAG != MP_EVENT_PLAYER_HIT,
             "and it is none of the event tags either");
}

static void check_the_generation(void)
{
    ut_section("a note from before a level change is refused after one");

    ut_check(mp_world_state_generation_current(0u, 0u),
             "a side that has been told of nothing takes the first note it is given");
    ut_check(mp_world_state_generation_current(4u, 3u), "a newer transition is taken");
    ut_check(mp_world_state_generation_current(3u, 3u), "the same one is taken again");
    ut_check(!mp_world_state_generation_current(2u, 3u),
             "and an older one is refused: its mover ids name a map that is gone");

    /* The count is a sequence and not a number the two machines share. A peer that joined late has
     * seen fewer transitions, permanently, so a test for equality against this side's own count
     * would refuse every note for the life of the session. */
    ut_check(mp_world_state_generation_current(9u, 0u),
             "a host far ahead of a client that has just joined is taken, not refused");
}

/* The pulled back pose, and the threshold that has to live with it.
 *
 * Every shipped mover travels exactly 29.0 units, and the fastest is 120 a second, so a substep
 * of the fastest is 3.75 of 29, almost thirteen per cent of the whole travel. Those are the
 * numbers the two rules below have to be right for. */
static void check_the_one_shot_arms(void)
{
    const float length = 29.0f;   /* every one of the 528 shipped records */
    float       pose;
    float       tol;

    ut_section("a one-shot is put SHORT of its end, never on it");

    /* The floor: one substep of this mover's own travel, whatever the settling step is. At the
     * start of a level the settling step is a hundred thousandth of a second and four times it
     * would be a five thousandth of a unit, far too small to reach the integrator's tail. */
    pose = mp_world_state_one_shot_pose(1.0f, length, 120.0f, 1e-05f);
    ut_check(pose < length, "the end pose does not land on the end");
    ut_check(pose > length - 4.0f && pose < length - 3.0f,
             "it lands one substep of a 120 unit mover short of it, which is 3.75");

    /* And the floor is what actually holds, for every level time this game reaches. The engine's
     * settling step grows with the level clock, but four times it only overtakes one substep past
     * 1/128 of a second, which is two hours and ten minutes of level time. At an hour the settle
     * term is 1.7 units against the floor's 3.75, so the floor is still the answer, which is the
     * point of having one. */
    ut_check(mp_world_state_one_shot_pose(1.0f, length, 120.0f, 3.6e-03f) == pose,
             "at an hour of level time the substep floor is STILL the margin");
    ut_check(mp_world_state_one_shot_pose(1.0f, length, 120.0f, 2.0e-02f) < pose,
             "and past two hours of it the settling step finally takes over, as it must");

    ut_section("and everywhere else it is left where the host has it");

    pose = mp_world_state_one_shot_pose(0.5f, length, 120.0f, 1e-05f);
    ut_check(pose > 14.0f && pose < 15.0f, "half way is half way, untouched");
    pose = mp_world_state_one_shot_pose(0.0f, length, 120.0f, 1e-05f);
    ut_check(pose == 0.0f, "and home is home");

    ut_section("the arithmetic refuses what it cannot divide");

    ut_check(mp_world_state_one_shot_pose(1.0f, 0.0f, 120.0f, 1e-05f) == 0.0f,
             "a mover with no travel gets no guard rather than a division");
    pose = mp_world_state_one_shot_pose(1.0f, length, 0.0f, 1e-05f);
    ut_check(pose == length, "and one with no speed is left alone: there is nothing to run it");
    pose = mp_world_state_one_shot_pose(1.0f, 1.0f, 120.0f, 1e-05f);
    ut_check(pose >= 0.0f, "a mover slower than its own travel is clamped, never sent behind home");

    ut_section("the threshold a one-shot earns is wide enough for the guard it was given");

    /* The pair that matters, and it is checked at every settling step rather than at one: the
     * threshold has to cover the margin the placement leaves, or a mover this module has just
     * placed disagrees with the host on the next note and is placed again, once a second, for
     * the rest of the level.
     *
     * The steps below span the two regimes. Up to about two hours of level time the substep floor
     * is the wider of the two terms; past that the engine's own settling step overtakes it, and
     * an earlier version of this pair covered only the first case. */
    {
        static const float STEPS[] = { 1e-05f, 1e-04f, 3.6e-03f, 1.0e-02f, 4.0e-02f };
        static const float SPEEDS[] = { 1.0f, 12.0f, 60.0f, 120.0f };
        size_t             step;
        size_t             which;

        for (step = 0; step < sizeof STEPS / sizeof STEPS[0]; ++step) {
            for (which = 0; which < sizeof SPEEDS / sizeof SPEEDS[0]; ++which) {
                tol  = mp_world_state_tolerance(3u, length, SPEEDS[which], STEPS[step]);
                pose = mp_world_state_one_shot_pose(1.0f, length, SPEEDS[which], STEPS[step]);
                ut_checkf(tol > (length - pose) / length,
                          "the threshold covers the margin at speed %d and settling step %d/1e6",
                          (int)SPEEDS[which], (int)(STEPS[step] * 1e6f));
            }
        }
    }

    tol = mp_world_state_tolerance(3u, length, 120.0f, 1e-05f);
    ut_check(tol > MP_WORLD_STATE_SAME, "and it is wider than a door's");
    ut_check(mp_world_state_tolerance(2u, length, 120.0f, 1e-05f) == MP_WORLD_STATE_SAME,
             "while a door keeps the door's");
    ut_check(mp_world_state_tolerance(3u, 0.0f, 120.0f, 1e-05f) == MP_WORLD_STATE_SAME,
             "and a record that cannot be divided falls back to it rather than to infinity");
}

/* The digest and the state note ask different questions and need different selections.
 * The note says what a joiner has to be put right about, so a mover nobody has touched is not in
 * it; the digest is a measurement, and the one type that drifts with nobody touching it is exactly
 * the one the note leaves out. While the digest used the note's selection, the phase controller for
 * those movers had no input at all and every client log said `0 debt(s) taken`. */
static void check_what_a_digest_carries(void)
{
    ut_section("a free runner is in no state note and in every digest");

    ut_check(!mp_world_state_disturbed(0u, 1u, 0u),
             "the note leaves it out: nobody started it, and it is turning on both machines");
    ut_check(mp_world_state_in_digest(0u, 1u, 0u),
             "the digest carries it: it is the one type that drifts without being touched");

    ut_section("and for every other type the two selections agree");
    ut_check(mp_world_state_in_digest(2u, 1u, 0u) == mp_world_state_disturbed(2u, 1u, 0u),
             "a door that has been opened");
    ut_check(mp_world_state_in_digest(2u, 0u, 0u) == mp_world_state_disturbed(2u, 0u, 0u),
             "a door nobody has touched");
    ut_check(mp_world_state_in_digest(6u, 1u, 3u) == mp_world_state_disturbed(6u, 1u, 3u),
             "a button that is held down");
    ut_check(mp_world_state_in_digest(5u, 1u, 0u) == mp_world_state_disturbed(5u, 1u, 0u),
             "a one shot that has not fired");
    ut_check(!mp_world_state_in_digest(7u, 1u, 1u),
             "and a push block is in neither: it has no travel to be a fraction of");
}

/* A push block that sank is a one-shot of kind 4, opened by the sink and running on its own
 * track, and from then on it is the map note's and the digest's like any other: that is what
 * lets the two sides compare it and what makes the host's type disagreements go away. One that
 * has not sunk stays the push block note's alone. */
static void check_a_sunk_push_block(void)
{
    ut_section("a push block that sank is a one-shot like any other");
    ut_check(mp_world_state_disturbed(4u, 1u, 1u) && mp_world_state_in_digest(4u, 1u, 1u),
             "sinking, opened and running: in the note and in the digest");
    ut_check(mp_world_state_disturbed(4u, 1u, 2u) && mp_world_state_in_digest(4u, 1u, 2u),
             "sunk and latched: in both still");
    ut_check(!mp_world_state_disturbed(7u, 0u, 0u) && !mp_world_state_in_digest(7u, 0u, 0u),
             "one that has not sunk is in neither");
}

int main(void)
{
    check_the_rest_arms();
    check_what_is_carried();
    check_away_from_home();
    check_agreement();
    check_the_round_trip();
    check_the_cap_covers_the_largest_level();
    check_the_length_family();
    check_the_generation();

    check_the_one_shot_arms();
    check_what_a_digest_carries();
    check_a_sunk_push_block();
    return ut_summary("mp_world_state");
}
