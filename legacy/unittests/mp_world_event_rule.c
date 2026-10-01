/* mp_world_event_rule.c: the world events' codec and the decisions both ends make.
 *
 * What would be silent if it were wrong: an event that reads back as another kind, a length that
 * walks the reader into the next event, a number that reads as old after the wrap or after a long
 * silence, and a memory too narrow for what a host can have in flight, which would perform the
 * same death blast twice on a client that got two blocks carrying it.
 */
#include "unittest.h"

#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t s_seed = 0x2545F491u;

static uint32_t next_random(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static const uint8_t KNOWN[] = {
    MP_WORLD_EVENT_SCRIPT_EMITTER, MP_WORLD_EVENT_NPC_CLANG,  MP_WORLD_EVENT_LIMB_FLY,
    MP_WORLD_EVENT_SCRIPT_SOUND,   MP_WORLD_EVENT_EXPLODE_AT, MP_WORLD_EVENT_ZAP_ARCS,
};

static mp_world_event_t random_event(void)
{
    mp_world_event_t e;
    size_t           i;

    memset(&e, 0, sizeof e);
    e.sequence  = (uint16_t)(next_random() % 65535u + 1u);
    e.age       = (uint8_t)(next_random() % MP_WORLD_EVENT_WINDOW);
    e.kind      = KNOWN[next_random() % sizeof KNOWN];
    e.at_actor  = (next_random() & 1u) != 0u;
    e.has_place = (next_random() & 1u) != 0u;
    for (i = 0; i < 3u; ++i) {
        e.place[i] = e.has_place ? (uint16_t)next_random() : 0u;
    }
    e.a    = (uint16_t)next_random();
    e.key  = e.at_actor ? (uint16_t)(next_random() % 384u) : 0u;
    e.life = e.at_actor ? (uint8_t)next_random() : 0u;
    e.tail_bytes = (uint8_t)mp_world_event_tail_bytes(e.kind);
    for (i = 0; i < e.tail_bytes; ++i) {
        e.tail[i] = (uint8_t)next_random();
    }
    return e;
}

static bool same_event(const mp_world_event_t *a, const mp_world_event_t *b)
{
    return a->sequence == b->sequence && a->age == b->age && a->kind == b->kind &&
           a->at_actor == b->at_actor && a->has_place == b->has_place &&
           memcmp(a->place, b->place, sizeof a->place) == 0 && a->a == b->a && a->key == b->key &&
           a->life == b->life && a->tail_bytes == b->tail_bytes &&
           memcmp(a->tail, b->tail, a->tail_bytes) == 0;
}

static void check_the_round_trip(void)
{
    uint8_t  buffer[64];
    unsigned trips = 0;
    unsigned sized = 0;
    unsigned whole = 0;
    unsigned cut   = 0;
    unsigned i;

    ut_section("an event comes back exactly as it went, and no prefix of one is an event");
    for (i = 0; i < 4000u; ++i) {
        mp_world_event_t sent = random_event();
        mp_world_event_t got;
        size_t           wrote = 0;
        size_t           read  = 0;
        size_t           n;
        bool             refused = true;

        if (!mp_world_event_put(&sent, buffer, sizeof buffer, &wrote)) {
            continue;
        }
        sized += wrote == mp_world_event_bytes(&sent) && wrote <= MP_WORLD_EVENT_MAX_BYTES;
        if (mp_world_event_get(buffer, wrote, &got, &read) == MP_WORLD_EVENT_READ_OK &&
            read == wrote && same_event(&sent, &got)) {
            ++trips;
        }
        for (n = 0; n < wrote; ++n) {
            refused = refused && mp_world_event_get(buffer, n, &got, &read) ==
                                     MP_WORLD_EVENT_READ_TORN;
        }
        cut += refused ? 1u : 0u;
        ++whole;
    }
    ut_checkf(whole == 4000u, "every random event of a known kind is written (%u)", whole);
    ut_checkf(trips == whole, "and reads back the same, field for field (%u of %u)", trips, whole);
    ut_checkf(sized == whole, "over exactly the bytes it said it takes, at most %u (%u)",
              (unsigned)MP_WORLD_EVENT_MAX_BYTES, sized);
    ut_checkf(cut == whole, "and every shorter read of it is torn (%u)", cut);
}

/* What no honest host sends: random bytes, read as an event, never claim more than they were. */
static void check_random_bytes(void)
{
    uint8_t  buffer[64];
    unsigned inside = 0;
    unsigned taken  = 0;
    unsigned i;

    ut_section("random bytes read as an event never claim more bytes than they were");
    for (i = 0; i < 20000u; ++i) {
        mp_world_event_t      got;
        size_t                available = next_random() % sizeof buffer;
        size_t                read      = 0;
        size_t                k;
        mp_world_event_read_t result;

        for (k = 0; k < sizeof buffer; ++k) {
            buffer[k] = (uint8_t)next_random();
        }
        result = mp_world_event_get(buffer, available, &got, &read);
        if (result == MP_WORLD_EVENT_READ_TORN) {
            ++inside;
            continue;
        }
        ++taken;
        inside += read <= available && read >= MP_WORLD_EVENT_FIXED_BYTES ? 1u : 0u;
        if (result == MP_WORLD_EVENT_READ_OK) {
            inside -= (got.sequence == 0u || !mp_world_event_kind_known(got.kind)) ? 1u : 0u;
        }
    }
    ut_checkf(inside == 20000u && taken != 0u,
              "every read stays inside what it was handed, and every event taken has a number "
              "and a kind (%u of 20000, %u taken)", inside, taken);
}

static void check_the_sizes(void)
{
    mp_world_event_t e;

    ut_section("what one event costs");
    memset(&e, 0, sizeof e);
    e.sequence = 7u;
    e.kind     = MP_WORLD_EVENT_NPC_CLANG;
    ut_check(mp_world_event_bytes(&e) == 7u, "an event with no actor and no place is 7 bytes");
    e.at_actor = true;
    ut_check(mp_world_event_bytes(&e) == 10u, "at an actor 10: the key and the life");
    e.has_place = true;
    ut_check(mp_world_event_bytes(&e) == 16u, "and with a place 16: three axes of two bytes");
    e.kind       = MP_WORLD_EVENT_EXPLODE_AT;
    e.tail_bytes = 4u;
    ut_check(mp_world_event_bytes(&e) == MP_WORLD_EVENT_MAX_BYTES,
             "the blast's pitch makes the largest, 20 bytes");
    ut_check(MP_WORLD_EVENT_HEAD_BYTES + MP_WORLD_EVENT_PART_MAX_BYTES == 485u,
             "a full part is 485 bytes with its head: 24 events of the largest");

    ut_section("what the writer refuses rather than writes wrong");
    e.tail_bytes = 0u;
    ut_check(mp_world_event_bytes(&e) == 0u, "a tail that is not its kind's");
    e.tail_bytes = 4u;
    e.age        = MP_WORLD_EVENT_WINDOW;
    ut_check(mp_world_event_bytes(&e) == 0u, "an age past the window");
    e.age      = 0u;
    e.sequence = 0u;
    ut_check(mp_world_event_bytes(&e) == 0u, "the number 0, which no event has");
    e.sequence = 1u;
    e.kind     = MP_WORLD_EVENT_NONE;
    ut_check(mp_world_event_bytes(&e) == 0u, "and a kind that is none");
    e.kind = MP_WORLD_EVENT_KINDS;
    ut_check(mp_world_event_bytes(&e) == 0u, "or one past the table");
}

static void check_a_later_build(void)
{
    uint8_t          buffer[64];
    mp_world_event_t e;
    mp_world_event_t got;
    size_t           wrote = 0;
    size_t           read  = 0;

    ut_section("a later build's kind is stepped over, and its longer tail kept to what is known");
    memset(&e, 0, sizeof e);
    e.sequence = 300u;
    e.kind     = MP_WORLD_EVENT_NPC_CLANG;
    e.at_actor = true;
    e.key      = 12u;
    e.life     = 3u;
    e.a        = 2u;
    ut_check(mp_world_event_put(&e, buffer, sizeof buffer, &wrote), "a clang is written");
    buffer[3] = (uint8_t)((buffer[3] & ~MP_WORLD_EVENT_KIND_BITS) | 0x21u);
    ut_check(mp_world_event_get(buffer, wrote, &got, &read) == MP_WORLD_EVENT_READ_SKIPPED &&
                 read == wrote,
             "a kind this build does not know is skipped by its length, to the byte");

    buffer[3] = (uint8_t)((buffer[3] & ~MP_WORLD_EVENT_KIND_BITS) | MP_WORLD_EVENT_NPC_CLANG);
    buffer[wrote - 1u] = 3u;   /* the length: three bytes a later build appended */
    buffer[wrote]      = 0xAAu;
    buffer[wrote + 1u] = 0xBBu;
    buffer[wrote + 2u] = 0xCCu;
    ut_check(mp_world_event_get(buffer, wrote + 3u, &got, &read) == MP_WORLD_EVENT_READ_OK &&
                 read == wrote + 3u && got.tail_bytes == 0u && got.a == 2u,
             "a known kind with a longer tail is read, and what follows it is stepped over");

    memset(&e, 0, sizeof e);
    e.sequence   = 301u;
    e.kind       = MP_WORLD_EVENT_EXPLODE_AT;
    e.tail_bytes = 4u;
    ut_check(mp_world_event_put(&e, buffer, sizeof buffer, &wrote), "a blast is written");
    buffer[wrote - 5u] = 2u;   /* its length, cut to two */
    ut_check(mp_world_event_get(buffer, wrote - 2u, &got, &read) == MP_WORLD_EVENT_READ_TORN,
             "a known kind with a shorter tail than its table is torn");
}

static void check_the_head(void)
{
    mp_world_event_head_t head = { 24u, 1100u, 2610u };
    mp_world_event_head_t got;
    uint8_t               buffer[MP_WORLD_EVENT_HEAD_BYTES];

    ut_section("the part's head: the count and the music the host claims for the peer");
    mp_world_event_put_head(&head, buffer);
    mp_world_event_get_head(buffer, &got);
    ut_check(got.count == 24u && got.music_state == 1100u && got.music_sequence == 2610u,
             "all three come back, the calls little end first like the rest of the block");
    ut_check(buffer[1] == (uint8_t)(1100u & 0xFFu), "low byte first");
}

static void check_the_numbers(void)
{
    ut_section("the numbers run 1 to 65535 and never 0, and \"after\" survives the wrap");
    ut_check(mp_world_event_sequence_next(0u) == 1u, "the first is 1");
    ut_check(mp_world_event_sequence_next(0xFFFFu) == 1u, "65535 is followed by 1, not 0");
    ut_check(mp_world_event_sequence_distance(5u, 3u) == 2, "5 lies two after 3");
    ut_check(mp_world_event_sequence_distance(3u, 5u) == -2, "3 lies two before 5");
    ut_check(mp_world_event_sequence_distance(2u, 0xFFFEu) > 0, "2 lies after 65534");
}

static void check_the_memory(void)
{
    mp_world_event_memory_t memory;
    uint16_t                s = 0xFF00u;
    uint16_t                first;
    unsigned                i;
    unsigned                news = 0;

    ut_section("a client takes each number once, across every number a host can have in flight");
    mp_world_event_memory_reset(&memory);
    mp_world_event_memory_block(&memory, 100u);
    first = mp_world_event_sequence_next(s);
    for (i = 0; i < MP_WORLD_EVENT_POSTS_PER_SUBSTEP * MP_WORLD_EVENT_WINDOW; ++i) {
        s = mp_world_event_sequence_next(s);
        news += mp_world_event_memory_note(&memory, s) == MP_WORLD_EVENT_FIRST_TIME ? 1u : 0u;
    }
    ut_checkf(news == MP_WORLD_EVENT_POSTS_PER_SUBSTEP * MP_WORLD_EVENT_WINDOW,
              "%u numbers across the wrap past 65535 are all new", news);
    ut_check(mp_world_event_memory_note(&memory, first) == MP_WORLD_EVENT_SEEN_AGAIN,
             "and the oldest of them, riding a later block, is known again rather than taken "
             "twice");
    ut_check(mp_world_event_memory_note(&memory, s) == MP_WORLD_EVENT_SEEN_AGAIN,
             "and so is the newest");
    ut_check(mp_world_event_memory_note(&memory, (uint16_t)(first - 600u)) ==
                 MP_WORLD_EVENT_PAST_MEMORY,
             "a number further back than the memory reaches is past it, not new");

    ut_section("a gap of a whole window with no event starts the memory over, and loses nothing");
    mp_world_event_memory_block(&memory, 100u + MP_WORLD_EVENT_WINDOW - 1u);
    ut_check(mp_world_event_memory_note(&memory, s) == MP_WORLD_EVENT_SEEN_AGAIN,
             "a block inside the window still knows what it saw");
    mp_world_event_memory_block(&memory, 100u + 2u * MP_WORLD_EVENT_WINDOW);
    ut_check(mp_world_event_memory_note(&memory, (uint16_t)(s - 20000u)) ==
                 MP_WORLD_EVENT_FIRST_TIME,
             "a block a window after the last one with events takes a number that would have "
             "read as old");

    ut_section("the reference: one event a window, as the field on the record carried it");
    mp_world_event_memory_reset(&memory);
    news = 0;
    for (i = 0; i < 40u; ++i) {
        uint16_t number = (uint16_t)(1u + i / 7u);   /* one event every seven substeps */

        mp_world_event_memory_block(&memory, 500u + i);
        news += mp_world_event_memory_note(&memory, number) == MP_WORLD_EVENT_FIRST_TIME ? 1u : 0u;
    }
    ut_checkf(news == 6u, "a clang every seven substeps, carried in every block, is taken six "
                          "times in forty substeps, once per event, as the old field did (%u)",
              news);
}

static void check_where(void)
{
    mp_world_event_t e;

    ut_section("what a client does with an event now");
    memset(&e, 0, sizeof e);
    e.sequence = 1u;
    e.kind     = MP_WORLD_EVENT_SCRIPT_EMITTER;
    e.at_actor = true;
    ut_check(mp_world_event_where(&e, true, 3u) == MP_WORLD_EVENT_AT_REPLICA,
             "at its replica when there is one");
    ut_check(mp_world_event_where(&e, false, MP_WORLD_EVENT_WINDOW - 1u) == MP_WORLD_EVENT_WAIT,
             "without one it waits while the window lasts");
    ut_check(mp_world_event_where(&e, false, MP_WORLD_EVENT_WINDOW) == MP_WORLD_EVENT_DROP,
             "and is dropped at the end of it");
    e.has_place = true;
    ut_check(mp_world_event_where(&e, false, 0u) == MP_WORLD_EVENT_WAIT,
             "an emitter has nothing to hang on at a place, so a place changes nothing");
    e.kind = MP_WORLD_EVENT_SCRIPT_SOUND;
    ut_check(mp_world_event_where(&e, false, 0u) == MP_WORLD_EVENT_AT_PLACE,
             "a sound with a place plays there without its replica");
    e.at_actor = false;
    e.kind     = MP_WORLD_EVENT_NPC_CLANG;
    ut_check(mp_world_event_where(&e, false, 0u) == MP_WORLD_EVENT_DROP,
             "a clang with no actor has nothing to be played on");
    ut_check(mp_world_event_where(NULL, true, 0u) == MP_WORLD_EVENT_DROP, "and nothing is none");
}

static void check_concerns(void)
{
    const float here[3] = { 10.0f, 10.0f, 1.0f };
    const float close_by[3] = { 30.0f, 10.0f, 1.0f };
    const float far_off[3] = { 90.0f, 10.0f, 1.0f };

    ut_section("which peer an event concerns");
    ut_check(mp_world_event_concerns(MP_WORLD_EVENT_SCRIPT_EMITTER, true, true, false, NULL, false,
                                     NULL, 0.0f),
             "an emitter concerns the peer that holds its actor or would be told of it");
    ut_check(!mp_world_event_concerns(MP_WORLD_EVENT_NPC_CLANG, true, false, true, close_by, true,
                                      here, 0.0f),
             "and a clang no other, however near its place");
    ut_check(mp_world_event_concerns(MP_WORLD_EVENT_SCRIPT_SOUND, true, false, true, close_by, true,
                                     here, 0.0f),
             "a sound concerns a player within the margin of its place, whatever it holds");
    ut_check(!mp_world_event_concerns(MP_WORLD_EVENT_SCRIPT_SOUND, true, true, true, far_off, true,
                                      here, 30.0f),
             "and not one beyond its radius and the margin");
    ut_check(mp_world_event_concerns(MP_WORLD_EVENT_SCRIPT_SOUND, true, false, true, far_off, true,
                                     here, 60.0f),
             "a louder one reaches further");
    ut_check(mp_world_event_concerns(MP_WORLD_EVENT_EXPLODE_AT, false, false, false, NULL, false,
                                     NULL, 0.0f),
             "with nothing to measure an event concerns everybody, and the engine decides");
    ut_check(!mp_world_event_concerns(MP_WORLD_EVENT_KINDS, true, true, false, NULL, false, NULL,
                                      0.0f),
             "an unknown kind concerns nobody");
}

static void check_the_table(void)
{
    uint8_t kind;
    bool    named = true;

    ut_section("the kinds");
    for (kind = MP_WORLD_EVENT_SCRIPT_EMITTER; kind < MP_WORLD_EVENT_KINDS; ++kind) {
        named = named && mp_world_event_kind_known(kind) &&
                strcmp(mp_world_event_kind_name(kind), "unknown") != 0 &&
                mp_world_event_tail_bytes(kind) <= MP_WORLD_EVENT_TAIL_MAX;
    }
    ut_check(named, "every kind has a name and a tail the part can carry");
    ut_check(!mp_world_event_kind_known(MP_WORLD_EVENT_NONE), "none is no kind");
    ut_check(((MP_WORLD_EVENT_KINDS - 1u) & (MP_WORLD_EVENT_AT_ACTOR | MP_WORLD_EVENT_HAS_PLACE)) ==
                 0u,
             "no kind reaches into the two flags beside it");
}

/* A full substep: only a kind that is heard gives way, and only to one that is seen. Every pair of
 * kinds, none and one past the table included. */
static void check_the_rank(void)
{
    uint8_t  held;
    uint8_t  coming;
    unsigned wrong = 0u;

    ut_section("a full substep: a sound gives way to what is seen, nothing else gives way");
    for (held = 0u; held <= MP_WORLD_EVENT_KINDS; ++held) {
        for (coming = 0u; coming <= MP_WORLD_EVENT_KINDS; ++coming) {
            bool want = held == MP_WORLD_EVENT_SCRIPT_SOUND && coming != held &&
                        mp_world_event_kind_known(coming);

            wrong += mp_world_event_gives_way(held, coming) != want ? 1u : 0u;
        }
    }
    ut_checkf(wrong == 0u, "a sound gives way to each of the five seen kinds and to nothing else, "
                           "and no other kind ever gives way (%u pair(s) wrong)", wrong);
    ut_check(mp_world_event_kind_seen(MP_WORLD_EVENT_NPC_CLANG) &&
                 !mp_world_event_kind_seen(MP_WORLD_EVENT_SCRIPT_SOUND) &&
                 !mp_world_event_kind_seen(MP_WORLD_EVENT_NONE),
             "a clang is seen, its sparks and flash on the blade; a script's sound is only heard");
}

/* The event the census did not read under a key. The rule as built first (a life noted is the
 * life, wherever the actor is) and the rule before it (dropped, always) are the references: the
 * new one is the first for an actor that is gone and the second for one that is still there. */
static void check_the_unread(void)
{
    unsigned i;

    ut_section("an event whose actor the census did not read: anchored only when it is gone");
    for (i = 0; i < 4u; ++i) {
        bool                    in_pool    = (i & 1u) != 0u;
        bool                    life_noted = (i & 2u) != 0u;
        mp_world_event_unread_t now        = mp_world_event_unread(in_pool, life_noted);
        mp_world_event_unread_t first      = life_noted ? MP_WORLD_EVENT_ANCHORED
                                                        : MP_WORLD_EVENT_UNCLAIMED;

        if (!in_pool) {
            ut_checkf(now == first, "gone from the pool, life noted %d: as the first rule (%d)",
                      (int)life_noted, (int)now);
        } else {
            ut_checkf(now == MP_WORLD_EVENT_PASSED_OVER,
                      "still in the pool, life noted %d: dropped as before, counted apart (%d)",
                      (int)life_noted, (int)now);
        }
    }
}

int main(void)
{
    check_the_round_trip();
    check_random_bytes();
    check_the_sizes();
    check_a_later_build();
    check_the_head();
    check_the_numbers();
    check_the_memory();
    check_where();
    check_concerns();
    check_the_table();
    check_the_rank();
    check_the_unread();
    return ut_summary("mp_world_event_rule");
}
