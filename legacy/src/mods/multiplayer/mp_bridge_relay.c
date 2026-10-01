/* mp_bridge_relay.c: a listen host passes its clients' moments on. See the header. */
#include "mp_bridge_relay.h"

#include "mp_bridge_far.h"
#include "mp_events.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Module state because a process holds one listen host, and the report reads it once at the end. */
static uint32_t copies_passed;
static uint32_t copies_refused;

void mp_bridge_relay_pass_on(mp_session_t *host, size_t from, uint32_t tick, const uint8_t *note,
                             size_t bytes)
{
    uint8_t copy[MP_EVENT_MAX_BYTES];
    size_t  to;

    if (host == NULL || note == NULL || bytes > sizeof copy) {
        return;
    }
    memcpy(copy, note, bytes);
    if (!mp_event_restamp(copy, bytes, mp_session_slot_of_peer(from), tick)) {
        return;   /* not one of a player's moments: the host answers those, it does not relay */
    }
    for (to = 0; to < MP_SESSION_MAX_PEERS; ++to) {
        const mp_peer_t *peer = mp_session_peer(host, to);

        if (to == from || peer == NULL || peer->state != MP_PEER_CONNECTED) {
            continue;
        }
        /* A moment passed on has nobody to repeat it: it is sent or held, never dropped. */
        if (mp_session_send_or_hold(host, to, copy, bytes)) {
            ++copies_passed;
            mp_bridge_far_note_passed_on(mp_session_slot_of_peer(to), tick);
        } else {
            ++copies_refused;
        }
    }
}

void mp_bridge_relay_counts(uint32_t *passed, uint32_t *refused)
{
    if (passed != NULL) {
        *passed = copies_passed;
    }
    if (refused != NULL) {
        *refused = copies_refused;
    }
}
