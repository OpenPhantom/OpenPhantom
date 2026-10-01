/* overlay_host_value.h: what the host of a running session has set on this machine, as the panel
 * shows it.
 *
 * In a session the host decides four settings for every client, the draw distance, the fog band,
 * whether the fog follows the draw distance, and lightsaber dismemberment, and the client runs the
 * host's value without a byte of its own ini changing. The panel takes those rows away in a
 * session, because they are the session's; what it showed in their place was the word `session`
 * and nothing else, while the ini value the row would have read was not the one in force. So on a
 * client the taken rows read the host's value, and the note under the draw distance reads what
 * view_distance_fix says is in force after this machine's own guards.
 *
 * The values come the one way two feature DLLs may talk, a record: the multiplayer files the
 * host's values (common/host_settings_note) and view_distance_fix files what it applied. Nothing
 * here asks unless this machine is a client of a running session, as the session lock read it for
 * this picture, so the panel outside a session and on a host reads nothing it did not read before.
 */
#ifndef DEV_OVERLAY_OVERLAY_HOST_VALUE_H
#define DEV_OVERLAY_OVERLAY_HOST_VALUE_H

#include "common/host_settings_note.h"

#include <stdbool.h>
#include <stddef.h>

/* The host's value of `id`, while this machine is a client of a running session whose host named
 * it. False on a host, outside a session, and for a setting the host did not name. */
bool overlay_host_value(host_setting_id_t id, float *value);

/* The chip's words for a host's value: "host " and the number as the row formats its own, or
 * "host ON" and "host OFF" for a switch. Both answer true, so a row's hook can end in them. */
bool overlay_host_number_word(float value, void (*format)(float, char *, size_t), char *out,
                              size_t size);
bool overlay_host_switch_word(bool on, char *out, size_t size);

/* The draw distance in force on this machine while the host's value is its target: what
 * view_distance_fix filed after the frame governor and the cell watchdog, which may have lowered
 * it. False unless this machine is a client of a running session and that DLL says the host's
 * value is the one it applies; the note then reads this machine's own ini as it always did, since
 * the DLL does not write the key while the host's value holds. */
bool overlay_host_view_range_in_force(float *value);

#endif /* DEV_OVERLAY_OVERLAY_HOST_VALUE_H */
