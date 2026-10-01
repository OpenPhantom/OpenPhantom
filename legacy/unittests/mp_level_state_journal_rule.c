/* mp_level_state_journal_rule.c: the level's journal, the host's window and what a client plays.
 *
 * The window is sixteen, the numbers run 1..65535. The whole note goes round at the size one
 * message of the reliable channel carries, because that is the buffer its one caller hands the
 * encoder, and random notes of every part go round at that bound.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_level_state_journal_rule.h"
#include "mp_level_state_rule.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t s_buffer[MP_CHANNEL_MESSAGE_BYTES];

static void note_of(const mp_level_journal_t *journal, mp_level_state_note_t *note)
{
    mp_level_state_note_init(note, 500u, 57u, 1u);
    mp_level_journal_to_note(journal, note);
}

static void check_the_numbers(void)
{
    ut_section("the numbers");
    ut_check(mp_level_journal_next(0u) == 1u && mp_level_journal_next(65534u) == 65535u &&
                 mp_level_journal_next(65535u) == 1u,
             "1 to 65535, and never 0");
    ut_check(mp_level_journal_after(1u, 0u) && !mp_level_journal_after(0u, 5u),
             "every number comes after none, and none after anything");
    ut_check(mp_level_journal_after(1u, 65535u) && !mp_level_journal_after(65535u, 1u),
             "1 comes after 65535 across the wrap");
    ut_check(!mp_level_journal_after(7u, 7u), "a number does not come after itself");
    ut_check(mp_level_journal_between(4u, 7u) == 2u && mp_level_journal_between(65534u, 2u) == 2u &&
                 mp_level_journal_between(0u, 3u) == 2u,
             "and between two of them lie the ones they skip, across the wrap too");
}

static void check_the_window(void)
{
    mp_level_journal_t journal;
    unsigned           i;

    ut_section("the host keeps the last sixteen, numbered");
    memset(&journal, 0, sizeof journal);
    ut_check(mp_level_journal_push(&journal, (uint8_t)MP_LEVEL_JOURNAL_NONE, 0u, 0u, 0u) == 0u &&
                 mp_level_journal_push(&journal, (uint8_t)MP_LEVEL_JOURNAL_KINDS, 0u, 0u, 0u) ==
                     0u &&
                 journal.newest == 0u,
             "a kind the codec does not know is no entry");
    for (i = 0; i < 20u; ++i) {
        (void)mp_level_journal_push(&journal, (uint8_t)MP_LEVEL_JOURNAL_LIGHT, 1u, (uint16_t)i,
                                    0u);
    }
    ut_checkf(journal.count == MP_LEVEL_STATE_JOURNAL_MAX && journal.newest == 20u &&
                  journal.entry[0].sequence == 5u && journal.entry[15].sequence == 20u,
              "twenty pushed, sixteen kept, 5 to 20 (%u..%u)",
              (unsigned)journal.entry[0].sequence, (unsigned)journal.entry[15].sequence);
}

static void check_the_plan(void)
{
    mp_level_journal_t      journal;
    mp_level_state_note_t   note;
    mp_level_journal_plan_t plan;
    unsigned                i;

    memset(&journal, 0, sizeof journal);
    for (i = 0; i < 6u; ++i) {
        (void)mp_level_journal_push(&journal,
                                    i == 2u ? (uint8_t)MP_LEVEL_JOURNAL_CRAWL
                                            : (uint8_t)MP_LEVEL_JOURNAL_EMITTER,
                                    1u, (uint16_t)i, 0u);
    }
    note_of(&journal, &note);

    ut_section("a client plays what comes after the last it played, in order");
    plan = mp_level_journal_plan(&note, true, 3u);
    ut_check(plan.first == 3u && plan.count == 3u && !plan.jump && plan.newest == 6u,
             "a side that played 3 plays 4, 5 and 6 and remembers 6");
    plan = mp_level_journal_plan(&note, true, 6u);
    ut_check(plan.count == 0u && !plan.jump && plan.newest == 6u,
             "and a side that played 6 plays nothing");

    ut_section("a first note of a level shows nothing and starts the count at its end");
    plan = mp_level_journal_plan(&note, false, 0u);
    ut_check(plan.first_snapshot && plan.count == 6u && plan.newest == 6u && !plan.jump,
             "the window is only looked at, for the lines it will not show");

    ut_section("two notes before one substep cost nothing inside the window");
    for (i = 0; i < 4u; ++i) {
        (void)mp_level_journal_push(&journal, (uint8_t)MP_LEVEL_JOURNAL_FOG, 6u, 0u, 0u);
    }
    note_of(&journal, &note);   /* the note that replaced the one with 1..6 */
    plan = mp_level_journal_plan(&note, true, 2u);
    ut_check(plan.first == 2u && plan.count == 8u && !plan.jump,
             "a side at 2 that never saw the note with 1..6 plays 3 to 10 from the next one");

    ut_section("a jump past the window heals from the state and keeps the lines");
    for (i = 0; i < 20u; ++i) {
        (void)mp_level_journal_push(&journal,
                                    i == 18u ? (uint8_t)MP_LEVEL_JOURNAL_CRAWL
                                             : (uint8_t)MP_LEVEL_JOURNAL_LIGHT,
                                    0u, (uint16_t)i, 0u);
    }
    note_of(&journal, &note);   /* 15..30 */
    plan = mp_level_journal_plan(&note, true, 4u);
    ut_checkf(plan.jump && plan.lost == 10u && plan.first == 0u && plan.count == 16u &&
                  plan.newest == 30u,
              "a side at 4 lost 5 to 14 (%u) and looks at 15 to 30", (unsigned)plan.lost);
    ut_check(mp_level_journal_is_moment((uint8_t)MP_LEVEL_JOURNAL_CRAWL) &&
                 !mp_level_journal_is_moment((uint8_t)MP_LEVEL_JOURNAL_LIGHT) &&
                 !mp_level_journal_is_moment((uint8_t)MP_LEVEL_JOURNAL_FOG),
             "of which only the line is played; the lights and the fog come from the state");
    note.journal_count = 0u;
    plan = mp_level_journal_plan(&note, true, 4u);
    ut_check(plan.jump && plan.lost == 26u && plan.count == 0u,
             "newer numbers and an empty window: every one of them was lost");
    note.parts = 0u;
    plan = mp_level_journal_plan(&note, true, 4u);
    ut_check(!plan.jump && plan.count == 0u && plan.newest == 4u,
             "a note without a journal leaves the count where it was");
}

/* One random note that the encoder takes: every part, with values inside what each carries. */
static void random_note(mp_level_state_note_t *note)
{
    size_t i;

    mp_level_state_note_init(note, (uint32_t)rand(), (uint16_t)rand(), (uint32_t)rand() & 0xFFu);
    note->parts = (uint8_t)((unsigned)rand() & 0x7Fu);
    note->emitters = (uint16_t)((unsigned)rand() % (MP_LEVEL_STATE_MAX_EMITTERS + 1u));
    for (i = 0; i < note->emitters; ++i) {
        note->emitter[i] = (uint8_t)((unsigned)rand() & 3u);
    }
    note->lights = (uint16_t)((unsigned)rand() % (MP_LEVEL_STATE_MAX_LIGHTS + 1u));
    for (i = 0; i < note->lights; ++i) {
        mp_level_state_set_light(note, i, (rand() & 1) != 0);
    }
    note->fog.flags = (uint8_t)((unsigned)rand() & MP_LEVEL_FOG_FLAGS);
    note->fog.cur    = (uint32_t)rand() * 7919u;
    note->fog.end    = (uint32_t)rand() * 104729u;
    note->fog.target = (uint32_t)rand();
    note->fog.span   = (uint32_t)rand() << 8;
    note->fog.left   = (note->fog.flags & MP_LEVEL_FOG_RAMP) != 0u ? (uint16_t)rand() : 0u;
    note->fog.colour[0] = (uint8_t)rand();
    note->fog.colour[1] = (uint8_t)rand();
    note->fog.colour[2] = (uint8_t)rand();
    note->loops = (uint8_t)((unsigned)rand() % (MP_LEVEL_STATE_LOOPS_MAX + 1u));
    for (i = 0; i < note->loops; ++i) {
        note->loop[i].key  = (uint16_t)((unsigned)rand() % MP_WIRE_KEY_COUNT);
        note->loop[i].life = (uint8_t)rand();
        note->loop[i].call = (uint16_t)rand();
    }
    note->fog_viewers = (uint8_t)((unsigned)rand() % (MP_LEVEL_STATE_FOG_VIEWERS_MAX + 1u));
    for (i = 0; i < note->fog_viewers; ++i) {
        note->fog_viewer[i].key    = (uint16_t)((unsigned)rand() % MP_WIRE_KEY_COUNT);
        note->fog_viewer[i].life   = (uint8_t)rand();
        note->fog_viewer[i].script = (uint16_t)rand();
        note->fog_viewer[i].flags  = (uint8_t)((unsigned)rand() & 3u);
        note->fog_viewer[i].place[0] = (uint16_t)rand();
        note->fog_viewer[i].place[1] = (uint16_t)rand();
        note->fog_viewer[i].place[2] = (uint16_t)rand();
    }
    note->escort_health = (uint8_t)((unsigned)rand() % (MP_LEVEL_STATE_ESCORT_HEALTH + 1u));
    note->escort_flags  = (uint8_t)((unsigned)rand() & MP_LEVEL_STATE_ESCORT_SHOWN);
    note->journal_newest = (uint16_t)(1u + (unsigned)rand() % 65535u);
    note->journal_count  = (uint8_t)((unsigned)rand() % (MP_LEVEL_STATE_JOURNAL_MAX + 1u));
    for (i = 0; i < note->journal_count; ++i) {
        mp_level_journal_entry_t *e = &note->journal[i];

        e->sequence = (uint16_t)(1u + (unsigned)rand() % 65535u);
        e->kind     = (uint8_t)(1u + (unsigned)rand() % (MP_LEVEL_JOURNAL_KINDS - 1u));
        e->a        = e->kind == MP_LEVEL_JOURNAL_FOG ? (uint8_t)(6 + (rand() & 1))
                                                      : (uint8_t)((unsigned)rand() % 2u);
        e->b        = (uint16_t)rand();
        e->c        = (uint32_t)rand() * 31u;
    }
}

static bool same_bytes_again(const mp_level_state_note_t *note, size_t bytes)
{
    mp_level_state_note_t back;
    static uint8_t        again[MP_CHANNEL_MESSAGE_BYTES];

    return mp_level_state_decode(s_buffer, bytes, &back) &&
           mp_level_state_encode(&back, again, sizeof again) == bytes &&
           memcmp(again, s_buffer, bytes) == 0 && back.parts == note->parts &&
           ((note->parts & MP_LEVEL_STATE_PART_FOG) == 0u ||
            (back.fog.left == note->fog.left && back.fog.cur == note->fog.cur)) &&
           ((note->parts & MP_LEVEL_STATE_PART_JOURNAL) == 0u ||
            back.journal_newest == note->journal_newest);
}

static void check_the_round_trip(void)
{
    mp_level_state_note_t note;
    unsigned              round;
    unsigned              encoded = 0;
    unsigned              faithful = 0;
    size_t                largest = 0;

    ut_section("random notes of every part go round at the channel's own bound");
    srand(32u);
    for (round = 0; round < 4000u; ++round) {
        size_t bytes;

        random_note(&note);
        bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
        if (bytes == 0u) {
            continue;
        }
        ++encoded;
        largest = bytes > largest ? bytes : largest;
        faithful += same_bytes_again(&note, bytes) ? 1u : 0u;
    }
    ut_checkf(encoded == 4000u, "every random note inside the codec's bounds encodes (%u of 4000)",
              encoded);
    ut_checkf(faithful == encoded, "and every one reads back and says itself again (%u of %u)",
              faithful, encoded);
    ut_checkf(largest <= MP_LEVEL_STATE_MAX_BYTES && MP_LEVEL_STATE_MAX_BYTES <=
                                                         MP_CHANNEL_MESSAGE_BYTES,
              "the largest seen is %u of the %u the rule allows and the %u one message carries",
              (unsigned)largest, (unsigned)MP_LEVEL_STATE_MAX_BYTES,
              (unsigned)MP_CHANNEL_MESSAGE_BYTES);
}

int main(void)
{
    check_the_numbers();
    check_the_window();
    check_the_plan();
    check_the_round_trip();

    return ut_summary("mp_level_state_journal_rule");
}
