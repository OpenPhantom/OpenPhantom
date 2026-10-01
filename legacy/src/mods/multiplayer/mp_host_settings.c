/* mp_host_settings.c: the host's world settings, said by the host and held by each client. See the
 * header.
 */
#include "mp_host_settings.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_cells.h"
#include "mp_host_settings_rule.h"
#include "mp_session.h"
#include "mp_session_now.h"
#include "mp_wallclock.h"

#include "common/host_settings_note.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* How often the host reads its own ini at most. The setup note goes out once a second and at every
 * choice, so a burst of choices in the menu reads the file once. */
#define OWN_READ_MS 1000u

/* How long a publication the channel refused waits before it is tried again, so a channel that
 * cannot open its mapping is not asked from every pump. */
#define PUBLISH_RETRY_MS 1000u

/* Room for one line of the log about all four settings. */
#define LINE_BYTES 512u

/* The words each setting has in the log, in id order. */
static const char *const LABELS[HOST_SETTING_COUNT] = {
    "draw distance", "fog band", "authored fog band", "dismemberment mode",
};

/* Why the host's values stopped being in force here. */
typedef enum withdrawn_by {
    WITHDRAWN_NEVER = 0,
    WITHDRAWN_TRANSPORT,   /* the one exit took the transport down */
    WITHDRAWN_SESSION,     /* the setup note no longer says a started session of this client's */
    WITHDRAWN_HOST_GONE    /* the connection to the host is gone or is another one */
} withdrawn_by_t;

typedef struct host_settings_state {
    /* The host's. */
    bool                    own_known;
    uint32_t                own_read_ms;
    mp_host_settings_note_t own;          /* the ini's part, read at most once a second */
    bool                    said_known;
    mp_host_settings_note_t said_last;    /* what the last note said, the cheats included */
    uint32_t                said;
    uint32_t                said_to_nobody;
    uint32_t                unencoded;    /* the host's own values that would not encode */

    /* The client's. */
    bool                    heard_known;
    uint64_t                heard_connection;   /* the connection the note came on */
    mp_host_settings_note_t heard;
    uint32_t                taken;
    uint32_t                refused_shape;
    uint32_t                refused_bit;
    uint32_t                refused_range;
    /* Heard by a host or the loopback, and kept for nobody. */
    uint32_t                not_a_client;
    bool                    refusal_said;

    host_settings_t         filed;              /* the record as last published; zero before */
    uint32_t                publications;
    uint32_t                publish_refused;
    bool                    retry_waits;
    uint32_t                refused_at_ms;
    withdrawn_by_t          withdrawn_by;
    uint32_t                withdrawals;
} host_settings_state_t;

static host_settings_state_t hs;

/* ============================================================================================ */
/* A line of the log built from pieces. It stops at its room rather than writing past it. */
typedef struct line {
    char   text[LINE_BYTES];
    size_t used;
} line_t;

static void line_append(line_t *line, const char *text)
{
    size_t length = strlen(text);

    if (line->used + length >= sizeof line->text) {
        length = sizeof line->text - 1u - line->used;
    }
    memcpy(line->text + line->used, text, length);
    line->used += length;
    line->text[line->used] = '\0';
}

/* "draw distance 1.50", or "dismemberment mode 2" for a setting of whole numbers. */
static void line_value(line_t *line, size_t id, float value)
{
    char piece[64];

    if (host_settings_keys(NULL)[id].whole_numbers) {
        text_format(piece, sizeof piece, "%s %.0f", LABELS[id], (double)value);
    } else {
        text_format(piece, sizeof piece, "%s %.2f", LABELS[id], (double)value);
    }
    line_append(line, piece);
}

/* ============================================================================================ */
/* The host. */

static bool mod_is_loaded(const char *section)
{
    char name[64];

    text_format(name, sizeof name, "%s.dll", section);
    return GetModuleHandleA(name) != NULL;
}

/* The values the host's own mods run: its ini, read the way each mod reads it, and only for the
 * mods loaded in this process. A setting whose mod is not loaded here is not the host's to decide,
 * because the host itself does not run it, and a client keeps its own. */
static void read_own(uint32_t now_ms)
{
    const host_setting_key_t *keys;
    size_t                    count = 0;
    size_t                    id;

    if (hs.own_known && now_ms - hs.own_read_ms < OWN_READ_MS) {
        return;
    }
    hs.own_known   = true;
    hs.own_read_ms = now_ms;
    memset(&hs.own, 0, sizeof hs.own);
    keys = host_settings_keys(&count);
    for (id = 0; id < count; ++id) {
        if (!mod_is_loaded(keys[id].section)) {
            continue;
        }
        hs.own.values[id] = mp_host_settings_own_value(
            (host_setting_id_t)id,
            ini_read_float(keys[id].section, keys[id].key, keys[id].default_value));
        hs.own.present = (uint16_t)(hs.own.present | (1u << id));
    }
}

/* A cheat cell of this machine's, on when it holds anything but 0: the engine toggles each with
 * an exclusive or of 1 and tests it for not zero. */
static bool cell_is_on(mp_cell_t cell)
{
    uintptr_t address = mp_cells_address(cell);
    uint32_t  value   = 0u;

    return address != 0u && memory_try_read_u32(address, &value) && value != 0u;
}

static bool notes_agree(const mp_host_settings_note_t *a, const mp_host_settings_note_t *b)
{
    size_t id;

    if (a->cheats != b->cheats || a->present != b->present) {
        return false;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if (a->values[id] != b->values[id]) {
            return false;
        }
    }
    return true;
}

/* Said when what the host says changes, which is its first note and every change after. */
static void say_what_the_host_says(const mp_host_settings_note_t *note)
{
    const host_setting_key_t *keys = host_settings_keys(NULL);
    line_t                    line;
    size_t                    id;
    char                      piece[96];

    memset(&line, 0, sizeof line);
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        line_append(&line, id == 0u ? "" : ", ");
        if ((note->present & (1u << id)) != 0u) {
            line_value(&line, id, note->values[id]);
            continue;
        }
        text_format(piece, sizeof piece, "%s not said (%s is not loaded here)", LABELS[id],
                    keys[id].section);
        line_append(&line, piece);
    }
    log_info("the host's settings said to the clients: %s; cheats happy %u and evil force %u",
             line.text, (unsigned)((note->cheats & MP_HOST_SETTINGS_CHEAT_HAPPY) != 0u),
             (unsigned)((note->cheats & MP_HOST_SETTINGS_CHEAT_EVIL_FORCE) != 0u));
}

void mp_host_settings_send(mp_session_t *host)
{
    mp_host_settings_note_t note;
    uint8_t                 bytes[MP_HOST_SETTINGS_BYTES];

    if (host == NULL || mp_bridge_drain_is_client()) {
        return;
    }
    read_own(mp_wallclock_ms());
    note        = hs.own;
    note.cheats = (uint8_t)((cell_is_on(MP_CELL_CHEAT_HAPPY) ? MP_HOST_SETTINGS_CHEAT_HAPPY : 0u) |
                            (cell_is_on(MP_CELL_CHEAT_EVIL_FORCE)
                                 ? MP_HOST_SETTINGS_CHEAT_EVIL_FORCE : 0u));
    if (mp_host_settings_encode(&note, bytes, sizeof bytes) != sizeof bytes) {
        ++hs.unencoded;
        return;
    }
    ++hs.said;
    if (mp_session_broadcast_reliable(host, bytes, sizeof bytes) == 0u) {
        ++hs.said_to_nobody;
    }
    if (!hs.said_known || !notes_agree(&note, &hs.said_last)) {
        hs.said_known = true;
        hs.said_last  = note;
        say_what_the_host_says(&note);
    }
}

/* ============================================================================================ */
/* The client. */

bool mp_host_settings_take(bool is_client, const uint8_t *note, size_t bytes)
{
    mp_host_settings_note_t    heard;
    mp_host_settings_verdict_t verdict;

    if (note == NULL || bytes == 0u || note[0] != (uint8_t)MP_HOST_SETTINGS_TAG) {
        return false;
    }
    if (!is_client) {
        ++hs.not_a_client;
        return true;
    }
    verdict = mp_host_settings_decode(note, bytes, &heard);
    if (verdict != MP_HOST_SETTINGS_TAKEN) {
        hs.refused_shape += verdict == MP_HOST_SETTINGS_WRONG_SHAPE ? 1u : 0u;
        hs.refused_bit   += verdict == MP_HOST_SETTINGS_UNKNOWN_BIT ? 1u : 0u;
        hs.refused_range += verdict == MP_HOST_SETTINGS_OUT_OF_RANGE ? 1u : 0u;
        if (!hs.refusal_said) {
            hs.refusal_said = true;
            log_warning("a note of the host's settings was refused (verdict %d, %u byte(s)): "
                        "the mods of this machine keep their own values until one is taken",
                        (int)verdict, (unsigned)bytes);
        }
        return true;
    }
    ++hs.taken;
    hs.heard            = heard;
    hs.heard_known      = true;
    hs.heard_connection = mp_bridge_drain_host_connection();
    return true;
}

static bool heard_on(uint64_t connection)
{
    return hs.heard_known && connection != 0u && hs.heard_connection == connection;
}

bool mp_host_settings_cheats(uint8_t *cheats)
{
    if (cheats == NULL || !heard_on(mp_bridge_drain_host_connection())) {
        return false;
    }
    *cheats = hs.heard.cheats;
    return true;
}

static bool records_agree(const host_settings_t *a, const host_settings_t *b)
{
    size_t id;

    if (a->running != b->running || a->present != b->present) {
        return false;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((a->present & (1u << id)) != 0u && a->values[id] != b->values[id]) {
            return false;
        }
    }
    return true;
}

/* The record, with a generation that moved and a publication count; kept as filed only when the
 * channel took it. */
static bool file_record(const host_settings_t *wanted)
{
    host_settings_t record = *wanted;

    record.generation = (uint8_t)(hs.filed.generation + 1u);
    record.published  = hs.publications + 1u;
    if (!host_settings_publish(&record)) {
        ++hs.publish_refused;
        return false;
    }
    hs.publications = record.published;
    hs.filed        = record;
    return true;
}

static const char *withdrawn_words(withdrawn_by_t why)
{
    switch (why) {
    case WITHDRAWN_TRANSPORT: return "the transport came down";
    case WITHDRAWN_SESSION:   return "the session is no longer one this machine is a client of";
    case WITHDRAWN_HOST_GONE: return "the connection to the host is gone";
    default:                  return "never withdrawn";
    }
}

static void say_withdrawn(withdrawn_by_t why)
{
    hs.withdrawn_by = why;
    ++hs.withdrawals;
    log_info("the host's settings are withdrawn (%s): every mod goes back to its own "
             "engine_fixes.ini", withdrawn_words(why));
}

/* This machine's own value of a setting, read as its mod reads it, for the line that sets the two
 * side by side. Read on an edge, never per pump. */
static float own_here(size_t id)
{
    const host_setting_key_t *key = &host_settings_keys(NULL)[id];

    return mp_host_settings_own_value((host_setting_id_t)id,
                                      ini_read_float(key->section, key->key, key->default_value));
}

static void say_arrived(const host_settings_t *record)
{
    line_t line;
    size_t id;
    char   piece[48];

    if (record->present == 0u) {
        log_info("the host's settings arrive: the host names none of them yet (no note from it, "
                 "or none of its mods is loaded there), so every mod here keeps its own "
                 "engine_fixes.ini");
        return;
    }
    memset(&line, 0, sizeof line);
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((record->present & (1u << id)) == 0u) {
            continue;
        }
        line_append(&line, line.used == 0u ? "" : ", ");
        line_value(&line, id, record->values[id]);
        if (host_settings_keys(NULL)[id].whole_numbers) {
            text_format(piece, sizeof piece, " (own %.0f)", (double)own_here(id));
        } else {
            text_format(piece, sizeof piece, " (own %.2f)", (double)own_here(id));
        }
        line_append(&line, piece);
    }
    log_info("the host's settings arrive: %s; published for the mods of this process, and no "
             "line of this machine's engine_fixes.ini is written", line.text);
}

void mp_host_settings_pump(void)
{
    host_settings_t wanted;
    uint64_t        connection;
    uint32_t        now;
    bool            was_running = hs.filed.running;
    bool            client      = false;
    bool            plays;
    size_t          id;

    if (!mp_armed_transport()) {
        return;
    }
    /* The one answer the world holds and the hero carry take as well; for a client it includes
     * the connection to the host, so a host's goodbye takes its values away at the next pump. */
    plays      = mp_session_now_plays_in_a_running_session(&client);
    connection = client ? mp_bridge_drain_host_connection() : 0u;
    memset(&wanted, 0, sizeof wanted);
    wanted.running = plays && client;
    if (wanted.running && heard_on(connection)) {
        wanted.present = hs.heard.present;
        for (id = 0; id < HOST_SETTING_COUNT; ++id) {
            wanted.values[id] = hs.heard.values[id];
        }
    }
    if (records_agree(&wanted, &hs.filed)) {
        return;
    }
    now = mp_wallclock_ms();
    if (hs.retry_waits && now - hs.refused_at_ms < PUBLISH_RETRY_MS) {
        return;
    }
    hs.retry_waits = !file_record(&wanted);
    if (hs.retry_waits) {
        hs.refused_at_ms = now;
        return;
    }
    if (wanted.running) {
        say_arrived(&wanted);
    } else if (was_running) {
        say_withdrawn(client && connection == 0u ? WITHDRAWN_HOST_GONE : WITHDRAWN_SESSION);
    }
}

void mp_host_settings_withdraw(void)
{
    host_settings_t none;

    /* The host's word belongs to the session that carried it. Kept, it would be published again
     * by a second session in this process before that session's host had said anything. */
    hs.heard_known      = false;
    hs.heard_connection = 0u;
    memset(&hs.heard, 0, sizeof hs.heard);
    hs.retry_waits      = false;
    if (!hs.filed.running) {
        return;   /* a host, or a client whose session never began: nothing of a host's stands */
    }
    memset(&none, 0, sizeof none);
    if (!file_record(&none)) {
        log_warning("the host's settings could not be withdrawn: the channel refused the record, "
                    "so the mods of this machine go on reading the host's values until it quits");
        return;
    }
    say_withdrawn(WITHDRAWN_TRANSPORT);
}

/* ============================================================================================ */
/* The report. */

static void report_one_consumer(const char *mod)
{
    host_settings_taken_t taken;
    line_t                line;
    size_t                id;
    char                  piece[64];

    if (!host_settings_read_taken(mod, &taken)) {
        log_info("  the host's settings taken by %s: no note answered", mod);
        return;
    }
    if (taken.in_force == 0u) {
        log_info("  the host's settings taken by %s: no, nothing of the host's in force "
                 "(generation %u, answer %u)", mod, (unsigned)taken.generation,
                 (unsigned)taken.published);
        return;
    }
    memset(&line, 0, sizeof line);
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((taken.in_force & (1u << id)) == 0u) {
            continue;
        }
        line_append(&line, line.used == 0u ? "" : ", ");
        line_value(&line, id, hs.filed.values[id]);
        text_format(piece, sizeof piece, " in force (effective here %.2f)",
                    (double)taken.effective[id]);
        line_append(&line, piece);
    }
    log_info("  the host's settings taken by %s: yes, %s (generation %u, answer %u)", mod,
             line.text, (unsigned)taken.generation, (unsigned)taken.published);
}

/* One line per consumer, each named once however many settings of the table it holds. */
static void report_consumers(void)
{
    const host_setting_key_t *keys;
    size_t                    count = 0;
    size_t                    id;
    size_t                    earlier;

    keys = host_settings_keys(&count);
    for (id = 0; id < count; ++id) {
        bool seen = false;

        for (earlier = 0; earlier < id; ++earlier) {
            seen = seen || strcmp(keys[earlier].section, keys[id].section) == 0;
        }
        if (!seen) {
            report_one_consumer(keys[id].section);
        }
    }
}

void mp_host_settings_report(void)
{
    line_t          said;
    host_settings_t on_channel;
    const char     *in_force = "no";

    /* What a mod reads now, from the channel itself rather than from what this module believes it
     * filed, so a record another hand replaced would show here. */
    if (host_settings_read(&on_channel) && on_channel.running) {
        in_force = on_channel.present != 0u ? "yes" : "running, nothing named";
    }
    memset(&said, 0, sizeof said);
    if (!hs.said_known) {
        line_append(&said, "nothing said");
    } else {
        size_t id;

        for (id = 0; id < HOST_SETTING_COUNT; ++id) {
            if ((hs.said_last.present & (1u << id)) != 0u) {
                line_append(&said, said.used == 0u ? "" : ", ");
                line_value(&said, id, hs.said_last.values[id]);
            }
        }
        line_append(&said, said.used == 0u ? "no setting of a loaded mod" : "");
    }
    log_info("  the host's settings (host): said %u time(s), %u of them to nobody, %u that would "
             "not encode (must be 0); the last: %s", (unsigned)hs.said, (unsigned)hs.said_to_nobody,
             (unsigned)hs.unencoded, said.text);
    log_info("  the host's settings (client): %u note(s) taken, %u refused (range %u, unknown bit "
             "%u, shape %u), %u heard where this side holds them for nobody; published %u "
             "time(s), %u refused by the channel; in force now: %s; last withdrawn by %s, %u "
             "withdrawal(s)", (unsigned)hs.taken,
             (unsigned)(hs.refused_range + hs.refused_bit + hs.refused_shape),
             (unsigned)hs.refused_range, (unsigned)hs.refused_bit, (unsigned)hs.refused_shape,
             (unsigned)hs.not_a_client, (unsigned)hs.publications, (unsigned)hs.publish_refused,
             in_force, withdrawn_words(hs.withdrawn_by), (unsigned)hs.withdrawals);
    report_consumers();
}
