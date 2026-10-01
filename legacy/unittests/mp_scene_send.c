/* When the host's scene note goes out: at once on a change, throttled to one change in four
 * substeps, repeated once a second, none said once, and a full channel never loses a change.
 *
 * The send is a stand-in that records what it was handed and can refuse, the way a full reliable
 * channel refuses. */
#include "unittest.h"

#include "mp_scene_send.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static unsigned sends;
static bool     channel_full;
static uint8_t  last_phase;

static bool fake_send(const uint8_t *bytes, size_t count)
{
    if (channel_full) {
        return false;
    }
    ++sends;
    last_phase = count > 4u ? bytes[4] : 0xFFu;
    return true;
}

static mp_scene_note_t a_note(uint16_t serial, uint8_t phase)
{
    mp_scene_note_t note;

    memset(&note, 0, sizeof note);
    note.serial       = serial;
    note.phase        = phase;
    note.what         = MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS;
    note.trigger_slot = 1u;
    note.anchor[0]    = 10.0f;
    return note;
}

static void check_the_cadence(void)
{
    mp_scene_sender_t sender;
    mp_scene_note_t   note;
    uint32_t          now;

    memset(&sender, 0, sizeof sender);
    sends        = 0u;
    channel_full = false;

    ut_section("no scene yet says nothing");
    note = a_note(0u, MP_SCENE_PHASE_NONE);
    ut_check(!mp_scene_sender_offer(&sender, &note, 1u, &fake_send) && sends == 0u,
             "a scene numbered nought is no scene");
    note = a_note(1u, MP_SCENE_PHASE_NONE);
    ut_check(!mp_scene_sender_offer(&sender, &note, 2u, &fake_send) && sends == 0u,
             "and none, with nothing said before, is nothing to say");

    ut_section("a scene goes out at once, and is repeated once a second");
    note = a_note(1u, MP_SCENE_PHASE_GATHERING);
    ut_check(mp_scene_sender_offer(&sender, &note, 10u, &fake_send) && sends == 1u &&
                 last_phase == MP_SCENE_PHASE_GATHERING,
             "a new scene is sent in the substep it appears");
    note.age_ms = 500u;
    for (now = 11u; now < 42u; ++now) {
        (void)mp_scene_sender_offer(&sender, &note, now, &fake_send);
    }
    ut_check(sends == 1u, "the same scene a moment older is not a change");
    ut_check(mp_scene_sender_offer(&sender, &note, 42u, &fake_send) && sends == 2u &&
                 sender.as_repeat == 1u,
             "and a second after it was sent it goes again, for whoever missed it");

    ut_section("a change waits for the throttle, never for good");
    note = a_note(1u, MP_SCENE_PHASE_RUNNING);
    ut_check(mp_scene_sender_offer(&sender, &note, 43u, &fake_send) && sends == 3u,
             "running, more than four substeps after the last change: at once");
    note = a_note(1u, MP_SCENE_PHASE_OVER);
    ut_check(!mp_scene_sender_offer(&sender, &note, 44u, &fake_send) && sender.throttled == 1u,
             "over one substep later is held back");
    ut_check(mp_scene_sender_offer(&sender, &note, 47u, &fake_send) && last_phase ==
                 MP_SCENE_PHASE_OVER && sender.longest_wait == 3u,
             "and goes out four substeps after the change before it, three after it was seen");

    ut_section("a full channel costs a substep, not the change");
    note         = a_note(1u, MP_SCENE_PHASE_NONE);
    channel_full = true;
    ut_check(!mp_scene_sender_offer(&sender, &note, 60u, &fake_send) && sender.unsent == 1u,
             "refused by the channel, counted");
    channel_full = false;
    ut_check(mp_scene_sender_offer(&sender, &note, 61u, &fake_send) &&
                 last_phase == MP_SCENE_PHASE_NONE,
             "and sent on the next offer, because it still differs from the last one sent");
    for (now = 62u; now < 200u; ++now) {
        (void)mp_scene_sender_offer(&sender, &note, now, &fake_send);
    }
    ut_checkf(last_phase == MP_SCENE_PHASE_NONE && sender.as_repeat == 1u,
              "none is said once and not repeated: %u repeat(s) in all", sender.as_repeat);

    ut_section("a note the encoder refuses is counted, not sent");
    note           = a_note(2u, MP_SCENE_PHASE_GATHERING);
    note.anchor[2] = (float)NAN;
    ut_check(!mp_scene_sender_offer(&sender, &note, 300u, &fake_send) && sender.refused == 1u,
             "an anchor that is not a number");
    mp_scene_sender_report(&sender);
    ut_check(true, "and the report runs");
}

int main(void)
{
    check_the_cadence();
    return ut_summary("the scene note's cadence");
}
