/* face_bounds.c: the two bounds the deferred face path is missing, checked without the game.
 *
 * Each of these decides whether a polygon is drawn, and each can be wrong in either direction with
 * nothing to show for it. Refuse one vertex too early and geometry starts disappearing from scenes
 * that were always inside the engine's limits, and nobody will connect that to this DLL. Refuse one
 * vertex too late and the guard reports success while the write it exists to stop goes through. So
 * the values on both sides of every boundary are checked here rather than argued about.
 *
 * The numbers used below are the ones the guard runs with. 30 vertices is the limit the editor
 * build's own assert names, "Can't have more than 30 sided polys as special faces". 31 is the
 * highest a configuration file is allowed to raise it to, because the outcode array holds 32 and
 * its 33rd entry is the saved return address at [esp+0x38]. 8196 is 0x2004, the queue entry ceiling
 * the submit function tests on its first instruction, `cmp eax,0x2004` at 0x00487D31, and the pool
 * ceiling is derived from it.
 */
#include "unittest.h"

#include "face_bounds.h"

#include <stdbool.h>
#include <stdint.h>

#define AUTHORED_MAX     30u   /* the limit the guard installs unless the ini says otherwise */
#define HIGHEST_ALLOWED  31u   /* the most a configuration file may raise it to               */
#define DERIVED_POOL   8196u   /* the pool ceiling derived from the queue entry ceiling       */

static void test_vertex_bound(void)
{
    ut_section("the outcode array");

    ut_check(!face_bounds_vertex_count_refused(0u, AUTHORED_MAX),
             "a face with no vertices is accepted");
    ut_check(!face_bounds_vertex_count_refused(3u, AUTHORED_MAX),
             "a triangle is accepted");
    ut_check(!face_bounds_vertex_count_refused(29u, AUTHORED_MAX),
             "29 vertices sits one below the limit and is accepted");
    ut_check(!face_bounds_vertex_count_refused(30u, AUTHORED_MAX),
             "30 vertices is the authored limit itself and is accepted");
    ut_check(face_bounds_vertex_count_refused(31u, AUTHORED_MAX),
             "31 vertices is one past the limit and is refused");
    ut_check(face_bounds_vertex_count_refused(32u, AUTHORED_MAX),
             "32 vertices, which fills the outcode array exactly, is refused");
    ut_check(face_bounds_vertex_count_refused(33u, AUTHORED_MAX),
             "33 vertices, the face whose last outcode lands on the saved return address, "
             "is refused");
    ut_check(face_bounds_vertex_count_refused(UINT32_MAX, AUTHORED_MAX),
             "an absurd vertex count is refused");

    /* The reason the configuration stops at 31: even raised as far as it goes, the count that
     * writes the return address is still refused. If this ever passed, the ini would be able to
     * switch the corruption back on. */
    ut_check(!face_bounds_vertex_count_refused(31u, HIGHEST_ALLOWED),
             "at the highest configurable limit 31 vertices is accepted");
    ut_check(face_bounds_vertex_count_refused(32u, HIGHEST_ALLOWED),
             "at the highest configurable limit 32 vertices is refused");
    ut_check(face_bounds_vertex_count_refused(33u, HIGHEST_ALLOWED),
             "no configurable limit lets the 33rd vertex through");

    /* A limit the caller lowered, which is what the ini key is for on a build where the authored
     * one turns out to be wrong. */
    ut_check(!face_bounds_vertex_count_refused(1u, 1u), "a limit of 1 accepts a single vertex");
    ut_check(face_bounds_vertex_count_refused(2u, 1u), "a limit of 1 refuses two");

    /* The degenerate limit. It is not a way to switch this bound off, and saying so here is what
     * stops somebody adding a zero default later and quietly refusing every face in the game. */
    ut_check(!face_bounds_vertex_count_refused(0u, 0u),
             "a limit of 0 still accepts a face with no vertices");
    ut_check(face_bounds_vertex_count_refused(1u, 0u),
             "a limit of 0 refuses every face that carries a vertex, it does not disable the bound");
}

static void test_pool_bound(void)
{
    ut_section("the shared vertex pool");

    ut_check(!face_bounds_pool_would_overflow(0u, DERIVED_POOL, 30u),
             "a face at the start of an empty pool is accepted");
    ut_check(!face_bounds_pool_would_overflow(4000u, DERIVED_POOL, 30u),
             "a face in the middle of a half full pool is accepted");

    /* The exact boundary, from both sides: a face that fills the pool to its last vertex is still
     * drawn, and the one vertex after that is not. */
    ut_check(!face_bounds_pool_would_overflow(DERIVED_POOL - 30u, DERIVED_POOL, 30u),
             "a face that ends exactly on the ceiling is accepted");
    ut_check(face_bounds_pool_would_overflow(DERIVED_POOL - 30u, DERIVED_POOL, 31u),
             "one vertex more than the pool has room for is refused");
    ut_check(!face_bounds_pool_would_overflow(DERIVED_POOL - 1u, DERIVED_POOL, 1u),
             "the single vertex that fills the last slot is accepted");
    ut_check(face_bounds_pool_would_overflow(DERIVED_POOL - 1u, DERIVED_POOL, 2u),
             "two vertices into the last slot are refused");

    /* A full pool. The cursor has not passed the ceiling, so a face carrying nothing is still an
     * honest answer of "this fits". */
    ut_check(!face_bounds_pool_would_overflow(DERIVED_POOL, DERIVED_POOL, 0u),
             "a face with no vertices fits a pool that is exactly full");
    ut_check(face_bounds_pool_would_overflow(DERIVED_POOL, DERIVED_POOL, 1u),
             "a single vertex does not fit a pool that is exactly full");

    /* A cursor that is already past the ceiling, which is the state the guard was installed too
     * late to prevent. Everything is refused from there, including an empty face, because the pool
     * is already in a condition no further write should be added to. */
    ut_check(face_bounds_pool_would_overflow(DERIVED_POOL + 1u, DERIVED_POOL, 0u),
             "a cursor already past the ceiling refuses even a face with no vertices");
    ut_check(face_bounds_pool_would_overflow(UINT32_MAX, DERIVED_POOL, 1u),
             "a wild cursor value refuses the face rather than computing with it");

    ut_check(!face_bounds_pool_would_overflow(0u, DERIVED_POOL, 0u),
             "an empty face on an empty pool is accepted");
}

/* The property the expression is written for, and the one a handful of ordinary values would not
 * have caught. Forming used + count and comparing that against the ceiling wraps, and a wrapped sum
 * is small, so the naive form answers "it fits" for exactly the faces that overrun the pool worst. */
static void test_no_wrap(void)
{
    ut_section("the sum that is never formed");

    ut_check(face_bounds_pool_would_overflow(8000u, DERIVED_POOL, UINT32_MAX),
             "a cursor at 8000 and a count of 4294967295 is refused, "
             "where adding the two would have wrapped to 7999 and accepted it");
    ut_check(face_bounds_pool_would_overflow(1u, UINT32_MAX, UINT32_MAX),
             "a count of 4294967295 against a ceiling of the same is refused, "
             "where adding would have wrapped to 0");
    ut_check(!face_bounds_pool_would_overflow(1u, UINT32_MAX, UINT32_MAX - 1u),
             "and one vertex less than that fits exactly, so the bound is not simply refusing "
             "everything large");
    ut_check(face_bounds_pool_would_overflow(0u, 1u, UINT32_MAX),
             "an enormous face against a ceiling of one vertex is refused");

    /* The subtraction must not borrow either. capacity - used is only reached once used is known to
     * be at most capacity, and this is the value where getting that order wrong would show. */
    ut_check(face_bounds_pool_would_overflow(DERIVED_POOL + 1u, DERIVED_POOL, 0u),
             "a cursor one past the ceiling is answered before anything is subtracted from it");

    /* Over a run of counts the answer must change exactly once and never change back. A wrap
     * anywhere inside the range would show up as a second flip, which single values cannot see. */
    {
        uint32_t count;
        int      flips = 0;
        bool     previous = face_bounds_pool_would_overflow(8000u, DERIVED_POOL, 0u);

        for (count = 1u; count <= 400u; ++count) {
            bool now = face_bounds_pool_would_overflow(8000u, DERIVED_POOL, count);

            if (now != previous) {
                ++flips;
            }
            previous = now;
        }
        ut_checkf(flips == 1, "the pool answer changes exactly once as the face grows (%d changes)",
                  flips);
        ut_check(previous, "and it has settled on refusing by the time the face is 400 vertices");
    }
}

/* A ceiling of zero is the caller's way of saying the bound is not in force, which happens when the
 * cursor could not be located in the image or the ini asked for it. Read as an ordinary ceiling it
 * would refuse every face in the game, so which of the two it means is worth pinning down. */
static void test_capacity_zero_is_off(void)
{
    ut_section("a ceiling of zero");

    ut_check(!face_bounds_pool_would_overflow(0u, 0u, 0u),
             "a ceiling of 0 refuses nothing on an empty pool");
    ut_check(!face_bounds_pool_would_overflow(0u, 0u, 30u),
             "a ceiling of 0 refuses nothing on an ordinary face");
    ut_check(!face_bounds_pool_would_overflow(UINT32_MAX, 0u, UINT32_MAX),
             "a ceiling of 0 refuses nothing whatever the cursor and the count are");
}

int main(void)
{
    test_vertex_bound();
    test_pool_bound();
    test_no_wrap();
    test_capacity_zero_is_off();

    return ut_summary("face_bounds");
}
