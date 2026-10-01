/* view_host_value.c: which draw distance, fog band and authored band the settings poll adopts, a
 * multiplayer host's or this machine's own, and the one table of those settings held against the
 * clamps they are applied with.
 *
 * Three places hold the same bounds: this DLL's clamps (view_settings.h), the table a host's
 * values are checked against on the wire and in the shared record (common/host_settings_note.c),
 * and the developer panel's rows, which refuse a number before writing it. None of them can include
 * another's header at run time, so this test is where they are held together.
 */
#include "unittest.h"

#include "view_host_value.h"
#include "view_settings.h"

#include "common/host_settings_note.h"
#include "mods/dev_overlay/fog_band_row.h"
#include "mods/dev_overlay/view_range_row.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static void check_the_table_against_the_clamps(void)
{
    const host_setting_key_t *keys;
    size_t                    count = 0;

    ut_section("the table a host's values are held to is this DLL's clamps and the panel's");
    keys = host_settings_keys(&count);
    ut_check(count == (size_t)HOST_SETTING_COUNT, "the table has one key per setting");
    ut_check(keys[HOST_SETTING_VIEW_RANGE_SCALE].minimum == VIEW_SETTINGS_RANGE_MIN &&
                 keys[HOST_SETTING_VIEW_RANGE_SCALE].maximum == VIEW_SETTINGS_RANGE_MAX &&
                 VIEW_RANGE_MIN == VIEW_SETTINGS_RANGE_MIN &&
                 VIEW_RANGE_MAX == VIEW_SETTINGS_RANGE_MAX,
             "the draw distance: the table, the clamp and the panel's row all say 1.0 to 2.5");
    ut_check(keys[HOST_SETTING_VIEW_RANGE_SCALE].default_value == 1.0f,
             "and a missing ViewRangeScale is 1.0 in the table as it is in the load");
    ut_check(keys[HOST_SETTING_FOG_BAND_SCALE].minimum == VIEW_SETTINGS_FOG_BAND_MIN &&
                 keys[HOST_SETTING_FOG_BAND_SCALE].maximum == VIEW_SETTINGS_FOG_BAND_MAX &&
                 FOG_BAND_MIN == VIEW_SETTINGS_FOG_BAND_MIN &&
                 FOG_BAND_MAX == VIEW_SETTINGS_FOG_BAND_MAX,
             "the fog band: the table, the clamp and the panel's row all say 0.25 to 1.0");
    ut_check(keys[HOST_SETTING_FOG_BAND_SCALE].default_value == FOG_BAND_DEFAULT,
             "and a missing FogBandScale is the panel's shipped default in the table too");
    ut_check(keys[HOST_SETTING_AUTHORED_FOG_BAND].whole_numbers &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].minimum == 0.0f &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].maximum == 1.0f &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].default_value == 0.0f,
             "the authored band is a switch that starts off, as the load reads it");
}

static void check_the_draw_distance(void)
{
    view_choice_t choice;

    ut_section("the draw distance: the host's while it names one, else this machine's own");
    choice = view_host_pick_range(false, true, 1.75f, 2.5f);
    ut_check(choice.value == 1.75f && choice.from_host, "the host's 1.75 over this machine's 2.5");
    choice = view_host_pick_range(false, false, 1.75f, 2.0f);
    ut_check(choice.value == 2.0f && !choice.from_host, "with no host named, this machine's 2.0");
    choice = view_host_pick_range(false, true, 3.0f, 2.0f);
    ut_check(choice.value == 2.0f && !choice.from_host,
             "a host's 3.0 is past the clamp and is not taken, whatever sent it");
    choice = view_host_pick_range(false, true, NAN, 1.25f);
    ut_check(choice.value == 1.25f && !choice.from_host, "nor is a NaN");
    choice = view_host_pick_range(false, false, 0.0f, 3.0f);
    ut_check(choice.value == 2.5f, "this machine's own 3.0 is clamped to 2.5, as it always was");
    choice = view_host_pick_range(false, false, 0.0f, NAN);
    ut_check(choice.value == 1.0f, "and an own value that is not a number is 1.0");

    ut_section("with no cell watchdog the draw distance stays at 1.0, the host's and the own");
    choice = view_host_pick_range(true, true, 2.0f, 2.5f);
    ut_check(choice.value == 1.0f && !choice.from_host,
             "pinned: the host's 2.0 is not taken, since nothing could catch the overflow");
    choice = view_host_pick_range(true, false, 0.0f, 2.5f);
    ut_check(choice.value == 1.0f,
             "and neither is this machine's own 2.5, which the poll would otherwise adopt a "
             "second later");
}

static void check_the_fog(void)
{
    view_choice_t choice;
    bool          from_host = false;

    ut_section("the fog band and the authored band: the host's while named, else the own");
    choice = view_host_pick_fog_band(true, 0.5f, 1.0f);
    ut_check(choice.value == 0.5f && choice.from_host, "the host's band of 0.50");
    choice = view_host_pick_fog_band(true, 0.2f, 0.75f);
    ut_check(choice.value == 0.75f && !choice.from_host, "a host's 0.20 is under the clamp");
    choice = view_host_pick_fog_band(false, 0.5f, 2.0f);
    ut_check(choice.value == 1.0f && !choice.from_host, "an own 2.0 is clamped to 1.0");

    ut_check(view_host_pick_authored(true, 1.0f, false, &from_host) && from_host,
             "the host's authored band on over this machine's off");
    ut_check(!view_host_pick_authored(true, 0.0f, true, &from_host) && from_host,
             "the host's off over this machine's on");
    ut_check(view_host_pick_authored(true, 0.5f, true, &from_host) && !from_host,
             "a host's 0.5 is no switch, and this machine's own stands");
    ut_check(!view_host_pick_authored(false, 1.0f, false, &from_host) && !from_host,
             "with nothing named, this machine's own");
    ut_check(view_host_pick_authored(true, 1.0f, false, NULL),
             "and a caller that does not ask whose it is still gets the answer");
}

/* A session does not change the client's ini. EffectiveViewRange is the one key this DLL writes
 * while the game runs, so the decision to write it is held here: a test that goes red the day
 * somebody drops the condition. */
static void check_the_ini_is_not_written_in_a_session(void)
{
    static const float JUMPS[] = { 1.5f, 2.5f, 1.0f, 1.21f, 2.0f };
    float              published = -1.0f;
    unsigned           written = 0u;
    size_t             i;

    ut_section("EffectiveViewRange is not written while the host's draw distance is in force");
    ut_check(view_host_writes_effective(false, 1.21f, &published) && published == 1.21f,
             "on this machine's own draw distance the first value is written");
    ut_check(!view_host_writes_effective(false, 1.21f, &published) &&
                 !view_host_writes_effective(false, 1.214f, &published),
             "and a value that shows the same is not written again");
    ut_check(view_host_writes_effective(false, 1.22f, &published) && published == 1.22f,
             "one that shows differently is");
    ut_check(view_host_writes_effective(false, 1.21f, &published),
             "and back to 1.21 as well");

    for (i = 0; i < sizeof JUMPS / sizeof JUMPS[0]; ++i) {
        written += view_host_writes_effective(true, JUMPS[i], &published) ? 1u : 0u;
    }
    ut_checkf(written == 0u && published < 0.0f,
              "while the host's is in force no value is written, not even across jumps (%u "
              "written)", written);
    ut_check(view_host_writes_effective(false, 1.21f, &published) && published == 1.21f,
             "the first value after the session is written, although it equals the last one "
             "written before it");
    ut_check(!view_host_writes_effective(false, 1.0f, NULL),
             "and with nowhere to keep what was written nothing is written");
    ut_check(view_host_shows_the_same(1.214f, 1.21f) && !view_host_shows_the_same(1.216f, 1.21f),
             "two values show the same when they print alike with two decimals");
}

int main(void)
{
    check_the_table_against_the_clamps();
    check_the_draw_distance();
    check_the_fog();
    check_the_ini_is_not_written_in_a_session();
    return ut_summary("view_host_value");
}
