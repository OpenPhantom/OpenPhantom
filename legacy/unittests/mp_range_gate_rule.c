/* mp_range_gate_rule.c: which player an enemy's range is measured against. */
#include "unittest.h"

#include "mp_range_gate_rule.h"

#include "common/host_image.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void put(mp_range_gate_players_t *p, size_t row, float x, float y, float z)
{
    p->positions[row][0] = x;
    p->positions[row][1] = y;
    p->positions[row][2] = z;
    p->have[row] = true;
    if (row + 1u > p->count) {
        p->count = row + 1u;
    }
}

static bool within(float ax, float ay, float az, float px, float py, float pz, float r)
{
    float at[3];
    float player[3];

    at[0]     = ax;
    at[1]     = ay;
    at[2]     = az;
    player[0] = px;
    player[1] = py;
    player[2] = pz;
    return mp_range_gate_within(at, player, r);
}

/* The engine compares a squared distance against a squared radius with a STRICT less than. A body
 * standing exactly on its own radius is therefore out, in both gates: it does not wake, and once
 * awake it is removed. Getting this edge wrong by one comparison would make every placement on a
 * round number of units behave the other way round, which is the kind of change that looks like a
 * level bug and never like a netcode bug. */
static void test_the_boundary_is_the_engines(void)
{
    ut_check(within(0, 0, 0, 0, 0, 9.9f, 10.0f), "just inside the radius is inside");
    ut_check(!within(0, 0, 0, 0, 0, 10.0f, 10.0f),
             "exactly on the radius is OUT, because the engine's comparison is strict");
    ut_check(!within(0, 0, 0, 0, 0, 10.1f, 10.0f), "and past it is out");
}

/* Z counts. The engine's test is three dimensional, which is why a tall room can hold an actor
 * dormant directly above the player's head, and the rule must not quietly become a floor plan. */
static void test_height_counts(void)
{
    ut_check(!within(0, 0, 100.0f, 0, 0, 0, 10.0f),
             "a hundred units straight up is out of a radius of ten");
    ut_check(within(0, 0, 5.0f, 0, 0, 0, 10.0f), "five units up is inside it");
}

/* A radius of zero keeps nothing, and that is the engine's own reading: nothing is strictly less
 * than zero. The two callers test the radius against zero before they ask, so this case only
 * arises through the hull, and it must not answer yes there. */
static void test_a_radius_of_zero_keeps_nothing(void)
{
    ut_check(!within(0, 0, 0, 0, 0, 0, 0.0f),
             "a body standing exactly on the player is still out of a radius of zero");
}

/* The point of the whole file: the far player counts as much as the local one. Without this the
 * host measures against itself alone, and an enemy standing next to a client who has walked away
 * is out of range here and gets taken away under that client. */
static void test_any_player_counts(void)
{
    mp_range_gate_players_t players;
    float                   at[3];

    memset(&players, 0, sizeof players);
    put(&players, 0, 0.0f, 0.0f, 0.0f);          /* the host, far away from `at` */
    put(&players, 1, 1000.0f, 0.0f, 0.0f);       /* the client, standing on it */

    at[0] = 1000.0f;
    at[1] = 0.0f;
    at[2] = 0.0f;
    ut_check(mp_range_gate_any_within(&players, at, 50.0f),
             "an enemy at the client's feet is in range, although the host is a thousand units "
             "away: this is the case the whole gate exists for");

    at[0] = 500.0f;
    ut_check(!mp_range_gate_any_within(&players, at, 50.0f),
             "and one halfway between them is in nobody's range, so it is still removed");
}

/* A row with no position is skipped, never read. A bank with no body carries zeros, and zeros are
 * the world origin: read as a position they would hold every enemy near the origin alive for a
 * player who is not in the level. */
static void test_a_row_without_a_position_is_not_the_origin(void)
{
    mp_range_gate_players_t players;
    float                   at[3];

    memset(&players, 0, sizeof players);
    put(&players, 0, 1000.0f, 0.0f, 0.0f);
    players.have[1] = false;                      /* a bank with no body, its position zeroed */
    players.count = 2u;

    at[0] = 0.0f;
    at[1] = 0.0f;
    at[2] = 0.0f;
    ut_check(!mp_range_gate_any_within(&players, at, 50.0f),
             "an enemy at the origin is NOT held alive by a bank that has no body");
}

static void test_nothing_to_measure_answers_no(void)
{
    mp_range_gate_players_t players;
    float                   at[3];

    memset(&players, 0, sizeof players);
    at[0] = 0.0f;
    at[1] = 0.0f;
    at[2] = 0.0f;
    ut_check(!mp_range_gate_any_within(&players, at, 50.0f),
             "no players, no widening: the engine's own answer stands alone");
    ut_check(!mp_range_gate_any_within(NULL, at, 50.0f), "and a null table answers no");
    ut_check(!mp_range_gate_any_within(&players, NULL, 50.0f), "as does a null position");
}

/* `count` is what the loop trusts, and a count past the array must not walk off it. */
static void test_a_count_past_the_array_is_clamped(void)
{
    mp_range_gate_players_t players;
    float                   at[3];

    memset(&players, 0, sizeof players);
    put(&players, 0, 0.0f, 0.0f, 0.0f);
    players.count = 1000u;

    at[0] = 0.0f;
    at[1] = 0.0f;
    at[2] = 0.0f;
    ut_check(mp_range_gate_any_within(&players, at, 50.0f),
             "the row that is there still answers, and the walk stops at the array's end");
}

/* ==============================================================================================
 * The caller census.
 *
 * The census used to read its bytes one five byte window at a time, each window an open of the
 * executable, and that stood the host's menu still for eleven seconds. The search now
 * runs over a buffer read once. The old loop is kept here, reading its windows out of the same
 * buffer, so the new one is held to exactly the answers the old one gave.
 * ============================================================================================ */

#define CODE_VA 0x00401000u

/* The old loop, word for word, with its window read replaced by a copy out of `code`. Its size
 * limit stays as it was: it answered nothing for five bytes, although one window fits. */
static size_t old_count_callers(const uint8_t *code, size_t size, uintptr_t text, uintptr_t target,
                                uintptr_t *returns, size_t max_returns)
{
    size_t  found = 0;
    size_t  at;
    uint8_t window[5];

    if (text == 0u || size <= 5u) {
        return 0;
    }
    for (at = 0; at + 5u <= size; ++at) {
        int32_t   displacement;
        uintptr_t after;

        memcpy(window, code + at, sizeof window);
        if (window[0] != 0xE8u) {
            continue;
        }
        memcpy(&displacement, &window[1], sizeof displacement);
        after = text + at + 5u;
        if ((uintptr_t)((intptr_t)after + displacement) != target) {
            continue;
        }
        if (found < max_returns) {
            returns[found] = after;
        }
        ++found;
    }
    return found;
}

/* Writes `call target` at `at`, as a linker would, and answers the return address. */
static uint32_t put_call(uint8_t *code, size_t at, uint32_t code_va, uint32_t target)
{
    uint32_t after        = code_va + (uint32_t)at + 5u;
    uint32_t displacement = target - after;

    code[at] = 0xE8u;
    memcpy(&code[at + 1u], &displacement, sizeof displacement);
    return after;
}

/* A third caller answers 3. Stopping at the second one would report the two this build knows and
 * install the hull for a caller nobody has read, which is the assertion the count exists for. */
static void test_a_third_caller_is_counted(void)
{
    uint8_t  code[64];
    uint32_t returns[2];
    uint32_t target = CODE_VA + 0x5000u;

    memset(code, 0x90, sizeof code);
    (void)put_call(code, 3u, CODE_VA, target);
    (void)put_call(code, 20u, CODE_VA, target);
    (void)put_call(code, 40u, CODE_VA, target);
    ut_check(mp_range_gate_count_callers(code, sizeof code, CODE_VA, target, returns, 2u) == 3u,
             "three calls answer 3 with room for two returns: the count goes on past the room");
}

/* What goes into `returns` is the address after the call, which is what _ReturnAddress reads in
 * the hull. The call's own address would count right, install, and then never match a caller. */
static void test_the_return_is_after_the_call(void)
{
    uint8_t  code[32];
    uint32_t returns[2] = { 0u, 0u };
    uint32_t target = CODE_VA + 0x100u;
    uint32_t after;

    memset(code, 0xCC, sizeof code);
    after = put_call(code, 7u, CODE_VA, target);
    ut_check(mp_range_gate_count_callers(code, sizeof code, CODE_VA, target, returns, 2u) == 1u,
             "one call is found");
    ut_check(returns[0] == after && after == CODE_VA + 12u,
             "and the address written down is the byte after its five, not the E8");
}

/* The installation line calls the second return the activation scan and the first the removal
 * test, which is true because the search runs upwards and 004332F7 lies below 00437201. */
static void test_the_returns_ascend(void)
{
    uint8_t  code[4096];
    uint32_t returns[2] = { 0u, 0u };
    uint32_t target = CODE_VA + 0x800u;
    uint32_t low;
    uint32_t high;

    memset(code, 0x90, sizeof code);
    high = put_call(code, 3000u, CODE_VA, target);
    low  = put_call(code, 100u, CODE_VA, target);
    ut_check(mp_range_gate_count_callers(code, sizeof code, CODE_VA, target, returns, 2u) == 2u,
             "two calls are found");
    ut_check(returns[0] == low && returns[1] == high,
             "the lower one first, whichever was written first");
}

/* The section is read at its live address and the target is live too. Measured against the
 * address the file names, a moved image finds nothing, and at the preferred base nobody would see
 * the difference; the same bytes at another base must answer the same. */
static void test_the_answer_moves_with_the_image(void)
{
    uint8_t  code[256];
    uint32_t moved = CODE_VA + 0x01230000u;

    memset(code, 0x90, sizeof code);
    (void)put_call(code, 10u, CODE_VA, CODE_VA + 0x4000u);
    (void)put_call(code, 90u, CODE_VA, CODE_VA + 0x4000u);
    ut_check(mp_range_gate_count_callers(code, sizeof code, CODE_VA, CODE_VA + 0x4000u, NULL,
                                         0u) == 2u,
             "at its own base the two calls are found");
    ut_check(mp_range_gate_count_callers(code, sizeof code, moved, moved + 0x4000u, NULL, 0u) ==
                 2u,
             "moved as a whole, section and target alike, the same two are found");
    ut_check(mp_range_gate_count_callers(code, sizeof code, CODE_VA, moved + 0x4000u, NULL, 0u) ==
                 0u,
             "and a section at one base asked about a target at another finds nothing, which is "
             "the trap of measuring a file address against a live one");
}

static void test_too_little_to_search(void)
{
    uint8_t code[5] = { 0xE8u, 0u, 0u, 0u, 0u };

    ut_check(mp_range_gate_count_callers(NULL, 64u, CODE_VA, CODE_VA, NULL, 0u) == 0u,
             "no buffer, nothing found");
    ut_check(mp_range_gate_count_callers(code, 4u, CODE_VA, CODE_VA + 5u, NULL, 0u) == 0u,
             "four bytes hold no call");
    ut_check(mp_range_gate_count_callers(code, 5u, CODE_VA, CODE_VA + 5u, NULL, 0u) == 1u,
             "five bytes hold exactly one, where the old loop answered 0 for them");
}

/* One step of the 32 bit xorshift the random sections below are drawn from. */
static uint32_t xorshift(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* Random sections, sown with calls to one target and with E8 bytes that land anywhere, compared
 * against the old loop: the count and every return written must agree. The code address stays
 * in the range a 32 bit image loads at, where the old signed sum could not overflow. */
static void test_the_old_loop_is_matched(void)
{
    static uint8_t code[8192];
    uint32_t       state      = 0x2545F491u;
    unsigned       round;
    unsigned       differ     = 0;
    unsigned       calls_seen = 0;

    for (round = 0; round < 400u; ++round) {
        uint32_t  code_va = CODE_VA + ((round * 0x10000u) & 0x0FFF0000u);
        uint32_t  target;
        size_t    size;
        size_t    i;
        size_t    got_new;
        size_t    got_old;
        uint32_t  new_returns[4];
        uintptr_t old_returns[4];

        state = xorshift(state);
        size   = 6u + (size_t)(state % (sizeof code - 6u));
        target = code_va + (state % 0x20000u);
        for (i = 0; i < size; ++i) {
            state = xorshift(state);
            code[i] = (uint8_t)((state & 7u) == 0u ? 0xE8u : state >> 8);
        }
        for (i = 0; i < 6u; ++i) {
            size_t at;

            state = xorshift(state);
            at = (size_t)(state % (size - 5u));
            (void)put_call(code, at, code_va, target);
        }
        memset(new_returns, 0, sizeof new_returns);
        memset(old_returns, 0, sizeof old_returns);
        got_new = mp_range_gate_count_callers(code, size, code_va, target, new_returns, 4u);
        got_old = old_count_callers(code, size, code_va, target, old_returns, 4u);
        calls_seen += (unsigned)got_old;
        if (got_new != got_old) {
            ++differ;
            continue;
        }
        for (i = 0; i < got_new && i < 4u; ++i) {
            if ((uintptr_t)new_returns[i] != old_returns[i]) {
                ++differ;
                break;
            }
        }
    }
    ut_check(calls_seen >= 400u, "the sections carried calls to find, at least one a round");
    ut_check(differ == 0u,
             "over 400 random sections the new search and the old window loop agree on the count "
             "and on every return written");
}

/* The census over a real image: this test's own executable, read in one piece exactly as the
 * hull reads the game's, asked about a function with two call sites. It is the only check that
 * runs the live address of the section against a live target on an image the loader may have
 * moved. It leans on the compiler keeping the two calls as calls: not inlined, not a jump in tail
 * position, not folded into another function of the same body. */
static volatile int census_sink;

__declspec(noinline) static int census_target(int x)
{
    census_sink += x * 7 + 3;
    return census_sink ^ 0x5A5A;
}

static void test_the_census_on_this_image(void)
{
    uintptr_t text;
    size_t    size;
    uint8_t  *code;
    uint32_t  returns[2] = { 0u, 0u };
    size_t    found;
    int       a;
    int       b;

    a = census_target(1);
    b = census_target(2);
    census_sink += a + b;

    ut_check(host_image_resolve(), "this test's own image resolves");
    text = host_image_text();
    size = host_image_text_size();
    code = (size != 0u) ? (uint8_t *)malloc(size) : NULL;
    ut_check(code != NULL, "its code section has room to be read into");
    if (code == NULL) {
        return;
    }
    ut_check(host_image_read_original(text, code, size),
             "its code section is read out of the file in one piece");
    found = mp_range_gate_count_callers(code, size, (uint32_t)text,
                                        (uint32_t)(uintptr_t)&census_target, returns, 2u);
    ut_check(found == 2u,
             "and the search finds exactly the two calls to the test's own function, measured at "
             "the address the image was loaded at (a compiler that inlined or folded them would "
             "make this fail, not the search)");
    ut_check(found != 2u || (returns[0] < returns[1] && returns[0] > (uint32_t)text),
             "their returns ascend and lie inside the section");
    free(code);
}

int main(void)
{
    test_the_boundary_is_the_engines();
    test_height_counts();
    test_a_radius_of_zero_keeps_nothing();
    test_any_player_counts();
    test_a_row_without_a_position_is_not_the_origin();
    test_nothing_to_measure_answers_no();
    test_a_count_past_the_array_is_clamped();

    test_a_third_caller_is_counted();
    test_the_return_is_after_the_call();
    test_the_returns_ascend();
    test_the_answer_moves_with_the_image();
    test_too_little_to_search();
    test_the_old_loop_is_matched();
    test_the_census_on_this_image();

    return ut_summary("mp_range_gate_rule");
}
