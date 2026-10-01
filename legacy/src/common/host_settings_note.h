/* common/host_settings_note.h: the host's world settings, handed to the feature DLLs of a client
 * for the length of a multiplayer session, without a byte of that client's engine_fixes.ini
 * changing.
 *
 * A setting such as the draw distance belongs to one feature DLL, and in a session the host's value
 * has to be the one in force on every machine. Feature DLLs may not call each other, and writing
 * the host's value into the client's ini would outlive the session and overwrite the player's own
 * choice. So the value travels the way Source hands a replicated console variable to its clients:
 * held in memory for the connection, never written to the client's configuration, and only for the
 * keys a table admits. The table is written once, below, and both the wire codec of the multiplayer
 * and every consumer check against it.
 *
 * Two kinds of record are filed through common/shared_note, each with exactly one writer:
 *
 *   host_settings           written by the multiplayer of a CLIENT: whether a session it is a
 *                           client of runs, and the values the host named. A host never
 *                           publishes it; its own ini is what it sends.
 *   host_taken_<mod>        written by one consumer DLL each: which of the host's values it applies
 *                           right now, and the value in force after this machine's own guards (the
 *                           frame governor may lower a draw distance on a slow machine, and that is
 *                           wanted: the host sets the target, the machine protects itself).
 *
 * STATE, NOT EVENTS. Both records are rewritten whole. A consumer asks in the poll it already has
 * and takes the host's value while `running` says so; when the record says the session is over, or
 * when there is no record at all, it goes back to its own ini value, which was never touched. The
 * one exit of the session publishes `running = false`, so every way out ends up in the same place.
 *
 * No note at all is what a machine without a multiplayer session has, and it reads exactly like a
 * session that is over. A record of another shape is treated the same way: here a wrong value only
 * costs picture, so the safe answer is the player's own setting.
 */
#ifndef COMMON_HOST_SETTINGS_NOTE_H
#define COMMON_HOST_SETTINGS_NOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The name the multiplayer files the host's values under. */
#define HOST_SETTINGS_NOTE_NAME "host_settings"

/* A consumer's acknowledgement is filed under this prefix followed by the consumer's own name, the
 * same word as its directory, its DLL and its ini section: "host_taken_view_distance_fix". The
 * channel admits only letters, digits and underscores in a name, and at most SHARED_NOTE_NAME_MAX
 * characters, which the longest consumer name here stays inside. */
#define HOST_SETTINGS_TAKEN_PREFIX "host_taken_"

/* How long a reader that found no record waits before it asks the channel again. The same patience
 * the session note has, so a machine without a session costs one lookup a second at most. */
#define HOST_SETTINGS_RETRY_MS 1000u

/* The settings a host may decide for its clients. Only keys that their DLL reads again while the
 * game runs are here, because a value that only takes effect at the next start cannot be the host's
 * for a session. Appending is fine; the order is part of the wire, so an entry is never moved or
 * reused. */
typedef enum host_setting_id {
    HOST_SETTING_VIEW_RANGE_SCALE = 0,   /* [view_distance_fix] ViewRangeScale */
    HOST_SETTING_FOG_BAND_SCALE,         /* [view_distance_fix] FogBandScale */
    HOST_SETTING_AUTHORED_FOG_BAND,      /* [view_distance_fix] AuthoredFogBand, 0 or 1 */
    HOST_SETTING_DISMEMBERMENT_MODE,     /* [dismemberment] Mode */
    HOST_SETTING_COUNT
} host_setting_id_t;

/* One admitted key: where it lives in the ini, the range its own DLL clamps it to, and the value
 * that DLL uses when the key is absent. Range and default are the consumer's, copied here so that
 * the wire can refuse what the consumer would have clamped and a host reads its own ini exactly as
 * the consumer does; the consumer's own unit test keeps the two against each other. A key whose
 * consumer takes only whole numbers (a switch, a mode) is marked, and the codec refuses a fraction
 * for it. */
typedef struct host_setting_key {
    const char *section;
    const char *key;
    float       minimum;
    float       maximum;
    float       default_value;
    bool        whole_numbers;
} host_setting_key_t;

/* The table, in host_setting_id_t order. */
const host_setting_key_t *host_settings_keys(size_t *count);

/* Whether `value` is one the table admits for `id`: finite and inside the range. */
bool host_settings_value_is_admitted(host_setting_id_t id, float value);

typedef struct host_settings {
    bool     running;      /* a session this machine is a client of has started and not ended */
    uint8_t  generation;   /* grows whenever a value or `present` changes, so a consumer can log a
                            * change once instead of once per poll */
    uint16_t present;      /* bit n: the host named setting n; a setting it did not name stays the
                            * client's own */
    float    values[HOST_SETTING_COUNT];
    uint32_t published;    /* bumped at every publication */
} host_settings_t;

/* Files the record. False when a present value is not admitted, when `present` names a setting that
 * does not exist, when a record that does not run names anything, or when the channel refused;
 * nothing is published then and readers go on seeing the record before it. */
bool host_settings_publish(const host_settings_t *settings);

/* Reads the record back. False when nobody has published it, when it is torn, or when it holds
 * anything the publisher would have refused; `out` is left alone then. */
bool host_settings_read(host_settings_t *out);

/* The question a consumer asks in its poll: true, with the host's value in `value`, while a session
 * runs and the host named `id`. False in every other case, and the caller then uses its own ini
 * value. Asks the channel at most once per HOST_SETTINGS_RETRY_MS while nothing has been published,
 * so a machine that never joins a session pays nothing measurable. `now_ms` is GetTickCount's. */
bool host_settings_value(host_setting_id_t id, float *value, uint32_t now_ms);

typedef struct host_settings_taken {
    uint16_t in_force;                        /* bit n: the host's value of setting n is what this
                                               * DLL applies now */
    uint8_t  generation;                      /* the host_settings generation it acted on */
    float    effective[HOST_SETTING_COUNT];   /* what is in force after this machine's own guards,
                                               * for the settings in `in_force`; 0 for the rest */
    uint32_t published;
} host_settings_taken_t;

/* Files `taken` under HOST_SETTINGS_TAKEN_PREFIX followed by `mod`. False for a name that does not
 * fit the channel's name limit, for bits that name no setting, for a non-finite effective value, or
 * when the channel refused. */
bool host_settings_publish_taken(const char *mod, const host_settings_taken_t *taken);

/* Reads one consumer's acknowledgement back, with the same refusals as host_settings_read. */
bool host_settings_read_taken(const char *mod, host_settings_taken_t *out);

/* The size is the shape a reader recognises a record by: a record of another size is one of another
 * shape and is refused whole. Growing HOST_SETTING_COUNT changes both, on purpose. */
_Static_assert(sizeof(host_settings_t) == 8u + 4u * HOST_SETTING_COUNT,
               "host_settings_t is read back by its size");
_Static_assert(sizeof(host_settings_taken_t) == 8u + 4u * HOST_SETTING_COUNT,
               "host_settings_taken_t is read back by its size");

#endif /* COMMON_HOST_SETTINGS_NOTE_H */
