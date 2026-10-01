#include "movie_note.h"

#include "language.h"
#include "shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bumped when either record below changes shape. A reader that finds a version it does not know
 * answers false, which the movie player reads as no session and the multiplayer as no movie
 * player: the one answer that holds nobody's movie on a record it cannot read. */
#define NOTE_VERSION 1u

/* Bytes rather than bools between two DLLs, and the connection as two halves, so the layout is the
 * same whatever either compiler does with the alignment of a 64 bit field. */
typedef struct state_record {
    uint32_t version;
    uint16_t movies_begun;
    uint8_t  state;
    uint8_t  end_reason;
    uint8_t  path;
    uint8_t  locked;
    uint8_t  reserved[2];
    char     stem[MOVIE_NOTE_STEM_MAX];
    uint32_t begin_ms;
    uint32_t end_ms;
} state_record_t;

typedef struct gate_record {
    uint32_t version;
    uint8_t  running;
    uint8_t  client;
    uint8_t  host_connected;
    uint8_t  host_moving;
    uint8_t  generation;
    uint8_t  language;
    uint8_t  reserved[2];
    uint32_t connection_low;
    uint32_t connection_high;
    uint32_t host_payloads;
    uint32_t published;
} gate_record_t;

_Static_assert(sizeof(state_record_t) == 36u, "Unexpected state_record_t size");
_Static_assert(sizeof(gate_record_t) == 28u, "Unexpected gate_record_t size");

/* A name the log and the multiplayer can print as it stands: printable ASCII, terminated inside
 * the field. An empty one is a movie whose name did not fit, and is allowed. */
static bool stem_is_sound(const char *stem)
{
    size_t i;

    for (i = 0; i < MOVIE_NOTE_STEM_MAX; ++i) {
        char c = stem[i];

        if (c == '\0') {
            return true;
        }
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return false;
}

static bool state_fields_sound(uint8_t state, uint8_t end_reason, uint8_t path)
{
    return state <= MOVIE_STATE_ENDED && end_reason <= MOVIE_END_MAX && path <= MOVIE_PATH_BINK;
}

static bool flag_is_sound(uint8_t flag)
{
    return flag <= 1u;
}

bool movie_note_publish_state(const movie_state_t *state)
{
    state_record_t record;
    size_t         length;

    if (state == NULL || !stem_is_sound(state->stem) ||
        !state_fields_sound(state->state, state->end_reason, state->path)) {
        return false;
    }
    memset(&record, 0, sizeof record);
    record.version      = NOTE_VERSION;
    record.movies_begun = state->movies_begun;
    record.state        = state->state;
    record.end_reason   = state->end_reason;
    record.path         = state->path;
    record.locked       = state->locked ? 1u : 0u;
    /* Only the name and its terminator: whatever the caller's field held behind them stays out of
     * the record, so two publications of one name are the same bytes. */
    length = strlen(state->stem);
    memcpy(record.stem, state->stem, length);
    record.begin_ms = state->begin_ms;
    record.end_ms   = state->end_ms;
    return shared_note_publish(MOVIE_STATE_NOTE_NAME, &record, sizeof record);
}

bool movie_note_read_state(movie_state_t *out)
{
    state_record_t record;
    size_t         count = 0;

    if (out == NULL) {
        return false;
    }
    if (!shared_note_read(MOVIE_STATE_NOTE_NAME, &record, sizeof record, &count, NULL)) {
        return false;
    }
    if (count != sizeof record || record.version != NOTE_VERSION ||
        !state_fields_sound(record.state, record.end_reason, record.path) ||
        !flag_is_sound(record.locked) || !stem_is_sound(record.stem)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->movies_begun = record.movies_begun;
    out->state        = record.state;
    out->end_reason   = record.end_reason;
    out->path         = record.path;
    out->locked       = record.locked != 0u;
    memcpy(out->stem, record.stem, sizeof out->stem);
    out->begin_ms = record.begin_ms;
    out->end_ms   = record.end_ms;
    return true;
}

bool movie_note_publish_gate(const movie_gate_t *gate)
{
    gate_record_t record;
    bool          client;

    if (gate == NULL || gate->language >= (uint8_t)LANGUAGE_COUNT) {
        return false;
    }
    /* One consistent shape. A client of a session that does not run, or a host that sees its own
     * host moving, is a combination no reader can act on, and publishing the fields independently
     * would let one outlive the other. */
    client = gate->running && gate->client;
    memset(&record, 0, sizeof record);
    record.version         = NOTE_VERSION;
    record.running         = gate->running ? 1u : 0u;
    record.client          = client ? 1u : 0u;
    record.host_connected  = (client && gate->host_connected) ? 1u : 0u;
    record.host_moving     = (client && gate->host_moving) ? 1u : 0u;
    record.generation      = gate->generation;
    record.language        = gate->language;
    record.connection_low  = (uint32_t)(gate->connection & 0xFFFFFFFFu);
    record.connection_high = (uint32_t)(gate->connection >> 32);
    record.host_payloads   = gate->host_payloads;
    record.published       = gate->published;
    return shared_note_publish(MOVIE_GATE_NOTE_NAME, &record, sizeof record);
}

bool movie_note_read_gate(movie_gate_t *out)
{
    gate_record_t record;
    size_t        count = 0;

    if (out == NULL) {
        return false;
    }
    if (!shared_note_read(MOVIE_GATE_NOTE_NAME, &record, sizeof record, &count, NULL)) {
        return false;
    }
    if (count != sizeof record || record.version != NOTE_VERSION ||
        !flag_is_sound(record.running) || !flag_is_sound(record.client) ||
        !flag_is_sound(record.host_connected) || !flag_is_sound(record.host_moving) ||
        record.language >= (uint8_t)LANGUAGE_COUNT) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->running        = record.running != 0u;
    out->client         = record.client != 0u;
    out->host_connected = record.host_connected != 0u;
    out->host_moving    = record.host_moving != 0u;
    out->generation     = record.generation;
    out->language       = record.language;
    out->connection     = ((uint64_t)record.connection_high << 32) | record.connection_low;
    out->host_payloads  = record.host_payloads;
    out->published      = record.published;
    return true;
}
