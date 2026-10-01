/* overlay_host_value.c: see overlay_host_value.h. */
#include "overlay_host_value.h"

#include "session_lock.h"

#include "common/host_settings_note.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>

/* The acknowledgement of the one consumer whose applied value the panel shows. */
#define VIEW_DISTANCE_MOD "view_distance_fix"

/* A client of a running session, as the session lock read it for this picture. The lock is the one
 * reader of the multiplayer's session note in the panel, and the rows this module speaks for are
 * taken by it on exactly this reading, so the words and the lock cannot come apart. A host decides
 * its own values and outside a session there is no host, and in both cases nothing below is asked,
 * so a machine that never plays together reads no record at all. */
static bool a_client_in_a_session(void)
{
    return session_lock_running() && !session_lock_is_host();
}

bool overlay_host_value(host_setting_id_t id, float *value)
{
    return value != NULL && a_client_in_a_session() &&
           host_settings_value(id, value, (uint32_t)GetTickCount());
}

bool overlay_host_number_word(float value, void (*format)(float, char *, size_t), char *out,
                              size_t size)
{
    char number[16];

    if (out == NULL || size == 0u) {
        return false;
    }
    number[0] = '\0';
    if (format != NULL) {
        format(value, number, sizeof number);
        number[sizeof number - 1u] = '\0';
    }
    text_format(out, size, "host %s", number);
    return true;
}

bool overlay_host_switch_word(bool on, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return false;
    }
    text_format(out, size, "host %s", on ? "ON" : "OFF");
    return true;
}

bool overlay_host_view_range_in_force(float *value)
{
    host_settings_taken_t taken;

    if (value == NULL || !a_client_in_a_session() ||
        !host_settings_read_taken(VIEW_DISTANCE_MOD, &taken) ||
        (taken.in_force & (1u << HOST_SETTING_VIEW_RANGE_SCALE)) == 0u) {
        return false;
    }
    *value = taken.effective[HOST_SETTING_VIEW_RANGE_SCALE];
    return true;
}
