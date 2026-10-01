/* mp_scene_send.h: when the host's scene note goes out.
 *
 * Layer 1, pure: the note is handed in built, and a send is handed in as a function. The host's
 * scene is a state, so the note repeats it: at once on every change, at most once in four
 * substeps with the newest winning, once a second while the scene is anything but none, and none
 * itself once, as the change it is. The same cadence as the level's state (mp_level_state_due),
 * asked of the same rule. A note the channel had no room for is not kept: the next offer builds it
 * again from the scene as it stands, and the difference to the last one sent makes it go.
 */
#ifndef MULTIPLAYER_MP_SCENE_SEND_H
#define MULTIPLAYER_MP_SCENE_SEND_H

#include "mp_scene_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The send every reliable message of the bridge goes through. False when the channel had no
 * room. */
typedef bool (*mp_scene_send_fn_t)(const uint8_t *bytes, size_t count);

typedef struct mp_scene_sender {
    bool     sent_before;
    uint32_t last_sent;
    bool     change_before;
    uint32_t last_change;
    bool     change_waiting;
    uint32_t change_seen;
    uint8_t  last_note[MP_SCENE_NOTE_MAX_BYTES];
    size_t   last_bytes;

    uint32_t sent;
    uint32_t on_change;
    uint32_t as_repeat;
    uint32_t unsent;
    uint32_t throttled;
    uint32_t refused;        /* a note the encoder would not write */
    uint32_t longest_wait;   /* substeps a change waited before it went out */
} mp_scene_sender_t;

/* One offer, once a substep. Answers whether a note went out. A scene numbered nought, which is no
 * scene yet, is never sent. */
bool mp_scene_sender_offer(mp_scene_sender_t *sender, const mp_scene_note_t *note,
                           uint32_t substep, mp_scene_send_fn_t send);

/* The report line, `the scene note (the host):`. */
void mp_scene_sender_report(const mp_scene_sender_t *sender);

#endif /* MULTIPLAYER_MP_SCENE_SEND_H */
