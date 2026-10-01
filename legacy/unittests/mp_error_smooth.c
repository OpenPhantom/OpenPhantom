/* mp_error_smooth.c: the glide, the snap, and the promise that the offset reaches home.
 *
 * A correction is absorbed so the drawn position is continuous across the jump. Then the offset
 * decays toward zero, faster the larger it is, and actually reaches zero rather than creeping
 * forever. A correction too large to hide snaps instead, with no offset. Every check is a property
 * of a sequence, because a single step of an exponential decay proves nothing.
 */
#include "unittest.h"

#include "mp_error_smooth.h"

#include <stdbool.h>
#include <stddef.h>

static float absf(float x)
{
    return x < 0.0f ? -x : x;
}

static void check_hides_the_jump(void)
{
    mp_error_smooth_t smooth;
    float             before[3] = { 10.0f, 0.0f, 0.0f };
    float             delta[3]  = { 2.0f, 0.0f, 0.0f };   /* the body jumps forward by 2 */
    float             corrected[3];
    float             drawn[3];

    ut_section("a correction is invisible the frame it lands");

    mp_error_smooth_init(&smooth, 1.0f);

    corrected[0] = before[0] + delta[0];   /* 12 */
    corrected[1] = 0.0f;
    corrected[2] = 0.0f;

    ut_check(mp_error_smooth_correct(&smooth, delta),
             "a small correction is smoothed, not snapped");
    mp_error_smooth_render(&smooth, corrected, drawn);
    ut_check(absf(drawn[0] - before[0]) < 0.001f,
             "the drawn position is exactly where it was before the jump");
}

static void check_glides_home(void)
{
    mp_error_smooth_t smooth;
    float             delta[3] = { 3.0f, 0.0f, 0.0f };
    float             previous;
    int               frame;
    bool              monotonic = true;

    ut_section("the offset decays toward zero and reaches it");

    mp_error_smooth_init(&smooth, 1.0f);
    mp_error_smooth_correct(&smooth, delta);

    previous = mp_error_smooth_magnitude(&smooth);
    ut_check(previous > 0.0f, "the offset starts nonzero");

    for (frame = 0; frame < 500; ++frame) {
        float now;

        mp_error_smooth_decay(&smooth);
        now = mp_error_smooth_magnitude(&smooth);
        if (now > previous + 0.0001f) {
            monotonic = false;
        }
        previous = now;
        if (now == 0.0f) {
            break;
        }
    }

    ut_check(monotonic, "it never grows, only shrinks");
    ut_check(mp_error_smooth_magnitude(&smooth) == 0.0f,
             "and it reaches exactly zero rather than creeping forever");
}

static void check_large_decays_faster(void)
{
    mp_error_smooth_t small;
    mp_error_smooth_t large;
    float             small_delta[3] = { 0.5f, 0.0f, 0.0f };   /* below small_error */
    float             large_delta[3] = { 6.0f, 0.0f, 0.0f };   /* above large_error */

    ut_section("a large error is caught up faster than a small one");

    mp_error_smooth_init(&small, 1.0f);
    mp_error_smooth_init(&large, 1.0f);
    mp_error_smooth_correct(&small, small_delta);
    mp_error_smooth_correct(&large, large_delta);

    /* One frame of decay, then compare the fraction remaining. The large offset should have shed a
     * larger fraction of itself. */
    {
        float small_before = mp_error_smooth_magnitude(&small);
        float large_before = mp_error_smooth_magnitude(&large);
        float small_kept;
        float large_kept;

        mp_error_smooth_decay(&small);
        mp_error_smooth_decay(&large);
        small_kept = mp_error_smooth_magnitude(&small) / small_before;
        large_kept = mp_error_smooth_magnitude(&large) / large_before;

        ut_check(large_kept < small_kept,
                 "the large offset keeps a smaller fraction, so it catches up faster");
    }
}

static void check_snap(void)
{
    mp_error_smooth_t smooth;
    float             huge[3] = { 1000.0f, 0.0f, 0.0f };   /* past snap_error */
    float             position[3] = { 0.0f, 0.0f, 0.0f };
    float             drawn[3];

    ut_section("a correction too large to hide snaps");

    mp_error_smooth_init(&smooth, 1.0f);
    ut_check(!mp_error_smooth_correct(&smooth, huge),
             "a huge correction reports a snap, not a glide");
    ut_check(mp_error_smooth_magnitude(&smooth) == 0.0f, "and leaves no offset");
    mp_error_smooth_render(&smooth, position, drawn);
    ut_check(drawn[0] == position[0], "so the drawn position is the corrected one, at once");
}

static void check_two_corrections_accumulate(void)
{
    mp_error_smooth_t smooth;
    float             a[3] = { 1.0f, 0.0f, 0.0f };
    float             b[3] = { 1.0f, 0.0f, 0.0f };

    ut_section("corrections in the same direction accumulate their offset");

    mp_error_smooth_init(&smooth, 1.0f);
    mp_error_smooth_correct(&smooth, a);
    {
        float after_one = mp_error_smooth_magnitude(&smooth);

        mp_error_smooth_correct(&smooth, b);
        ut_check(mp_error_smooth_magnitude(&smooth) > after_one,
                 "a second correction before the first has decayed grows the offset");
    }
}

int main(void)
{
    check_hides_the_jump();
    check_glides_home();
    check_large_decays_faster();
    check_snap();
    check_two_corrections_accumulate();

    return ut_summary("mp_error_smooth");
}
