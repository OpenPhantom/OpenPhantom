/* mp_events_moments.c: the moments of a body, as the four questions a host and a server ask.
 *
 * A moment a client sends goes through four gates before another player sees it: is it an event
 * at all, may a player say it, does a server pass it between players, and does the host restamp it
 * with its sender's slot on the way. The four are four lists in two files, and a tag added to one
 * and not the others fails without a word: the listen host refuses it as a note a player may not
 * say, or passes it on under the wrong body. So every moment tag is asked all four here.
 *
 * And a player's own sound, 0xA8, the newest of them, as a codec.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_snapshot.h"
#include "mp_trust.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct moment {
    uint8_t     kind;
    bool        names_slot;
    const char *name;
} moment_t;

static const moment_t MOMENTS[] = {
    { MP_EVENT_SHOT,         true,  "a shot"               },
    { MP_EVENT_PUSH,         true,  "a push"               },
    { MP_EVENT_SABRE,        true,  "a sabre action"       },
    { MP_EVENT_WEAPON,       true,  "a weapon change"      },
    { MP_EVENT_PLAYER_SOUND, true,  "a player's own sound" },
    { MP_EVENT_MOVER,        false, "a mover"              },
};

static size_t encode_moment(uint8_t kind, uint8_t *buffer, size_t capacity)
{
    mp_event_t e;

    memset(&e, 0, sizeof e);
    e.kind        = kind;
    e.tick        = 1234u;
    e.source_slot = 2u;
    e.sound_what  = MP_PLAYER_SOUND_BURN;
    e.sound_index = 3u;
    e.mover_id    = 17u;
    e.mover_mode  = MP_EVENT_MOVER_OPEN;
    return mp_event_encode(&e, buffer, capacity);
}

static void check_the_four_questions(void)
{
    size_t i;

    ut_section("every moment tag is asked all four questions, and answers yes to each");
    for (i = 0; i < sizeof MOMENTS / sizeof MOMENTS[0]; ++i) {
        const moment_t *m = &MOMENTS[i];
        uint8_t         note[MP_EVENT_MAX_BYTES];
        size_t          bytes = encode_moment(m->kind, note, sizeof note);
        mp_event_t      back;

        ut_checkf(bytes != 0u && mp_event_is_event(note, bytes), "%s is an event", m->name);
        ut_checkf(mp_trust_player_may_say(note, bytes), "%s is a player's to say", m->name);
        ut_checkf(mp_trust_passes_between_players(note, bytes),
                  "%s is passed on between players by a server", m->name);
        ut_checkf(mp_event_restamp(note, bytes, 5u, 777u) && mp_event_decode(note, bytes, &back) &&
                      back.tick == 777u && (!m->names_slot || back.source_slot == 5u),
                  "%s is restamped by the host that passes it on, with %s", m->name,
                  m->names_slot ? "its sender's slot" : "no slot, since it names none");
    }
}

static void check_the_player_sound(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;
    uint8_t    what;
    unsigned   trips = 0;

    ut_section("a player's own sound, 0xA8, as a codec");
    ut_check(MP_EVENT_PLAYER_SOUND_BYTES == 9u,
             "nine bytes: the tag, the tick, the slot, what it was, the sound and the flags");
    for (what = 0u; what <= MP_PLAYER_SOUND_KIND_MAX; ++what) {
        memset(&sent, 0, sizeof sent);
        sent.kind        = MP_EVENT_PLAYER_SOUND;
        sent.tick        = 0x01020304u + what;
        sent.source_slot = (uint8_t)(what % MP_SNAPSHOT_MAX_BODIES);
        sent.sound_what  = what;
        sent.sound_index = (uint8_t)(52u + what);
        sent.sound_flags = (uint8_t)(0xF0u | what);
        bytes = mp_event_encode(&sent, buffer, sizeof buffer);
        if (bytes == MP_EVENT_PLAYER_SOUND_BYTES && mp_event_decode(buffer, bytes, &got) &&
            got.kind == MP_EVENT_PLAYER_SOUND && got.tick == sent.tick &&
            got.source_slot == sent.source_slot && got.sound_what == what &&
            got.sound_index == sent.sound_index && got.sound_flags == sent.sound_flags) {
            ++trips;
        }
    }
    ut_checkf(trips == MP_PLAYER_SOUND_KIND_MAX + 1u,
              "the death cry, the burning cry, a pickup, a key, the water, the burning ground and "
              "the shield's rise and end all come back whole, flags included (%u)", trips);

    sent.sound_what = MP_PLAYER_SOUND_KIND_MAX + 1u;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "a kind of sound past the eight names no table and is not written");
    sent.sound_what  = MP_PLAYER_SOUND_DEATH;
    sent.source_slot = MP_SNAPSHOT_MAX_BODIES;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "and neither is a slot past the snapshot's table");
    sent.source_slot = 1u;
    bytes            = mp_event_encode(&sent, buffer, sizeof buffer);
    buffer[6]        = (uint8_t)(MP_PLAYER_SOUND_KIND_MAX + 1u);
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "a torn kind of sound is refused on the way in");
    ut_check(!mp_event_is_event(buffer, bytes - 1u) && !mp_event_is_event(buffer, bytes + 1u),
             "and a length other than nine is not this message");
}

int main(void)
{
    check_the_four_questions();
    check_the_player_sound();
    return ut_summary("mp_events_moments");
}
