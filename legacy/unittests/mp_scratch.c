/* mp_scratch.c: the campaign bank on the wire, and the window inside it that is not the world's.
 *
 * Three of the properties here would be silent if they were wrong, and the third is the one that
 * would cost a player their evening:
 *
 *   a run delta that loses a byte leaves a door locked on one machine and open on the other, and
 *   nothing in either log would say so;
 *
 *   a decode that applies what it can before meeting a bad run leaves half a campaign, which is
 *   worse than a refused message because nothing records that it happened;
 *
 *   and an encode that lets the per-hero window through hands one player the other player's keys
 *   and takes away their own.
 */
#include "unittest.h"

#include "mp_scratch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: three banks of 1250 bytes plus a packet buffer is past the default stack. */
static uint8_t s_live[MP_SCRATCH_BANK_BYTES];
static uint8_t s_mirror[MP_SCRATCH_BANK_BYTES];
static uint8_t s_far[MP_SCRATCH_BANK_BYTES];
static uint8_t s_packet[2048];

static void start_from_agreement(void)
{
    memset(s_live, 0, sizeof s_live);
    memset(s_mirror, 0, sizeof s_mirror);
    memset(s_far, 0, sizeof s_far);
    memset(s_packet, 0, sizeof s_packet);
}

/* One encode, its commit and its decode: what a caller does when the channel took the bytes. The
 * commit is separate on purpose, and a test that always did both would never notice. */
static bool round_trip(size_t budget, size_t *sent, size_t *changed, mp_scratch_cursor_t *cursor)
{
    size_t              bytes = 0;
    mp_scratch_cursor_t after;

    if (!mp_scratch_encode_bank(s_mirror, s_live, cursor, s_packet,
                                budget < sizeof s_packet ? budget : sizeof s_packet, &bytes,
                                &after)) {
        return false;
    }
    mp_scratch_commit_bank(s_mirror, s_live, cursor, &after);
    *cursor = after;
    *sent = bytes;
    return mp_scratch_decode_bank(s_far, s_packet, bytes, changed);
}

static void check_one_bit_of_progress(void)
{
    mp_scratch_cursor_t cursor;
    size_t              sent = 0;
    size_t              changed = 0;

    ut_section("a single changed byte crosses and nothing else does");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[900] = 0x40u;

    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "it encodes and decodes");
    ut_checkf(sent == 2u + MP_SCRATCH_RUN_HEADER_BYTES + 1u,
              "one run of one byte, %u bytes on the wire", (unsigned)sent);
    ut_checkf(changed == 1u, "and one byte of the far bank moved (%u)", (unsigned)changed);
    ut_check(memcmp(s_far, s_live, sizeof s_live) == 0, "the two banks now agree exactly");
    ut_check(cursor.pending == 0u, "and nothing is left pending");
}

static void check_nothing_to_say_is_cheap(void)
{
    mp_scratch_cursor_t cursor;
    size_t              sent = 0;
    size_t              changed = 0;

    ut_section("two banks that agree cost the run count and nothing more");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "it encodes");
    ut_checkf(sent == 2u, "%u bytes, which is the count alone", (unsigned)sent);
    ut_check(changed == 0u, "and the far bank did not move");
}

static void check_the_per_hero_window_never_travels(void)
{
    mp_scratch_cursor_t cursor;
    size_t              sent = 0;
    size_t              changed = 0;
    uint32_t            i;

    ut_section("the per-hero window is not the world's and does not cross");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    /* Every byte of the window differs, and one world byte on either side of it. */
    for (i = MP_SCRATCH_HERO_FIRST; i < MP_SCRATCH_HERO_FIRST + MP_SCRATCH_HERO_BYTES; ++i) {
        s_live[i] = 0xFFu;
    }
    s_live[MP_SCRATCH_HERO_FIRST - 1u] = 0x11u;
    s_live[MP_SCRATCH_HERO_FIRST + MP_SCRATCH_HERO_BYTES] = 0x22u;

    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "it encodes and decodes");
    ut_checkf(changed == 2u, "the two world bytes crossed (%u)", (unsigned)changed);
    for (i = MP_SCRATCH_HERO_FIRST; i < MP_SCRATCH_HERO_FIRST + MP_SCRATCH_HERO_BYTES; ++i) {
        ut_checkf(s_far[i] == 0u, "window byte %u is untouched", (unsigned)i);
        ut_checkf(s_mirror[i] == 0u,
                  "and the mirror did not pretend to have sent byte %u", (unsigned)i);
    }
    ut_check(s_far[MP_SCRATCH_HERO_FIRST - 1u] == 0x11u &&
             s_far[MP_SCRATCH_HERO_FIRST + MP_SCRATCH_HERO_BYTES] == 0x22u,
             "while the world bytes on either side of it did cross");
    ut_check(cursor.pending == 0u,
             "and the window does not count as pending, or a sender would sweep for ever");
}

static void check_a_message_naming_the_window_is_refused(void)
{
    uint8_t  forged[16];
    size_t   changed = 0;

    ut_section("a message that names the window is refused whole");

    start_from_agreement();
    s_far[MP_SCRATCH_HERO_FIRST] = 0x5Au;   /* a key this player holds */

    /* One run of one byte, aimed straight at the window. No sender of this build writes that, so
     * it is either a different build or a forgery; either way those bytes are a player's keys. */
    forged[0] = 0u;
    forged[1] = 1u;                                  /* one run */
    forged[2] = 0u;
    forged[3] = (uint8_t)MP_SCRATCH_HERO_FIRST;      /* offset */
    forged[4] = 1u;                                  /* length */
    forged[5] = 0x00u;                               /* the value it wants to write */
    ut_check(!mp_scratch_decode_bank(s_far, forged, 6u, &changed),
             "the decode refuses");
    ut_check(s_far[MP_SCRATCH_HERO_FIRST] == 0x5Au,
             "and the player still holds the key it held");
}

static void check_a_bad_run_leaves_the_bank_alone(void)
{
    uint8_t forged[32];
    size_t  changed = 0;

    ut_section("a message whose LAST run is bad changes nothing at all");

    start_from_agreement();

    /* Two runs. The first is perfectly good and would land at offset 100; the second names an
     * offset past the end of the bank. A decoder that wrote as it read would have applied the
     * first one by the time it met the second. */
    forged[0] = 0u;
    forged[1] = 2u;
    forged[2] = 0u;
    forged[3] = 100u;
    forged[4] = 1u;
    forged[5] = 0x77u;
    forged[6] = 0xFFu;
    forged[7] = 0xFFu;
    forged[8] = 1u;
    forged[9] = 0x88u;
    ut_check(!mp_scratch_decode_bank(s_far, forged, 10u, &changed), "the decode refuses");
    ut_check(s_far[100] == 0u,
             "and the good run before it was NOT applied, which is what all or nothing means");
}

static void check_a_whole_bank_converges_inside_a_budget(void)
{
    mp_scratch_cursor_t cursor;
    uint32_t            i;
    unsigned            packets = 0;
    size_t              total = 0;

    ut_section("a bank that differs everywhere is swept across packets and finishes");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    for (i = 0; i < MP_SCRATCH_BANK_BYTES; ++i) {
        s_live[i] = (uint8_t)(i * 7u + 1u);
    }

    /* A budget well under a packet, so the sweep certainly takes several rounds. The cap on the
     * loop is what turns "it eventually finishes" from a hope into a check. */
    while (packets < 200u) {
        size_t sent = 0;
        size_t changed = 0;

        if (!round_trip(300u, &sent, &changed, &cursor)) {
            break;
        }
        total += sent;
        ++packets;
        if (cursor.pending == 0u) {
            break;
        }
    }
    ut_checkf(cursor.pending == 0u, "the sweep converged, %u pending left",
              (unsigned)cursor.pending);
    ut_checkf(packets < 200u, "in %u packets and %u bytes", packets, (unsigned)total);
    {
        /* Byte for byte OUTSIDE the window, and deliberately not inside it. A sweep that made the
         * two banks identical would have carried a player's keys across, which is the one thing
         * this module exists to prevent; so the window differing here is the check, not an
         * exception to it. */
        bool world_agrees = true;
        bool window_kept = true;

        for (i = 0; i < MP_SCRATCH_BANK_BYTES; ++i) {
            if (mp_scratch_is_per_hero(i)) {
                window_kept = window_kept && s_far[i] == 0u;
            } else {
                world_agrees = world_agrees && s_far[i] == s_live[i];
            }
        }
        ut_check(world_agrees, "every world byte of the far bank matches the live one");
        ut_check(window_kept,
                 "and every byte of the per-hero window is untouched, which is the point");
    }

    {
        /* Every world byte crossed exactly once, so the sweep is not re-sending what it has
         * already sent. The window is the difference between the two counts. */
        size_t world_bytes = MP_SCRATCH_BANK_BYTES - MP_SCRATCH_HERO_BYTES;

        ut_checkf(total >= world_bytes,
                  "%u bytes carried at least the %u world bytes", (unsigned)total,
                  (unsigned)world_bytes);
        ut_checkf(total < world_bytes * 2u,
                  "and not twice them (%u), so the headers are not the message", (unsigned)total);
    }
}

static void check_runs_bridge_a_short_gap(void)
{
    mp_scratch_cursor_t cursor;
    size_t              sent = 0;
    size_t              changed = 0;

    ut_section("two changes close together ride one run rather than two");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[400] = 1u;
    s_live[402] = 2u;   /* one unchanged byte between them */

    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "it encodes and decodes");
    ut_checkf(sent == 2u + MP_SCRATCH_RUN_HEADER_BYTES + 3u,
              "one run of three, %u bytes, not two runs of one", (unsigned)sent);
    ut_check(memcmp(s_far, s_live, sizeof s_live) == 0, "and the banks agree");

    ut_section("two changes far apart do not");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[400] = 1u;
    s_live[500] = 2u;
    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "it encodes and decodes");
    ut_checkf(sent == 2u + 2u * (MP_SCRATCH_RUN_HEADER_BYTES + 1u),
              "two runs of one, %u bytes", (unsigned)sent);

    /* The two sides of the break even, which is where a bridge width chosen by taste rather than
     * by arithmetic goes wrong. A gap of three ties in bytes and is taken because it is one header
     * fewer; a gap of four would cost a byte and is not. */
    ut_section("the bridge stops exactly where it stops paying");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[400] = 1u;
    s_live[404] = 2u;      /* three unchanged bytes between them */
    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "a gap of three encodes");
    ut_checkf(sent == 2u + MP_SCRATCH_RUN_HEADER_BYTES + 5u,
              "as one run of five, %u bytes, which ties with two runs and saves a header",
              (unsigned)sent);

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[400] = 1u;
    s_live[405] = 2u;      /* four unchanged bytes between them */
    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "a gap of four encodes");
    ut_checkf(sent == 2u + 2u * (MP_SCRATCH_RUN_HEADER_BYTES + 1u),
              "as TWO runs, %u bytes, because one would have cost nine against eight",
              (unsigned)sent);
}

/* The property the split exists for. An encode that is never delivered must leave the mirror where
 * it was, or the sender believes the far side holds bytes it never received and nothing will ever
 * correct it: the difference it would have to notice is exactly the one it forgot. */
static void check_an_undelivered_encode_moves_nothing(void)
{
    mp_scratch_cursor_t cursor;
    mp_scratch_cursor_t after;
    size_t              bytes = 0;
    size_t              changed = 0;

    ut_section("an encode the channel refused leaves the mirror alone");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[700] = 0x99u;

    ut_check(mp_scratch_encode_bank(s_mirror, s_live, &cursor, s_packet, sizeof s_packet,
                                    &bytes, &after), "it encodes");
    ut_check(s_mirror[700] == 0u, "and the mirror has NOT moved, because nothing was delivered");
    ut_checkf(after.pending == 1u,
              "the byte is still counted as pending (%u)", (unsigned)after.pending);

    /* Now the channel takes it, and only now does the mirror move. */
    mp_scratch_commit_bank(s_mirror, s_live, &cursor, &after);
    ut_check(s_mirror[700] == 0x99u, "the commit moves it");
    ut_check(after.pending == 0u, "and nothing is pending any more");

    ut_section("a refused encode is simply encoded again next time");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[700] = 0x99u;
    ut_check(mp_scratch_encode_bank(s_mirror, s_live, &cursor, s_packet, sizeof s_packet,
                                    &bytes, &after), "the first encode is dropped on the floor");
    /* The cursor is NOT advanced by a caller whose send failed, so the next encode starts over. */
    ut_check(round_trip(sizeof s_packet, &bytes, &changed, &cursor),
             "the second encode carries it");
    ut_check(s_far[700] == 0x99u, "and the far bank has it");
}

static void check_no_prefix_is_taken_for_a_message(void)
{
    mp_scratch_cursor_t cursor;
    size_t              sent = 0;
    size_t              changed = 0;
    size_t              cut;
    bool                any = false;

    ut_section("no short read of a bank message is mistaken for one");

    start_from_agreement();
    memset(&cursor, 0, sizeof cursor);
    s_live[10] = 1u;
    s_live[600] = 2u;
    ut_check(round_trip(sizeof s_packet, &sent, &changed, &cursor), "a message exists");

    for (cut = 0; cut < sent; ++cut) {
        size_t ignored = 0;

        if (mp_scratch_decode_bank(s_far, s_packet, cut, &ignored)) {
            any = true;
        }
    }
    ut_checkf(!any, "all %u short reads were refused", (unsigned)sent);
}

static void check_the_blackboard_travels_as_a_duration(void)
{
    int32_t         flag[MP_SCRATCH_AI_SLOTS]     = { 7, -1, 0, 1234 };
    int32_t         previous[MP_SCRATCH_AI_SLOTS] = { 1, 2, 3, 4 };
    float           expiry[MP_SCRATCH_AI_SLOTS]   = { 0.0f, 100.0f, 90.5f, 12.25f };
    uint8_t         buffer[64];
    size_t          bytes;
    mp_scratch_ai_t got;
    size_t          i;

    ut_section("the blackboard's expiry is a duration on the wire, never an absolute time");

    /* The sender's world clock stands at 90; the receiver's at 5000, which is the whole point. */
    bytes = mp_scratch_encode_ai(flag, previous, expiry, 90.0f, buffer, sizeof buffer);
    ut_checkf(bytes == MP_SCRATCH_AI_BYTES, "it encodes to %u bytes", (unsigned)bytes);
    ut_check(mp_scratch_decode_ai(buffer, bytes, &got), "and decodes");

    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        ut_checkf(got.flag[i] == flag[i] && got.previous[i] == previous[i],
                  "slot %u carried its two integers", (unsigned)i);
    }
    ut_check(mp_scratch_expiry_at(got.remaining[0], 5000.0f) == 0.0f,
             "a slot with no timer stays no timer, rather than expiring at the receiver's now");
    ut_checkf(mp_scratch_expiry_at(got.remaining[1], 5000.0f) == 5010.0f,
              "ten seconds left becomes 5010 on the receiver's clock (%f)",
              (double)mp_scratch_expiry_at(got.remaining[1], 5000.0f));
    ut_check(mp_scratch_expiry_at(got.remaining[2], 5000.0f) == 5000.5f,
             "and half a second becomes 5000.5, so the millisecond resolution is real");
    ut_check(mp_scratch_expiry_at(got.remaining[3], 5000.0f) == 0.0f,
             "a timer that had ALREADY run out arrives as none rather than as a past moment");
}

static void check_a_torn_blackboard_is_refused(void)
{
    int32_t         flag[MP_SCRATCH_AI_SLOTS]     = { 1, 1, 1, 1 };
    int32_t         previous[MP_SCRATCH_AI_SLOTS] = { 0, 0, 0, 0 };
    float           expiry[MP_SCRATCH_AI_SLOTS]   = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint8_t         buffer[64];
    size_t          bytes;
    mp_scratch_ai_t got;
    size_t          cut;
    bool            any = false;

    ut_section("a blackboard of the wrong length is refused");

    bytes = mp_scratch_encode_ai(flag, previous, expiry, 0.0f, buffer, sizeof buffer);
    for (cut = 0; cut < bytes; ++cut) {
        if (mp_scratch_decode_ai(buffer, cut, &got)) {
            any = true;
        }
    }
    ut_check(!any, "no prefix of it is taken for one");
    ut_check(!mp_scratch_decode_ai(buffer, bytes + 1u, &got), "and neither is a longer one");
    ut_check(mp_scratch_decode_ai(buffer, bytes, &got), "the whole one is");
}

static void check_a_value_that_is_not_finite(void)
{
    int32_t flag[MP_SCRATCH_AI_SLOTS]     = { 0, 0, 0, 0 };
    int32_t previous[MP_SCRATCH_AI_SLOTS] = { 0, 0, 0, 0 };
    float   expiry[MP_SCRATCH_AI_SLOTS]   = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint8_t buffer[64];
    size_t  bytes;
    mp_scratch_ai_t got;
    float   huge = 3.0e38f;

    ut_section("an expiry the engine should never hold does not become a wrapped duration");

    expiry[0] = huge;   /* far past what a duration in milliseconds can hold */
    bytes = mp_scratch_encode_ai(flag, previous, expiry, 0.0f, buffer, sizeof buffer);
    ut_check(bytes == MP_SCRATCH_AI_BYTES, "it still encodes");
    ut_check(mp_scratch_decode_ai(buffer, bytes, &got), "and decodes");
    ut_check(got.remaining[0] > 0.0f,
             "the clamped duration is still positive rather than having wrapped to nothing");
}

int main(void)
{
    check_one_bit_of_progress();
    check_nothing_to_say_is_cheap();
    check_the_per_hero_window_never_travels();
    check_a_message_naming_the_window_is_refused();
    check_a_bad_run_leaves_the_bank_alone();
    check_a_whole_bank_converges_inside_a_budget();
    check_runs_bridge_a_short_gap();
    check_an_undelivered_encode_moves_nothing();
    check_no_prefix_is_taken_for_a_message();
    check_the_blackboard_travels_as_a_duration();
    check_a_torn_blackboard_is_refused();
    check_a_value_that_is_not_finite();

    ut_section("the commit needs BOTH ends of the range, and aliasing them copies nothing");
    {
        /* This is a caller trap rather than a fault in the function, and it cost a field run.
         * mp_scratch_wire passed its own cursor as both `from` and `to`, which makes the two ends
         * of the range equal. The copy loop then runs zero times, the mirror never advances, the
         * cursor stays where it started for the life of the session, and every message is a full
         * re-send described as a difference.
         *
         * Nothing about that looks wrong from outside: messages still go out and still decode. */
        mp_scratch_cursor_t start;
        mp_scratch_cursor_t after;
        size_t              wrote = 0;

        start_from_agreement();
        s_live[700] ^= 0x55u;
        s_live[701] ^= 0x11u;

        start.at = 0u;
        start.pending = 0u;
        ut_check(mp_scratch_encode_bank(s_mirror, s_live, &start, s_packet, sizeof s_packet,
                                        &wrote, &after),
                 "two changed bytes encode");
        ut_check(after.at > start.at, "and the encode reports a cursor past where it began");

        /* The way it was called. */
        {
            mp_scratch_cursor_t aliased = start;

            mp_scratch_commit_bank(s_mirror, s_live, &aliased, &aliased);
            ut_check(s_mirror[700] != s_live[700],
                     "committing with one cursor for both ends copies NOTHING into the mirror");
            ut_check(aliased.at == start.at, "and leaves the cursor exactly where it was");
        }

        /* The way it must be called. */
        mp_scratch_commit_bank(s_mirror, s_live, &start, &after);
        ut_check(s_mirror[700] == s_live[700] && s_mirror[701] == s_live[701],
                 "committing with the encode's own cursor as the far end carries the bytes over");
    }

    return ut_summary("mp_scratch");
}
