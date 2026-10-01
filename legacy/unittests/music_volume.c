/* The two-deep value rule of music_volume_latch.c, driven as the audio screen drives it.
 *
 * The rule under test: the value to put back after a provider change is the one BEFORE the current
 * one whenever the current one is a zero standing in front of the detach. The screen always
 * silences the music on the line before it detaches, so that zero is the screen's, and a player who
 * chose silence has already pushed their own zero into `previous` on the call before.
 *
 * The three hooks in music_volume.c are not linked: each calls a trampoline that only exists in a
 * patched process, and the game running and the log line the attach writes are what prove them.
 * What they do between the trampoline calls is one call each into the file under test, and
 * `attach` below repeats the attach hook's sequence: the original attach applies the file's value
 * through the setter, which the set-volume hook sees, and the restore is then taken and
 * applied. */
#include "music_volume_latch.h"
#include "unittest.h"

#include <math.h>
#include <stddef.h>
#include <stdbool.h>

/* What bapMusicAttach does at its tail, and what hook_attach does after it. */
static void attach(music_volume_latch_t *l, float value_from_file)
{
    float restore = 0.0f;

    music_volume_latch_set(l, value_from_file);
    if (music_volume_latch_take_restore(l, &restore)) {
        music_volume_latch_restored(l, restore);
    }
}

int main(void)
{
    ut_section("a provider change");
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 1.00f);      /* startup, MVOL=100 */
        music_volume_latch_set(&l, 0.30f);      /* the player drags the slider */
        music_volume_latch_set(&l, 0.00f);      /* the screen silences for the switch */
        music_volume_latch_detach(&l);
        ut_check(l.restore_valid, "a zero in front of a detach arms a restore");
        ut_near(l.restore_value, 0.30f, 0.0001f, "of the value before the zero");
        attach(&l, 1.00f);                      /* the re-attach reloads the stale file value */
        ut_near(l.current, 0.30f, 0.0001f, "a slider moved before the switch survives it");
        ut_near(l.previous, 0.30f, 0.0001f, "and both depths hold it, not the file's value");
        ut_check(!l.restore_valid, "the restore is consumed");
    }
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 1.00f);
        music_volume_latch_set(&l, 0.00f);      /* the player drags to silence */
        music_volume_latch_set(&l, 0.00f);      /* the screen silences too, changing nothing */
        music_volume_latch_detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.00f, 0.0001f, "silence the player chose survives it as well");
    }
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 1.00f);
        music_volume_latch_set(&l, 0.42f);
        music_volume_latch_set(&l, 0.00f);
        music_volume_latch_detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.42f, 0.0001f, "the first switch of a visit keeps the value");
        music_volume_latch_set(&l, 0.00f);
        music_volume_latch_detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.42f, 0.0001f, "and so does the second");
    }

    ut_section("a detach that is not a provider change");
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 0.55f);
        music_volume_latch_detach(&l);
        ut_check(!l.restore_valid, "a detach with the music audible arms no restore");
        attach(&l, 0.80f);
        ut_near(l.current, 0.80f, 0.0001f, "so the file value stands, as it should");
    }
    {
        music_volume_latch_t l = {0};
        float                value = 7.0f;

        music_volume_latch_detach(&l);
        ut_check(!l.restore_valid, "a detach before any volume was set arms no restore");
        ut_check(!music_volume_latch_take_restore(&l, &value), "and there is nothing to take");
        ut_near(value, 7.0f, 0.0f, "the out value is left alone when nothing was armed");
    }

    ut_section("the first call and the edges of the value");
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 0.00f);      /* the very first call is a zero */
        music_volume_latch_detach(&l);
        ut_check(l.restore_valid, "a first call of zero in front of a detach still arms");
        ut_near(l.restore_value, 0.00f, 0.0001f, "and puts back the zero, the only value seen");
    }
    {
        music_volume_latch_t l = {0};

        music_volume_latch_set(&l, 1.00f);
        music_volume_latch_set(&l, -0.0f);      /* a negative zero compares equal to zero */
        music_volume_latch_detach(&l);
        ut_check(l.restore_valid, "a negative zero counts as the screen's silence");
    }
    {
        music_volume_latch_t l = {0};
        float                nan = NAN;

        music_volume_latch_set(&l, 1.00f);
        music_volume_latch_set(&l, nan);        /* a value that is not a number is not a zero */
        music_volume_latch_detach(&l);
        ut_check(!l.restore_valid, "a current value that is not a number arms nothing");
        music_volume_latch_set(&l, 0.00f);
        music_volume_latch_detach(&l);
        ut_check(music_volume_latch_take_restore(&l, NULL),
                 "a NULL out pointer takes an armed restore all the same");
        ut_check(!l.restore_valid, "and consumes it");
    }

    return ut_summary("music volume");
}
