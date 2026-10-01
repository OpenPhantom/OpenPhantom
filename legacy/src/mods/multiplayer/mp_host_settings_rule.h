/* mp_host_settings_rule.h: the note that carries the host's world settings to every client, and
 * what the host reads out of its own configuration before it says them.
 *
 * Layer 1, pure. The note is a state: the host sends it in front of every setup note, so it is
 * repeated once a second and goes out again at every choice, start, world change and ending, and a
 * younger copy replaces an older one on the channel. It names which settings of the table in
 * common/host_settings_note the host decided (those whose mod is loaded in the host's process),
 * their values in hundredths, and two bits for the host's cheats that change what a client replays
 * of the host's world: happy, which sends every kind 7 bolt half as fast again, and evil force,
 * which turns a kind 11 bolt into a kind 18. Source hands its clients a replicated console
 * variable the same way, and Quake 3 its system info: held for the connection, never written to
 * the client's configuration, and only for the keys a table admits.
 *
 * Bounds are checked both ways against the one table: the encoder refuses a value the table does
 * not admit, and the decoder refuses whatever the encoder would not have written. That is a value
 * outside its range, a fraction for a switch or a mode, a value for a setting the host did not
 * name, a bit for a setting or a cheat this build does not know, and a note of another length or
 * version.
 */
#ifndef MULTIPLAYER_MP_HOST_SETTINGS_RULE_H
#define MULTIPLAYER_MP_HOST_SETTINGS_RULE_H

#include "common/host_settings_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The note's first byte. The name ends in _TAG like every other tag of the wire, so a search for
 * the suffix finds them all. */
#define MP_HOST_SETTINGS_TAG 0xABu

/* Raised when the layout below changes; a note of another version is refused whole. */
#define MP_HOST_SETTINGS_VERSION 1u

/* tag, version, the cheat bits, the present bits (two bytes, little endian), then one value in
 * hundredths per setting of the table, in id order, two bytes each. */
#define MP_HOST_SETTINGS_HEAD_BYTES 5u
#define MP_HOST_SETTINGS_BYTES (MP_HOST_SETTINGS_HEAD_BYTES + 2u * (unsigned)HOST_SETTING_COUNT)

/* The host's two cheat switches whose effect a client replays: its cell of each is not 0. */
#define MP_HOST_SETTINGS_CHEAT_HAPPY      0x01u
#define MP_HOST_SETTINGS_CHEAT_EVIL_FORCE 0x02u
#define MP_HOST_SETTINGS_CHEATS_KNOWN     0x03u

typedef struct mp_host_settings_note {
    uint8_t  cheats;                        /* MP_HOST_SETTINGS_CHEAT_* */
    uint16_t present;                       /* bit n: the host named host_setting_id_t n */
    float    values[HOST_SETTING_COUNT];    /* 0 for a setting the host did not name */
} mp_host_settings_note_t;

/* What the decoder made of a note, so that a refusal is counted by its reason. */
typedef enum mp_host_settings_verdict {
    MP_HOST_SETTINGS_TAKEN = 0,
    MP_HOST_SETTINGS_NOT_THIS_NOTE,   /* another tag: a note for somebody else to read */
    MP_HOST_SETTINGS_WRONG_SHAPE,     /* the tag, but another length or version */
    MP_HOST_SETTINGS_UNKNOWN_BIT,     /* a setting or a cheat this build does not know */
    MP_HOST_SETTINGS_OUT_OF_RANGE     /* a value no encoder here would have written */
} mp_host_settings_verdict_t;

/* Writes the note into `out`. Returns MP_HOST_SETTINGS_BYTES, or 0 when `capacity` is short or
 * when the note says something the decoder would refuse; nothing half formed is written then. */
size_t mp_host_settings_encode(const mp_host_settings_note_t *note, uint8_t *out,
                               size_t capacity);

/* Reads a note. `out` is written only when the verdict is MP_HOST_SETTINGS_TAKEN. */
mp_host_settings_verdict_t mp_host_settings_decode(const uint8_t *in, size_t bytes,
                                                   mp_host_settings_note_t *out);

/* The value the host's own mod runs for setting `id` when its ini holds `raw`, which is what the
 * host says: a host with ViewRangeScale=3.0 draws at 2.5 and says 2.5, not a number every client
 * would refuse. A missing or unreadable number is the key's default. A key of whole numbers is read
 * by its mod as an integer, which stops at the decimal point; a switch (the range 0 to 1) is on for
 * any value but 0, as the mod's own boolean read has it, and a mode outside its range is the
 * default, as the mod's own load has it. Any other key is clamped to its range. */
float mp_host_settings_own_value(host_setting_id_t id, float raw);

#endif /* MULTIPLAYER_MP_HOST_SETTINGS_RULE_H */
