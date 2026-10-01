/* mp_scene_send.c: when the host's scene note goes out. See the header. */
#include "mp_scene_send.h"

#include "mp_level_state_rule.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Milliseconds a substep, for the report. */
#define MS_A_SUBSTEP 31.25f

bool mp_scene_sender_offer(mp_scene_sender_t *sender, const mp_scene_note_t *note,
                           uint32_t substep, mp_scene_send_fn_t send)
{
    uint8_t         bytes_out[MP_SCENE_NOTE_MAX_BYTES];
    size_t          bytes;
    bool            changed;
    mp_level_send_t due;

    if (sender == NULL || note == NULL || send == NULL || note->serial == 0u) {
        return false;
    }
    bytes = mp_scene_note_encode(note, bytes_out, sizeof bytes_out);
    if (bytes == 0u) {
        ++sender->refused;
        return false;
    }
    changed = !mp_scene_note_same(bytes_out, bytes, sender->last_note, sender->last_bytes);
    /* None is said once, as the change it is, and then nothing until the next scene; and a world
     * that never had a scene says nothing at all. */
    if (note->phase == (uint8_t)MP_SCENE_PHASE_NONE && (!changed || !sender->sent_before)) {
        return false;
    }
    if (changed && !sender->change_waiting) {
        sender->change_waiting = true;
        sender->change_seen    = substep;
    }
    due = mp_level_state_due(changed, sender->sent_before, substep - sender->last_sent,
                             sender->change_before, substep - sender->last_change);
    if (due == MP_LEVEL_SEND_NONE) {
        return false;
    }
    if (due == MP_LEVEL_SEND_HELD) {
        ++sender->throttled;
        return false;
    }
    if (!send(bytes_out, bytes)) {
        ++sender->unsent;
        return false;
    }
    ++sender->sent;
    if (due == MP_LEVEL_SEND_CHANGE) {
        ++sender->on_change;
        sender->change_before = true;
        sender->last_change   = substep;
        if (sender->change_waiting && substep - sender->change_seen > sender->longest_wait) {
            sender->longest_wait = substep - sender->change_seen;
        }
        sender->change_waiting = false;
    } else {
        ++sender->as_repeat;
    }
    sender->sent_before = true;
    sender->last_sent   = substep;
    memcpy(sender->last_note, bytes_out, bytes);
    sender->last_bytes = bytes;
    return true;
}

void mp_scene_sender_report(const mp_scene_sender_t *sender)
{
    if (sender == NULL) {
        return;
    }
    log_info("  the scene note (the host): %u sent (%u on a change, %u as a repeat), %u unsent for "
             "a full channel, %u held back by the throttle, %u the encoder refused; the longest a "
             "change waited %u ms before it went out", (unsigned)sender->sent,
             (unsigned)sender->on_change, (unsigned)sender->as_repeat, (unsigned)sender->unsent,
             (unsigned)sender->throttled, (unsigned)sender->refused,
             (unsigned)((float)sender->longest_wait * MS_A_SUBSTEP));
}
