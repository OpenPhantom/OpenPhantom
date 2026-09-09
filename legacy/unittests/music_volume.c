/* The two-deep value rule from music_volume.c, exercised as the audio screen actually drives it.
 *
 * The rule under test: the value to put back after a provider change is the one BEFORE the current
 * one whenever the current one is a zero standing in front of the detach. The screen always
 * silences the music on the line before it detaches, so that zero is the screen's, and a player who
 * chose silence has already pushed their own zero into `previous` on the call before.
 *
 * The real file cannot be linked here: every entry point is a __cdecl detour whose body calls a
 * trampoline that only exists in a patched process. This mirrors the three lines of state that
 * decide the answer, which is the part with reasoning in it. The detours themselves are proved by
 * the game running and by the log line the attach writes. */
#include "unittest.h"

#include <stdbool.h>

typedef struct latch {
    float current;
    float previous;
    bool  seen_any;
    bool  restore_valid;
    float restore_value;
} latch_t;

static void set_volume(latch_t *l, float volume)
{
    l->previous = l->seen_any ? l->current : volume;
    l->current  = volume;
    l->seen_any = true;
}

static void detach(latch_t *l)
{
    if (l->seen_any && l->current == 0.0f) {
        l->restore_value = l->previous;
        l->restore_valid = true;
    }
}

/* What bapMusicAttach does at its tail: read MVOL out of obi.ini and apply it. */
static void attach(latch_t *l, float value_from_file)
{
    set_volume(l, value_from_file);
    if (l->restore_valid) {
        l->restore_valid = false;
        set_volume(l, l->restore_value);
    }
}

int main(void)
{
    ut_section("a provider change");
    {
        latch_t l = {0};

        set_volume(&l, 1.00f);          /* startup, MVOL=100 */
        set_volume(&l, 0.30f);          /* the player drags the slider */
        set_volume(&l, 0.00f);          /* the screen silences for the switch */
        detach(&l);
        attach(&l, 1.00f);              /* the re-attach reloads the stale file value */
        ut_near(l.current, 0.30f, 0.0001f, "a slider moved before the switch survives it");
    }
    {
        latch_t l = {0};

        set_volume(&l, 1.00f);
        set_volume(&l, 0.00f);          /* the player drags to silence */
        set_volume(&l, 0.00f);          /* the screen silences too, changing nothing */
        detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.00f, 0.0001f, "silence the player chose survives it as well");
    }
    {
        latch_t l = {0};

        set_volume(&l, 1.00f);
        set_volume(&l, 0.42f);
        set_volume(&l, 0.00f);
        detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.42f, 0.0001f, "the first switch of a visit keeps the value");
        set_volume(&l, 0.00f);
        detach(&l);
        attach(&l, 1.00f);
        ut_near(l.current, 0.42f, 0.0001f, "and so does the second");
    }

    ut_section("a detach that is not a provider change");
    {
        latch_t l = {0};

        set_volume(&l, 0.55f);
        detach(&l);
        ut_check(!l.restore_valid, "a detach with the music audible arms no restore");
        attach(&l, 0.80f);
        ut_near(l.current, 0.80f, 0.0001f, "so the file value stands, as it should");
    }
    {
        latch_t l = {0};

        detach(&l);
        ut_check(!l.restore_valid, "a detach before any volume was set arms no restore");
    }

    return ut_summary("music volume");
}
