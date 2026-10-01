/* view_host_value.h: which draw distance, fog band and authored band the settings poll adopts, the
 * host's or this machine's own.
 *
 * In a multiplayer session the host's values reach this DLL through common/host_settings_note and
 * are held in memory only: engine_fixes.ini keeps this machine's own values for the whole session,
 * and they are in force again the moment the session is over. The choice is pure so a test can
 * hold it, and it sits apart from view_settings.c because that file pulls in the frame governor,
 * the cell watchdog and the fog.
 */
#ifndef VIEW_HOST_VALUE_H
#define VIEW_HOST_VALUE_H

#include <stdbool.h>

typedef struct view_choice {
    float value;
    bool  from_host;
} view_choice_t;

/* The draw distance. 1.0 while the range is pinned, which is what a missing cell watchdog leaves
 * behind: nothing could catch a draw table overflow then, so neither this machine's own value nor
 * the host's is taken. Otherwise the host's value when the host named one inside the clamp, and
 * this machine's own, clamped, in every other case. */
view_choice_t view_host_pick_range(bool pinned, bool host_named, float host_value,
                                   float own_value);

/* The fog band: the host's value when named and inside the clamp, else this machine's own,
 * clamped. */
view_choice_t view_host_pick_fog_band(bool host_named, float host_value, float own_value);

/* The authored band, a switch: the host's value when named and exactly 0 or 1, else this
 * machine's own. `from_host` says which. */
bool view_host_pick_authored(bool host_named, float host_value, bool own_value, bool *from_host);

/* Whether two draw distances read the same where they are shown, on the panel's note and in the
 * ini, both with two decimals: closer than half a hundredth either way. */
bool view_host_shows_the_same(float scale, float shown);

/* Whether the draw distance in force, `scale`, is written to EffectiveViewRange now. `published` is
 * the value written last, or a negative one when the next has to be written whatever it is.
 *
 * While the host's draw distance is in force nothing is written, because the number would be the
 * host's, written into this machine's file, which is exactly what a session must not do to a
 * client's ini. `published` is set negative then, so the first value after the session goes to the
 * file even when it equals the last one written before the session. Otherwise a value is written
 * when it shows differently from the last. */
bool view_host_writes_effective(bool host_range_in_force, float scale, float *published);

#endif /* VIEW_HOST_VALUE_H */
