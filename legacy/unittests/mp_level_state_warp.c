/* mp_level_state_warp.c: a warp of the host's through the level's journal, from the host's word
 * to what a client hears.
 *
 * The two ends are the real module, and between them stand the real journal, the real note and
 * its real codec: the host says a warp, the journal's window goes into a note, the note is
 * encoded and decoded, and a client plays what the journal's plan names. The host's journal is
 * this test's, behind the one call the level's state gives the module for it, so both ends run
 * in one process the way a host and a client run in two.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_enemy_wire.h"
#include "mp_level_state_internal.h"
#include "mp_level_state_journal_rule.h"
#include "mp_level_state_rule.h"
#include "mp_level_state_warp.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Half a step of the quantisation, and a hair for the float. */
#define NEAR_ENOUGH (0.5f / MP_ENEMY_POS_SCALE + 0.0001f)

static struct {
    mp_level_journal_t journal;   /* the host's */
    bool               keeps;     /* whether the host keeps a journal now */
} s_host;

static uint8_t s_buffer[MP_CHANNEL_MESSAGE_BYTES];

/* The level's state, as far as the module asks it. */
uint16_t mp_level_state_journal_note(uint8_t kind, uint8_t a, uint16_t b, uint32_t c)
{
    return s_host.keeps ? mp_level_journal_push(&s_host.journal, kind, a, b, c) : (uint16_t)0u;
}

/* The host's journal as a client receives it: into a note, across the codec. */
static bool across_the_wire(mp_level_state_note_t *arrived)
{
    mp_level_state_note_t note;
    size_t                bytes;

    mp_level_state_note_init(&note, 900u, 57u, 1u);
    mp_level_journal_to_note(&s_host.journal, &note);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    return bytes != 0u && mp_level_state_decode(s_buffer, bytes, arrived);
}

/* What a client's application does with a note's journal, for the warps in it. Returns how many
 * entries it played. */
static unsigned a_client_plays(const mp_level_state_note_t *note, bool known, uint16_t *last)
{
    mp_level_journal_plan_t plan = mp_level_journal_plan(note, known, *last);
    unsigned                played = 0u;
    size_t                  k;

    *last = plan.newest;
    if (plan.first_snapshot) {
        return 0u;
    }
    for (k = 0u; k < plan.count; ++k) {
        const mp_level_journal_entry_t *entry = &note->journal[plan.first + k];

        if (plan.jump && !mp_level_journal_is_moment(entry->kind)) {
            continue;
        }
        if (entry->kind == (uint8_t)MP_LEVEL_JOURNAL_WARP) {
            mp_level_state_warp_play(entry);
            ++played;
        }
    }
    return played;
}

static bool close_to(const float a[3], const float b[3])
{
    return fabsf(a[0] - b[0]) <= NEAR_ENOUGH && fabsf(a[1] - b[1]) <= NEAR_ENOUGH &&
           fabsf(a[2] - b[2]) <= NEAR_ENOUGH;
}

static void check_the_entry(void)
{
    /* The assault's first warp as a field run logged it, and the swamp's negative ground. */
    static const float palace[3] = { 24.50f, 45.50f, 41.00f };
    static const float swamp[3]  = { 240.0f, -2.1f, 1.0f };
    mp_level_journal_entry_t entry;
    float                    back[3] = { 0.0f, 0.0f, 0.0f };
    int32_t                  hero    = 0;
    float                    off[3];

    ut_section("the entry carries the hero and the target");
    memset(&entry, 0, sizeof entry);
    ut_check(mp_level_state_warp_encode(3, palace, &entry.a, &entry.b, &entry.c),
             "a target inside a shipped level is written");
    mp_level_state_warp_decode(&entry, &hero, back);
    ut_checkf(hero == 3 && close_to(back, palace),
              "and read back as hero 3 at the same place, within half a step of the position's "
              "quantisation (%.4f %.4f %.4f)", (double)back[0], (double)back[1], (double)back[2]);
    ut_check(mp_level_state_warp_encode(0, swamp, &entry.a, &entry.b, &entry.c),
             "ground below zero is written too");
    mp_level_state_warp_decode(&entry, &hero, back);
    ut_check(hero == 0 && close_to(back, swamp), "and read back where it was");

    ut_section("a target the quantisation does not carry is not written");
    memcpy(off, palace, sizeof off);
    off[1] = MP_ENEMY_POS_MAX + 1.0f;
    ut_check(!mp_level_state_warp_encode(3, off, &entry.a, &entry.b, &entry.c),
             "past the top of the range");
    off[1] = MP_ENEMY_POS_MIN - 1.0f;
    ut_check(!mp_level_state_warp_encode(3, off, &entry.a, &entry.b, &entry.c),
             "below its bottom");
    off[1] = (float)NAN;
    ut_check(!mp_level_state_warp_encode(3, off, &entry.a, &entry.b, &entry.c),
             "or no number at all: a client is never sent to a place the host is not at");
    ut_check(!mp_level_state_warp_encode(3, NULL, &entry.a, &entry.b, &entry.c),
             "nor is no target");

    ut_section("a hero the byte does not carry is unknown, and the target still travels");
    ut_check(mp_level_state_warp_encode(-1, palace, &entry.a, &entry.b, &entry.c) &&
                 entry.a == MP_LEVEL_WARP_HERO_UNKNOWN,
             "a hero below zero");
    mp_level_state_warp_decode(&entry, &hero, back);
    ut_check(hero == -1 && close_to(back, palace), "comes back as -1 at the target");
    ut_check(mp_level_state_warp_encode(4000, palace, &entry.a, &entry.b, &entry.c) &&
                 entry.a == MP_LEVEL_WARP_HERO_UNKNOWN,
             "and so does one past the byte");
}

static void check_the_way(void)
{
    static const float first[3]  = { 24.50f, 45.50f, 41.00f };
    static const float second[3] = { 92.50f, 143.50f, 41.00f };
    mp_level_state_note_t note;
    float                 at[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t              count = 77u;
    int32_t               hero  = 77;
    uint16_t              last  = 0u;
    uint16_t              number;
    unsigned              i;

    ut_section("with no journal the host says nothing, and a client hears nothing");
    s_host.keeps = false;
    ut_check(mp_level_state_warp_say(3, first) == 0u, "the warp gets no number");
    ut_check(!mp_level_state_warp_heard(&count, at, &hero) && count == 77u && hero == 77,
             "and a client that asks is told there is none, with nothing written");

    ut_section("a client's first note of a level plays no warp");
    s_host.keeps = true;
    number = mp_level_state_warp_say(3, first);
    ut_check(number == 1u && s_host.journal.count == 1u &&
                 s_host.journal.entry[0].kind == (uint8_t)MP_LEVEL_JOURNAL_WARP,
             "the host's warp is number 1 of its journal");
    ut_check(across_the_wire(&note) && a_client_plays(&note, false, &last) == 0u && last == 1u &&
                 !mp_level_state_warp_heard(NULL, NULL, NULL),
             "a client that arrives now takes the number and follows nobody: it arrives beside "
             "the host as he stands");

    ut_section("a warp after that is heard once, with its target");
    number = mp_level_state_warp_say(0, second);
    ut_check(number == 2u && across_the_wire(&note) && a_client_plays(&note, true, &last) == 1u,
             "the next warp is played, and only that one");
    ut_check(mp_level_state_warp_heard(&count, at, &hero) && count == 1u && hero == 0 &&
                 close_to(at, second),
             "the client knows one warp, as hero 0, to the second target");
    ut_check(across_the_wire(&note) && a_client_plays(&note, true, &last) == 0u &&
                 mp_level_state_warp_heard(&count, NULL, NULL) && count == 1u,
             "the same note again, the once a second repeat, plays nothing more");

    ut_section("a warp is a moment: it is still heard after a jump past the window");
    for (i = 0u; i < 20u; ++i) {
        (void)mp_level_journal_push(&s_host.journal, (uint8_t)MP_LEVEL_JOURNAL_LIGHT, 1u,
                                    (uint16_t)i, 0u);
    }
    (void)mp_level_state_warp_say(3, first);
    for (i = 0u; i < 3u; ++i) {
        (void)mp_level_journal_push(&s_host.journal, (uint8_t)MP_LEVEL_JOURNAL_LIGHT, 0u,
                                    (uint16_t)i, 0u);
    }
    ut_check(across_the_wire(&note) && mp_level_journal_plan(&note, true, last).jump,
             "twenty four changes later the client's number has fallen out of the window");
    ut_check(a_client_plays(&note, true, &last) == 1u &&
                 mp_level_state_warp_heard(&count, at, &hero) && count == 2u && hero == 3 &&
                 close_to(at, first),
             "and the warp inside the window is played all the same: no state says where a "
             "script sent the host");

    ut_section("a level or a session that ends forgets the warp and keeps the count");
    mp_level_state_warp_reset();
    ut_check(!mp_level_state_warp_heard(&count, at, &hero),
             "after the reset there is no warp to follow");
    memset(&s_host.journal, 0, sizeof s_host.journal);
    last = 0u;
    (void)mp_level_state_warp_say(0, second);
    ut_check(across_the_wire(&note) && a_client_plays(&note, false, &last) == 0u &&
                 !mp_level_state_warp_heard(NULL, NULL, NULL),
             "the next level's first note plays none, though its journal starts at 1 again");
    (void)mp_level_state_warp_say(3, first);
    ut_check(across_the_wire(&note) && a_client_plays(&note, true, &last) == 1u &&
                 mp_level_state_warp_heard(&count, at, NULL) && count == 3u && close_to(at, first),
             "and its next warp is the third this process heard: the count never goes back, so "
             "a reader that remembers it sees every warp once");
}

int main(void)
{
    check_the_entry();
    check_the_way();

    return ut_summary("mp_level_state_warp");
}
